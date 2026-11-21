#include "DetectionModule.h"

namespace platelet {

namespace {

void appendU16LittleEndian(QByteArray &frame, quint16 value)
{
    frame.append(static_cast<char>(value & 0x00FF));
    frame.append(static_cast<char>((value >> 8) & 0x00FF));
}

quint16 readU16LittleEndian(const QByteArray &frame, int offset)
{
    return static_cast<quint16>(
                static_cast<quint8>(frame.at(offset))
                | (static_cast<quint16>(static_cast<quint8>(frame.at(offset + 1))) << 8));
}

quint16 clampU16(quint16 value, quint16 minimum, quint16 maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

quint16 celsiusToRaw(double celsius)
{
    const double resolution = DetectionModule::temperatureResolution();
    const double maximum = 65535.0 * resolution;
    double value = celsius;
    if (value < 0.0) {
        value = 0.0;
    } else if (value > maximum) {
        value = maximum;
    }
    return static_cast<quint16>(qRound(value / resolution));
}

double rawToCelsius(quint16 raw)
{
    return static_cast<double>(raw) * DetectionModule::temperatureResolution();
}

} // namespace

double DetectionModule::temperatureResolution()
{
    return 0.0625;
}

double DetectionModule::defaultTargetTemperature()
{
    return 37.0;
}

DetectionModule::DetectionModule(quint8 slaveAddress)
    : m_slaveAddress(slaveAddress)
{
    m_targetTempRaw = celsiusToRaw(defaultTargetTemperature());
    if (!isValidSlaveAddress(m_slaveAddress)) {
        m_slaveAddress = quint8(Slave::Module1);
        m_lastError = QStringLiteral("从机地址无效，已回退为模组1（0x01）");
    }
}

quint8 DetectionModule::slaveAddress() const
{
    return m_slaveAddress;
}

bool DetectionModule::isValidSlaveAddress(quint8 slaveAddress)
{
    return slaveAddress == quint8(Slave::Module1)
            || slaveAddress == quint8(Slave::Module2)
            || slaveAddress == quint8(Slave::Module3);
}

bool DetectionModule::setSlaveAddress(quint8 slaveAddress)
{
    if (!isValidSlaveAddress(slaveAddress)) {
        m_lastError = QStringLiteral("从机地址只能是 0x01/0x02/0x03（模组1~3）");
        return false;
    }
    m_slaveAddress = slaveAddress;
    m_lastError.clear();
    return true;
}

void DetectionModule::setTargetTemperature(double celsius)
{
    m_targetTempRaw = celsiusToRaw(celsius);
    m_lastError.clear();
}

double DetectionModule::targetTemperature() const
{
    return rawToCelsius(m_targetTempRaw);
}

quint16 DetectionModule::targetTemperatureRaw() const
{
    return m_targetTempRaw;
}

quint8 DetectionModule::lastCommandNumber() const
{
    return m_lastCmdNum;
}

void DetectionModule::resetCommandNumber(quint8 next)
{
    m_nextCmdNum = (next == 0) ? 1 : next;
}

void DetectionModule::setCmdSize(quint8 cmdSize)
{
    m_cmdSize = cmdSize;
}

quint8 DetectionModule::cmdSize() const
{
    return m_cmdSize;
}

QByteArray DetectionModule::setTemperature(double celsius)
{
    setTargetTemperature(celsius);
    const std::array<quint16, ChannelCount> channels{{0, 0, 0, 0}};
    return build(Func::MotorSpeed, FrameType::Command, channels, m_targetTempRaw);
}

QByteArray DetectionModule::setDimming(quint16 dimming)
{
    m_lastError.clear();
    const quint16 value = clampDimming(dimming);
    const std::array<quint16, ChannelCount> channels{{value, value, value, value}};
    return build(Func::LedLight, FrameType::Command, channels, m_targetTempRaw);
}

QByteArray DetectionModule::setChannelDimming(int channel, quint16 dimming)
{
    m_lastError.clear();
    if (!checkChannel(channel)) {
        return {};
    }
    const quint16 value = clampDimming(dimming);
    std::array<quint16, ChannelCount> channels{{0, 0, 0, 0}};
    channels[static_cast<size_t>(channel - 1)] = value;
    return build(Func::LedLight, FrameType::Command, channels, m_targetTempRaw);
}

QByteArray DetectionModule::setLedCurrent(quint16 current)
{
    m_lastError.clear();
    const quint16 value = clampLedCurrent(current);
    const std::array<quint16, ChannelCount> channels{{value, value, value, value}};
    return build(Func::LedCurrent, FrameType::Command, channels, m_targetTempRaw);
}

QByteArray DetectionModule::setChannelLedCurrent(int channel, quint16 current)
{
    m_lastError.clear();
    if (!checkChannel(channel)) {
        return {};
    }
    const quint16 value = clampLedCurrent(current);
    std::array<quint16, ChannelCount> channels{{0, 0, 0, 0}};
    channels[static_cast<size_t>(channel - 1)] = value;
    return build(Func::LedCurrent, FrameType::Command, channels, m_targetTempRaw);
}

QByteArray DetectionModule::setSpeed(int channel, quint16 speed)
{
    m_lastError.clear();
    if (!checkChannel(channel)) {
        return {};
    }
    const quint16 value = clampSpeed(speed);
    m_speeds[static_cast<size_t>(channel - 1)] = value;

    std::array<quint16, ChannelCount> channels{{0, 0, 0, 0}};
    channels[static_cast<size_t>(channel - 1)] = value;
    return build(Func::MotorSpeed, FrameType::Command, channels, m_targetTempRaw);
}

QByteArray DetectionModule::setAllSpeeds(quint16 speed)
{
    m_lastError.clear();
    const quint16 value = clampSpeed(speed);
    for (size_t i = 0; i < m_speeds.size(); ++i) {
        m_speeds[i] = value;
    }
    const std::array<quint16, ChannelCount> channels{{value, value, value, value}};
    return build(Func::MotorSpeed, FrameType::Command, channels, m_targetTempRaw);
}

QByteArray DetectionModule::startChannel(int channel)
{
    m_lastError.clear();
    if (!checkChannel(channel)) {
        return {};
    }
    std::array<quint16, ChannelCount> channels{{0, 0, 0, 0}};
    channels[static_cast<size_t>(channel - 1)] =
            m_speeds[static_cast<size_t>(channel - 1)];
    return build(Func::MotorSpeed, FrameType::Command, channels, m_targetTempRaw);
}

QByteArray DetectionModule::startAllChannels()
{
    m_lastError.clear();
    return build(Func::MotorSpeed, FrameType::Command, m_speeds, m_targetTempRaw);
}

QByteArray DetectionModule::stopChannel(int channel)
{
    m_lastError.clear();
    if (!checkChannel(channel)) {
        return {};
    }
    std::array<quint16, ChannelCount> channels{{0, 0, 0, 0}};
    channels[static_cast<size_t>(channel - 1)] = SpeedStop;
    return build(Func::MotorSpeed, FrameType::Command, channels, m_targetTempRaw);
}

QByteArray DetectionModule::stopAllChannels()
{
    m_lastError.clear();
    const std::array<quint16, ChannelCount> channels{{SpeedStop, SpeedStop,
                                                      SpeedStop, SpeedStop}};
    return build(Func::MotorSpeed, FrameType::Command, channels, m_targetTempRaw);
}

QByteArray DetectionModule::saveParameters()
{
    m_lastError.clear();
    const std::array<quint16, ChannelCount> channels{{0, 0, 0, 0}};
    return build(Func::Save, FrameType::Command, channels, m_targetTempRaw);
}

QByteArray DetectionModule::readTemperatureAndChannels()
{
    m_lastError.clear();
    const std::array<quint16, ChannelCount> channels{{0, 0, 0, 0}};
    return build(Func::ReadTempAndChannels, FrameType::Report, channels, 0);
}

QByteArray DetectionModule::readLedCurrent()
{
    m_lastError.clear();
    const std::array<quint16, ChannelCount> channels{{0, 0, 0, 0}};
    return build(Func::LedLight, FrameType::Report, channels, 0);
}

QByteArray DetectionModule::readSpeed()
{
    m_lastError.clear();
    const std::array<quint16, ChannelCount> channels{{0, 0, 0, 0}};
    return build(Func::MotorSpeed, FrameType::Report, channels, 0);
}

quint16 DetectionModule::configuredSpeed(int channel) const
{
    if (channel < 1 || channel > ChannelCount) {
        return 0;
    }
    return m_speeds[static_cast<size_t>(channel - 1)];
}

QString DetectionModule::lastError() const
{
    return m_lastError;
}

QByteArray DetectionModule::build(Func func, FrameType type,
                                  const std::array<quint16, ChannelCount> &channels,
                                  quint16 temperatureRaw)
{
    QByteArray frame;
    frame.reserve(FrameSize);

    const quint8 cmdNum = takeCommandNumber();
    const quint8 ctrl = static_cast<quint8>(
                ((static_cast<quint8>(func) & 0x1F) << 3)
                | ((static_cast<quint8>(Status::Waiting) & 0x03) << 1)
                | (static_cast<quint8>(type) & 0x01));

    frame.append(static_cast<char>(m_slaveAddress));
    frame.append(static_cast<char>(FuncCode));
    frame.append(static_cast<char>(cmdNum));
    frame.append(static_cast<char>(m_cmdSize));
    frame.append(static_cast<char>(ctrl));
    appendU16LittleEndian(frame, temperatureRaw);
    for (int i = 0; i < ChannelCount; ++i) {
        appendU16LittleEndian(frame, channels[static_cast<size_t>(i)]);
    }
    return frame;
}

bool DetectionModule::checkChannel(int channel)
{
    if (channel < 1 || channel > ChannelCount) {
        m_lastError = QStringLiteral("通道号必须是 1~4，收到 %1").arg(channel);
        return false;
    }
    return true;
}

quint16 DetectionModule::clampDimming(quint16 value)
{
    const quint16 clamped = clampU16(value, DimmingMin, DimmingMax);
    if (clamped != value) {
        m_lastError = QStringLiteral("调光值 %1 超出 %2~%3，已钳位为 %4")
                .arg(value).arg(DimmingMin).arg(DimmingMax).arg(clamped);
    }
    return clamped;
}

quint16 DetectionModule::clampSpeed(quint16 value)
{
    const quint16 clamped = clampU16(value, SpeedMin, SpeedMax);
    if (clamped != value) {
        m_lastError = QStringLiteral("转速 %1 超出 %2~%3，已钳位为 %4")
                .arg(value).arg(SpeedMin).arg(SpeedMax).arg(clamped);
    }
    return clamped;
}

quint16 DetectionModule::clampLedCurrent(quint16 value)
{
    const quint16 clamped = clampU16(value, LedCurrentMin, LedCurrentMax);
    if (clamped != value) {
        m_lastError = QStringLiteral("LED 电流 %1 超出 %2~%3，已钳位为 %4")
                .arg(value).arg(LedCurrentMin).arg(LedCurrentMax).arg(clamped);
    }
    return clamped;
}

quint8 DetectionModule::takeCommandNumber()
{
    const quint8 cmdNum = m_nextCmdNum;
    m_lastCmdNum = cmdNum;
    m_nextCmdNum = (cmdNum >= 0xFF) ? 1 : static_cast<quint8>(cmdNum + 1);
    return cmdNum;
}

bool DetectionModule::isValidDimming(quint16 value)
{
    return value >= DimmingMin && value <= DimmingMax;
}

bool DetectionModule::isValidSpeed(quint16 value)
{
    return value >= SpeedMin && value <= SpeedMax;
}

bool DetectionModule::isValidLedCurrent(quint16 value)
{
    // LedCurrentMin 为 0，无符号数下界比较恒为真，无需判断
    return value <= LedCurrentMax;
}

DetectionModule::Frame DetectionModule::parse(const QByteArray &frame, QString *errorString)
{
    auto fail = [errorString](const QString &message) -> Frame {
        if (errorString) {
            *errorString = message;
        }
        Frame invalid;
        return invalid;
    };

    if (frame.size() < FrameSize) {
        return fail(QStringLiteral("帧长度不足：需要 %1 字节，实际 %2 字节")
                    .arg(int(FrameSize)).arg(frame.size()));
    }
    if (static_cast<quint8>(frame.at(1)) != FuncCode) {
        return fail(QStringLiteral("功能码错误：期望 0x17，实际 0x%1")
                    .arg(static_cast<quint8>(frame.at(1)), 2, 16, QLatin1Char('0')));
    }

    Frame result;
    result.valid = true;
    result.slaveAddress = static_cast<quint8>(frame.at(0));
    result.funcCode = static_cast<quint8>(frame.at(1));
    result.cmdNum = static_cast<quint8>(frame.at(2));
    result.cmdSize = static_cast<quint8>(frame.at(3));
    result.ctrl = static_cast<quint8>(frame.at(4));
    result.func = static_cast<Func>((result.ctrl >> 3) & 0x1F);
    result.status = static_cast<Status>((result.ctrl >> 1) & 0x03);
    result.type = static_cast<FrameType>(result.ctrl & 0x01);
    result.temperatureRaw = readU16LittleEndian(frame, 5);
    result.temperatureCelsius = rawToCelsius(result.temperatureRaw);
    for (int i = 0; i < ChannelCount; ++i) {
        result.channels[static_cast<size_t>(i)] =
                readU16LittleEndian(frame, 7 + i * 2);
    }

    if (errorString) {
        errorString->clear();
    }
    return result;
}

bool DetectionModule::Frame::isError() const
{
    return valid && status == Status::Error;
}

bool DetectionModule::Frame::isReadReply() const
{
    return valid && type == FrameType::Report;
}

bool DetectionModule::Frame::isChannelReport() const
{
    return valid && type == FrameType::Report && func == Func::ReadTempAndChannels;
}

bool DetectionModule::Frame::isSpeedReport() const
{
    return valid && type == FrameType::Report && func == Func::MotorSpeed;
}

bool DetectionModule::Frame::isLedCurrentReport() const
{
    return valid && type == FrameType::Report && func == Func::LedLight;
}

quint16 DetectionModule::Frame::channel(int channel) const
{
    if (!valid || channel < 1 || channel > ChannelCount) {
        return 0;
    }
    return channels[static_cast<size_t>(channel - 1)];
}

QString DetectionModule::funcText(Func func)
{
    switch (func) {
        case Func::ReadTempAndChannels: return QStringLiteral("读取温度和通道数据");
        case Func::LedLight:            return QStringLiteral("LED 调光/读电流");
        case Func::MotorSpeed:          return QStringLiteral("电机转速");
        case Func::Save:                return QStringLiteral("保存参数");
        case Func::LedCurrent:          return QStringLiteral("设置 LED 电流");
        default: break;
    }
    return QStringLiteral("未知功能(0x%1)")
            .arg(static_cast<int>(func), 2, 16, QLatin1Char('0'));
}

QString DetectionModule::statusText(Status status)
{
    switch (status) {
        case Status::Waiting:   return QStringLiteral("等待");
        case Status::Running:   return QStringLiteral("运行中");
        case Status::Completed: return QStringLiteral("执行完成");
        case Status::Error:     return QStringLiteral("命令错误(从机掉线)");
        default: break;
    }
    return QStringLiteral("未知状态(%1)").arg(static_cast<int>(status));
}

} // namespace platelet
