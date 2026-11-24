#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "motionprotocol.h"

namespace {

void appendInt32Le(QByteArray &frame, qint32 value)
{
    const quint32 raw = static_cast<quint32>(value);
    frame.append(static_cast<char>(raw & 0x000000FFu));
    frame.append(static_cast<char>((raw >> 8) & 0x000000FFu));
    frame.append(static_cast<char>((raw >> 16) & 0x000000FFu));
    frame.append(static_cast<char>((raw >> 24) & 0x000000FFu));
}

qint32 readInt32Le(const QByteArray &frame, int offset)
{
    const quint32 raw =
            static_cast<quint32>(static_cast<quint8>(frame.at(offset)))
            | (static_cast<quint32>(
                   static_cast<quint8>(frame.at(offset + 1))) << 8)
            | (static_cast<quint32>(
                   static_cast<quint8>(frame.at(offset + 2))) << 16)
            | (static_cast<quint32>(
                   static_cast<quint8>(frame.at(offset + 3))) << 24);
    return static_cast<qint32>(raw);
}

quint8 makeCtrl(platelet::MotionProtocol::Axis axis,
                platelet::MotionProtocol::OperationMode mode,
                platelet::MotionProtocol::Status status,
                platelet::MotionProtocol::FrameType type)
{
    return static_cast<quint8>(
                ((static_cast<quint8>(axis) & 0x07) << 5)
                | ((static_cast<quint8>(mode) & 0x03) << 3)
                | ((static_cast<quint8>(status) & 0x03) << 1)
                | (static_cast<quint8>(type) & 0x01));
}

} // namespace

