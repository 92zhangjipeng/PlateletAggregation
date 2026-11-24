#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "CommunicationWorker.h"
#include "SerialPortManager.h"
#include "mainboardpoller.h"

#include <QDebug>
#include <QTimer>

CommunicationWorker::CommunicationWorker(QObject *parent)
    : QObject(parent)
{
}

CommunicationWorker::~CommunicationWorker() = default;

void CommunicationWorker::startCommunication()
{
    if (m_started) {
        return;
    }
    m_started = true;

    m_serial = new SerialPortManager(this);
    m_adPoller = new ModuleAdPoller(m_serial, this);
    m_boardPoller = new MainBoardPoller(m_serial, this);

    // 与老软件一致：开机灌注完成前不读 100~112 耗材。老软件灌注期间
    // 总线上只有 0x17 动作帧；首轮灌注结束后 resumePeriodicPolling()
    // 再打开耗材读取。状态帧 0xFE 仍可提前读取，用于确认主板在线。
    m_boardPoller->setConsumablePollingEnabled(false);
    m_boardPoller->setPollingDeferred(true);

    connect(m_serial, &SerialPortManager::connectedChanged,
            this, [this](bool connected) {
        emit connectedChanged(connected, m_serial->portName(),
                              m_serial->baudRate());
    });
    connect(m_serial, &SerialPortManager::errorOccurred,
            this, [this](const QString &message) {
        emit errorOccurred(message, m_serial && m_serial->isOpen());
    });
    connect(m_serial, &SerialPortManager::frameReceived,
            this, [this](const QByteArray &frame) {
        // 每秒轮询的模组/主板应答不再逐帧送到界面日志
        if (m_adPoller && m_adPoller->isPeriodicReply(frame)) {
            return;
        }
        if (m_boardPoller && m_boardPoller->isStatusReply(frame)) {
            return;
        }
        if (m_boardPoller && m_boardPoller->isConsumableReply(frame)) {
            return;
        }
        // 诊断：主板非 0x17 帧（例如 0x16 的非常规回执）也记一条，
        // 正常周期状态/耗材应答已在上面过滤，不会刷屏。
        if (frame.size() >= 2
                && static_cast<quint8>(frame.at(0)) == 0x05
                && static_cast<quint8>(frame.at(1)) != 0x17) {
            qInfo() << "主板非运动帧:"
                    << QString::fromLatin1(frame.toHex(' ').toUpper());
        }
        emit frameReceived(frame);
    });

    connect(m_adPoller, &ModuleAdPoller::moduleAdUpdated,
            this, &CommunicationWorker::moduleAdUpdated);
    connect(m_adPoller, &ModuleAdPoller::moduleCountChanged,
            this, &CommunicationWorker::moduleCountChanged);
    connect(m_boardPoller, &MainBoardPoller::statusUpdated,
            this, &CommunicationWorker::mainBoardStatusUpdated);
    connect(m_boardPoller, &MainBoardPoller::consumableUpdated,
            this, &CommunicationWorker::mainBoardConsumableUpdated);
    connect(m_boardPoller, &MainBoardPoller::cardRechargeResult,
            this, &CommunicationWorker::cardRechargeResult);

    m_adPoller->setMachineModel(defaultMachineModel());
    // 模组 AD 采集与老软件一样，等开机灌注完成后再由
    // resumePeriodicPolling() 启动；灌注期间总线上只走 0x17 动作帧。

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(2000);
    connect(m_reconnectTimer, &QTimer::timeout,
            this, &CommunicationWorker::attemptConnect);
    m_reconnectTimer->start();

    attemptConnect();
}

void CommunicationWorker::stopCommunication()
{
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
        delete m_reconnectTimer;
        m_reconnectTimer = nullptr;
    }
    if (m_boardPoller) {
        delete m_boardPoller;
        m_boardPoller = nullptr;
    }
    if (m_adPoller) {
        m_adPoller->stop();
        delete m_adPoller;
        m_adPoller = nullptr;
    }
    if (m_serial) {
        m_serial->closePort();
        delete m_serial;
        m_serial = nullptr;
    }
    m_started = false;
}

void CommunicationWorker::sendFrameRequested(const QByteArray &frame,
                                             const QString &description)
{
    if (!m_serial) {
        emit sendResult(description, false, tr("通讯线程未启动"));
        return;
    }

    QString error;
    const bool ok = m_serial->sendFrame(frame, &error);
    emit sendResult(description, ok, error);
}

void CommunicationWorker::sendCardRechargeConfirm(
        const QByteArray &frame, int cardType, const QString &description)
{
    if (!m_serial) {
        emit cardRechargeCommandSent(false, tr("通讯线程未启动"));
        return;
    }
    if (m_boardPoller) {
        m_boardPoller->pausePolling();
        m_boardPoller->expectCardRechargeResult(cardType);
    }

    QString error;
    const bool ok = m_serial->sendFrame(frame, &error);
    emit cardRechargeCommandSent(ok, error);
    if (!ok) {
        // 发送失败立即恢复轮询，让界面继续刷新耗材。
        if (m_boardPoller) {
            m_boardPoller->resumePolling();
        }
        emit sendResult(description, false, error);
    }
}

void CommunicationWorker::resumeMainboardPolling()
{
    if (m_boardPoller) {
        m_boardPoller->resumePolling();
    }
}

void CommunicationWorker::pausePeriodicPolling()
{
    // 老软件在开机灌注完成前不启动主板/模组采集，灌注期间总线上只有
    // 0x17 运动命令和应答；这里保持同样的行为。
    if (m_boardPoller) {
        m_boardPoller->pausePolling();
    }
    if (m_adPoller) {
        m_adPoller->stop();
    }
    qInfo() << "已暂停主板/模组周期轮询";
}

void CommunicationWorker::resumePeriodicPolling()
{
    if (m_boardPoller) {
        m_boardPoller->setConsumablePollingEnabled(true);
        m_boardPoller->setPollingDeferred(false);
        m_boardPoller->resumePolling();
    }
    if (m_adPoller) {
        m_adPoller->start();
    }
    qInfo() << "已恢复主板/模组周期轮询";
}

void CommunicationWorker::setMotionExchangeActive(bool active)
{
    if (m_boardPoller) {
        m_boardPoller->setBusQuiet(active);
    }
    if (m_adPoller) {
        m_adPoller->setBusQuiet(active);
    }
}

void CommunicationWorker::setMachineModel(MachineModel model)
{
    if (m_adPoller) {
        m_adPoller->setMachineModel(model);
    }
}

void CommunicationWorker::requestLatestModuleData()
{
    if (!m_adPoller) {
        return;
    }
    const int count = m_adPoller->activeModuleCount();
    emit moduleCountChanged(count);
    for (int i = 0; i < count; ++i) {
        emit moduleAdUpdated(m_adPoller->latestModuleData(i + 1));
    }
}

void CommunicationWorker::requestConnectionState()
{
    if (!m_serial) {
        emit connectedChanged(false, QString(),
                              SerialPortManager::kInstrumentBaudRate);
        return;
    }
    emit connectedChanged(m_serial->isOpen(), m_serial->portName(),
                          m_serial->baudRate());
}

void CommunicationWorker::attemptConnect()
{
    if (!m_serial || m_serial->isOpen()) {
        return;
    }

    const QString portName = SerialPortManager::findInstrumentPortName();
    if (portName.isEmpty()) {
        return;
    }

    QString error;
    m_serial->openPort(portName, SerialPortManager::kInstrumentBaudRate, &error);
    // 打开失败会通过 errorOccurred 通知界面，2 秒后继续重试
}
