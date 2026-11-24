#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "perfusioncontroller.h"

#include "pumppositiontracker.h"

#include <QDebug>
#include <QTimer>
#include <QtMath>

PerfusionController::PerfusionController(InstrumentCoordinates *coordinates,
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
        const Step &step = m_steps.at(m_currentStep);
        if (step.reportOptional) {
            // 现场反馈 XY 负速度复位完成后可能收不到完成报告；
            // 等满超时后继续下一句“移动到样本针原点”，不能卡死整机。
            qWarning() << "开机灌注步骤未收到完成报告，继续下一步:"
                       << step.description;
            m_timeoutTimer->stop();
            sendNextStep();
            return;
        }
        finish(false, tr("%1超时，已中止").arg(step.description));
    });
}

void PerfusionController::setProtocol(platelet::MotionProtocol *protocol)
{
    m_protocol = protocol ? protocol : &m_ownProtocol;
}

void PerfusionController::setCleaningTimeMs(int ms)
{
    if (ms > 0) {
        m_cleaningTimeMs = ms;
    }
}

void PerfusionController::setStageTimeouts(int zResetMs, int xyMs,
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

bool PerfusionController::isBusy() const
{
    return m_busy;
}

bool PerfusionController::lastRunWasPumpEmpty() const
{
    return m_pumpEmptyMode;
}

void PerfusionController::startPumpEmpty()
{
    if (m_busy) {
        emit finished(false, tr("当前动作正在进行，请等待结束后再排空泵"));
        return;
    }

    m_pumpEmptyMode = true;
    m_steps.clear();
    QString error;
    if (!appendStep(m_protocol->moveBloodPump(0, false),
                    tr("泵排空：血样泵到 0"),
                    platelet::MotionProtocol::MainBoard,
                    platelet::MotionProtocol::Axis::BloodPump,
                    m_pumpTimeoutMs, &error)
            || !appendStep(m_protocol->moveReagentPump(0, false),
                           tr("泵排空：试剂泵到 0"),
                           platelet::MotionProtocol::MainBoard,
                           platelet::MotionProtocol::Axis::ReagentPump,
                           m_pumpTimeoutMs, &error)) {
        m_steps.clear();
        qWarning() << "泵排空构帧失败:" << error;
        emit finished(false, error);
        return;
    }

    m_busy = true;
    m_currentStep = -1;
    qInfo() << "泵排空开始，共" << m_steps.size() << "步";
    emit statusChanged(tr("泵排空：血样泵与试剂泵回到零位"));
    sendNextStep();
}

void PerfusionController::start()
{
    if (m_busy) {
        emit finished(false, tr("开机灌注正在进行，请等待当前流程结束"));
        return;
    }

    m_pumpEmptyMode = false;
    QString error;
    if (!buildSequence(&error)) {
        m_steps.clear();
        qWarning() << "开机灌注构帧失败:" << error;
        emit finished(false, error);
        return;
    }
    if (m_steps.isEmpty()) {
        emit finished(false, tr("开机灌注序列为空"));
        return;
    }

    m_busy = true;
    m_currentStep = -1;
    qInfo() << "开机灌注开始，共" << m_steps.size() << "步";
    emit progressChanged(0, m_steps.size(), tr("开机灌注准备中…"));
    sendNextStep();
}

void PerfusionController::handleReport(
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
        finish(false, tr("%1失败：%2，已中止")
               .arg(step.description, reason));
        return;
    }
    if (frame.status != MotionProtocol::Status::Completed) {
        return;
    }

    m_timeoutTimer->stop();
    sendNextStep();
}

void PerfusionController::cancel()
{
    if (!m_busy) {
        return;
    }
    finish(false, tr("开机灌注已取消"));
}

