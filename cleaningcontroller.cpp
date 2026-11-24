#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "cleaningcontroller.h"

#include "instrumentparameters.h"
#include "pumppositiontracker.h"

#include <QDebug>
#include <QTimer>
#include <QtMath>

namespace {
constexpr int kSampleAirSuck = 360;
constexpr int kReagentAirSuck = 720;
constexpr int kZAxis = platelet::MotionProtocol::ZAxisBoard;
constexpr int kMainBoard = platelet::MotionProtocol::MainBoard;
}

CleaningController::CleaningController(InstrumentCoordinates *coordinates,
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
        finish(false, tr("%1超时，清洗已中止")
               .arg(m_steps.at(m_currentStep).description));
    });
}

void CleaningController::setProtocol(platelet::MotionProtocol *protocol)
{
    m_protocol = protocol ? protocol : &m_ownProtocol;
}

void CleaningController::setCleaningTimeMs(int ms)
{
    if (ms > 0) {
        m_cleaningTimeMs = ms;
    }
}

void CleaningController::setStageTimeouts(int zResetMs, int xyMs,
                                          int zMoveMs, int pumpMs,
                                          int pumpCleanMs)
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
    if (pumpMs > 0) {
        m_pumpTimeoutMs = pumpMs;
    }
    if (pumpCleanMs > 0) {
        m_pumpCleanTimeoutMs = pumpCleanMs;
    }
}

bool CleaningController::isBusy() const
{
    return m_busy;
}

double CleaningController::sampleWashVolumeUl() const
{
    return m_sampleWashUl;
}

double CleaningController::reagentWashVolumeUl() const
{
    return m_reagentWashUl;
}

void CleaningController::start()
{
    if (m_busy) {
        emit finished(false, tr("清洗正在进行，请等待当前流程结束"));
        return;
    }

    QString error;
    if (!buildSequence(&error)) {
        m_steps.clear();
        qWarning() << "清洗构帧失败:" << error
                   << "样本洗针量" << m_sampleWashUl
                   << "试剂洗针量" << m_reagentWashUl;
        emit finished(false, error);
        return;
    }
    if (m_steps.isEmpty()) {
        emit finished(false, tr("清洗序列为空"));
        return;
    }

    m_busy = true;
    m_currentStep = -1;
    qInfo() << "清洗开始，共" << m_steps.size() << "步";
    emit progressChanged(0, m_steps.size(), tr("清洗准备中…"));
    sendNextStep();
}

void CleaningController::handleReport(
        const platelet::MotionProtocol::Frame &frame)
{
    if (!m_busy || !frame.valid || !frame.isReport()) {
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
        finish(false, tr("%1失败：%2，清洗已中止")
               .arg(step.description, reason));
        return;
    }
    if (frame.status != MotionProtocol::Status::Completed) {
        return;
    }

    m_timeoutTimer->stop();
    if (step.pumpUpdate == PumpUpdate::SamplePump) {
        PumpPositionTracker::instance().setSamplePosition(
                    step.pumpPosition);
    } else if (step.pumpUpdate == PumpUpdate::ReagentPump) {
        PumpPositionTracker::instance().setReagentPosition(
                    step.pumpPosition);
    }
    sendNextStep();
}

void CleaningController::cancel()
{
    if (!m_busy) {
        return;
    }
    finish(false, tr("清洗已取消"));
}