namespace platelet {

MotionProtocol::MotionProtocol() = default;

quint8 MotionProtocol::lastCommandNumber() const
{
    return m_lastCmdNum;
}

void MotionProtocol::resetCommandNumber(quint8 next)
{
    // 老软件动作序列的 cmd_num 从 0 开始（0、1、2、3…），这里保留 0；
    // 部分主板对第一条动作命令的命令号有要求。
    m_nextCmdNum = next;
}

void MotionProtocol::setCmdSize(quint8 cmdSize)
{
    m_cmdSize = cmdSize;
}

quint8 MotionProtocol::cmdSize() const
{
    return m_cmdSize;
}

QString MotionProtocol::lastError() const
{
    return m_lastError;
}

QByteArray MotionProtocol::moveXy(int x, int y, XyReference reference,
                                  Area area)
{
    m_lastError.clear();
    const quint8 axisByte = static_cast<quint8>(
                (static_cast<quint8>(reference) & 0x03)
                | ((static_cast<quint8>(area) & 0x03) << 2));
    return buildCommand(Slave::MainBoard, Axis::Xy, OperationMode::Position,
                        axisByte, x, y);
}

QByteArray MotionProtocol::moveGripperTo(int height, Area area,
                                         bool grabCup, bool liquidLevel)
{
    m_lastError.clear();
    return buildZMove(Axis::Gripper, height, area, grabCup, liquidLevel);
}

QByteArray MotionProtocol::moveGripperDownSpeed(int speed, int limitHeight,
                                                Area area, bool grabCup)
{
    m_lastError.clear();
    return buildZSpeedMove(Axis::Gripper, speed, limitHeight, area, grabCup);
}

QByteArray MotionProtocol::moveReagentNeedleTo(int height, Area area,
                                               bool liquidLevel)
{
    m_lastError.clear();
    return buildZMove(Axis::ReagentNeedle, height, area, false, liquidLevel);
}

QByteArray MotionProtocol::moveSampleNeedleTo(int height, Area area,
                                              bool liquidLevel)
{
    m_lastError.clear();
    return buildZMove(Axis::SampleNeedle, height, area, false, liquidLevel);
}

QByteArray MotionProtocol::resetXy(int speed)
{
    m_lastError.clear();
    if (speed <= 0) {
        m_lastError = QStringLiteral("XY 复位速度必须大于 0，收到 %1")
                .arg(speed);
        return QByteArray();
    }
    return buildCommand(Slave::MainBoard, Axis::Xy,
                        OperationMode::NegativeSpeed, 0, speed, speed);
}

QByteArray MotionProtocol::resetGripper(int speed, Area area, bool grabCup)
{
    return buildZReset(Axis::Gripper, speed, area, grabCup);
}

QByteArray MotionProtocol::resetReagentNeedle(int speed, Area area)
{
    return buildZReset(Axis::ReagentNeedle, speed, area, false);
}

QByteArray MotionProtocol::resetSampleNeedle(int speed, Area area)
{
    return buildZReset(Axis::SampleNeedle, speed, area, false);
}

QByteArray MotionProtocol::moveBloodPump(int units, bool aspirate,
                                         bool clean, int cleaningTimeMs)
{
    if (!checkPumpDistance(units)) {
        return QByteArray();
    }
    return buildPumpAction(Axis::BloodPump, OperationMode::Position,
                           units, aspirate, clean, cleaningTimeMs);
}

QByteArray MotionProtocol::moveReagentPump(int units, bool aspirate,
                                           bool clean, int cleaningTimeMs)
{
    if (!checkPumpDistance(units)) {
        return QByteArray();
    }
    return buildPumpAction(Axis::ReagentPump, OperationMode::Position,
                           units, aspirate, clean, cleaningTimeMs);
}

QByteArray MotionProtocol::resetBloodPump(int speed, bool clean)
{
    if (speed <= 0 || speed > PumpFullScale) {
        m_lastError = QStringLiteral("血样泵复位速度必须在 1~%1，收到 %2")
                .arg(PumpFullScale).arg(speed);
        return QByteArray();
    }
    m_lastError.clear();
    return buildPumpAction(Axis::BloodPump, OperationMode::NegativeSpeed,
                           speed, false, clean, 0);
}

QByteArray MotionProtocol::resetReagentPump(int speed, bool clean)
{
    if (speed <= 0 || speed > PumpFullScale) {
        m_lastError = QStringLiteral("试剂泵复位速度必须在 1~%1，收到 %2")
                .arg(PumpFullScale).arg(speed);
        return QByteArray();
    }
    m_lastError.clear();
    return buildPumpAction(Axis::ReagentPump, OperationMode::NegativeSpeed,
                           speed, false, clean, 0);
}

QByteArray MotionProtocol::buildCommand(Slave slave, Axis axis,
                                        OperationMode mode, quint8 axisByte,
                                        qint32 distance1, qint32 distance2)
{
    QByteArray frame;
    frame.reserve(FrameSize);
    frame.append(static_cast<char>(slave));
    frame.append(static_cast<char>(FuncCode));
    frame.append(static_cast<char>(takeCommandNumber()));
    frame.append(static_cast<char>(m_cmdSize));
    frame.append(static_cast<char>(
                     makeCtrl(axis, mode, Status::Waiting,
                              FrameType::Command)));
    frame.append(static_cast<char>(axisByte));
    appendInt32Le(frame, distance1);
    appendInt32Le(frame, distance2);
    frame.append(static_cast<char>(0));   // fault
    return frame;
}

QByteArray MotionProtocol::buildZMove(Axis axis, qint32 height, Area area,
                                      bool grabCup, bool liquidLevel)
{
    const quint8 axisByte = static_cast<quint8>(
                zReference(axis)
                | ((static_cast<quint8>(area) & 0x03) << 2)
                | (grabCup ? 0x10 : 0x00)   // gripper_status / 空气阀
                | (liquidLevel ? 0x20 : 0x00));
    return buildCommand(Slave::ZAxisBoard, axis, OperationMode::Position,
                        axisByte, height, 0);
}

QByteArray MotionProtocol::buildZSpeedMove(Axis axis, int speed,
                                           int limitHeight, Area area,
                                           bool grabCup)
{
    m_lastError.clear();
    if (speed <= 0 || speed > PumpFullScale) {
        m_lastError = QStringLiteral("速度模式速度必须在 1~%1，收到 %2")
                .arg(PumpFullScale).arg(speed);
        return QByteArray();
    }
    const quint8 axisByte = static_cast<quint8>(
                zReference(axis)
                | ((static_cast<quint8>(area) & 0x03) << 2)
                | (grabCup ? 0x10 : 0x00));
    // 正速度模式：distance1=速度，distance2=行程上限
    return buildCommand(Slave::ZAxisBoard, axis, OperationMode::PositiveSpeed,
                        axisByte, speed, limitHeight);
}

QByteArray MotionProtocol::buildZReset(Axis axis, int speed, Area area,
                                        bool grabCup)
{
    m_lastError.clear();
    if (speed <= 0 || speed > PumpFullScale) {
        m_lastError = QStringLiteral("Z 轴复位速度必须在 1~%1，收到 %2")
                .arg(PumpFullScale).arg(speed);
        return QByteArray();
    }
    // 旧上位机复位时 sport_asix 固定为 0；抓手复位可带 gripper_status。
    const quint8 axisByte = static_cast<quint8>(
                ((static_cast<quint8>(area) & 0x03) << 2)
                | (grabCup ? 0x10 : 0x00));
    return buildCommand(Slave::ZAxisBoard, axis, OperationMode::NegativeSpeed,
                        axisByte, speed, 0);
}

QByteArray MotionProtocol::buildPumpAction(Axis axis, OperationMode mode,
                                           int units, bool aspirate,
                                           bool clean, int cleaningTimeMs)
{
    const quint8 axisByte = static_cast<quint8>(
                (aspirate ? 0x10 : 0x00)
                | (clean ? 0x60 : 0x00));
    const qint32 distance2 = clean ? qMax(0, cleaningTimeMs) : 0;
    return buildCommand(Slave::MainBoard, axis, mode, axisByte,
                        units, distance2);
}

bool MotionProtocol::checkPumpDistance(int units)
{
    m_lastError.clear();
    if (units < 0 || units > PumpFullScale) {
        m_lastError = QStringLiteral("柱塞泵行程 %1 超出 0~%2")
                .arg(units).arg(PumpFullScale);
        return false;
    }
    return true;
}

quint8 MotionProtocol::takeCommandNumber()
{
    const quint8 number = m_nextCmdNum;
    m_lastCmdNum = number;
    m_nextCmdNum = (number >= 0xFF)
            ? 0 : static_cast<quint8>(number + 1);
    return number;
}

quint8 MotionProtocol::zReference(Axis axis)
{
    switch (axis) {
    case Axis::Gripper:       return 2;   // 坐标相对抓手
    case Axis::ReagentNeedle: return 1;   // 坐标相对试剂针
    case Axis::SampleNeedle:  return 0;   // 坐标相对血样针
    default:                  return 0;
    }
}

MotionProtocol::Frame MotionProtocol::parse(const QByteArray &frame,
                                            QString *errorString)
{
    const auto fail = [errorString](const QString &message) -> Frame {
        if (errorString) {
            *errorString = message;
        }
        return Frame();
    };

    if (frame.size() < FrameSize) {
        return fail(QStringLiteral("帧长度不足：需要 %1 字节，实际 %2 字节")
                    .arg(int(FrameSize)).arg(frame.size()));
    }
    if (static_cast<quint8>(frame.at(1)) != FuncCode) {
        return fail(QStringLiteral("功能码错误：期望 0x17，实际 0x%1")
                    .arg(static_cast<quint8>(frame.at(1)),
                         2, 16, QLatin1Char('0')));
    }

    Frame result;
    result.valid = true;
    result.slaveAddress = static_cast<quint8>(frame.at(0));
    result.funcCode = static_cast<quint8>(frame.at(1));
    result.cmdNum = static_cast<quint8>(frame.at(2));
    result.cmdSize = static_cast<quint8>(frame.at(3));
    result.ctrl = static_cast<quint8>(frame.at(4));
    result.type = static_cast<FrameType>(result.ctrl & 0x01);
    result.status = static_cast<Status>((result.ctrl >> 1) & 0x03);
    result.mode = static_cast<OperationMode>((result.ctrl >> 3) & 0x03);
    result.axis = static_cast<Axis>((result.ctrl >> 5) & 0x07);
    result.axisByte = static_cast<quint8>(frame.at(5));
    result.distance1 = readInt32Le(frame, 6);
    result.distance2 = readInt32Le(frame, 10);
    result.fault = static_cast<quint8>(frame.at(14));

    if (errorString) {
        errorString->clear();
    }
    return result;
}

bool MotionProtocol::Frame::isReport() const
{
    // 老软件解析运动回包时不检查 ctrl.bit0（type）。部分主板 0x17
    // 完成帧的 type 位仍为 0，若严格要求 type=1 会永远等不到完成报告。
    // 串口收到的 0x17 帧只可能是从机应答，因此按“有效应答”处理；
    // type 位仍保留在 Frame::type 中供日志排查。
    return valid;
}

bool MotionProtocol::Frame::isError() const
{
    return valid && (status == Status::Error || fault != 0);
}

QString MotionProtocol::Frame::faultDescription() const
{
    return faultText(fault);
}

quint32 MotionProtocol::Frame::gripperVacuum() const
{
    if (!isReport() || axis != Axis::Gripper) {
        return 0;
    }
    // 与老软件 backHandsSuckAirValue 一致：报告帧 distance2 即负压原始值。
    return static_cast<quint32>(distance2);
}

bool MotionProtocol::Frame::gripperSuctionBit() const
{
    return (axisByte & 0x10) != 0;
}

QString MotionProtocol::axisName(Axis axis)
{
    switch (axis) {
    case Axis::Gripper:       return QStringLiteral("抓手");
    case Axis::ReagentNeedle: return QStringLiteral("试剂针");
    case Axis::SampleNeedle:  return QStringLiteral("样本针");
    case Axis::ReagentPump:   return QStringLiteral("试剂泵");
    case Axis::Xy:            return QStringLiteral("XY 联动");
    default: break;
    }
    return QStringLiteral("未知轴(%1)").arg(static_cast<int>(axis));
}

QString MotionProtocol::areaName(Area area)
{
    switch (area) {
    case Area::Test:        return QStringLiteral("测试区");
    case Area::EmptyCup:    return QStringLiteral("空杯区");
    case Area::BloodSample: return QStringLiteral("血样区");
    case Area::DiscardCup:  return QStringLiteral("丢杯区");
    }
    return QStringLiteral("未知区域(%1)").arg(static_cast<int>(area));
}

QString MotionProtocol::xyReferenceName(XyReference reference)
{
    switch (reference) {
    case XyReference::BloodNeedle:   return QStringLiteral("血样针");
    case XyReference::ReagentNeedle: return QStringLiteral("试剂针");
    case XyReference::Gripper:       return QStringLiteral("抓手");
    }
    return QStringLiteral("未知参考(%1)").arg(static_cast<int>(reference));
}

QString MotionProtocol::operationModeName(OperationMode mode)
{
    switch (mode) {
    case OperationMode::Position:      return QStringLiteral("位置模式");
    case OperationMode::PositiveSpeed: return QStringLiteral("正速度");
    case OperationMode::NegativeSpeed: return QStringLiteral("负速度复位");
    case OperationMode::Hold:          return QStringLiteral("保持模式");
    }
    return QStringLiteral("未知模式(%1)").arg(static_cast<int>(mode));
}

QString MotionProtocol::statusText(Status status)
{
    switch (status) {
    case Status::Waiting:   return QStringLiteral("等待");
    case Status::Running:   return QStringLiteral("运行中");
    case Status::Completed: return QStringLiteral("执行完成");
    case Status::Error:     return QStringLiteral("命令错误");
    }
    return QStringLiteral("未知状态(%1)").arg(static_cast<int>(status));
}

QString MotionProtocol::faultText(quint8 fault)
{
    switch (fault) {
    case 0: return QStringLiteral("无故障");
    case 1: return QStringLiteral("Z轴通信超时");
    case 2: return QStringLiteral("XY执行错误：血样针或试剂针未复位");
    case 3: return QStringLiteral("X电机运动超时");
    case 4: return QStringLiteral("Y电机运动超时");
    case 5: return QStringLiteral("XY电机运动超时");
    }
    return QStringLiteral("未知故障(0x%1)")
            .arg(static_cast<int>(fault), 2, 16, QLatin1Char('0'));
}

} // namespace platelet
