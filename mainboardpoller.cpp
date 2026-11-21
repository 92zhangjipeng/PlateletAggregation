#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "mainboardpoller.h"
#include "mainboardconsumables.h"
#include "SerialPortManager.h"

namespace {
constexpr quint8 kMainBoardAddress = 0x05;
constexpr quint8 kMainBoardFuncCode = 0x16;
constexpr quint8 kStatusCommandNumber = 0xFE;
constexpr int kPollIntervalMs = 1000;
constexpr int kConsumablePollIntervalMs = 500;
}

MainBoardPoller::MainBoardPoller(SerialPortManager *serial, QObject *parent)
    : QObject(parent)
    , m_serial(serial)
{
    m_timer.setInterval(kPollIntervalMs);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &MainBoardPoller::pollStatus);

    // 连接后逐个试剂位读取余量：每次读一个位置，循环刷新。
    m_consumableTimer.setInterval(kConsumablePollIntervalMs);
    m_consumableTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_consumableTimer, &QTimer::timeout,
            this, &MainBoardPoller::pollConsumable);

    if (m_serial) {
        connect(m_serial, &SerialPortManager::frameReceived,
                this, &MainBoardPoller::handleFrame);
        connect(m_serial, &SerialPortManager::connectedChanged,
                this, [this](bool connected) {
            if (connected) {
                startPolling();
                pollStatus();
                m_consumablePosition = 0;
                m_consumableTimer.start();
                pollConsumable();
            } else {
                stopPolling();
                m_consumableTimer.stop();
                m_consumablePosition = 0;
            }
        });

        if (m_serial->isOpen()) {
            startPolling();
            pollStatus();
            m_consumableTimer.start();
            pollConsumable();
        }
    }
}

MainBoardPoller::~MainBoardPoller()
{
    stopPolling();
    m_consumableTimer.stop();
}

bool MainBoardPoller::isStatusReply(const QByteArray &frame) const
{
    return frame.size() >= SerialPortManager::kFrameLength
            && static_cast<quint8>(frame.at(0)) == kMainBoardAddress
            && static_cast<quint8>(frame.at(1)) == kMainBoardFuncCode
            && static_cast<quint8>(frame.at(2)) == kStatusCommandNumber;
}

bool MainBoardPoller::isConsumableReply(const QByteArray &frame) const
{
    if (frame.size() < SerialPortManager::kFrameLength) {
        return false;
    }
    const int command = static_cast<quint8>(frame.at(2));
    return static_cast<quint8>(frame.at(0)) == kMainBoardAddress
            && static_cast<quint8>(frame.at(1)) == kMainBoardFuncCode
            && command >= MainBoardConsumable::kCommandBase
            && command < MainBoardConsumable::kCommandBase
                       + MainBoardConsumable::kPositionCount;
}

QByteArray MainBoardPoller::statusRequestFrame()
{
    QByteArray frame;
    frame.reserve(SerialPortManager::kFrameLength);
    frame.append(static_cast<char>(kMainBoardAddress));
    frame.append(static_cast<char>(kMainBoardFuncCode));
    frame.append(static_cast<char>(kStatusCommandNumber));
    frame.append(static_cast<char>(0x00));   // cmd_size
    frame.append(static_cast<char>(0x01));   // read/report
    frame.append(QByteArray(10, static_cast<char>(0x00)));
    return frame;
}

QByteArray MainBoardPoller::consumableRequestFrame(int position)
{
    if (position < 0 || position >= MainBoardConsumable::kPositionCount) {
        return QByteArray();
    }

    QByteArray frame(SerialPortManager::kFrameLength, '\0');
    frame[0] = static_cast<char>(kMainBoardAddress);
    frame[1] = static_cast<char>(kMainBoardFuncCode);
    frame[2] = static_cast<char>(MainBoardConsumable::kCommandBase + position);
    frame[3] = '\0';                         // cmd_size
    frame[4] = static_cast<char>(0x01);      // 读
    const char typeByte = static_cast<char>((position << 4) & 0xF0);
    frame[8] = typeByte;                     // 刷卡数据类型
    frame[9] = '\0';                         // 单位：瓶/板
    frame[10] = typeByte;                    // 耗材余量种类
    return frame;
}

