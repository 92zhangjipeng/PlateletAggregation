#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "testactioncontroller.h"

#include "appconfig.h"
#include "instrumentparameters.h"
#include "mainboardconsumables.h"
#include "moduleoverviewwidget.h"
#include "pumppositiontracker.h"

#include <QDebug>
#include <QTimer>
#include <QtMath>

namespace {
constexpr int kZAxis = platelet::MotionProtocol::ZAxisBoard;
constexpr int kMainBoard = platelet::MotionProtocol::MainBoard;
constexpr int kZResetTimeout = 45000;
constexpr int kXyTimeout = 45000;
constexpr int kZMoveTimeout = 45000;
constexpr int kPumpTimeout = 45000;
constexpr int kPumpCleanTimeout = 45000;
constexpr int kAdSettleMs = 1000;

int channelModule(int globalChannel)
{
    return (globalChannel - 1) / 4 + 1;
}

int channelLocal(int globalChannel)
{
    return (globalChannel - 1) % 4 + 1;
}

/**
 * 试剂坐标位 -> 主板耗材位置。
 * 坐标顺序：0~4 = AA/ADP/EPI/COL/RIS 左，5~9 = 同顺序的右位；
 * 主板位置则是 AA/EPI/ADP/COL/RIS（注意 EPI 与 ADP 顺序不同），
 * 右侧位置统一加 AA_Second - AA（=8）。无效坐标返回 -1。
 */
int reagentConsumablePosition(int coordinateIndex)
{
    using namespace MainBoardConsumable;
    static const int leftPositions[] = { AA, ADP, EPI, COL, RIS };
    if (coordinateIndex < 0 || coordinateIndex >= 10) {
        return -1;
    }
    const int base = leftPositions[coordinateIndex % 5];
    return coordinateIndex < 5 ? base : base + (AA_Second - AA);
}
}

TestActionController::TestActionController(InstrumentCoordinates *coordinates,
                                           ModuleOverviewWidget *overview,
                                           QObject *parent)
    : QObject(parent)
    , m_coordinates(coordinates)
    , m_overview(overview)
    , m_protocol(&m_ownProtocol)
{
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        if (!m_busy || m_currentStep < 0 || m_currentStep >= m_steps.size()) {
            return;
        }
        finishAction(false, tr("%1超时")
                     .arg(m_steps.at(m_currentStep).description));
    });
}

void TestActionController::setProtocol(platelet::MotionProtocol *protocol)
{
    m_protocol = protocol ? protocol : &m_ownProtocol;
}

bool TestActionController::isBusy() const
{
    return m_busy;
}

bool TestActionController::beginAction(const QString &name)
{
    if (m_busy) {
        emit statusChanged(tr("动作层忙，忽略新动作"));
        return false;
    }
    m_busy = true;
    m_cancelRequested = false;
    m_preflightBlocksTest = false;
    ++m_actionId;
    m_steps.clear();
    m_currentStep = -1;
    m_actionName = name;
    m_error.clear();
    m_globalChannel = -1;
    m_gripperRetried = false;
    m_idleGripperVacuum = 0;
    m_grabFailurePaused = false;
    m_grabFailureMessage.clear();
    m_grabRetryAppend = nullptr;
    emit statusChanged(name);
    return true;
}

void TestActionController::finishAction(bool ok, const QString &message)
{
    if (!m_busy) {
        return;
    }
    // 序列构建阶段就失败（m_currentStep < 0）说明还没有下发任何运动命令，
    // 属于坐标/参数等预检错误；单独上报，调度器只处理当前样本/试剂。
    const bool rejectedBeforeMotion =
            !ok && !m_cancelRequested && m_currentStep < 0;

    // 超时/取消/失败时也要放开总线，否则周期轮询会一直静默。
    setMotionExchangeActive(false);
    m_timeoutTimer->stop();
    m_busy = false;
    m_currentStep = -1;
    if (ok) {
        qInfo() << m_actionName << "完成";
    } else {
        qWarning() << m_actionName
                   << (rejectedBeforeMotion ? "预检失败（未下发运动）:"
                                            : "失败:")
                   << message;
    }
    if (rejectedBeforeMotion) {
        emit actionRejected(message, m_preflightBlocksTest);
        return;
    }
    emit actionFinished(ok, message);
}

void TestActionController::appendStep(const Step &step)
{
    m_steps.append(step);
}

void TestActionController::appendMotion(
        const QByteArray &frame, const QString &description,
        quint8 slaveAddress, platelet::MotionProtocol::Axis axis,
        int timeoutMs, PumpUpdate pumpUpdate, int pumpPosition,
        const std::function<void(const platelet::MotionProtocol::Frame &)>
                &onComplete)
{
    if (frame.isEmpty()) {
        m_error = tr("%1构帧失败：%2")
                .arg(description,
                     m_protocol->lastError().isEmpty()
                     ? tr("未知错误") : m_protocol->lastError());
        finishAction(false, m_error);
        return;
    }
    Step step;
    step.kind = StepKind::Motion;
    step.frame = frame;
    step.description = description;
    step.slaveAddress = slaveAddress;
    step.axis = axis;
    step.timeoutMs = timeoutMs;
    step.pumpUpdate = pumpUpdate;
    step.pumpPosition = pumpPosition;
    step.onMotionComplete = onComplete;
    // Z 轴命令字节 bit5 = 液面探测；记录该步骤便于收集回包里的探测高度。
    step.liquidDetection =
            (axis == platelet::MotionProtocol::Axis::SampleNeedle
             || axis == platelet::MotionProtocol::Axis::ReagentNeedle)
            && frame.size() > 5
            && (static_cast<quint8>(frame.at(5)) & 0x20) != 0;
    appendStep(step);
}

void TestActionController::appendDelay(int ms, const QString &description)
{
    Step step;
    step.kind = StepKind::Delay;
    step.delayMs = ms;
    step.description = description;
    appendStep(step);
}

void TestActionController::appendCustom(const std::function<void()> &custom,
                                        const QString &description)
{
    Step step;
    step.kind = StepKind::Custom;
    step.custom = custom;
    step.description = description;
    appendStep(step);
}

void TestActionController::insertNextStep(const Step &step)
{
    if (m_currentStep >= 0 && m_currentStep < m_steps.size()) {
        m_steps.insert(m_currentStep + 1, step);
    }
}

void TestActionController::runNextStep()
{
    if (!m_busy) {
        return;
    }
    ++m_currentStep;
    if (m_currentStep >= m_steps.size()) {
        finishAction(true, m_actionName + tr("完成"));
        return;
    }

    const Step step = m_steps.at(m_currentStep);
    const int stepIndex = m_currentStep;
    const quint64 actionId = m_actionId;
    emit statusChanged(step.description);

    if (step.kind == StepKind::Delay) {
        QTimer::singleShot(step.delayMs, this,
                           [this, stepIndex, actionId]() {
            if (m_busy && m_actionId == actionId
                    && m_currentStep == stepIndex) {
                runNextStep();
            }
        });
        return;
    }
    if (step.kind == StepKind::Custom) {
        if (step.custom) {
            step.custom();
        }
        // 抓杯失败暂停时停在当前步骤，等操作员选择重试/放弃。
        if (!m_grabFailurePaused && m_busy && m_actionId == actionId
                && m_currentStep == stepIndex) {
            runNextStep();
        }
        return;
    }

    // 测试流程中主板/模组 0x16 轮询是开着的；动作命令在途期间必须
    // 静默 0x16 读命令，否则动作完成应答（0x17）会被读命令冲掉。
    // 静默请求先入通讯线程队列，保证排在 sendRequested 之前执行。
    setMotionExchangeActive(true);
    m_timeoutTimer->start(step.timeoutMs);
    emit sendRequested(step.frame, step.description);
}

void TestActionController::setMotionExchangeActive(bool active)
{
    if (m_motionExchangeActive == active) {
        return;
    }
    m_motionExchangeActive = active;
    emit motionExchangeActiveChanged(active);
}