bool CleaningController::buildSequence(QString *error)
{
    using MotionProtocol = platelet::MotionProtocol;
    using ZAxis = InstrumentCoordinates::ZAxis;
    using Axis = MotionProtocol::Axis;
    using Reference = MotionProtocol::XyReference;
    using Area = MotionProtocol::Area;

    m_steps.clear();
    // 注意：本类有同名成员函数 sampleWashVolumeUl()/reagentWashVolumeUl()，
    // 这里必须用 :: 限定，否则调用的是成员函数（返回还没赋值的 m_*，恒为 0）。
    m_sampleWashUl = ::sampleWashVolumeUl();
    m_reagentWashUl = ::reagentWashVolumeUl();
    if (m_sampleWashUl <= 0.0 || m_reagentWashUl <= 0.0) {
        if (error) {
            *error = tr("洗样本针量(%1 µL)/洗试剂针量(%2 µL)必须大于 0")
                    .arg(m_sampleWashUl, 0, 'f', 1)
                    .arg(m_reagentWashUl, 0, 'f', 1);
        }
        return false;
    }
    if (!m_coordinates) {
        if (error) {
            *error = tr("坐标表不可用，无法执行清洗");
        }
        return false;
    }

    const int sampleSteps = pumpStepsForUl(
                m_sampleWashUl, bloodPumpStepsPerUl());
    const int reagentSteps = pumpStepsForUl(
                m_reagentWashUl, reagentPumpStepsPerUl());
    if (sampleSteps <= 0 || reagentSteps <= 0) {
        if (error) {
            *error = tr("洗针量换算步数失败");
        }
        return false;
    }

    PumpPositionTracker &pumps = PumpPositionTracker::instance();
    if (!pumps.sampleValid() || !pumps.reagentValid()) {
        if (error) {
            *error = tr("样本泵/试剂泵位置未知，请先执行开机灌注");
        }
        return false;
    }
    const int sampleTarget = pumps.samplePosition() + sampleSteps;
    const int reagentTarget = pumps.reagentPosition() + reagentSteps;
    if (sampleTarget > MotionProtocol::PumpFullScale
            || reagentTarget > MotionProtocol::PumpFullScale) {
        if (error) {
            *error = tr("泵余量不足以吸清洗液（样本 %1，试剂 %2，满量程 %3）")
                    .arg(sampleTarget).arg(reagentTarget)
                    .arg(int(MotionProtocol::PumpFullScale));
        }
        return false;
    }

    const auto sampleClean = m_coordinates->locateCleaningHole(
                ZAxis::SampleNeedle);
    const auto reagentClean = m_coordinates->locateCleaningHole(
                ZAxis::ReagentNeedle);
    const auto sampleOrigin = m_coordinates->locateOrigin(ZAxis::SampleNeedle);
    const auto reagentOrigin = m_coordinates->locateOrigin(
                ZAxis::ReagentNeedle);
    if (!sampleClean.xyValid || !reagentClean.xyValid) {
        if (error) {
            *error = tr("样本针/试剂针清洗位 XY 未设置，无法执行清洗");
        }
        return false;
    }
    if (!sampleOrigin.valid() || !reagentOrigin.valid()) {
        if (error) {
            *error = tr("原点坐标的样本针/试剂针 XY 或 Z 未设置，无法执行清洗");
        }
        return false;
    }

    const int sampleFailHeight = sampleWashDetectionFailHeightMm();
    const int reagentFailHeight = reagentWashDetectionFailHeightMm();
    // 清洗吐液高度用独立参数，不用灌注的原点 Z（老软件固定 45mm）。
    const int cleaningDumpHeight = cleaningDumpHeightMm();
    const int originX = qRound(sampleOrigin.x);
    const int originY = qRound(sampleOrigin.y);
    const int pumpCleanTimeout = qMax(m_pumpCleanTimeoutMs,
                                      m_cleaningTimeMs + 15000);

    const auto add = [this, error](const QByteArray &frame,
                                   const QString &description,
                                   quint8 slaveAddress, Axis axis,
                                   int timeoutMs,
                                   PumpUpdate pumpUpdate = PumpUpdate::None,
                                   int pumpPosition = -1) {
        return appendStep(frame, description, slaveAddress, axis,
                          timeoutMs, error, pumpUpdate, pumpPosition);
    };

    // 1. 三根 Z 复位，之后才允许第一次 XY。
    if (!add(m_protocol->resetSampleNeedle(), tr("清洗：样本针复位"),
             kZAxis, Axis::SampleNeedle, m_zResetTimeoutMs)
            || !add(m_protocol->resetReagentNeedle(), tr("清洗：试剂针复位"),
                    kZAxis, Axis::ReagentNeedle, m_zResetTimeoutMs)
            || !add(m_protocol->resetGripper(), tr("清洗：抓手复位"),
                    kZAxis, Axis::Gripper, m_zResetTimeoutMs)) {
        return false;
    }

    // 2. 样本针清洗位：下针探液 → 吸洗样本针量。
    if (!add(m_protocol->moveXy(qRound(sampleClean.x),
                                qRound(sampleClean.y),
                                Reference::BloodNeedle, Area::Test),
             tr("清洗：XY 到样本针清洗位"),
             kMainBoard, Axis::Xy, m_xyTimeoutMs)
            || !add(m_protocol->moveSampleNeedleTo(sampleFailHeight,
                                                   Area::Test, true),
                    tr("清洗：样本针下针探液"),
                    kZAxis, Axis::SampleNeedle, m_zMoveTimeoutMs)
            || !add(m_protocol->moveBloodPump(sampleTarget, true),
                    tr("清洗：样本泵吸清洗液 %1µL").arg(m_sampleWashUl),
                    kMainBoard, Axis::BloodPump, m_pumpTimeoutMs,
                    PumpUpdate::SamplePump, sampleTarget)
            || !add(m_protocol->resetSampleNeedle(),
                    tr("清洗：样本针复位"),
                    kZAxis, Axis::SampleNeedle, m_zResetTimeoutMs)) {
        return false;
    }

    // 3. 去试剂针清洗位前再次复位三根 Z。
    if (!add(m_protocol->resetSampleNeedle(), tr("清洗：样本针复位"),
             kZAxis, Axis::SampleNeedle, m_zResetTimeoutMs)
            || !add(m_protocol->resetReagentNeedle(), tr("清洗：试剂针复位"),
                    kZAxis, Axis::ReagentNeedle, m_zResetTimeoutMs)
            || !add(m_protocol->resetGripper(), tr("清洗：抓手复位"),
                    kZAxis, Axis::Gripper, m_zResetTimeoutMs)) {
        return false;
    }

    // 4. 试剂针清洗位：下针探液 → 吸洗试剂针量。
    if (!add(m_protocol->moveXy(qRound(reagentClean.x),
                                qRound(reagentClean.y),
                                Reference::ReagentNeedle, Area::Test),
             tr("清洗：XY 到试剂针清洗位"),
             kMainBoard, Axis::Xy, m_xyTimeoutMs)
            || !add(m_protocol->moveReagentNeedleTo(reagentFailHeight,
                                                    Area::Test, true),
                    tr("清洗：试剂针下针探液"),
                    kZAxis, Axis::ReagentNeedle, m_zMoveTimeoutMs)
            || !add(m_protocol->moveReagentPump(reagentTarget, true),
                    tr("清洗：试剂泵吸清洗液 %1µL").arg(m_reagentWashUl),
                    kMainBoard, Axis::ReagentPump, m_pumpTimeoutMs,
                    PumpUpdate::ReagentPump, reagentTarget)
            || !add(m_protocol->resetReagentNeedle(),
                    tr("清洗：试剂针复位"),
                    kZAxis, Axis::ReagentNeedle, m_zResetTimeoutMs)) {
        return false;
    }

    // 5. 回原点前再次复位三根 Z，然后双针下到清洗高度吐液。
    if (!add(m_protocol->resetSampleNeedle(), tr("清洗：样本针复位"),
             kZAxis, Axis::SampleNeedle, m_zResetTimeoutMs)
            || !add(m_protocol->resetReagentNeedle(), tr("清洗：试剂针复位"),
                    kZAxis, Axis::ReagentNeedle, m_zResetTimeoutMs)
            || !add(m_protocol->resetGripper(), tr("清洗：抓手复位"),
                    kZAxis, Axis::Gripper, m_zResetTimeoutMs)) {
        return false;
    }
    if (!add(m_protocol->moveXy(originX, originY,
                                Reference::BloodNeedle, Area::Test),
             tr("清洗：XY 回原点"),
             kMainBoard, Axis::Xy, m_xyTimeoutMs)
            || !add(m_protocol->moveSampleNeedleTo(cleaningDumpHeight,
                                                   Area::Test, false),
                    tr("清洗：样本针下到清洗高度 Z=%1")
                    .arg(cleaningDumpHeight),
                    kZAxis, Axis::SampleNeedle, m_zMoveTimeoutMs)
            || !add(m_protocol->moveReagentNeedleTo(cleaningDumpHeight,
                                                    Area::Test, false),
                    tr("清洗：试剂针下到清洗高度 Z=%1")
                    .arg(cleaningDumpHeight),
                    kZAxis, Axis::ReagentNeedle, m_zMoveTimeoutMs)
            || !add(m_protocol->moveBloodPump(0, false, true,
                                              m_cleaningTimeMs),
                    tr("清洗：样本泵吐液清洗 %1ms").arg(m_cleaningTimeMs),
                    kMainBoard, Axis::BloodPump, pumpCleanTimeout,
                    PumpUpdate::SamplePump, 0)
            || !add(m_protocol->moveReagentPump(0, false, true,
                                                m_cleaningTimeMs),
                    tr("清洗：试剂泵吐液清洗 %1ms").arg(m_cleaningTimeMs),
                    kMainBoard, Axis::ReagentPump, pumpCleanTimeout,
                    PumpUpdate::ReagentPump, 0)) {
        return false;
    }

    // 6. 双针复位、回吸空气、XYZ 回原点。
    if (!add(m_protocol->resetSampleNeedle(), tr("清洗：样本针复位"),
             kZAxis, Axis::SampleNeedle, m_zResetTimeoutMs)
            || !add(m_protocol->resetReagentNeedle(), tr("清洗：试剂针复位"),
                    kZAxis, Axis::ReagentNeedle, m_zResetTimeoutMs)
            || !add(m_protocol->moveBloodPump(kSampleAirSuck, true),
                    tr("清洗：样本泵回吸空气 %1").arg(kSampleAirSuck),
                    kMainBoard, Axis::BloodPump, m_pumpTimeoutMs,
                    PumpUpdate::SamplePump, kSampleAirSuck)
            || !add(m_protocol->moveReagentPump(kReagentAirSuck, true),
                    tr("清洗：试剂泵回吸空气 %1").arg(kReagentAirSuck),
                    kMainBoard, Axis::ReagentPump, m_pumpTimeoutMs,
                    PumpUpdate::ReagentPump, kReagentAirSuck)
            || !add(m_protocol->resetGripper(), tr("清洗：抓手复位"),
                    kZAxis, Axis::Gripper, m_zResetTimeoutMs)
            || !add(m_protocol->moveXy(originX, originY,
                                       Reference::BloodNeedle, Area::Test),
                    tr("清洗：XY 回原点"),
                    kMainBoard, Axis::Xy, m_xyTimeoutMs)) {
        return false;
    }

    return true;
}