bool PerfusionController::buildSequence(QString *error)
{
    using MotionProtocol = platelet::MotionProtocol;
    using ZAxis = InstrumentCoordinates::ZAxis;
    using Axis = MotionProtocol::Axis;
    using Reference = MotionProtocol::XyReference;
    using Area = MotionProtocol::Area;

    m_steps.clear();
    if (!m_coordinates) {
        if (error) {
            *error = tr("坐标表不可用，无法执行开机灌注");
        }
        return false;
    }

    const InstrumentCoordinates::MoveTarget sampleOrigin =
            m_coordinates->locateOrigin(ZAxis::SampleNeedle);
    const InstrumentCoordinates::MoveTarget reagentOrigin =
            m_coordinates->locateOrigin(ZAxis::ReagentNeedle);
    if (!sampleOrigin.valid() || !reagentOrigin.valid()) {
        if (error) {
            *error = tr("原点坐标的样本针/试剂针 XY 或下降高度 Z 未设置，"
                        "无法执行开机灌注");
        }
        return false;
    }

    // 灌注下针高度使用坐标表「原点」里各工具自己的下降高度 Z，
    // 由维护人员现场调整；不再使用固定值。
    const int sampleDown = qRound(sampleOrigin.z);
    const int reagentDown = qRound(reagentOrigin.z);

    constexpr quint8 kZAxis = MotionProtocol::ZAxisBoard;
    constexpr quint8 kMainBoard = MotionProtocol::MainBoard;
    constexpr int kInitialFill = 4320;       // 7200 × INIT_CLEAN_RATIO(0.6)
    constexpr int kBackSuckAir = 360;
    constexpr int kReagentInhaleAir = 720;

    // 清洗时间可由基础设置调整（500~65535ms），清洗步骤超时必须跟着放大，
    // 否则用户设得比默认 6000ms 长时会被误判超时中止。
    // 老软件 armAckWatch 为 max(45s, 清洗时间 + 20s)，这里保持一致。
    const int pumpCleanTimeoutMs = qMax(m_pumpCleanTimeoutMs,
                                        m_cleaningTimeMs + 20000);

    const auto add = [this, error](const QByteArray &frame,
                                   const QString &description,
                                   quint8 slaveAddress,
                                   Axis axis, int timeoutMs,
                                   bool reportOptional = false) {
        return appendStep(frame, description, slaveAddress, axis,
                          timeoutMs, error, reportOptional);
    };

    // 1. Z 轴复位：顺序与老软件一致（样本针 → 试剂针 → 抓手）
    if (!add(m_protocol->resetSampleNeedle(), tr("灌注：样本针复位"),
             kZAxis, Axis::SampleNeedle, m_zResetTimeoutMs)
            || !add(m_protocol->resetReagentNeedle(), tr("灌注：试剂针复位"),
                    kZAxis, Axis::ReagentNeedle, m_zResetTimeoutMs)
            || !add(m_protocol->resetGripper(), tr("灌注：抓手复位"),
                    kZAxis, Axis::Gripper, m_zResetTimeoutMs)) {
        return false;
    }

    // 2. XY 负速度复位 → 样本针原点（按现场要求不再先到 (0,0)）
    // XY 负速度复位在部分机器上不发完成报告，超时后继续走下一步。
    if (!add(m_protocol->resetXy(), tr("灌注：XY 复位"),
             kMainBoard, Axis::Xy, m_xyTimeoutMs, true)
            || !add(m_protocol->moveXy(qRound(sampleOrigin.x),
                                       qRound(sampleOrigin.y),
                                       Reference::BloodNeedle, Area::Test),
                    tr("灌注：XY 到样本针原点"),
                    kMainBoard, Axis::Xy, m_xyTimeoutMs)) {
        return false;
    }

    // 3. 样本针 + 大泵：排空 → 吸 60% → 下针排空清洗 → 回吸空气
    if (!add(m_protocol->moveSampleNeedleTo(sampleDown, Area::Test, false),
             tr("灌注：样本针下到 Z=%1").arg(sampleDown),
             kZAxis, Axis::SampleNeedle, m_zMoveTimeoutMs)
            || !add(m_protocol->moveBloodPump(0, false),
                    tr("灌注：血样泵排空"),
                    kMainBoard, Axis::BloodPump, m_pumpTimeoutMs)
            || !add(m_protocol->resetSampleNeedle(),
                    tr("灌注：样本针复位"),
                    kZAxis, Axis::SampleNeedle, m_zResetTimeoutMs)
            || !add(m_protocol->moveBloodPump(kInitialFill, true),
                    tr("灌注：血样泵吸满 60%"),
                    kMainBoard, Axis::BloodPump, m_pumpTimeoutMs)
            || !add(m_protocol->moveSampleNeedleTo(sampleDown, Area::Test,
                                                   false),
                    tr("灌注：样本针下到 Z=%1").arg(sampleDown),
                    kZAxis, Axis::SampleNeedle, m_zMoveTimeoutMs)
            || !add(m_protocol->moveBloodPump(0, false, true,
                                              m_cleaningTimeMs),
                    tr("灌注：血样泵排空并清洗 %1ms").arg(m_cleaningTimeMs),
                    kMainBoard, Axis::BloodPump, pumpCleanTimeoutMs)
            || !add(m_protocol->resetSampleNeedle(),
                    tr("灌注：样本针复位"),
                    kZAxis, Axis::SampleNeedle, m_zResetTimeoutMs)
            || !add(m_protocol->moveBloodPump(kBackSuckAir, true),
                    tr("灌注：血样泵回吸空气"),
                    kMainBoard, Axis::BloodPump, m_pumpTimeoutMs)) {
        return false;
    }

    // 4. 试剂针 + 小泵：同样流程（此时三根 Z 都已复位，才允许 XY）
    if (!add(m_protocol->moveXy(qRound(reagentOrigin.x),
                                qRound(reagentOrigin.y),
                                Reference::ReagentNeedle, Area::Test),
             tr("灌注：XY 到试剂针原点"),
             kMainBoard, Axis::Xy, m_xyTimeoutMs)
            || !add(m_protocol->moveReagentNeedleTo(reagentDown,
                                                    Area::Test, false),
                    tr("灌注：试剂针下到 Z=%1").arg(reagentDown),
                    kZAxis, Axis::ReagentNeedle, m_zMoveTimeoutMs)
            || !add(m_protocol->moveReagentPump(0, false),
                    tr("灌注：试剂泵排空"),
                    kMainBoard, Axis::ReagentPump, m_pumpTimeoutMs)
            || !add(m_protocol->resetReagentNeedle(),
                    tr("灌注：试剂针复位"),
                    kZAxis, Axis::ReagentNeedle, m_zResetTimeoutMs)
            || !add(m_protocol->moveReagentPump(MotionProtocol::PumpFullScale,
                                                true),
                    tr("灌注：试剂泵吸满"),
                    kMainBoard, Axis::ReagentPump, m_pumpTimeoutMs)
            || !add(m_protocol->moveReagentNeedleTo(reagentDown,
                                                    Area::Test, false),
                    tr("灌注：试剂针下到 Z=%1").arg(reagentDown),
                    kZAxis, Axis::ReagentNeedle, m_zMoveTimeoutMs)
            || !add(m_protocol->moveReagentPump(0, false, true,
                                                m_cleaningTimeMs),
                    tr("灌注：试剂泵排空并清洗 %1ms").arg(m_cleaningTimeMs),
                    kMainBoard, Axis::ReagentPump, pumpCleanTimeoutMs)
            || !add(m_protocol->resetReagentNeedle(),
                    tr("灌注：试剂针复位"),
                    kZAxis, Axis::ReagentNeedle, m_zResetTimeoutMs)
            || !add(m_protocol->moveReagentPump(kReagentInhaleAir, true),
                    tr("灌注：试剂泵回吸空气"),
                    kMainBoard, Axis::ReagentPump, m_pumpTimeoutMs)) {
        return false;
    }

    return true;
}