bool TestActionController::isReportFor(
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

void TestActionController::handleReport(
        const platelet::MotionProtocol::Frame &frame)
{
    if (!m_busy || m_currentStep < 0 || m_currentStep >= m_steps.size()) {
        return;
    }
    const Step step = m_steps.at(m_currentStep);
    if (step.kind != StepKind::Motion || !isReportFor(frame, step)) {
        return;
    }

    using platelet::MotionProtocol;

    // 液面探测回包诊断与缓存：协议 Z 轴回包 axisByte bit7=1 表示探测到液面，
    // 探测高度在 distance2。部分固件在“运行中”报告里先给出高度，完成帧
    // 反而为 0；这里缓存任一报告里的有效结果，完成时再补回。
    if (step.liquidDetection) {
        const bool ok = (frame.axisByte & 0x80) != 0;
        if (ok) {
            m_steps[m_currentStep].detected = true;
            if (frame.distance2 > 0) {
                m_steps[m_currentStep].detectedHeightMm = frame.distance2;
            }
        } else if (frame.distance2 > 0
                   && !m_steps.at(m_currentStep).detected
                   && m_steps.at(m_currentStep).detectedHeightMm <= 0) {
            m_steps[m_currentStep].detectedHeightMm = frame.distance2;
        }
        qInfo().nospace()
                << "液面探测回包 " << step.description
                << " 状态=" << MotionProtocol::statusText(frame.status)
                << " 成功位=" << (ok ? 1 : 0)
                << " axisByte=0x" << QString::number(frame.axisByte, 16)
                << " distance1=" << frame.distance1
                << " distance2=" << frame.distance2;
    }

    if (frame.status == MotionProtocol::Status::Error || frame.fault != 0) {
        const QString reason = frame.fault != 0
                ? frame.faultDescription()
                : MotionProtocol::statusText(frame.status);
        finishAction(false, tr("%1失败：%2")
                     .arg(step.description, reason));
        return;
    }
    if (frame.status != MotionProtocol::Status::Completed) {
        return;
    }

    m_timeoutTimer->stop();
    setMotionExchangeActive(false);
    if (step.pumpUpdate == PumpUpdate::SamplePump) {
        PumpPositionTracker::instance().setSamplePosition(
                    step.pumpPosition);
    } else if (step.pumpUpdate == PumpUpdate::ReagentPump) {
        PumpPositionTracker::instance().setReagentPosition(
                    step.pumpPosition);
    }
    MotionProtocol::Frame effective = frame;
    if (step.liquidDetection) {
        const Step &stored = m_steps.at(m_currentStep);
        if ((effective.axisByte & 0x80) == 0 && stored.detected) {
            effective.axisByte =
                    static_cast<quint8>(effective.axisByte | 0x80);
        }
        if (effective.distance2 <= 0 && stored.detectedHeightMm > 0) {
            effective.distance2 = stored.detectedHeightMm;
            qInfo() << "液面探测完成帧未带高度，采用此前回包高度"
                    << stored.detectedHeightMm << "mm:"
                    << step.description;
        }
    }
    if (step.onMotionComplete) {
        step.onMotionComplete(effective);
    }
    if (!m_busy) {
        return;
    }
    runNextStep();
}

void TestActionController::cancel()
{
    if (m_busy) {
        // 主动取消即使发生在下发第一步之前，也按普通失败结束，
        // 避免被误判为“预检失败”而跳过调度器的中止流程。
        m_cancelRequested = true;
        finishAction(false, tr("动作已取消"));
    }
}

void TestActionController::parkAtOrigin()
{
    if (!beginAction(tr("收尾：XY 复位后移动到清洗原点"))) {
        return;
    }
    const auto origin = m_coordinates->locateOrigin(
                InstrumentCoordinates::ZAxis::SampleNeedle);
    if (!origin.xyValid) {
        finishAction(false, tr("原点 XY 坐标未设置，无法执行收尾动作"));
        return;
    }
    // 测试结束收尾顺序：三根 Z 复位 → XY 负速度复位 → 移动到清洗原点。
    appendResetAllZ(tr("收尾："));
    appendMotion(m_protocol->resetXy(),
                 tr("收尾：XY 负速度复位"),
                 kMainBoard, platelet::MotionProtocol::Axis::Xy,
                 kXyTimeout);
    appendMotion(m_protocol->moveXy(
                     qRound(origin.x), qRound(origin.y),
                     platelet::MotionProtocol::XyReference::BloodNeedle,
                     platelet::MotionProtocol::Area::Test),
                 tr("收尾：XY 移动到清洗原点"),
                 kMainBoard, platelet::MotionProtocol::Axis::Xy,
                 kXyTimeout);
    runNextStep();
}

int TestActionController::globalToModule(int globalChannel) const
{
    return channelModule(globalChannel);
}

int TestActionController::globalToLocal(int globalChannel) const
{
    return channelLocal(globalChannel);
}

int TestActionController::reagentUiIndex(const QString &reagent) const
{
    return reagentNameUiIndex(reagent);
}

int TestActionController::reagentCoordinateIndex(
        const QString &reagent) const
{
    // 默认用左侧试剂位：AA左、ADP左、EPI左、COL左、RIS左。
    return reagentUiIndex(reagent);
}

int TestActionController::sampleHoleIndex(const QString &position)
{
    bool ok = false;
    const int hole = position.trimmed().toInt(&ok);
    return ok ? hole : -1;
}

bool TestActionController::pumpTarget(int current, int steps, int *target,
                                      QString *error) const
{
    if (steps <= 0) {
        if (error) {
            *error = tr("泵吸样步数必须大于 0");
        }
        return false;
    }
    const int value = current + steps;
    if (value > platelet::MotionProtocol::PumpFullScale) {
        if (error) {
            *error = tr("泵余量不足：当前位置 %1 + %2 步超过满量程 %3")
                    .arg(current).arg(steps)
                    .arg(int(platelet::MotionProtocol::PumpFullScale));
        }
        return false;
    }
    if (target) {
        *target = value;
    }
    return true;
}

void TestActionController::appendResetSampleReagentZ(const QString &prefix)
{
    appendMotion(m_protocol->resetSampleNeedle(),
                 prefix + tr("样本针复位"),
                 kZAxis, platelet::MotionProtocol::Axis::SampleNeedle,
                 kZResetTimeout);
    appendMotion(m_protocol->resetReagentNeedle(),
                 prefix + tr("试剂针复位"),
                 kZAxis, platelet::MotionProtocol::Axis::ReagentNeedle,
                 kZResetTimeout);
}

void TestActionController::appendResetAllZ(const QString &prefix)
{
    appendResetSampleReagentZ(prefix);
    appendMotion(m_protocol->resetGripper(),
                 prefix + tr("抓手复位"),
                 kZAxis, platelet::MotionProtocol::Axis::Gripper,
                 kZResetTimeout);
}

void TestActionController::appendGripperLiftHoldingCup(
        const QString &prefix,
        const std::function<void()> &retryBuilder)
{
    appendMotion(m_protocol->resetGripper(
                     platelet::MotionProtocol::DefaultZResetSpeed,
                     platelet::MotionProtocol::Area::Test, true),
                 prefix + tr("抓手带杯复位"),
                 kZAxis, platelet::MotionProtocol::Axis::Gripper,
                 kZResetTimeout, PumpUpdate::None, -1,
                 [this](const platelet::MotionProtocol::Frame &frame) {
        m_lastGripperVacuum = frame.gripperVacuum();
    });
    appendCustom([this, prefix, retryBuilder]() {
        if (!m_busy || gripperStillHolding(m_lastGripperVacuum)) {
            return;
        }
        const QString message =
                prefix + tr("抬Z后未吸牢：负压 %1，抓杯阈值 ≤ %2")
                .arg(m_lastGripperVacuum)
                .arg(gripperGrabVacuumMax());
        if (retryBuilder) {
            pauseForGrabFailure(message, retryBuilder);
        } else {
            finishAction(false, message);
        }
    }, prefix + tr("抬Z确认仍吸牢"));
}

void TestActionController::appendXyHome(const QString &prefix)
{
    const auto origin = m_coordinates->locateOrigin(
                InstrumentCoordinates::ZAxis::SampleNeedle);
    if (!origin.xyValid) {
        return;
    }
    appendMotion(m_protocol->moveXy(qRound(origin.x), qRound(origin.y),
                                    platelet::MotionProtocol::XyReference::BloodNeedle,
                                    platelet::MotionProtocol::Area::Test),
                 prefix + tr("XY回原点"),
                 kMainBoard, platelet::MotionProtocol::Axis::Xy,
                 kXyTimeout);
}

void TestActionController::appendReadChannelAd(int globalChannel, bool ppp,
                                               const QString &prefix)
{
    appendDelay(kAdSettleMs, prefix + tr("等待AD稳定 1s"));
    appendCustom([this, globalChannel, ppp]() {
        const int module = globalToModule(globalChannel);
        const int local = globalToLocal(globalChannel);
        const int value = m_overview
                ? m_overview->channelAdValue(module, local) : -1;
        if (ppp) {
            emit pppValueRead(globalChannel, value);
        } else {
            emit prp0ValueRead(globalChannel, value);
        }
    }, prefix + tr("读取AD"));
}

void TestActionController::appendGripperVacuumCheck(
        bool grab, const QString &description,
        const std::function<void()> &retryBuilder)
{
    appendCustom([this, grab, description, retryBuilder]() {
        if (!m_busy) {
            return;
        }
        if (grab) {
            if (gripperStillHolding(m_lastGripperVacuum)) {
                return;
            }
            const QString message =
                    tr("%1失败：负压 %2，抓杯阈值 ≤ %3")
                    .arg(description)
                    .arg(m_lastGripperVacuum)
                    .arg(gripperGrabVacuumMax());
            if (retryBuilder) {
                pauseForGrabFailure(message, retryBuilder);
            } else {
                finishAction(false, message);
            }
        } else if (!gripperReleaseSucceeded(m_lastGripperVacuum)) {
            finishAction(false,
                         tr("%1失败：负压 %2，松杯阈值 ≥ %3")
                         .arg(description)
                         .arg(m_lastGripperVacuum)
                         .arg(gripperReleaseVacuumMin()));
        }
    }, description);
}

std::function<void()> TestActionController::grabRetryBuilder(
        int z, const QString &prefix, bool includeLift)
{
    return [this, z, prefix, includeLift]() {
        appendGrabRetrySequence(z, prefix, includeLift);
    };
}

void TestActionController::appendGrabRetrySequence(
        int z, const QString &prefix, bool includeLift)
{
    const QString retryPrefix = prefix + tr("重试：");
    appendMotion(m_protocol->moveGripperTo(
                     z, platelet::MotionProtocol::Area::Test, true, false),
                 retryPrefix + tr("原位开阀读空载负压"),
                 kZAxis, platelet::MotionProtocol::Axis::Gripper,
                 kZMoveTimeout, PumpUpdate::None, -1,
                 [this](const platelet::MotionProtocol::Frame &frame) {
        m_idleGripperVacuum = frame.gripperVacuum();
        qInfo() << "抓手重试空载负压:" << m_idleGripperVacuum;
    });
    appendGripperSpeedGrab(z, retryPrefix);
    appendDelay(gripperSuctionDelayMs(),
                retryPrefix + tr("等待抓杯负压建立"));
    appendGripperVacuumCheck(true, retryPrefix + tr("抓杯"),
                             grabRetryBuilder(z, prefix, false));
    if (includeLift) {
        appendGripperLiftHoldingCup(
                    retryPrefix, grabRetryBuilder(z, prefix, true));
    }
}

void TestActionController::pauseForGrabFailure(
        const QString &message,
        const std::function<void()> &retryBuilder)
{
    if (!m_busy) {
        return;
    }
    m_grabFailureMessage = message;
    m_grabRetryAppend = retryBuilder;

    Step pause;
    pause.kind = StepKind::Custom;
    pause.description = tr("抓杯失败：等待重试或放弃");
    pause.custom = [this]() {
        m_grabFailurePaused = true;
        emit statusChanged(tr("抓杯失败：等待操作员处理"));
        emit grabFailurePaused(m_grabFailureMessage);
    };

    Step reset;
    reset.kind = StepKind::Motion;
    reset.frame = m_protocol->resetGripper();
    if (reset.frame.isEmpty()) {
        finishAction(false,
                     tr("抓杯失败后抓手复位构帧失败：%1")
                     .arg(m_protocol->lastError().isEmpty()
                          ? tr("未知错误") : m_protocol->lastError()));
        return;
    }
    reset.description = tr("抓杯失败：抓手复位");
    reset.slaveAddress = kZAxis;
    reset.axis = platelet::MotionProtocol::Axis::Gripper;
    reset.timeoutMs = kZResetTimeout;

    // insertNextStep 插在当前步之后：先插暂停再插复位，
    // 实际执行顺序为 抓手复位 → 暂停等待操作员。
    insertNextStep(pause);
    insertNextStep(reset);
}

void TestActionController::retryPausedGrab()
{
    if (!m_busy || !m_grabFailurePaused) {
        return;
    }
    const quint64 actionId = m_actionId;
    // 暂停点之后的原步骤先取出来，重试抓杯成功后再接回去。
    QVector<Step> remaining = m_steps.mid(m_currentStep + 1);
    m_steps.resize(m_currentStep + 1);
    m_grabFailurePaused = false;
    m_gripperRetried = false;
    const std::function<void()> builder = m_grabRetryAppend;
    m_grabRetryAppend = nullptr;
    m_grabFailureMessage.clear();

    qInfo() << "抓杯失败：操作员选择重试";
    emit statusChanged(tr("抓杯失败：重试抓杯"));
    if (builder) {
        builder();
    }
    m_steps.append(remaining);
    if (m_busy && m_actionId == actionId) {
        runNextStep();
    }
}

void TestActionController::abandonPausedGrab()
{
    if (!m_busy || !m_grabFailurePaused) {
        return;
    }
    finishAbandoned(m_grabFailureMessage);
}

void TestActionController::finishAbandoned(const QString &message)
{
    if (!m_busy) {
        return;
    }
    setMotionExchangeActive(false);
    m_timeoutTimer->stop();
    m_busy = false;
    m_currentStep = -1;
    m_grabFailurePaused = false;
    m_grabRetryAppend = nullptr;
    qWarning() << m_actionName << "放弃:" << message;
    emit actionAbandoned(message);
}

void TestActionController::appendGrabFromTube(int tubeIndex,
                                              const QString &prefix)
{
    const auto loc = m_coordinates->locateTube(
                tubeIndex, InstrumentCoordinates::ZAxis::Gripper);
    if (!loc.valid()) {
        m_error = prefix + tr("试管位 %1 的抓手坐标未设置").arg(tubeIndex);
        finishAction(false, m_error);
        return;
    }
    appendMotion(m_protocol->moveXy(
                     qRound(loc.x), qRound(loc.y),
                     platelet::MotionProtocol::XyReference::Gripper,
                     platelet::MotionProtocol::Area::Test),
                 prefix + tr("XY 到试管位"),
                 kMainBoard, platelet::MotionProtocol::Axis::Xy,
                 kXyTimeout);
    // 1. 原位开阀（负压泵从此打开，不再关闭，直到放杯/弃杯才关）
    //    读空载负压
    appendMotion(m_protocol->moveGripperTo(
                     qRound(loc.z),
                     platelet::MotionProtocol::Area::Test, true, false),
                 prefix + tr("原位开阀读空载负压"),
                 kZAxis, platelet::MotionProtocol::Axis::Gripper,
                 kZMoveTimeout, PumpUpdate::None, -1,
                 [this](const platelet::MotionProtocol::Frame &frame) {
        m_idleGripperVacuum = frame.gripperVacuum();
        qInfo() << "抓手空载负压:" << m_idleGripperVacuum;
    });
    // 2. 速度模式开阀下压吸杯（阀保持开启）：行程上限=试管位 z，
    //    完成报告里读负压，未达抓杯阈值时重试一次。
    //    抓杯失败可恢复：重试从当前 XY 重新抓，放弃由调度器跳过样本/试剂。
    const int z = qRound(loc.z);
    appendGripperSpeedGrab(z, prefix);
    appendGripperVacuumCheck(true, prefix + tr("抓杯"),
                             grabRetryBuilder(z, prefix, false));
    appendGripperLiftHoldingCup(prefix,
                                grabRetryBuilder(z, prefix, true));
}

void TestActionController::appendGripperSpeedGrab(int limitHeight,
                                                  const QString &prefix)
{
    const int speed = gripperGrabSpeed();
    Step step;
    step.kind = StepKind::Motion;
    step.frame = m_protocol->moveGripperDownSpeed(
                speed, limitHeight, platelet::MotionProtocol::Area::Test,
                true);
    step.description = prefix + tr("速度模式 %1 下压吸杯（上限 %2）")
            .arg(speed).arg(limitHeight);
    step.slaveAddress = kZAxis;
    step.axis = platelet::MotionProtocol::Axis::Gripper;
    step.timeoutMs = kZMoveTimeout;
    step.onMotionComplete =
            [this, limitHeight, prefix](
            const platelet::MotionProtocol::Frame &frame) {
        m_lastGripperVacuum = frame.gripperVacuum();
        if (gripperGrabSucceeded(m_lastGripperVacuum)) {
            qInfo() << "抓杯成功：负压" << m_lastGripperVacuum
                    << "≤" << gripperGrabVacuumMax();
            return;
        }
        // 未达阈值：重试一次（阀保持开启，抬起后再次速度下压）
        if (!m_gripperRetried) {
            m_gripperRetried = true;
            qWarning() << "抓杯负压未达阈值，重试："
                       << m_lastGripperVacuum;
            Step retry;
            retry.kind = StepKind::Motion;
            retry.frame = m_protocol->moveGripperDownSpeed(
                        gripperGrabSpeed(), limitHeight,
                        platelet::MotionProtocol::Area::Test, true);
            retry.description = prefix + tr("重试：速度模式下压吸杯");
            retry.slaveAddress = kZAxis;
            retry.axis = platelet::MotionProtocol::Axis::Gripper;
            retry.timeoutMs = kZMoveTimeout;
            retry.onMotionComplete =
                    [this](const platelet::MotionProtocol::Frame &f) {
                m_lastGripperVacuum = f.gripperVacuum();
            };

            // 延时一步给负压建立时间
            Step wait;
            wait.kind = StepKind::Delay;
            wait.delayMs = gripperSuctionDelayMs();
            wait.description = prefix + tr("重试：等待抓杯负压");

            insertNextStep(retry);
            insertNextStep(wait);
        }
    };
    appendStep(step);
    appendDelay(gripperSuctionDelayMs(), prefix + tr("等待抓杯负压建立"));
}

void TestActionController::appendPlaceInChannel(int globalChannel,
                                                const QString &prefix)
{
    const auto loc = m_coordinates->locateChannel(
                globalChannel - 1, InstrumentCoordinates::ZAxis::Gripper);
    if (!loc.valid()) {
        m_error = prefix + tr("通道 %1 的抓手坐标未设置").arg(globalChannel);
        finishAction(false, m_error);
        return;
    }
    appendMotion(m_protocol->moveXy(
                     qRound(loc.x), qRound(loc.y),
                     platelet::MotionProtocol::XyReference::Gripper,
                     platelet::MotionProtocol::Area::Test),
                 prefix + tr("XY 到通道"),
                 kMainBoard, platelet::MotionProtocol::Axis::Xy,
                 kXyTimeout);
    appendMotion(m_protocol->moveGripperTo(
                     qRound(loc.z),
                     platelet::MotionProtocol::Area::Test, true, false),
                 prefix + tr("下到通道杯位"),
                 kZAxis, platelet::MotionProtocol::Axis::Gripper,
                 kZMoveTimeout);
    appendMotion(m_protocol->moveGripperTo(
                     qRound(loc.z),
                     platelet::MotionProtocol::Area::Test, false, false),
                 prefix + tr("关阀放杯"),
                 kZAxis, platelet::MotionProtocol::Axis::Gripper,
                 kZMoveTimeout);
    appendDelay(gripperReleaseDelayMs(), prefix + tr("等待放杯负压释放"));
    appendMotion(m_protocol->moveGripperTo(
                     qRound(loc.z),
                     platelet::MotionProtocol::Area::Test, false, false),
                 prefix + tr("原位复读放杯负压"),
                 kZAxis, platelet::MotionProtocol::Axis::Gripper,
                 kZMoveTimeout, PumpUpdate::None, -1,
                 [this](const platelet::MotionProtocol::Frame &frame) {
        m_lastGripperVacuum = frame.gripperVacuum();
    });
    appendGripperVacuumCheck(false, prefix + tr("放杯"));
    appendMotion(m_protocol->resetGripper(),
                 prefix + tr("抓手复位"),
                 kZAxis, platelet::MotionProtocol::Axis::Gripper,
                 kZResetTimeout);
}

void TestActionController::appendGrabFromChannel(int globalChannel,
                                                 const QString &prefix)
{
    const auto loc = m_coordinates->locateChannel(
                globalChannel - 1, InstrumentCoordinates::ZAxis::Gripper);
    if (!loc.valid()) {
        m_error = prefix + tr("通道 %1 的抓手坐标未设置").arg(globalChannel);
        finishAction(false, m_error);
        return;
    }
    appendMotion(m_protocol->moveXy(
                     qRound(loc.x), qRound(loc.y),
                     platelet::MotionProtocol::XyReference::Gripper,
                     platelet::MotionProtocol::Area::Test),
                 prefix + tr("XY 到通道"),
                 kMainBoard, platelet::MotionProtocol::Axis::Xy,
                 kXyTimeout);
    // 1. 原位开阀（负压泵保持开启直到放杯/弃杯）读空载负压
    appendMotion(m_protocol->moveGripperTo(
                     qRound(loc.z),
                     platelet::MotionProtocol::Area::Test, true, false),
                 prefix + tr("原位开阀读空载负压"),
                 kZAxis, platelet::MotionProtocol::Axis::Gripper,
                 kZMoveTimeout, PumpUpdate::None, -1,
                 [this](const platelet::MotionProtocol::Frame &frame) {
        m_idleGripperVacuum = frame.gripperVacuum();
        qInfo() << "抓手空载负压(通道):" << m_idleGripperVacuum;
    });
    // 2. 速度模式开阀下压吸杯（阀保持开启），带一次重试
    appendGripperSpeedGrab(qRound(loc.z), prefix);
    appendGripperVacuumCheck(true, prefix + tr("抓杯"));
    appendGripperLiftHoldingCup(prefix);
}

void TestActionController::appendDiscardHeldCup(const QString &prefix)
{
    const auto loc = m_coordinates->locateDiscardCup(
                InstrumentCoordinates::ZAxis::Gripper);
    if (!loc.valid()) {
        m_error = prefix + tr("弃杯孔的抓手坐标未设置");
        finishAction(false, m_error);
        return;
    }
    appendMotion(m_protocol->moveXy(
                     qRound(loc.x), qRound(loc.y),
                     platelet::MotionProtocol::XyReference::Gripper,
                     platelet::MotionProtocol::Area::Test),
                 prefix + tr("XY 到弃杯孔"),
                 kMainBoard, platelet::MotionProtocol::Axis::Xy,
                 kXyTimeout);
    // 位置模式带杯下到弃杯孔
    appendMotion(m_protocol->moveGripperTo(
                     qRound(loc.z),
                     platelet::MotionProtocol::Area::Test, true, false),
                 prefix + tr("带杯下到弃杯孔"),
                 kZAxis, platelet::MotionProtocol::Axis::Gripper,
                 kZMoveTimeout);
    // 关阀（关负压泵）
    appendMotion(m_protocol->moveGripperTo(
                     qRound(loc.z),
                     platelet::MotionProtocol::Area::Test, false, false),
                 prefix + tr("关阀丢杯"),
                 kZAxis, platelet::MotionProtocol::Axis::Gripper,
                 kZMoveTimeout);
    // 等待释放：杯子掉落或到达设定的丢杯时间
    appendDelay(gripperReleaseDelayMs(), prefix + tr("等待丢杯负压释放"));
    // 读负压：达到空载水平（≥ 松杯阈值）判定丢杯成功
    appendMotion(m_protocol->moveGripperTo(
                     qRound(loc.z),
                     platelet::MotionProtocol::Area::Test, false, false),
                 prefix + tr("读丢杯负压"),
                 kZAxis, platelet::MotionProtocol::Axis::Gripper,
                 kZMoveTimeout, PumpUpdate::None, -1,
                 [this](const platelet::MotionProtocol::Frame &frame) {
        m_lastGripperVacuum = frame.gripperVacuum();
    });
    appendGripperVacuumCheck(false, prefix + tr("丢杯"));
    appendMotion(m_protocol->resetGripper(),
                 prefix + tr("抓手复位"),
                 kZAxis, platelet::MotionProtocol::Axis::Gripper,
                 kZResetTimeout);
}

bool TestActionController::buildAddSample(const QueuedSample &sample,
                                          int pppTube,
                                          const QVector<int> &prpTubes)
{
    if (sample.positions.size() < 2) {
        m_error = tr("PPP+PRP 模式需要 PPP、PRP 两个血样孔");
        return false;
    }
    const int pppHole = sampleHoleIndex(sample.positions.at(0));
    const int prpHole = sampleHoleIndex(sample.positions.at(1));
    if (pppHole < 0 || prpHole < 0) {
        m_error = tr("血样孔号无效：%1 / %2")
                .arg(sample.positions.value(0), sample.positions.value(1));
        return false;
    }
    if (sample.testItems.isEmpty()
            || sample.testItems.size() != prpTubes.size()) {
        m_error = tr("测试项目数与 PRP 杯数不一致");
        return false;
    }
    const auto pppWell = m_coordinates->locateSampleWell(
                pppHole, InstrumentCoordinates::ZAxis::SampleNeedle);
    const auto prpWell = m_coordinates->locateSampleWell(
                prpHole, InstrumentCoordinates::ZAxis::SampleNeedle);
    const auto pppCup = m_coordinates->locateTube(
                pppTube, InstrumentCoordinates::ZAxis::SampleNeedle);
    if (!pppWell.xyValid || !prpWell.xyValid) {
        m_error = tr("PPP/PRP 血样孔坐标未设置");
        return false;
    }
    if (!pppCup.valid()) {
        m_error = tr("PPP 杯 %1 的样本针坐标未设置").arg(pppTube);
        return false;
    }
    for (int tube : prpTubes) {
        if (!m_coordinates->locateTube(
                    tube, InstrumentCoordinates::ZAxis::SampleNeedle).valid()) {
            m_error = tr("PRP 杯 %1 的样本针坐标未设置").arg(tube);
            return false;
        }
    }

    PumpPositionTracker &pumps = PumpPositionTracker::instance();
    if (!pumps.sampleValid()) {
        m_preflightBlocksTest = true;
        m_error = tr("样本泵位置未知，请先执行开机灌注");
        return false;
    }
    m_sampleStart = pumps.samplePosition();
    const int pppSteps = pumpStepsForUl(sampleAspirateVolumeUl(),
                                        bloodPumpStepsPerUl());
    const int doseSteps = pumpStepsForUl(samplePrpDoseUl(),
                                         bloodPumpStepsPerUl());
    const int prpTotalSteps = doseSteps * sample.testItems.size();
    int pppTarget = 0;
    int prpTarget = 0;
    if (!pumpTarget(m_sampleStart, pppSteps, &pppTarget, &m_error)
            || !pumpTarget(m_sampleStart, prpTotalSteps, &prpTarget,
                           &m_error)) {
        return false;
    }
    if (doseSteps <= 0) {
        m_error = tr("PRP 单份吸样量换算为 0 步");
        return false;
    }

    appendResetAllZ(tr("加样："));
    appendMotion(m_protocol->moveXy(
                     qRound(pppWell.x), qRound(pppWell.y),
                     platelet::MotionProtocol::XyReference::BloodNeedle,
                     platelet::MotionProtocol::Area::Test),
                 tr("加样：XY 到 PPP 孔"),
                 kMainBoard, platelet::MotionProtocol::Axis::Xy,
                 kXyTimeout);

    const int mouth = sampleTubeMouthMm();
    const int failHeight = sampleDetectionFailHeightMm();
    appendMotion(m_protocol->moveSampleNeedleTo(
                     failHeight, platelet::MotionProtocol::Area::Test,
                     true),
                 tr("加样：PPP 下针液面探测"),
                 kZAxis, platelet::MotionProtocol::Axis::SampleNeedle,
                 kZMoveTimeout, PumpUpdate::None, -1,
                 [this, mouth, failHeight](
                 const platelet::MotionProtocol::Frame &frame) {
        // 协议：Z 轴回包 axisByte bit7 = 1 表示液面探测成功；
        // 回包高度取 distance2（字节 10~13，小端），与老软件一致。
        const bool detectedOk = (frame.axisByte & 0x80) != 0;
        const int detected = frame.distance2;
        const bool hanging = detectedOk
                && liquidLevelIsHangingDrop(detected, mouth);
        qInfo().nospace()
                << "样本针液面探测(PPP)：探测"
                << (detectedOk ? "成功" : "失败")
                << " 回包高度=" << detected << "mm"
                << " 试管口=" << mouth << "mm"
                << " 失败高度=" << failHeight << "mm"
                << " axisByte=0x" << QString::number(frame.axisByte, 16)
                << " distance1=" << frame.distance1
                << " 决策="
                << (hanging ? "挂珠，下到失败高度吸样"
                    : (!detectedOk ? "未探到，下到失败高度吸样"
                                   : "在回包高度吸样"));
        if (!detectedOk || hanging) {
            Step fallback;
            fallback.kind = StepKind::Motion;
            fallback.frame = m_protocol->moveSampleNeedleTo(
                        failHeight, platelet::MotionProtocol::Area::Test,
                        false);
            fallback.description = detectedOk
                    ? tr("加样：PPP 挂珠，下到失败高度")
                    : tr("加样：PPP 未探到液面，下到失败高度");
            fallback.slaveAddress = kZAxis;
            fallback.axis =
                    platelet::MotionProtocol::Axis::SampleNeedle;
            fallback.timeoutMs = kZMoveTimeout;
            insertNextStep(fallback);
        }
    });
    appendMotion(m_protocol->moveBloodPump(pppTarget, true),
                 tr("加样：吸 PPP %1µL").arg(sampleAspirateVolumeUl()),
                 kMainBoard, platelet::MotionProtocol::Axis::BloodPump,
                 kPumpTimeout, PumpUpdate::SamplePump, pppTarget);
    appendDelay(sampleSuckLiftDelayMs(), tr("加样：吸 PPP 后延时"));
    appendMotion(m_protocol->resetSampleNeedle(),
                 tr("加样：样本针复位"),
                 kZAxis, platelet::MotionProtocol::Axis::SampleNeedle,
                 kZResetTimeout);

    appendResetAllZ(tr("加样："));
    appendMotion(m_protocol->moveXy(
                     qRound(pppCup.x), qRound(pppCup.y),
                     platelet::MotionProtocol::XyReference::BloodNeedle,
                     platelet::MotionProtocol::Area::Test),
                 tr("加样：XY 到 PPP 杯"),
                 kMainBoard, platelet::MotionProtocol::Axis::Xy,
                 kXyTimeout);
    appendMotion(m_protocol->moveSampleNeedleTo(
                     qRound(pppCup.z),
                     platelet::MotionProtocol::Area::Test, false),
                 tr("加样：样本针下到 PPP 杯"),
                 kZAxis, platelet::MotionProtocol::Axis::SampleNeedle,
                 kZMoveTimeout);
    appendMotion(m_protocol->moveBloodPump(m_sampleStart, false),
                 tr("加样：吐 PPP"),
                 kMainBoard, platelet::MotionProtocol::Axis::BloodPump,
                 kPumpTimeout, PumpUpdate::SamplePump, m_sampleStart);
    appendDelay(sampleSpitLiftDelayMs(), tr("加样：吐 PPP 后延时"));
    appendMotion(m_protocol->resetSampleNeedle(),
                 tr("加样：样本针复位"),
                 kZAxis, platelet::MotionProtocol::Axis::SampleNeedle,
                 kZResetTimeout);

    appendResetAllZ(tr("加样："));
    appendMotion(m_protocol->moveXy(
                     qRound(prpWell.x), qRound(prpWell.y),
                     platelet::MotionProtocol::XyReference::BloodNeedle,
                     platelet::MotionProtocol::Area::Test),
                 tr("加样：XY 到 PRP 孔"),
                 kMainBoard, platelet::MotionProtocol::Axis::Xy,
                 kXyTimeout);
    bool heightOk = false;
    int prpHeight = sample.height.trimmed().toInt(&heightOk);
    if (!heightOk || prpHeight <= 0) {
        prpHeight = qRound(prpWell.z);
    }
    appendMotion(m_protocol->moveSampleNeedleTo(
                     prpHeight, platelet::MotionProtocol::Area::Test,
                     false),
                 tr("加样：样本针下到 PRP 高度 %1").arg(prpHeight),
                 kZAxis, platelet::MotionProtocol::Axis::SampleNeedle,
                 kZMoveTimeout);
    appendMotion(m_protocol->moveBloodPump(prpTarget, true),
                 tr("加样：吸 PRP %1 份").arg(sample.testItems.size()),
                 kMainBoard, platelet::MotionProtocol::Axis::BloodPump,
                 kPumpTimeout, PumpUpdate::SamplePump, prpTarget);
    appendDelay(sampleSuckLiftDelayMs(), tr("加样：吸 PRP 后延时"));
    appendMotion(m_protocol->resetSampleNeedle(),
                 tr("加样：样本针复位"),
                 kZAxis, platelet::MotionProtocol::Axis::SampleNeedle,
                 kZResetTimeout);

    int currentPump = prpTarget;
    for (int i = 0; i < prpTubes.size(); ++i) {
        const auto cup = m_coordinates->locateTube(
                    prpTubes.at(i),
                    InstrumentCoordinates::ZAxis::SampleNeedle);
        appendResetAllZ(tr("加样："));
        appendMotion(m_protocol->moveXy(
                         qRound(cup.x), qRound(cup.y),
                         platelet::MotionProtocol::XyReference::BloodNeedle,
                         platelet::MotionProtocol::Area::Test),
                     tr("加样：XY 到 PRP 杯 %1").arg(prpTubes.at(i) + 1),
                     kMainBoard, platelet::MotionProtocol::Axis::Xy,
                     kXyTimeout);
        appendMotion(m_protocol->moveSampleNeedleTo(
                         qRound(cup.z),
                         platelet::MotionProtocol::Area::Test, false),
                     tr("加样：样本针下到 PRP 杯"),
                     kZAxis, platelet::MotionProtocol::Axis::SampleNeedle,
                     kZMoveTimeout);
        currentPump -= doseSteps;
        appendMotion(m_protocol->moveBloodPump(currentPump, false),
                     tr("加样：吐 PRP %1")
                     .arg(sample.testItems.value(i)),
                     kMainBoard, platelet::MotionProtocol::Axis::BloodPump,
                     kPumpTimeout, PumpUpdate::SamplePump, currentPump);
        appendDelay(sampleSpitLiftDelayMs(),
                    tr("加样：吐 PRP 后延时"));
        appendMotion(m_protocol->resetSampleNeedle(),
                     tr("加样：样本针复位"),
                     kZAxis, platelet::MotionProtocol::Axis::SampleNeedle,
                     kZResetTimeout);
    }

    // 加样结束不再先回原点：下一步“洗样本针”直接从当前位置去清洗孔吸液。
    appendResetAllZ(tr("加样："));
    return true;
}

bool TestActionController::buildWashSampleNeedle()
{
    const auto clean = m_coordinates->locateCleaningHole(
                InstrumentCoordinates::ZAxis::SampleNeedle);
    const auto origin = m_coordinates->locateOrigin(
                InstrumentCoordinates::ZAxis::SampleNeedle);
    if (!clean.xyValid || !origin.xyValid) {
        m_error = tr("样本针清洗位/原点坐标未设置");
        return false;
    }
    PumpPositionTracker &pumps = PumpPositionTracker::instance();
    if (!pumps.sampleValid()) {
        m_preflightBlocksTest = true;
        m_error = tr("样本泵位置未知，请先执行开机灌注");
        return false;
    }
    const int steps = pumpStepsForUl(sampleWashVolumeUl(),
                                     bloodPumpStepsPerUl());
    int target = 0;
    if (!pumpTarget(pumps.samplePosition(), steps, &target, &m_error)) {
        return false;
    }
    const int cleaningTime = perfusionCleaningTimeMsFromSettings();
    const int cleanTimeout = qMax(kPumpCleanTimeout, cleaningTime + 15000);

    appendResetAllZ(tr("洗样本针："));
    appendMotion(m_protocol->moveXy(
                     qRound(clean.x), qRound(clean.y),
                     platelet::MotionProtocol::XyReference::BloodNeedle,
                     platelet::MotionProtocol::Area::Test),
                 tr("洗样本针：XY 到清洗位"),
                 kMainBoard, platelet::MotionProtocol::Axis::Xy,
                 kXyTimeout);
    appendMotion(m_protocol->moveSampleNeedleTo(
                     sampleWashDetectionFailHeightMm(),
                     platelet::MotionProtocol::Area::Test, true),
                 tr("洗样本针：下针探清洗液"),
                 kZAxis, platelet::MotionProtocol::Axis::SampleNeedle,
                 kZMoveTimeout);
    appendMotion(m_protocol->moveBloodPump(target, true),
                 tr("洗样本针：吸清洗液 %1µL")
                 .arg(sampleWashVolumeUl()),
                 kMainBoard, platelet::MotionProtocol::Axis::BloodPump,
                 kPumpTimeout, PumpUpdate::SamplePump, target);
    appendDelay(sampleSuckLiftDelayMs(), tr("洗样本针：吸液后延时"));
    appendMotion(m_protocol->resetSampleNeedle(),
                 tr("洗样本针：样本针复位"),
                 kZAxis, platelet::MotionProtocol::Axis::SampleNeedle,
                 kZResetTimeout);

    appendResetAllZ(tr("洗样本针："));
    appendMotion(m_protocol->moveXy(
                     qRound(origin.x), qRound(origin.y),
                     platelet::MotionProtocol::XyReference::BloodNeedle,
                     platelet::MotionProtocol::Area::Test),
                 tr("洗样本针：XY 回原点"),
                 kMainBoard, platelet::MotionProtocol::Axis::Xy,
                 kXyTimeout);
    appendMotion(m_protocol->moveSampleNeedleTo(
                     cleaningDumpHeightMm(),
                     platelet::MotionProtocol::Area::Test, false),
                 tr("洗样本针：下到吐液高度"),
                 kZAxis, platelet::MotionProtocol::Axis::SampleNeedle,
                 kZMoveTimeout);
    appendMotion(m_protocol->moveBloodPump(
                     0, false, true, cleaningTime),
                 tr("洗样本针：吐液清洗 %1ms").arg(cleaningTime),
                 kMainBoard, platelet::MotionProtocol::Axis::BloodPump,
                 cleanTimeout, PumpUpdate::SamplePump, 0);
    appendDelay(sampleSpitLiftDelayMs(), tr("洗样本针：吐液后延时"));
    appendMotion(m_protocol->resetSampleNeedle(),
                 tr("洗样本针：样本针复位"),
                 kZAxis, platelet::MotionProtocol::Axis::SampleNeedle,
                 kZResetTimeout);
    appendMotion(m_protocol->moveBloodPump(360, true),
                 tr("洗样本针：回吸空气 360"),
                 kMainBoard, platelet::MotionProtocol::Axis::BloodPump,
                 kPumpTimeout, PumpUpdate::SamplePump, 360);
    appendResetAllZ(tr("洗样本针："));
    // 清洗完成后 XY 走负速度复位（与「复位」动作相同），再执行后续动作。
    appendMotion(m_protocol->resetXy(),
                 tr("洗样本针：XY 负速度复位"),
                 kMainBoard, platelet::MotionProtocol::Axis::Xy,
                 kXyTimeout);
    return true;
}

bool TestActionController::buildWashReagentNeedle()
{
    const auto clean = m_coordinates->locateCleaningHole(
                InstrumentCoordinates::ZAxis::ReagentNeedle);
    const auto origin = m_coordinates->locateOrigin(
                InstrumentCoordinates::ZAxis::ReagentNeedle);
    if (!clean.xyValid || !origin.xyValid) {
        m_error = tr("试剂针清洗位/原点坐标未设置");
        return false;
    }
    PumpPositionTracker &pumps = PumpPositionTracker::instance();
    if (!pumps.reagentValid()) {
        m_preflightBlocksTest = true;
        m_error = tr("试剂泵位置未知，请先执行开机灌注");
        return false;
    }
    const int steps = pumpStepsForUl(reagentWashVolumeUl(),
                                     reagentPumpStepsPerUl());
    int target = 0;
    if (!pumpTarget(pumps.reagentPosition(), steps, &target, &m_error)) {
        return false;
    }
    const int cleaningTime = perfusionCleaningTimeMsFromSettings();
    const int cleanTimeout = qMax(kPumpCleanTimeout, cleaningTime + 15000);

    appendResetAllZ(tr("洗试剂针："));
    appendMotion(m_protocol->moveXy(
                     qRound(clean.x), qRound(clean.y),
                     platelet::MotionProtocol::XyReference::ReagentNeedle,
                     platelet::MotionProtocol::Area::Test),
                 tr("洗试剂针：XY 到清洗位"),
                 kMainBoard, platelet::MotionProtocol::Axis::Xy,
                 kXyTimeout);
    appendMotion(m_protocol->moveReagentNeedleTo(
                     reagentWashDetectionFailHeightMm(),
                     platelet::MotionProtocol::Area::Test, true),
                 tr("洗试剂针：下针探清洗液"),
                 kZAxis, platelet::MotionProtocol::Axis::ReagentNeedle,
                 kZMoveTimeout);
    appendMotion(m_protocol->moveReagentPump(target, true),
                 tr("洗试剂针：吸清洗液 %1µL")
                 .arg(reagentWashVolumeUl()),
                 kMainBoard, platelet::MotionProtocol::Axis::ReagentPump,
                 kPumpTimeout, PumpUpdate::ReagentPump, target);
    appendDelay(reagentSuckLiftDelayMs(), tr("洗试剂针：吸液后延时"));
    appendMotion(m_protocol->resetReagentNeedle(),
                 tr("洗试剂针：试剂针复位"),
                 kZAxis, platelet::MotionProtocol::Axis::ReagentNeedle,
                 kZResetTimeout);

    appendResetAllZ(tr("洗试剂针："));
    appendMotion(m_protocol->moveXy(
                     qRound(origin.x), qRound(origin.y),
                     platelet::MotionProtocol::XyReference::ReagentNeedle,
                     platelet::MotionProtocol::Area::Test),
                 tr("洗试剂针：XY 回原点"),
                 kMainBoard, platelet::MotionProtocol::Axis::Xy,
                 kXyTimeout);
    appendMotion(m_protocol->moveReagentNeedleTo(
                     cleaningDumpHeightMm(),
                     platelet::MotionProtocol::Area::Test, false),
                 tr("洗试剂针：下到吐液高度"),
                 kZAxis, platelet::MotionProtocol::Axis::ReagentNeedle,
                 kZMoveTimeout);
    appendMotion(m_protocol->moveReagentPump(
                     0, false, true, cleaningTime),
                 tr("洗试剂针：吐液清洗 %1ms").arg(cleaningTime),
                 kMainBoard, platelet::MotionProtocol::Axis::ReagentPump,
                 cleanTimeout, PumpUpdate::ReagentPump, 0);
    appendDelay(reagentSpitLiftDelayMs(), tr("洗试剂针：吐液后延时"));
    appendMotion(m_protocol->resetReagentNeedle(),
                 tr("洗试剂针：试剂针复位"),
                 kZAxis, platelet::MotionProtocol::Axis::ReagentNeedle,
                 kZResetTimeout);
    appendMotion(m_protocol->moveReagentPump(720, true),
                 tr("洗试剂针：回吸空气 720"),
                 kMainBoard, platelet::MotionProtocol::Axis::ReagentPump,
                 kPumpTimeout, PumpUpdate::ReagentPump, 720);
    appendResetAllZ(tr("洗试剂针："));
    // 清洗完成后先做一次 XY 负速度复位，再移动到清洗原点位。
    appendMotion(m_protocol->resetXy(),
                 tr("洗试剂针：XY 负速度复位"),
                 kMainBoard, platelet::MotionProtocol::Axis::Xy,
                 kXyTimeout);
    appendXyHome(tr("洗试剂针："));
    return true;
}

bool TestActionController::buildMovePppCup(int globalChannel, int pppTube)
{
    const QString prefix = tr("移PPP杯：");
    appendResetAllZ(prefix);
    appendGrabFromTube(pppTube, prefix);
    if (!m_busy) {
        return false;
    }
    appendPlaceInChannel(globalChannel, prefix);
    if (!m_busy) {
        return false;
    }
    appendCustom([this, globalChannel]() {
        emit cupPlacedInChannel(globalChannel, true);
    }, prefix + tr("PPP 杯已放入通道"));
    appendReadChannelAd(globalChannel, true, prefix);

    appendResetAllZ(prefix);
    appendGrabFromChannel(globalChannel, prefix);
    if (!m_busy) {
        return false;
    }
    appendCustom([this, globalChannel]() {
        emit cupRemovedFromChannel(globalChannel);
    }, prefix + tr("PPP 杯已拔出通道"));
    appendDiscardHeldCup(prefix);
    if (!m_busy) {
        return false;
    }
    // 不再先回清洗原点：下一步吸试剂会先复位三根 Z，再直接去试剂位。
    appendResetAllZ(prefix);
    return true;
}

bool TestActionController::buildSetupReagent(int globalChannel, int prpTube,
                                             const QString &reagent,
                                             int coordinateIndex)
{
    const int uiIndex = reagentUiIndex(reagent);
    if (uiIndex < 0) {
        m_error = tr("未知试剂：%1").arg(reagent);
        return false;
    }
    // 调度器已按“主位到报警限切预备位”的规则选好坐标位；
    // 未指定时沿用默认左位。
    const int coordIndex = coordinateIndex >= 0
            ? coordinateIndex
            : reagentCoordinateIndex(reagent);
    const auto bottle = m_coordinates->locateReagent(
                coordIndex, InstrumentCoordinates::ZAxis::ReagentNeedle);
    if (!bottle.xyValid) {
        m_error = tr("试剂位 %1 的试剂针坐标未设置")
                .arg(InstrumentCoordinates::reagentPositionName(coordIndex));
        return false;
    }
    const auto channel = m_coordinates->locateChannel(
                globalChannel - 1,
                InstrumentCoordinates::ZAxis::ReagentNeedle);
    if (!channel.valid()) {
        m_error = tr("通道 %1 的试剂针坐标未设置").arg(globalChannel);
        return false;
    }

    PumpPositionTracker &pumps = PumpPositionTracker::instance();
    if (!pumps.reagentValid()) {
        m_preflightBlocksTest = true;
        m_error = tr("试剂泵位置未知，请先执行开机灌注");
        return false;
    }
    m_reagentStart = pumps.reagentPosition();
    const int doseUl = reagentVolumeUl(uiIndex);
    // 实际使用哪个试剂位由 coordinateIndex 决定（当前测试默认左位），
    // 扣减必须跟着实际吸样的那一瓶走。
    const int consumablePosition = reagentConsumablePosition(coordIndex);
    const int steps = pumpStepsForUl(doseUl, reagentPumpStepsPerUl());
    int target = 0;
    if (steps <= 0) {
        m_error = tr("试剂 %1 吸样量换算为 0 步").arg(reagent);
        return false;
    }
    if (!pumpTarget(m_reagentStart, steps, &target, &m_error)) {
        return false;
    }

    const QString prefix = tr("测%1：").arg(reagent);
    // Z 轴复位保留（样本针/试剂针/抓手），但 XY 不再先回清洗位：
    // 直接从当前位置 XY 移动到试剂位，下针液面探测后吸试剂。
    appendResetAllZ(prefix);
    appendMotion(m_protocol->moveXy(
                     qRound(bottle.x), qRound(bottle.y),
                     platelet::MotionProtocol::XyReference::ReagentNeedle,
                     platelet::MotionProtocol::Area::Test),
                 prefix + tr("XY 到 %1 试剂位")
                 .arg(InstrumentCoordinates::reagentPositionName(coordIndex)),
                 kMainBoard, platelet::MotionProtocol::Axis::Xy,
                 kXyTimeout);

    const int mouth = reagentBottleMouthMm();
    const int failHeight = reagentDetectionFailHeightMm(uiIndex);
    appendMotion(m_protocol->moveReagentNeedleTo(
                     failHeight, platelet::MotionProtocol::Area::Test,
                     true),
                 prefix + tr("试剂针液面探测"),
                 kZAxis, platelet::MotionProtocol::Axis::ReagentNeedle,
                 kZMoveTimeout, PumpUpdate::None, -1,
                 [this, mouth, failHeight, reagent](
                 const platelet::MotionProtocol::Frame &frame) {
        // 协议：Z 轴回包 axisByte bit7 = 1 表示液面探测成功；
        // 回包高度取 distance2（字节 10~13，小端），与老软件一致。
        const bool detectedOk = (frame.axisByte & 0x80) != 0;
        const int detected = frame.distance2;
        const bool hanging = detectedOk
                && liquidLevelIsHangingDrop(detected, mouth);
        qInfo().nospace()
                << "试剂针液面探测(" << reagent << ")：探测"
                << (detectedOk ? "成功" : "失败")
                << " 回包高度=" << detected << "mm"
                << " 瓶口=" << mouth << "mm"
                << " 失败高度=" << failHeight << "mm"
                << " axisByte=0x" << QString::number(frame.axisByte, 16)
                << " distance1=" << frame.distance1
                << " 决策="
                << (hanging ? "挂珠，下到失败高度吸样"
                    : (!detectedOk ? "未探到，下到失败高度吸样"
                                   : "在回包高度吸样"));
        if (!detectedOk || hanging) {
            Step fallback;
            fallback.kind = StepKind::Motion;
            fallback.frame = m_protocol->moveReagentNeedleTo(
                        failHeight, platelet::MotionProtocol::Area::Test,
                        false);
            fallback.description = detectedOk
                    ? tr("试剂挂珠，下到失败高度")
                    : tr("试剂未探到液面，下到失败高度");
            fallback.slaveAddress = kZAxis;
            fallback.axis =
                    platelet::MotionProtocol::Axis::ReagentNeedle;
            fallback.timeoutMs = kZMoveTimeout;
            insertNextStep(fallback);
        }
    });
    appendMotion(m_protocol->moveReagentPump(target, true),
                 prefix + tr("吸 %1 %2µL").arg(reagent).arg(doseUl),
                 kMainBoard, platelet::MotionProtocol::Axis::ReagentPump,
                 kPumpTimeout, PumpUpdate::ReagentPump, target);
    appendDelay(reagentSuckLiftDelayMs(), prefix + tr("吸试剂后延时"));
    appendMotion(m_protocol->resetReagentNeedle(),
                 prefix + tr("试剂针复位"),
                 kZAxis, platelet::MotionProtocol::Axis::ReagentNeedle,
                 kZResetTimeout);

    appendResetAllZ(prefix);
    appendGrabFromTube(prpTube, prefix);
    if (!m_busy) {
        return false;
    }
    appendPlaceInChannel(globalChannel, prefix);
    if (!m_busy) {
        return false;
    }
    appendCustom([this, globalChannel]() {
        emit cupPlacedInChannel(globalChannel, false);
    }, prefix + tr("PRP 杯已放入通道"));
    appendReadChannelAd(globalChannel, false, prefix);

    appendResetAllZ(prefix);
    appendMotion(m_protocol->moveXy(
                     qRound(channel.x), qRound(channel.y),
                     platelet::MotionProtocol::XyReference::ReagentNeedle,
                     platelet::MotionProtocol::Area::Test),
                 prefix + tr("XY 到测试通道"),
                 kMainBoard, platelet::MotionProtocol::Axis::Xy,
                 kXyTimeout);
    appendMotion(m_protocol->moveReagentNeedleTo(
                     qRound(channel.z),
                     platelet::MotionProtocol::Area::Test, false),
                 prefix + tr("试剂针下到反应杯"),
                 kZAxis, platelet::MotionProtocol::Axis::ReagentNeedle,
                 kZMoveTimeout);
    appendMotion(m_protocol->moveReagentPump(m_reagentStart, false),
                 prefix + tr("吐 %1 到反应杯").arg(reagent),
                 kMainBoard, platelet::MotionProtocol::Axis::ReagentPump,
                 kPumpTimeout, PumpUpdate::ReagentPump, m_reagentStart,
                 [this, globalChannel, consumablePosition, doseUl](
                 const platelet::MotionProtocol::Frame &) {
        emit reagentDispensed(globalChannel);
        // 吐试剂成功才视为这份试剂真正被消耗；主窗口按与 S1 相同的
        // 流程扣减对应左/右瓶余量并写回主板缓存（退出时统一提交）。
        if (consumablePosition >= 0) {
            emit reagentConsumed(consumablePosition, doseUl);
        }
    });
    appendDelay(reagentSpitLiftDelayMs(), prefix + tr("吐试剂后延时"));
    appendMotion(m_protocol->resetReagentNeedle(),
                 prefix + tr("试剂针复位"),
                 kZAxis, platelet::MotionProtocol::Axis::ReagentNeedle,
                 kZResetTimeout);

    // 吐完试剂只复位 Z，不再先回清洗原点：后续洗试剂针会直接从
    // 当前位置去清洗剂位置吸液。
    appendResetAllZ(prefix);
    return true;
}

bool TestActionController::buildDiscardCup(int globalChannel)
{
    const QString prefix = tr("丢杯：");
    appendResetAllZ(prefix);
    appendGrabFromChannel(globalChannel, prefix);
    if (!m_busy) {
        return false;
    }
    appendCustom([this, globalChannel]() {
        emit cupRemovedFromChannel(globalChannel);
    }, prefix + tr("试杯已拔出通道"));
    appendDiscardHeldCup(prefix);
    if (!m_busy) {
        return false;
    }
    // 不再先回清洗原点：后续吸试剂/加样会先复位 Z 再直接去目标位。
    appendResetAllZ(prefix);
    return true;
}

void TestActionController::addSample(const QueuedSample &sample, int pppTube,
                                     const QVector<int> &prpTubes)
{
    if (!beginAction(tr("样本 %1 加样").arg(sample.sampleNumber))) {
        return;
    }
    if (!buildAddSample(sample, pppTube, prpTubes)) {
        finishAction(false, m_error);
        return;
    }
    runNextStep();
}

void TestActionController::washSampleNeedle()
{
    if (!beginAction(tr("清洗样本针"))) {
        return;
    }
    if (!buildWashSampleNeedle()) {
        finishAction(false, m_error);
        return;
    }
    runNextStep();
}

void TestActionController::movePppCup(int globalChannel, int pppTube)
{
    if (!beginAction(tr("通道 %1 移 PPP 杯").arg(globalChannel))) {
        return;
    }
    m_globalChannel = globalChannel;
    if (!buildMovePppCup(globalChannel, pppTube)) {
        finishAction(false, m_error);
        return;
    }
    runNextStep();
}

void TestActionController::setupReagent(int globalChannel, int prpTube,
                                        const QString &reagent,
                                        int coordinateIndex)
{
    if (!beginAction(tr("通道 %1 测 %2").arg(globalChannel).arg(reagent))) {
        return;
    }
    m_globalChannel = globalChannel;
    if (!buildSetupReagent(globalChannel, prpTube, reagent, coordinateIndex)) {
        finishAction(false, m_error);
        return;
    }
    runNextStep();
}

void TestActionController::washReagentNeedle()
{
    if (!beginAction(tr("清洗试剂针"))) {
        return;
    }
    if (!buildWashReagentNeedle()) {
        finishAction(false, m_error);
        return;
    }
    runNextStep();
}

void TestActionController::discardCup(int globalChannel, const QString &reagent)
{
    Q_UNUSED(reagent);
    if (!beginAction(tr("通道 %1 丢杯").arg(globalChannel))) {
        return;
    }
    m_globalChannel = globalChannel;
    if (!buildDiscardCup(globalChannel)) {
        finishAction(false, m_error);
        return;
    }
    runNextStep();
}
