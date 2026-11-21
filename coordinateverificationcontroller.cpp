#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "coordinateverificationcontroller.h"

#include <QDebug>
#include <QTimer>
#include <QtMath>

CoordinateVerificationController::CoordinateVerificationController(
        QObject *parent)
    : QObject(parent)
    , m_protocol(&m_ownProtocol)
{
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout,
            this, &CoordinateVerificationController::handleStageTimeout);
}

void CoordinateVerificationController::setProtocol(
        platelet::MotionProtocol *protocol)
{
    m_protocol = protocol ? protocol : &m_ownProtocol;
}

bool CoordinateVerificationController::isBusy() const
{
    return m_busy;
}

void CoordinateVerificationController::setStageTimeouts(int zResetMs, int xyMs,
                                                        int zMoveMs)
{
    if (zResetMs > 0) {
        m_zResetTimeoutMs = zResetMs;
    }
    if (xyMs > 0) {
        m_xyTimeoutMs = xyMs;
    }
    if (zMoveMs > 0) {
        m_zMoveTimeoutMs = zMoveMs;
    }
}

void CoordinateVerificationController::start(
        const InstrumentCoordinates::MoveTarget &target)
{
    if (m_busy) {
        emit finished(false, tr("定位校验正在进行，请等待当前流程结束"));
        return;
    }
    if (!target.valid()) {
        emit finished(false, tr("目标坐标未设置完整，无法触发移动"));
        return;
    }

    m_target = target;
    m_pendingResets.clear();
    m_xyCommandNumber = 0;
    m_zCommandNumber = 0;
    m_busy = true;

    const QString message = tr("定位校验：%1 · %2，正在复位三个 Z 轴…")
            .arg(targetName(target), axisDescription(target.axis));
    qInfo() << message;
    emit statusChanged(message);

    beginStage(Stage::ResettingZ, m_zResetTimeoutMs);
    sendZResets();
}

void CoordinateVerificationController::handleReport(
        const platelet::MotionProtocol::Frame &frame)
{
    if (!m_busy || !frame.valid || !frame.isReport()) {
        return;
    }

    using platelet::MotionProtocol;

    if (m_stage == Stage::ResettingZ) {
        int index = -1;
        for (int i = 0; i < m_pendingResets.size(); ++i) {
            if (m_pendingResets.at(i).cmdNum == frame.cmdNum) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            return;
        }

        const PendingReset pending = m_pendingResets.at(index);
        if (!isReportFor(frame, pending.cmdNum, MotionProtocol::ZAxisBoard,
                         protocolAxis(pending.axis))) {
            qWarning() << "定位校验：忽略不匹配的 Z 复位报告，命令号"
                       << frame.cmdNum;
            return;
        }

        if (frame.status == MotionProtocol::Status::Error
                || frame.fault != 0) {
            const QString reason = frame.fault != 0
                    ? frame.faultDescription()
                    : MotionProtocol::statusText(frame.status);
            finish(false, tr("%1复位失败：%2，已中止；XY 未移动")
                   .arg(axisDescription(pending.axis), reason));
            return;
        }
        if (frame.status != MotionProtocol::Status::Completed) {
            return;
        }

        m_pendingResets.removeAt(index);
        if (m_pendingResets.isEmpty()) {
            sendXyMove();
        } else {
            const QString message = tr("Z 轴复位完成 %1/3，等待其余轴…")
                    .arg(3 - m_pendingResets.size());
            qInfo() << message;
            emit statusChanged(message);
        }
        return;
    }

    if (m_stage == Stage::MovingXy) {
        if (frame.cmdNum != m_xyCommandNumber
                || !isReportFor(frame, m_xyCommandNumber,
                                MotionProtocol::MainBoard,
                                MotionProtocol::Axis::Xy)) {
            return;
        }
        if (frame.status == MotionProtocol::Status::Error
                || frame.fault != 0) {
            const QString reason = frame.fault != 0
                    ? frame.faultDescription()
                    : MotionProtocol::statusText(frame.status);
            finish(false, tr("XY 移动失败：%1，已中止").arg(reason));
            return;
        }
        if (frame.status == MotionProtocol::Status::Completed) {
            sendZMove();
        }
        return;
    }

    if (m_stage == Stage::MovingZ) {
        if (frame.cmdNum != m_zCommandNumber
                || !isReportFor(frame, m_zCommandNumber,
                                MotionProtocol::ZAxisBoard,
                                protocolAxis(m_target.axis))) {
            return;
        }
        if (frame.status == MotionProtocol::Status::Error
                || frame.fault != 0) {
            const QString reason = frame.fault != 0
                    ? frame.faultDescription()
                    : MotionProtocol::statusText(frame.status);
            finish(false, tr("%1下降失败：%2")
                   .arg(axisDescription(m_target.axis), reason));
            return;
        }
        if (frame.status == MotionProtocol::Status::Completed) {
            finish(true, tr("定位校验完成：%1 · %2，X=%3 Y=%4 Z=%5")
                   .arg(targetName(m_target),
                        axisDescription(m_target.axis))
                   .arg(qRound(m_target.x))
                   .arg(qRound(m_target.y))
                   .arg(qRound(m_target.z)));
        }
    }
}