MainBoardStatus MainBoardPoller::parseStatusFrame(const QByteArray &frame,
                                                  bool *ok)
{
    if (ok) {
        *ok = false;
    }
    if (frame.size() < SerialPortManager::kFrameLength) {
        return MainBoardStatus();
    }
    if (static_cast<quint8>(frame.at(0)) != kMainBoardAddress
            || static_cast<quint8>(frame.at(1)) != kMainBoardFuncCode
            || static_cast<quint8>(frame.at(2)) != kStatusCommandNumber) {
        return MainBoardStatus();
    }

    MainBoardStatus status;
    status.valid = true;
    status.raw = frame;
    status.cmdSize = static_cast<quint8>(frame.at(3));

    const quint8 byte4 = static_cast<quint8>(frame.at(4));
    status.type = byte4 & 0x01;
    status.cmdStatus = (byte4 >> 1) & 0x03;
    status.cleanNormal = ((byte4 >> 3) & 0x01) == 0;
    for (int i = 0; i < 4; ++i) {
        status.cupPresent[i] = ((byte4 >> (4 + i)) & 0x01) != 0;
    }

    const quint8 byte5 = static_cast<quint8>(frame.at(5));
    status.wasteNormal = (byte5 & 0x01) == 0;
    status.heightOk = ((byte5 >> 1) & 0x01) != 0;
    status.fanOn = ((byte5 >> 2) & 0x01) != 0;
    status.coolOn = ((byte5 >> 3) & 0x01) != 0;
    status.pump1On = ((byte5 >> 4) & 0x01) != 0;
    status.pump2On = ((byte5 >> 5) & 0x01) != 0;
    status.threeWay1Open = ((byte5 >> 6) & 0x01) != 0;
    status.threeWay2Open = ((byte5 >> 7) & 0x01) != 0;

    const quint8 byte8 = static_cast<quint8>(frame.at(8));
    status.mfcStatus = byte8 & 0x0F;
    status.regType = (byte8 >> 4) & 0x0F;
    status.regRemain = static_cast<quint8>(frame.at(9));

    const quint8 byte10 = static_cast<quint8>(frame.at(10));
    status.consStatus = byte10 & 0x0F;
    status.consType = (byte10 >> 4) & 0x0F;

    // 协议注明“高位在后，低位在前”
    status.consumable = static_cast<quint16>(
                static_cast<quint8>(frame.at(11))
                | (static_cast<quint16>(static_cast<quint8>(frame.at(12))) << 8));
    status.regYear = static_cast<quint8>(frame.at(13));
    status.regMonth = static_cast<quint8>(frame.at(14));

    if (ok) {
        *ok = true;
    }
    return status;
}

void MainBoardPoller::startPolling()
{
    if (!m_timer.isActive()) {
        m_timer.start();
    }
}

void MainBoardPoller::stopPolling()
{
    m_timer.stop();
}

void MainBoardPoller::pollStatus()
{
    if (!m_serial || !m_serial->isOpen()) {
        return;
    }
    QString error;
    m_serial->sendFrame(statusRequestFrame(), &error);
}

void MainBoardPoller::pollConsumable()
{
    if (!m_serial || !m_serial->isOpen()) {
        return;
    }

    const QByteArray frame = consumableRequestFrame(m_consumablePosition);
    if (frame.isEmpty()) {
        return;
    }

    QString error;
    m_serial->sendFrame(frame, &error);
    m_consumablePosition = (m_consumablePosition + 1)
            % MainBoardConsumable::kPositionCount;
}

void MainBoardPoller::handleConsumableFrame(const QByteArray &frame)
{
    if (frame.size() < SerialPortManager::kFrameLength) {
        return;
    }

    int position = static_cast<quint8>(frame.at(2))
            - MainBoardConsumable::kCommandBase;
    if (position < 0 || position >= MainBoardConsumable::kPositionCount) {
        return;
    }

    // 以应答的 Types_of_cons 修正 EPI/ADP 的顺序和右侧瓶位映射，
    // 避免命令号顺序与固件不一致时把余量显示到错误的试剂上。
    // 0~6 为主位置；7~11 为 AA1~RIS1，对应右侧瓶位 8~12。
    const int typeOfCons = (static_cast<quint8>(frame.at(10)) >> 4) & 0x0F;
    if ((position == MainBoardConsumable::EPI
         || position == MainBoardConsumable::ADP)
            && (typeOfCons == MainBoardConsumable::EPI
                || typeOfCons == MainBoardConsumable::ADP)) {
        position = typeOfCons;
    } else if (position >= MainBoardConsumable::AA_Second
               && position <= MainBoardConsumable::RIS_Second
               && typeOfCons >= 7 && typeOfCons <= 11
               && typeOfCons != position) {
        position = typeOfCons + 1;
    }

    // 读卡失败/刷卡异常/充值失败时保留上一次有效值。
    const quint8 mfcStatus = static_cast<quint8>(frame.at(8)) & 0x0F;
    if (mfcStatus == 3 || mfcStatus == 4 || mfcStatus == 6) {
        return;
    }

    const int bottles = static_cast<quint8>(frame.at(9));
    const quint16 ratio = static_cast<quint8>(frame.at(11))
            | (static_cast<quint16>(static_cast<quint8>(frame.at(12))) << 8);
    emit consumableUpdated(
                position,
                bottles,
                qBound(0, static_cast<int>(ratio),
                       MainBoardConsumable::kRatioMax));
}

void MainBoardPoller::handleFrame(const QByteArray &frame)
{
    if (isConsumableReply(frame)) {
        handleConsumableFrame(frame);
        return;
    }

    if (!isStatusReply(frame)) {
        return;
    }

    bool ok = false;
    const MainBoardStatus status = parseStatusFrame(frame, &ok);
    if (ok) {
        emit statusUpdated(status);
    }
}
