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
                if (!m_paused && !m_pollingDeferred) {
                    startPolling();
                    pollStatus();
                    if (m_consumablePollingEnabled) {
                        m_consumablePosition = 0;
                        m_consumableTimer.start();
                        pollConsumable();
                    }
                }
            } else {
                stopPolling();
                m_consumableTimer.stop();
                m_consumablePosition = 0;
                m_pendingCardResult = false;
                // 写卡期间断线时不要永久保持暂停，重连后要恢复正常轮询。
                m_paused = false;
            }
        });

        if (m_serial->isOpen() && !m_paused && !m_pollingDeferred) {
            startPolling();
            pollStatus();
            if (m_consumablePollingEnabled) {
                m_consumableTimer.start();
                pollConsumable();
            }
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

QByteArray MainBoardPoller::cardRechargeConfirmFrame(int cardType,
                                                     int newTotal)
{
    if (cardType < 0 || cardType > 0x0F
            || newTotal < 0 || newTotal > 0xFF) {
        return QByteArray();
    }

    QByteArray frame(SerialPortManager::kFrameLength, '\0');
    frame[0] = static_cast<char>(kMainBoardAddress);
    frame[1] = static_cast<char>(kMainBoardFuncCode);
    // 老软件始终用 0xFE（定时读主板状态命令号）发充值确认，
    // 卡类型放在 byte8 高 4 位，而不是命令号。
    frame[2] = static_cast<char>(kStatusCommandNumber);
    frame[3] = '\0';                          // cmd_size
    frame[4] = '\0';                          // 写
    frame[8] = static_cast<char>((cardType << 4) | 0x02);
    frame[9] = static_cast<char>(newTotal & 0xFF);
    frame[10] = '\0';
    // 比例字段保持 0：充值只增加总瓶数，不改变当前瓶余量。
    return frame;
}

void MainBoardPoller::pausePolling()
{
    m_paused = true;
    stopPolling();
    m_consumableTimer.stop();
}

void MainBoardPoller::resumePolling()
{
    m_paused = false;
    m_pendingCardResult = false;
    m_consumablePosition = 0;
    if (m_pollingDeferred || !m_serial || !m_serial->isOpen()) {
        return;
    }
    startPolling();
    pollStatus();
    if (m_consumablePollingEnabled) {
        m_consumableTimer.start();
        pollConsumable();
    }
}

void MainBoardPoller::setConsumablePollingEnabled(bool enabled)
{
    m_consumablePollingEnabled = enabled;
    if (!enabled) {
        m_consumableTimer.stop();
        m_consumablePosition = 0;
        return;
    }
    if (m_paused || m_pollingDeferred || !m_serial || !m_serial->isOpen()) {
        return;
    }
    m_consumablePosition = 0;
    m_consumableTimer.start();
    pollConsumable();
}

void MainBoardPoller::setPollingDeferred(bool deferred)
{
    m_pollingDeferred = deferred;
    if (deferred) {
        stopPolling();
        m_consumableTimer.stop();
        m_consumablePosition = 0;
        return;
    }
    if (m_paused || !m_serial || !m_serial->isOpen()) {
        return;
    }
    startPolling();
    pollStatus();
    if (m_consumablePollingEnabled) {
        m_consumablePosition = 0;
        m_consumableTimer.start();
        pollConsumable();
    }
}

void MainBoardPoller::setBusQuiet(bool quiet)
{
    m_busQuiet = quiet;
}

void MainBoardPoller::expectCardRechargeResult(int cardType)
{
    m_pendingCardType = static_cast<quint8>(cardType & 0x0F);
    m_pendingCardResult = true;
}

QByteArray MainBoardPoller::consumableWriteFrame(int position, int count,
                                                 quint16 ratio, bool cache)
{
    if (position < 0 || position >= MainBoardConsumable::kPositionCount
            || count < 0 || count > 0xFF) {
        return QByteArray();
    }

    QByteArray frame(SerialPortManager::kFrameLength, '\0');
    frame[0] = static_cast<char>(kMainBoardAddress);
    frame[1] = static_cast<char>(kMainBoardFuncCode);
    frame[2] = static_cast<char>(position);   // 老协议：命令号=耗材位置
    frame[3] = '\0';                          // cmd_size
    frame[4] = '\0';                          // 写
    // 老上位机：bit0=1 表示缓存写入；bit0=7 表示直接写入。
    const quint8 positionByte = static_cast<quint8>(
                (position << 4) | (cache ? 0x01 : 0x07));
    frame[8] = static_cast<char>(positionByte);
    frame[9] = static_cast<char>(count & 0xFF);
    frame[10] = static_cast<char>(cache ? positionByte : 0x00);
    frame[11] = static_cast<char>(ratio & 0xFF);
    frame[12] = static_cast<char>((ratio >> 8) & 0xFF);
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
    if (m_busQuiet || m_paused || m_pollingDeferred
            || !m_serial || !m_serial->isOpen()) {
        return;
    }
    QString error;
    m_serial->sendFrame(statusRequestFrame(), &error);
}

void MainBoardPoller::pollConsumable()
{
    if (!m_consumablePollingEnabled || m_busQuiet || m_paused
            || m_pollingDeferred || !m_serial || !m_serial->isOpen()) {
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

    // 命令号（100+下标）的下标空间已与主板确认：0 AA、1 EPI、2 ADP、
    // 3 COL、4 RIS、5 内部清洗剂 S1、6 试管、7 外部清洗液 S2，
    // 8~12 为右侧瓶位 AA1~RIS1。
    // 注意 Types_of_cons 是另一套编号：0~6 与下标相同，7~11 表示
    // AA1~RIS1（即命令位置 8~12），12 为 NONE；S2 在该字段中无定义。
    // 因此右侧瓶位需要用 typeOfCons+1 映射回 8~12。
    const int typeOfCons = (static_cast<quint8>(frame.at(10)) >> 4) & 0x0F;
    if (position >= MainBoardConsumable::AA_Second
            && position <= MainBoardConsumable::RIS_Second
            && typeOfCons >= 7 && typeOfCons <= 11
            && typeOfCons + 1 != position) {
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
    // 充值确认命令的回执：确认帧命令号固定 0xFE，卡类型在 byte8 高 4 位。
    // 命令可能先回“等待/运行中”，只有 mfcStatus 3/4/5/6 才是最终结果，
    // 中间回执不能清除 pending，否则会漏掉真正的成功/失败回执。
    if (m_pendingCardResult && frame.size() >= SerialPortManager::kFrameLength
            && static_cast<quint8>(frame.at(0)) == kMainBoardAddress
            && static_cast<quint8>(frame.at(1)) == kMainBoardFuncCode) {
        const quint8 command = static_cast<quint8>(frame.at(2));
        const quint8 mfcStatus = static_cast<quint8>(frame.at(8)) & 0x0F;
        const bool consumableReply =
                command >= MainBoardConsumable::kCommandBase
                && command < MainBoardConsumable::kCommandBase
                           + MainBoardConsumable::kPositionCount;
        const bool finalResult = mfcStatus == 3 || mfcStatus == 4
                || mfcStatus == 5 || mfcStatus == 6;
        if (!consumableReply && finalResult) {
            const quint8 typeCode =
                    (static_cast<quint8>(frame.at(8)) >> 4) & 0x0F;
            const quint8 cardQty = static_cast<quint8>(frame.at(9));
            const int cardType = command == kStatusCommandNumber
                    ? int(typeCode) : int(m_pendingCardType);
            m_pendingCardResult = false;
            emit cardRechargeResult(
                        int(mfcStatus), cardType, int(cardQty),
                        int(static_cast<quint8>(frame.at(13))),
                        int(static_cast<quint8>(frame.at(14))));
            if (command != kStatusCommandNumber) {
                return;
            }
        } else if (command == m_pendingCardType) {
            // 充值命令本身的非最终回执，继续等待。
            return;
        }
    }

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