void CoordinateVerificationController::cancel()
{
    if (!m_busy) {
        return;
    }
    finish(false, tr("定位校验已取消"));
}

bool CoordinateVerificationController::sendZResets()
{
    using ZAxis = InstrumentCoordinates::ZAxis;

    struct Request {
        ZAxis axis;
        QByteArray frame;
    };

    const ZAxis axes[3] = {
        ZAxis::Gripper, ZAxis::ReagentNeedle, ZAxis::SampleNeedle
    };

    QVector<Request> requests;
    for (ZAxis axis : axes) {
        QByteArray frame;
        switch (axis) {
        case ZAxis::Gripper:
            frame = m_protocol->resetGripper();
            break;
        case ZAxis::ReagentNeedle:
            frame = m_protocol->resetReagentNeedle();
            break;
        case ZAxis::SampleNeedle:
            frame = m_protocol->resetSampleNeedle();
            break;
        default:
            break;
        }
        if (frame.isEmpty()) {
            finish(false, tr("生成%1复位命令失败：%2")
                   .arg(axisDescription(axis),
                        m_protocol->lastError().isEmpty()
                        ? tr("未知错误") : m_protocol->lastError()));
            return false;
        }
        Request request;
        request.axis = axis;
        request.frame = frame;
        requests.append(request);
    }

    // 三帧都构好后再统一下发，避免只有部分轴收到复位命令。
    for (const Request &request : requests) {
        PendingReset pending;
        pending.cmdNum = commandNumberOf(request.frame);
        pending.axis = request.axis;
        m_pendingResets.append(pending);
        emit sendRequested(
                    request.frame,
                    tr("定位校验：%1复位")
                    .arg(axisDescription(request.axis)));
    }
    return true;
}

bool CoordinateVerificationController::sendXyMove()
{
    using MotionProtocol = platelet::MotionProtocol;

    const int x = qRound(m_target.x);
    const int y = qRound(m_target.y);
    const QByteArray frame = m_protocol->moveXy(
                x, y, xyReference(m_target.axis), MotionProtocol::Area::Test);
    if (frame.isEmpty()) {
        finish(false, tr("生成 XY 移动命令失败：%1")
               .arg(m_protocol->lastError().isEmpty()
                    ? tr("未知错误") : m_protocol->lastError()));
        return false;
    }

    m_xyCommandNumber = commandNumberOf(frame);
    const QString message =
            tr("Z 轴已全部复位，正在移动 XY 到 X=%1 Y=%2…").arg(x).arg(y);
    qInfo() << message;
    emit statusChanged(message);

    beginStage(Stage::MovingXy, m_xyTimeoutMs);
    emit sendRequested(frame,
                       tr("定位校验：XY 移动到 X=%1 Y=%2").arg(x).arg(y));
    return true;
}

bool CoordinateVerificationController::sendZMove()
{
    const int z = qRound(m_target.z);
    QByteArray frame;
    switch (m_target.axis) {
    case InstrumentCoordinates::ZAxis::Gripper:
        frame = m_protocol->moveGripperTo(z);
        break;
    case InstrumentCoordinates::ZAxis::ReagentNeedle:
        frame = m_protocol->moveReagentNeedleTo(z);
        break;
    case InstrumentCoordinates::ZAxis::SampleNeedle:
        frame = m_protocol->moveSampleNeedleTo(z);
        break;
    default:
        break;
    }
    if (frame.isEmpty()) {
        finish(false, tr("生成%1下降命令失败：%2")
               .arg(axisDescription(m_target.axis),
                    m_protocol->lastError().isEmpty()
                    ? tr("未知错误") : m_protocol->lastError()));
        return false;
    }

    m_zCommandNumber = commandNumberOf(frame);
    const QString message =
            tr("XY 已到位，正在让%1下降到 Z=%2…")
            .arg(axisDescription(m_target.axis)).arg(z);
    qInfo() << message;
    emit statusChanged(message);

    beginStage(Stage::MovingZ, m_zMoveTimeoutMs);
    emit sendRequested(
                frame,
                tr("定位校验：%1下降到 Z=%2")
                .arg(axisDescription(m_target.axis)).arg(z));
    return true;
}