bool CleaningController::appendStep(
        const QByteArray &frame, const QString &description,
        quint8 slaveAddress, platelet::MotionProtocol::Axis axis,
        int timeoutMs, QString *error, PumpUpdate pumpUpdate,
        int pumpPosition)
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
    step.pumpUpdate = pumpUpdate;
    step.pumpPosition = pumpPosition;
    m_steps.append(step);
    return true;
}

void CleaningController::sendNextStep()
{
    if (!m_busy) {
        return;
    }

    ++m_currentStep;
    if (m_currentStep >= m_steps.size()) {
        finish(true, tr("清洗完成"));
        return;
    }

    const Step &step = m_steps.at(m_currentStep);
    qInfo() << "清洗步骤" << (m_currentStep + 1) << "/"
            << m_steps.size() << step.description;
    emit progressChanged(m_currentStep, m_steps.size(), step.description);
    emit statusChanged(step.description);

    m_timeoutTimer->start(step.timeoutMs);
    emit sendRequested(step.frame, step.description);
}

void CleaningController::finish(bool ok, const QString &message)
{
    m_timeoutTimer->stop();
    m_busy = false;
    if (!ok) {
        // 只有泵步骤失败时泵位置才不可信；Z/XY 失败时泵位置仍是上一步的值。
        const bool pumpStepFailed =
                m_currentStep >= 0 && m_currentStep < m_steps.size()
                && m_steps.at(m_currentStep).pumpUpdate != PumpUpdate::None;
        if (pumpStepFailed) {
            PumpPositionTracker::instance().invalidate();
        }
    }

    const int total = m_steps.size();
    const int completed = ok ? total : qBound(0, m_currentStep, total);
    m_currentStep = -1;

    if (ok) {
        qInfo() << message;
    } else {
        qWarning() << message;
    }
    emit progressChanged(completed, total, message);
    emit finished(ok, message);
}

bool CleaningController::isReportFor(
        const platelet::MotionProtocol::Frame &frame, const Step &step) const
{
    if (step.frame.size() <= 2) {
        return false;
    }
    return frame.valid && frame.isReport()
            && frame.cmdNum == static_cast<quint8>(step.frame.at(2))
            && frame.slaveAddress == step.slaveAddress
            && frame.axis == step.axis;
}