bool PerfusionController::appendStep(const QByteArray &frame,
                                     const QString &description,
                                     quint8 slaveAddress,
                                     platelet::MotionProtocol::Axis axis,
                                     int timeoutMs, QString *error,
                                     bool reportOptional)
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
    step.reportOptional = reportOptional;
    m_steps.append(step);
    return true;
}

void PerfusionController::sendNextStep()
{
    if (!m_busy) {
        return;
    }

    ++m_currentStep;
    if (m_currentStep >= m_steps.size()) {
        finish(true, m_pumpEmptyMode
               ? tr("泵排空完成，泵零位已建立")
               : tr("开机灌注完成"));
        return;
    }

    const Step &step = m_steps.at(m_currentStep);
    qInfo() << (m_pumpEmptyMode ? "泵排空步骤" : "开机灌注步骤")
            << (m_currentStep + 1) << "/"
            << m_steps.size() << step.description;
    emit progressChanged(m_currentStep, m_steps.size(), step.description);
    emit statusChanged(step.description);

    m_timeoutTimer->start(step.timeoutMs);
    emit sendRequested(step.frame, step.description);
}

void PerfusionController::finish(bool ok, const QString &message)
{
    m_timeoutTimer->stop();
    m_busy = false;
    if (ok) {
        if (m_pumpEmptyMode) {
            // 两泵已排空到 0：记为已知零位，测试/清洗从 0 开始算绝对位置。
            PumpPositionTracker::instance().setSamplePosition(0);
            PumpPositionTracker::instance().setReagentPosition(0);
        } else {
            // 灌注结束：样本泵 360、试剂泵 720，供后续清洗/测试动作算绝对位置。
            PumpPositionTracker::instance().setBootPrimePositions();
        }
    } else {
        PumpPositionTracker::instance().invalidate();
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

bool PerfusionController::isReportFor(
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
