#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "resetcontroller.h"

#include <QDebug>
#include <QTimer>
#include <QtMath>

ResetController::ResetController(InstrumentCoordinates *coordinates,
                                 QObject *parent)
    : QObject(parent)
    , m_coordinates(coordinates)
    , m_protocol(&m_ownProtocol)
{
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        if (!m_busy || m_currentStep < 0 || m_currentStep >= m_steps.size()) {
            return;
        }
        finish(false, tr("%1超时，复位已中止")
               .arg(m_steps.at(m_currentStep).description));
    });
}

void ResetController::setProtocol(platelet::MotionProtocol *protocol)
{
    m_protocol = protocol ? protocol : &m_ownProtocol;
}

bool ResetController::isBusy() const
{
    return m_busy;
}

void ResetController::start()
{
    if (m_busy) {
        emit finished(false, tr("复位正在进行，请等待当前动作结束"));
        return;
    }

    QString error;
    if (!buildSequence(&error)) {
        m_steps.clear();
        qWarning() << "复位构帧失败:" << error;
        emit finished(false, error);
        return;
    }

    m_busy = true;
    m_currentStep = -1;
    qInfo() << "复位开始，共" << m_steps.size() << "步";
    sendNextStep();
}

void ResetController::handleReport(
        const platelet::MotionProtocol::Frame &frame)
{
    if (!m_busy || !frame.valid) {
        return;
    }
    if (m_currentStep < 0 || m_currentStep >= m_steps.size()) {
        return;
    }

    const Step &step = m_steps.at(m_currentStep);
    if (!isReportFor(frame, step)) {
        return;
    }

    using platelet::MotionProtocol;
    if (frame.status == MotionProtocol::Status::Error || frame.fault != 0) {
        const QString reason = frame.fault != 0
                ? frame.faultDescription()
                : MotionProtocol::statusText(frame.status);
        finish(false, tr("%1失败：%2，复位已中止")
               .arg(step.description, reason));
        return;
    }
    if (frame.status != MotionProtocol::Status::Completed) {
        return;
    }

    m_timeoutTimer->stop();
    sendNextStep();
}

void ResetController::cancel()
{
    if (!m_busy) {
        return;
    }
    finish(false, tr("复位已取消"));
}

bool ResetController::buildSequence(QString *error)
{
    using MotionProtocol = platelet::MotionProtocol;
    using ZAxis = InstrumentCoordinates::ZAxis;
    using Axis = MotionProtocol::Axis;
    using Reference = MotionProtocol::XyReference;
    using Area = MotionProtocol::Area;

    m_steps.clear();
    if (!m_coordinates) {
        if (error) {
            *error = tr("坐标表不可用，无法执行复位");
        }
        return false;
    }

    const InstrumentCoordinates::MoveTarget origin =
            m_coordinates->locateOrigin(ZAxis::SampleNeedle);
    if (!origin.xyValid) {
        if (error) {
            *error = tr("坐标表「原点」的样本针 XY 未设置，无法执行复位");
        }
        return false;
    }

    constexpr quint8 kZAxis = MotionProtocol::ZAxisBoard;
    constexpr quint8 kMainBoard = MotionProtocol::MainBoard;

    // 1. 三根 Z 轴依次复位
    if (!appendStep(m_protocol->resetSampleNeedle(),
                    tr("复位：样本针复位"),
                    kZAxis, Axis::SampleNeedle, m_zResetTimeoutMs, error)
            || !appendStep(m_protocol->resetReagentNeedle(),
                           tr("复位：试剂针复位"),
                           kZAxis, Axis::ReagentNeedle, m_zResetTimeoutMs,
                           error)
            || !appendStep(m_protocol->resetGripper(),
                           tr("复位：抓手复位"),
                           kZAxis, Axis::Gripper, m_zResetTimeoutMs, error)) {
        return false;
    }

    // 2. XY 负速度模式复位
    if (!appendStep(m_protocol->resetXy(), tr("复位：XY 复位"),
                    kMainBoard, Axis::Xy, m_xyTimeoutMs, error)) {
        return false;
    }

    // 3. XY 移动到原点位置
    if (!appendStep(m_protocol->moveXy(qRound(origin.x),
                                       qRound(origin.y),
                                       Reference::BloodNeedle, Area::Test),
                    tr("复位：XY 到原点"),
                    kMainBoard, Axis::Xy, m_xyTimeoutMs, error)) {
        return false;
    }

    return true;
}

bool ResetController::appendStep(const QByteArray &frame,
                                 const QString &description,
                                 quint8 slaveAddress,
                                 platelet::MotionProtocol::Axis axis,
                                 int timeoutMs, QString *error)
{
    if (frame.isEmpty()) {
        if (error) {
            *error = tr("%1构帧失败：%2")
                    .arg(description,
                         m_protocol->lastError().isEmpty()
                         ? tr("未知错误") : m_protocol->lastError());
        }
        return false;
    }

    Step step;
    step.frame = frame;
    step.description = description;
    step.slaveAddress = slaveAddress;
    step.axis = axis;
    step.timeoutMs = timeoutMs;
    m_steps.append(step);
    return true;
}

void ResetController::sendNextStep()
{
    if (!m_busy) {
        return;
    }

    ++m_currentStep;
    if (m_currentStep >= m_steps.size()) {
        finish(true, tr("复位完成，XY 已移动到原点"));
        return;
    }

    const Step &step = m_steps.at(m_currentStep);
    qInfo() << "复位步骤" << (m_currentStep + 1) << "/"
            << m_steps.size() << step.description;
    emit statusChanged(step.description);

    m_timeoutTimer->start(step.timeoutMs);
    emit sendRequested(step.frame, step.description);
}

void ResetController::finish(bool ok, const QString &message)
{
    m_timeoutTimer->stop();
    m_busy = false;
    m_currentStep = -1;
    if (ok) {
        qInfo() << message;
    } else {
        qWarning() << message;
    }
    emit finished(ok, message);
}

bool ResetController::isReportFor(
        const platelet::MotionProtocol::Frame &frame, const Step &step) const
{
    if (step.frame.size() <= 2) {
        return false;
    }
    return frame.valid
            && frame.cmdNum == static_cast<quint8>(step.frame.at(2))
            && frame.slaveAddress == step.slaveAddress
            && frame.axis == step.axis;
}