void CoordinateVerificationController::beginStage(Stage stage, int timeoutMs)
{
    m_stage = stage;
    armTimeout(timeoutMs);
}

void CoordinateVerificationController::armTimeout(int timeoutMs)
{
    m_timeoutTimer->stop();
    if (timeoutMs > 0) {
        m_timeoutTimer->start(timeoutMs);
    }
}

void CoordinateVerificationController::stopTimeout()
{
    m_timeoutTimer->stop();
}

void CoordinateVerificationController::handleStageTimeout()
{
    switch (m_stage) {
    case Stage::ResettingZ:
        finish(false, tr("Z 轴复位超时，已中止；XY 未移动"));
        break;
    case Stage::MovingXy:
        finish(false, tr("XY 移动超时，已中止"));
        break;
    case Stage::MovingZ:
        finish(false, tr("Z 轴下降超时，已中止"));
        break;
    case Stage::Idle:
        break;
    }
}

void CoordinateVerificationController::finish(bool ok, const QString &message)
{
    m_busy = false;
    m_stage = Stage::Idle;
    stopTimeout();
    m_pendingResets.clear();
    m_xyCommandNumber = 0;
    m_zCommandNumber = 0;

    if (ok) {
        qInfo() << message;
    } else {
        qWarning() << message;
    }
    emit finished(ok, message);
}

bool CoordinateVerificationController::isReportFor(
        const platelet::MotionProtocol::Frame &frame, quint8 commandNumber,
        quint8 slaveAddress, platelet::MotionProtocol::Axis axis) const
{
    return frame.valid && frame.isReport()
            && frame.cmdNum == commandNumber
            && frame.slaveAddress == slaveAddress
            && frame.axis == axis;
}

quint8 CoordinateVerificationController::commandNumberOf(
        const QByteArray &frame)
{
    return frame.size() > 2 ? static_cast<quint8>(frame.at(2)) : 0;
}

platelet::MotionProtocol::Axis CoordinateVerificationController::protocolAxis(
        InstrumentCoordinates::ZAxis axis)
{
    using Axis = platelet::MotionProtocol::Axis;
    switch (axis) {
    case InstrumentCoordinates::ZAxis::Gripper:
        return Axis::Gripper;
    case InstrumentCoordinates::ZAxis::ReagentNeedle:
        return Axis::ReagentNeedle;
    case InstrumentCoordinates::ZAxis::SampleNeedle:
        return Axis::SampleNeedle;
    case InstrumentCoordinates::ZAxis::AxisCount:
        break;
    }
    return Axis::Gripper;
}

platelet::MotionProtocol::XyReference
CoordinateVerificationController::xyReference(
        InstrumentCoordinates::ZAxis axis)
{
    using Reference = platelet::MotionProtocol::XyReference;
    switch (axis) {
    case InstrumentCoordinates::ZAxis::Gripper:
        return Reference::Gripper;
    case InstrumentCoordinates::ZAxis::ReagentNeedle:
        return Reference::ReagentNeedle;
    case InstrumentCoordinates::ZAxis::SampleNeedle:
        return Reference::BloodNeedle;
    case InstrumentCoordinates::ZAxis::AxisCount:
        break;
    }
    return Reference::BloodNeedle;
}

QString CoordinateVerificationController::targetName(
        const InstrumentCoordinates::MoveTarget &target)
{
    switch (target.kind) {
    case InstrumentCoordinates::TargetKind::Origin:
        return tr("原点");
    case InstrumentCoordinates::TargetKind::CleaningHole:
        return tr("清洗孔位");
    case InstrumentCoordinates::TargetKind::DiscardCup:
        return tr("弃杯孔位");
    case InstrumentCoordinates::TargetKind::Channel:
        return tr("CH%1").arg(target.index + 1);
    case InstrumentCoordinates::TargetKind::Tube:
        return tr("试管 %1").arg(target.index + 1);
    case InstrumentCoordinates::TargetKind::Reagent:
        return InstrumentCoordinates::reagentPositionName(target.index);
    case InstrumentCoordinates::TargetKind::SampleWell:
        return tr("样本孔 %1").arg(target.index + 1);
    }
    return tr("目标");
}

QString CoordinateVerificationController::axisDescription(
        InstrumentCoordinates::ZAxis axis)
{
    switch (axis) {
    case InstrumentCoordinates::ZAxis::Gripper:
        return tr("抓手");
    case InstrumentCoordinates::ZAxis::ReagentNeedle:
        return tr("试剂针");
    case InstrumentCoordinates::ZAxis::SampleNeedle:
        return tr("样本针");
    case InstrumentCoordinates::ZAxis::AxisCount:
        break;
    }
    return tr("未知轴");
}
