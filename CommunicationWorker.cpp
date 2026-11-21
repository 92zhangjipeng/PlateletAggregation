#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "CommunicationWorker.h"
#include "SerialPortManager.h"
#include "mainboardpoller.h"

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

    m_adPoller->setMachineModel(defaultMachineModel());
    m_adPoller->start();

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
