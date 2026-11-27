#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "testscheduler.h"

#include "addsamplewidget.h"
#include "aggregationcalculator.h"
#include "appconfig.h"
#include "instrumentparameters.h"
#include "modulerackwidget.h"
#include "moduleoverviewwidget.h"
#include "sampletraymodel.h"

#include <QDebug>
#include <QScopedValueRollback>
#include <QTimer>

namespace {
constexpr int kReactionSeconds = 300;
constexpr int kMaxPrpPoints = 300;
}

TestScheduler::TestScheduler(AddSampleWidget *addSampleWidget,
                             SampleTrayModel *trayModel,
                             ModuleOverviewWidget *overview,
                             QObject *parent)
    : QObject(parent)
    , m_addSampleWidget(addSampleWidget)
    , m_trayModel(trayModel)
    , m_overview(overview)
{
    if (m_trayModel) {
        // 换盘/释放杯子后自动重试被暂停的加样。
        connect(m_trayModel, &SampleTrayModel::modelChanged,
                this, [this]() {
            if (m_running && !m_paused) {
                pump();
            }
        });
    }
}

TestScheduler::~TestScheduler()
{
    stopAllTimers();
}

void TestScheduler::setActionLayerReady(bool ready)
{
    m_actionLayerReady = ready;
}

bool TestScheduler::isActionLayerReady() const
{
    return m_actionLayerReady;
}

bool TestScheduler::isRunning() const
{
    return m_running;
}

bool TestScheduler::isPaused() const
{
    return m_paused;
}

void TestScheduler::setReagentPositionSelector(
        ReagentPositionSelector selector)
{
    m_reagentPositionSelector = selector;
}

void TestScheduler::start()
{
    qInfo() << "测试启动请求: running" << m_running
            << "动作中" << m_actionInProgress
            << "待测队列" << (m_addSampleWidget
                              ? m_addSampleWidget->testQueue()->size() : -1);
    if (m_running) {
        emit errorOccurred(tr("测试已在进行中"));
        return;
    }
    if (!m_addSampleWidget || !m_trayModel || !m_overview) {
        qWarning() << "测试启动失败：调度器依赖未初始化";
        emit errorOccurred(tr("测试调度器依赖未初始化"));
        return;
    }
    qInfo() << "测试启动检查: 待测队列"
            << m_addSampleWidget->testQueue()->size()
            << "PPP+PRP" << testModeIsPppPrp()
            << "动作层就绪" << m_actionLayerReady;
    if (!testModeIsPppPrp()) {
        qWarning() << "测试启动失败：当前只支持 PPP+PRP 模式";
        emit errorOccurred(tr("当前调度器只支持 PPP+PRP 模式"));
        return;
    }
    if (!m_actionLayerReady) {
        qWarning() << "测试启动失败：动作层尚未接入";
        emit errorOccurred(tr("测试动作层尚未接入"));
        return;
    }
    if (m_addSampleWidget->testQueue()->size() <= 0 && !m_hasAddRun) {
        qWarning() << "测试启动失败：没有待测样本（队列为空）";
        emit errorOccurred(tr("没有待测样本：请先在加样页把样本「确认」到待测"));
        return;
    }

    m_running = true;
    m_paused = false;
    m_finalParkDone = false;
    m_overview->setTestingActive(true);
    emit statusChanged(tr("测试开始"));
    pump();
}

void TestScheduler::pause()
{
    if (!m_running) {
        return;
    }
    m_paused = true;
    emit statusChanged(tr("测试已暂停：当前机械动作完成后停止下发"));
}

void TestScheduler::resume()
{
    if (!m_running) {
        return;
    }
    m_paused = false;
    emit statusChanged(tr("测试继续"));
    pump();
}

void TestScheduler::reset()
{
    const bool hadAction = m_actionInProgress;
    m_running = false;
    m_paused = false;
    m_finalParkDone = false;
    stopAllTimers();
    clearAction();

    if (hadAction) {
        emit cancelActionRequested();
    }

    for (int channel : m_channelsInUse) {
        setChannelIdleUi(channel);
    }
    if (m_addSampleWidget) {
        // 复位后允许删除/修改原测试中的样本；加样页状态文字保持不变。
        m_addSampleWidget->clearTestingSamples();
    }
    m_channelsInUse.clear();
    m_active.clear();
    m_waitingChannel.clear();
    if (m_hasAddRun) {
        releaseCups(&m_addRun);
    }
    m_hasAddRun = false;
    m_addRun = SampleRun();
    m_sampleNeedleDirty = false;
    m_reagentNeedleDirty = false;

    if (m_overview) {
        m_overview->setTestingActive(false);
    }
    emit statusChanged(tr("测试已复位"));
}

void TestScheduler::handleActionFinished(bool ok, const QString &message)
{
    if (!m_actionInProgress) {
        return;
    }

    if (!ok) {
        cancelCurrentAction(message.isEmpty() ? tr("测试动作失败")
                                              : message);
        return;
    }

    switch (m_action) {
    case ActionType::AddSample:
        finishAddSample();
        break;
    case ActionType::WashSampleNeedle:
        m_sampleNeedleDirty = false;
        // 测试流程洗针吸的也是 S1，和主页手动清洗一样按实际用量扣减并写回主板。
        emit internalCleanUsed(double(sampleWashVolumeUl()));
        emit statusChanged(tr("样本针清洗完成"));
        break;
    case ActionType::MovePppCup:
        finishMovePppCup();
        break;
    case ActionType::SetupReagent:
        // 反应已在吐试剂完成时开始（handleReagentDispensed），
        // 动作收尾（针复位/回原点）不再影响计时。
        break;
    case ActionType::WashReagentNeedle:
        m_reagentNeedleDirty = false;
        emit internalCleanUsed(double(reagentWashVolumeUl()));
        emit statusChanged(tr("试剂针清洗完成"));
        break;
    case ActionType::DiscardCup:
        finishDiscardCup();
        break;
    case ActionType::Park:
        // 收尾动作完成，pump() 会走最后的总结束分支。
        break;
    case ActionType::None:
        break;
    }

    clearAction();
    pump();
}

void TestScheduler::handleActionRejected(const QString &message, bool blocksTest)
{
    if (!m_actionInProgress) {
        return;
    }

    const ActionType action = m_action;
    qWarning() << "动作预检失败（未下发任何运动）:" << int(action)
               << (blocksTest ? "全局阻塞" : "仅当前样本/试剂") << message;

    if (blocksTest) {
        // 泵位置未知等全局前置条件缺失：暂停下发新动作，但保持 m_running，
        // 已经在通道里反应的样本继续按 AD 采样计时。
        m_paused = true;
        clearAction();
        emit statusChanged(
                    tr("测试动作无法开始：%1。已暂停下发新动作，"
                       "正在反应的通道继续计时；处理后点“继续”。")
                    .arg(message));
        return;
    }

    switch (action) {
    case ActionType::SetupReagent:
        // 当前试剂无法开始：只跳过这个试剂，样本/其他通道继续。
        abandonCurrentReagent(message);
        break;
    case ActionType::MovePppCup:
        // 当前样本的 PPP/PRP 杯无法抓取：只放弃这个样本。
        abandonPppSample(message);
        break;
    case ActionType::AddSample:
        abandonAddSample(message);
        break;
    case ActionType::DiscardCup:
        abandonStuckChannel(message);
        break;
    case ActionType::WashSampleNeedle:
    case ActionType::WashReagentNeedle:
        // 洗针是全局前置动作：保留 m_running，让已经在反应的通道继续
        // 按 AD 采样计时，只暂停下发新的机械动作，等处理后点“继续”。
        m_paused = true;
        clearAction();
        emit statusChanged(
                    tr("测试动作无法开始：%1。已暂停下发新动作，"
                       "正在反应的通道继续计时；处理后点“继续”。")
                    .arg(message));
        return;
    case ActionType::Park:
        // 收尾动作无法构建：本次没有在途样本，直接结束会话。
        clearAction();
        emit statusChanged(tr("收尾动作无法开始：%1").arg(message));
        break;
    case ActionType::None:
        clearAction();
        return;
    }

    clearAction();
    // 预检失败是同步发生在 startXXX() 的调用栈里（此时外层 pump() 的
    // 重入保护仍生效），直接 pump() 会被挡掉，投递到事件循环后再继续调度。
    QTimer::singleShot(0, this, [this]() {
        if (m_running && !m_paused && !m_actionInProgress) {
            pump();
        }
    });
}

void TestScheduler::abandonAddSample(const QString &reason)
{
    if (!m_hasAddRun) {
        return;
    }
    const QString sampleNumber = m_addRun.sample.sampleNumber;
    if (m_addRun.cupsAllocated) {
        releaseCups(&m_addRun);
    }
    if (m_addSampleWidget) {
        m_addSampleWidget->setSampleAbnormal(sampleNumber);
    }
    emit sampleMarkedAbnormal(sampleNumber);
    emit statusChanged(tr("样本 %1 加样动作无法开始（%2），已标记异常并跳过")
                       .arg(sampleNumber, reason));
    m_addRun = SampleRun();
    m_hasAddRun = false;
}

void TestScheduler::abandonStuckChannel(const QString &reason)
{
    SampleRun *run = findRunByChannel(m_actionChannel);
    if (!run) {
        return;
    }
    const QString sampleNumber = run->sample.sampleNumber;
    const int channel = run->globalChannel;
    run->discardPending = false;
    run->abandoned = true;

    if (m_addSampleWidget) {
        m_addSampleWidget->setSampleAbnormal(sampleNumber);
    }
    emit sampleMarkedAbnormal(sampleNumber);

    const int module = globalToModule(channel);
    const int local = globalToLocal(channel);
    m_overview->stopMotor(module, local);
    m_overview->setChannelRunState(module, local, ChannelRunState::Abnormal);
    m_overview->setChannelStatus(
                module, local,
                tr("样本:%1 试杯未丢弃，请手动处理").arg(sampleNumber));

    // 试杯还在通道里，不能释放给下一个样本，否则会撞杯。
    // 通道保持占用（m_channelsInUse 不移除），等待人工处理后复位。
    for (int i = 0; i < m_active.size(); ++i) {
        if (m_active.at(i).sample.sampleNumber == sampleNumber) {
            m_active.removeAt(i);
            break;
        }
    }
    emit statusChanged(tr("样本 %1 丢杯动作无法开始（%2），通道 %3 保持占用")
                       .arg(sampleNumber, reason).arg(channel));
}

void TestScheduler::handleActionAbandoned(const QString &message)
{
    if (!m_actionInProgress) {
        return;
    }
    qWarning() << "操作员放弃当前动作:" << message;

    // 保持 m_actionInProgress=true 完成清理，防止托盘 modelChanged
    // 在清理过程中重入 pump()。
    switch (m_action) {
    case ActionType::MovePppCup:
        abandonPppSample(message);
        break;
    case ActionType::SetupReagent:
        abandonCurrentReagent(message);
        break;
    default:
        // 其它动作不支持放弃，按普通失败中止测试。
        cancelCurrentAction(message);
        return;
    }

    clearAction();
    pump();
}

void TestScheduler::discardTrayTube(int tube)
{
    if (!m_trayModel || tube < 0) {
        return;
    }
    const SampleTrayModel::TubeState state = m_trayModel->tubeState(tube);
    if (state == SampleTrayModel::TubeState::Sampled) {
        m_trayModel->markUsed(tube);
    } else if (state == SampleTrayModel::TubeState::Allocated) {
        m_trayModel->clearTube(tube);
    }
}

void TestScheduler::abandonPppSample(const QString &reason)
{
    SampleRun *run = findRunByChannel(m_actionChannel);
    if (!run) {
        return;
    }
    const QString sampleNumber = run->sample.sampleNumber;
    const int channel = run->globalChannel;
    qWarning() << "放弃样本:" << sampleNumber << reason;

    // 本次样本已加样的 PPP/PRP 杯全部作废，释放未加样的杯。
    discardTrayTube(run->pppTube);
    for (int tube : qAsConst(run->prpTubes)) {
        discardTrayTube(tube);
    }

    // 结果信息表标记异常；加样页状态继续保持“加样完成”。
    m_addSampleWidget->setSampleAbnormal(sampleNumber);

    for (int i = 0; i < m_active.size(); ++i) {
        if (m_active.at(i).sample.sampleNumber == sampleNumber) {
            m_active.removeAt(i);
            break;
        }
    }
    releaseChannel(channel);
    emit statusChanged(tr("样本 %1 已放弃（%2），其他通道继续")
                       .arg(sampleNumber, reason));
}

void TestScheduler::abandonCurrentReagent(const QString &reason)
{
    SampleRun *run = findRunByChannel(m_actionChannel);
    if (!run) {
        return;
    }
    const int index = m_actionReagentIndex >= 0
            ? m_actionReagentIndex : run->nextReagentIndex;
    if (index < 0 || index >= run->sample.testItems.size()) {
        return;
    }
    const QString sampleNumber = run->sample.sampleNumber;
    const QString reagent = run->sample.testItems.at(index);
    qWarning() << "放弃试剂:" << sampleNumber << reagent << reason;
    run->abandoned = true;

    // 该试剂动作失败/被放弃：发出信号，让主窗口把它落库标记为“异常结果”，
    // 结果详情便能独立显示「AA 完成 / ADP 异常 / …」而不再只是空曲线。
    emit reagentAbandoned(sampleNumber, reagent);

    // 当前 PRP 杯作废；试剂针里已经吸的试剂标记为需要清洗。
    if (index < run->prpTubes.size()) {
        discardTrayTube(run->prpTubes.at(index));
    }
    m_reagentNeedleDirty = true;
    run->reactionActive = false;
    run->discardPending = false;
    run->currentTest = TestCurveData();

    const int module = globalToModule(run->globalChannel);
    const int local = globalToLocal(run->globalChannel);
    if (index + 1 < run->sample.testItems.size()) {
        run->nextReagentIndex = index + 1;
        run->reagentPending = true;
        m_overview->setChannelStatus(
                    module, local,
                    tr("样本:%1 放弃%2，等待下一个试剂")
                    .arg(sampleNumber, reagent));
        emit statusChanged(tr("样本 %1 放弃试剂 %2（%3），继续下一个试剂")
                           .arg(sampleNumber, reagent, reason));
        return;
    }

    m_overview->setChannelStatus(
                module, local,
                tr("样本:%1 放弃%2，测试结束")
                .arg(sampleNumber, reagent));
    emit statusChanged(tr("样本 %1 放弃试剂 %2（%3），没有后续试剂，测试结束")
                       .arg(sampleNumber, reagent, reason));
    completeSample(run);
}

void TestScheduler::handleChannelAd(int moduleNumber, int localChannel,
                                    int adValue)
{
    if (!m_running || adValue <= 0) {
        return;
    }
    const int globalChannel = (moduleNumber - 1) * 4 + localChannel;
    SampleRun *run = findRunByChannel(globalChannel);
    if (!run || !run->reactionActive) {
        return;
    }
    // AD 轮询是 100ms 一次，反应曲线按 1s 一个点记录。
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (run->lastAdSampleMs > 0 && now - run->lastAdSampleMs < 1000) {
        return;
    }
    run->lastAdSampleMs = now;
    if (run->currentTest.prpPoints.size() >= kMaxPrpPoints) {
        return;
    }
    const int module = globalToModule(globalChannel);
    const int local = globalToLocal(globalChannel);
    const int second = run->currentTest.prpPoints.size() + 1;
    run->currentTest.prpPoints.append(adValue);
    // 测试曲线与集聚曲线共用同一个反应秒数：两条曲线都由吐试剂后的
    // 每秒 AD 采样驱动，x 轴与顶部“反应秒数”完全同步。
    m_overview->appendChannelTestPoint(module, local, second, adValue);
    // 集聚曲线：用老程序的对数分段公式逐点计算，并随 PRP 点一起落库，
    // 保证每个试剂都保存完整的 300 秒 PRP + 集聚率数据。
    if (run->currentTest.prp0 > 0 && run->pppValue > 0) {
        const double percent = AggregationCalculator::aggregationPercent(
                    adValue, run->currentTest.prp0, run->pppValue);
        run->currentTest.aggregationPoints.append(percent);
        m_overview->appendChannelAggregationPoint(module, local,
                                                  second, percent);
    }
    // 顶部反应秒数/PRP 值与曲线点同步刷新（不再由 rack 的 AD 回调自行累加）。
    m_overview->setChannelTestInfo(module, local,
                                   run->sample.sampleNumber,
                                   run->currentTest.reagent,
                                   second, adValue);
    // 进度按收到的点数走：300 个点 = 300 秒 = 100%，每点即更新
    const int progress = qMin(100, run->currentTest.prpPoints.size()
                              * 100 / kMaxPrpPoints);
    m_overview->setChannelProgress(moduleNumber, localChannel, progress);
    // 收满 300 个点（= 300 秒）即反应结束，不再需要额外定时器
    if (run->currentTest.prpPoints.size() >= kMaxPrpPoints) {
        finishReaction(globalChannel);
    }
}

void TestScheduler::setPppValue(int globalChannel, int value)
{
    SampleRun *run = findRunByChannel(globalChannel);
    if (!run) {
        return;
    }
    run->pppValue = value;
    // PPP 基准值过小（<1000）说明样本量不足或通道异常，标记异常结果；
    // 后续试剂测试照常走完动作流程，但样本状态不再变回“测试结束”。
    if (value >= 0 && value < 1000) {
        const QString sampleNumber = run->sample.sampleNumber;
        run->sample.testItems.clear();
        run->reagentPending = false;
        run->reactionActive = false;
        qWarning() << "PPP 值过低，标记样本异常" << sampleNumber << value;
        emit statusChanged(tr("样本 %1 PPP 值 %2 过低，标记异常结果")
                           .arg(sampleNumber).arg(value));
        emit sampleMarkedAbnormal(sampleNumber);
    }
}

void TestScheduler::setPrp0Value(int globalChannel, int value)
{
    SampleRun *run = findRunByChannel(globalChannel);
    if (!run) {
        return;
    }
    // PRP0 只作为集聚计算的基准值和曲线顶部初始 PRP 显示；
    // 300s 反应不在这里启动，而是等试剂真正吐入反应杯后开始。
    run->currentTest.prp0 = value;
}

bool TestScheduler::pump()
{
    if (!m_running || m_paused || m_actionInProgress || m_pumping) {
        return false;
    }
    QScopedValueRollback<bool> guard(m_pumping, true);

    // 有新任务进来后，下一次任务全部结束时要重新做收尾动作。
    if (m_hasAddRun || !m_waitingChannel.isEmpty() || !m_active.isEmpty()
            || m_addSampleWidget->testQueue()->size() > 0) {
        m_finalParkDone = false;
    }

    // 1. 加样完立刻洗样本针，洗完才允许下一个加样。
    if (m_sampleNeedleDirty) {
        return startWashSampleNeedle();
    }

    // 2. 通道里有杯子要丢，优先释放通道。
    for (const SampleRun &run : qAsConst(m_active)) {
        if (run.discardPending) {
            return startDiscardCup();
        }
    }

    // 3. 新上通道的样本先读 PPP。
    for (const SampleRun &run : qAsConst(m_active)) {
        if (!run.pppDone) {
            return startMovePppCup();
        }
    }

    // 4. PPP 读完 / 上一个试剂丢杯后，准备下一个试剂。
    if (!m_reagentNeedleDirty) {
        for (const SampleRun &run : qAsConst(m_active)) {
            if (run.reagentPending && !run.reactionActive) {
                return startSetupReagent();
            }
        }
    }

    // 5. 每吐完一个试剂洗试剂针。
    if (m_reagentNeedleDirty) {
        return startWashReagentNeedle();
    }

    // 6. 有空闲通道就分配给等通道的样本。
    if (!m_waitingChannel.isEmpty()) {
        const int channel = allocateChannel();
        if (channel > 0) {
            SampleRun run = m_waitingChannel.takeFirst();
            m_active.append(run);
            assignChannel(&m_active.last(), channel);
            return startMovePppCup();
        }
    }

    // 7. 没通道就继续给后面的样本加样。
    if (!m_hasAddRun && m_addSampleWidget->testQueue()->size() > 0) {
        QueuedSample sample;
        if (m_addSampleWidget->testQueue()->dequeue(&sample)) {
            m_addRun = SampleRun();
            m_addRun.sample = sample;
            m_hasAddRun = true;
        }
    }
    if (m_hasAddRun) {
        return startAddSample();
    }

    // 8. 没有待加样、等通道、在测样本则收尾。
    if (m_active.isEmpty() && m_waitingChannel.isEmpty()
            && !m_hasAddRun
            && m_addSampleWidget->testQueue()->size() <= 0) {
        if (!m_finalParkDone) {
            // 后续没有机械臂任务：先到清洗原点，再做一次 XY 负速度复位。
            m_finalParkDone = true;
            m_action = ActionType::Park;
            m_actionSample.clear();
            m_actionChannel = -1;
            m_actionReagentIndex = -1;
            m_actionInProgress = true;
            emit statusChanged(
                        tr("机械臂收尾：XY 负速度复位后移动到清洗原点"));
            emit parkAtOriginRequested();
            return true;
        }
        // 所有样本（含后续新确认的样本）都做完：结束本次测试会话，
        // 恢复开始按钮和耗材更换等操作。
        m_running = false;
        m_overview->setTestingActive(false);
        // 没有剩余任务：所有启用通道恢复空闲显示。
        const int channelCount = activeChannelCount();
        for (int channel = 1; channel <= channelCount; ++channel) {
            const int module = globalToModule(channel);
            const int local = globalToLocal(channel);
            if (!m_overview->isChannelEnabled(module, local)) {
                continue;
            }
            setChannelIdleUi(channel);
            m_overview->setChannelProgress(module, local, 0);
        }
        emit statusChanged(tr("测试完成"));
        emit finished();
    }
    return false;
}

void TestScheduler::notifyWorkAvailable()
{
    if (!m_running || m_paused || m_actionInProgress) {
        return;
    }
    qInfo() << "测试会话收到新样本，尝试继续调度，队列"
            << (m_addSampleWidget
                ? m_addSampleWidget->testQueue()->size() : -1);
    pump();
}

bool TestScheduler::startAddSample()
{
    if (!m_hasAddRun) {
        return false;
    }
    if (!m_addRun.cupsAllocated) {
        QString error;
        if (!allocateCups(&m_addRun, &error)) {
            qWarning() << "加样暂停，空试管分配失败:" << error
                       << "样本" << m_addRun.sample.sampleNumber;
            emit statusChanged(tr("空试管不足，已暂停加样：%1").arg(error));
            return false;
        }
        m_addRun.cupsAllocated = true;
    }

    m_action = ActionType::AddSample;
    m_actionSample = m_addRun.sample.sampleNumber;
    m_actionChannel = -1;
    m_actionReagentIndex = -1;
    m_actionInProgress = true;
    qInfo() << "开始加样" << m_addRun.sample.sampleNumber
            << "PPP杯" << m_addRun.pppTube << "PRP杯" << m_addRun.prpTubes;
    emit statusChanged(tr("样本 %1 加样中").arg(m_actionSample));
    emit addSampleRequested(m_addRun.sample, m_addRun.pppTube,
                            m_addRun.prpTubes);
    return true;
}

bool TestScheduler::startWashSampleNeedle()
{
    m_action = ActionType::WashSampleNeedle;
    m_actionSample.clear();
    m_actionChannel = -1;
    m_actionReagentIndex = -1;
    m_actionInProgress = true;
    emit statusChanged(tr("清洗样本针"));
    emit washSampleNeedleRequested();
    return true;
}

bool TestScheduler::startMovePppCup()
{
    for (SampleRun &run : m_active) {
        if (run.pppDone) {
            continue;
        }
        m_action = ActionType::MovePppCup;
        m_actionSample = run.sample.sampleNumber;
        m_actionChannel = run.globalChannel;
        m_actionReagentIndex = -1;
        m_actionInProgress = true;
        updateChannelUiForPpp(&run);
        emit statusChanged(tr("样本 %1 读取 PPP").arg(m_actionSample));
        emit movePppCupRequested(run.globalChannel, run.pppTube);
        return true;
    }
    return false;
}

bool TestScheduler::startSetupReagent()
{
    for (SampleRun &run : m_active) {
        if (!run.reagentPending || run.reactionActive
                || run.discardPending || !run.pppDone) {
            continue;
        }
        if (run.nextReagentIndex < 0
                || run.nextReagentIndex >= run.sample.testItems.size()
                || run.nextReagentIndex >= run.prpTubes.size()) {
            continue;
        }

        const QString reagent = run.sample.testItems.at(run.nextReagentIndex);

        // 吸试剂前先确认该试剂是否有可用瓶位（主位/预备位）。
        // 返回 <0：余量到报警限且不可切换，保留当前试剂待测状态并暂停，
        // 操作员更换试剂后点「继续」会重新走这里。
        int coordinateIndex = -1;
        if (m_reagentPositionSelector) {
            QString reason;
            coordinateIndex = m_reagentPositionSelector(reagent, &reason);
            if (coordinateIndex < 0) {
                m_paused = true;
                const QString message =
                        reason.isEmpty()
                        ? tr("试剂余量到报警限，测试已暂停，"
                             "请更换试剂后点「继续」。")
                        : reason;
                emit statusChanged(message);
                emit reagentAlarm(reagent, message);
                return false;
            }
        }

        run.currentTest = TestCurveData();
        run.currentTest.reagent = reagent;
        run.currentTest.moduleNumber = globalToModule(run.globalChannel);
        run.currentTest.localChannel = globalToLocal(run.globalChannel);
        run.currentTest.globalChannel = run.globalChannel;
        run.currentTest.status = tr("测试中");

        // 吐试剂前先把该试剂落一条“测试中”占位：主窗口据此在
        // sample_tests 插入一行，使结果详情/查询树能实时看到进行中的试剂。
        emit reagentTestStarted(run.sample.sampleNumber, reagent);

        m_action = ActionType::SetupReagent;
        m_actionSample = run.sample.sampleNumber;
        m_actionChannel = run.globalChannel;
        m_actionReagentIndex = run.nextReagentIndex;
        m_actionInProgress = true;
        updateChannelUiForReagent(&run);
        emit statusChanged(tr("样本 %1 测试 %2")
                           .arg(m_actionSample, reagent));
        emit setupReagentRequested(run.globalChannel,
                                   run.prpTubes.at(run.nextReagentIndex),
                                   reagent, coordinateIndex);
        return true;
    }
    return false;
}

bool TestScheduler::startWashReagentNeedle()
{
    m_action = ActionType::WashReagentNeedle;
    m_actionSample.clear();
    m_actionChannel = -1;
    m_actionReagentIndex = -1;
    m_actionInProgress = true;
    emit statusChanged(tr("清洗试剂针"));
    emit washReagentNeedleRequested();
    return true;
}

bool TestScheduler::startDiscardCup()
{
    for (SampleRun &run : m_active) {
        if (!run.discardPending) {
            continue;
        }
        m_action = ActionType::DiscardCup;
        m_actionSample = run.sample.sampleNumber;
        m_actionChannel = run.globalChannel;
        m_actionReagentIndex = run.nextReagentIndex;
        m_actionInProgress = true;
        emit statusChanged(tr("样本 %1 丢弃 %2 试杯")
                           .arg(m_actionSample,
                                run.currentTest.reagent));
        emit discardCupRequested(run.globalChannel,
                                 run.currentTest.reagent);
        return true;
    }
    return false;
}

void TestScheduler::finishAddSample()
{
    if (!m_hasAddRun
            || (!m_actionSample.isEmpty()
                && m_addRun.sample.sampleNumber != m_actionSample)) {
        return;
    }

    const QString sampleNumber = m_addRun.sample.sampleNumber;
    if (m_addRun.pppTube >= 0) {
        m_trayModel->markSampled(m_addRun.pppTube, QStringLiteral("PPP"));
    }
    for (int i = 0; i < m_addRun.prpTubes.size()
         && i < m_addRun.sample.testItems.size(); ++i) {
        m_trayModel->markSampled(m_addRun.prpTubes.at(i),
                                 m_addRun.sample.testItems.at(i));
    }

    m_addSampleWidget->setSampleAdded(sampleNumber);
    m_waitingChannel.append(m_addRun);
    m_addRun = SampleRun();
    m_hasAddRun = false;
    m_sampleNeedleDirty = true;
    emit statusChanged(tr("样本 %1 加样完成，等待通道").arg(sampleNumber));
}

void TestScheduler::finishMovePppCup()
{
    SampleRun *run = findRunByChannel(m_actionChannel);
    if (!run) {
        return;
    }
    if (run->pppTube >= 0) {
        m_trayModel->markUsed(run->pppTube);
    }
    run->pppDone = true;
    run->reagentPending = !run->sample.testItems.isEmpty();

    // PPP 是样本级基准值（与 PRP0 一样用于计算集聚率），不是试剂测试，
    // 单独上报给主窗口保存到 samples.ppp_value。
    emit pppBaselineFinished(run->sample.sampleNumber, run->pppValue);

    // PPP 值过低等情况下测试项目已被清空：本次没有后续试剂，直接完成
    // 并释放通道，避免通道一直占在“等待试剂”状态。
    if (run->sample.testItems.isEmpty()) {
        completeSample(run);
        return;
    }

    const int module = globalToModule(run->globalChannel);
    const int local = globalToLocal(run->globalChannel);
    m_overview->setTubeInserted(module, local, false);
    m_overview->setChannelStatus(module, local,
                                 tr("样本:%1 等待试剂")
                                 .arg(run->sample.sampleNumber));
    m_overview->setChannelProgress(module, local, 0);
}

void TestScheduler::beginReagentReaction(SampleRun *run)
{
    if (!run || run->reactionActive) {
        return;
    }
    if (m_actionReagentIndex >= 0
            && m_actionReagentIndex < run->prpTubes.size()) {
        m_trayModel->markUsed(run->prpTubes.at(m_actionReagentIndex));
    }
    run->reagentPending = false;
    m_reagentNeedleDirty = true;
    // 试剂吐入反应杯完成即开始反应：动作层已在放 PRP 杯后读过 PRP0，
    // 这里立刻开始采集 PRP1~PRP300，不再等动作收尾后的复位/回原点。
    beginReactionTiming(run);
}

void TestScheduler::handleReagentDispensed(int globalChannel)
{
    if (!m_running || m_action != ActionType::SetupReagent
            || m_actionChannel != globalChannel) {
        return;
    }
    SampleRun *run = findRunByChannel(globalChannel);
    if (!run) {
        return;
    }
    beginReagentReaction(run);
}

void TestScheduler::finishDiscardCup()
{
    SampleRun *run = findRunByChannel(m_actionChannel);
    if (!run) {
        return;
    }
    run->discardPending = false;
    const int module = globalToModule(run->globalChannel);
    const int local = globalToLocal(run->globalChannel);
    m_overview->setTubeInserted(module, local, false);
    m_overview->setChannelProgress(module, local, 100);

    if (run->nextReagentIndex + 1 < run->sample.testItems.size()) {
        ++run->nextReagentIndex;
        run->reagentPending = true;
        m_overview->setChannelStatus(module, local,
                                     tr("样本:%1 等待下一个试剂")
                                     .arg(run->sample.sampleNumber));
        return;
    }

    completeSample(run);
}

void TestScheduler::assignChannel(SampleRun *run, int globalChannel)
{
    run->globalChannel = globalChannel;
    m_channelsInUse.insert(globalChannel);

    const int module = globalToModule(globalChannel);
    const int local = globalToLocal(globalChannel);
    m_overview->setChannelRunState(module, local, ChannelRunState::Testing);
    m_overview->setChannelStatus(module, local,
                                 tr("样本:%1")
                                 .arg(run->sample.sampleNumber));
    m_overview->startMotor(module, local);
    m_addSampleWidget->setSampleTesting(run->sample.sampleNumber, true);
    emit statusChanged(tr("样本 %1 分配到通道 %2")
                       .arg(run->sample.sampleNumber)
                       .arg(globalChannel));
}

void TestScheduler::releaseChannel(int globalChannel)
{
    m_channelsInUse.remove(globalChannel);
    if (!m_overview || globalChannel <= 0) {
        return;
    }
    const int module = globalToModule(globalChannel);
    const int local = globalToLocal(globalChannel);
    // 样本所有试剂完成并丢杯后立即恢复通道空闲显示，
    // 不保留上一个样本的“完成”文字和 100% 进度。
    m_overview->stopMotor(module, local);
    m_overview->setTubeInserted(module, local, false);
    m_overview->setChannelRunState(module, local, ChannelRunState::Idle);
    m_overview->setChannelStatus(module, local, tr("空闲"));
    m_overview->setChannelProgress(module, local, 0);
}

void TestScheduler::completeSample(SampleRun *run)
{
    const QString sampleNumber = run->sample.sampleNumber;
    const int channel = run->globalChannel;
    m_addSampleWidget->setSampleCompleted(sampleNumber);
    // 有试剂/杯子被操作员放弃，或 PPP 值过低：最终结果保持“异常结果”。
    if ((run->abandoned
         || (run->pppValue >= 0 && run->pppValue < 1000))
            && m_addSampleWidget) {
        m_addSampleWidget->setSampleAbnormal(sampleNumber);
    }

    for (int i = 0; i < m_active.size(); ++i) {
        if (m_active.at(i).sample.sampleNumber == sampleNumber) {
            m_active.removeAt(i);
            break;
        }
    }
    releaseChannel(channel);
    emit statusChanged(tr("样本 %1 测试完成").arg(sampleNumber));
}

void TestScheduler::handleCupPlaced(int globalChannel, bool ppp)
{
    if (!m_overview || globalChannel <= 0) {
        return;
    }
    const int module = globalToModule(globalChannel);
    const int local = globalToLocal(globalChannel);
    if (ModuleRackWidget *rack = m_overview->moduleRack(module)) {
        rack->setTubeContent(local,
                             (ppp || sampleQualityIsPlasma())
                             ? ModuleRackWidget::TubeContent::Ppp
                             : ModuleRackWidget::TubeContent::Prp);
    }
    m_overview->setTubeInserted(module, local, true);
}

void TestScheduler::handleCupRemoved(int globalChannel)
{
    if (!m_overview || globalChannel <= 0) {
        return;
    }
    m_overview->setTubeInserted(globalToModule(globalChannel),
                                globalToLocal(globalChannel), false);
}

void TestScheduler::startReaction(SampleRun *run)
{
    const int channel = run->globalChannel;
    run->currentTest.prpPoints.clear();
    run->currentTest.prpPoints.reserve(kMaxPrpPoints);
    run->currentTest.aggregationPoints.clear();
    run->currentTest.aggregationPoints.reserve(kMaxPrpPoints);
    run->lastAdSampleMs = 0;

    const int module = globalToModule(channel);
    const int local = globalToLocal(channel);
    m_overview->setChannelProgress(module, local, 0);
    // 吐试剂即反应起点：反应秒数归零，PRP 先显示 PRP0 基准值；
    // 之后每秒 AD 同时写入测试曲线和集聚曲线。
    m_overview->setChannelTestInfo(module, local,
                                   run->sample.sampleNumber,
                                   run->currentTest.reagent,
                                   0, run->currentTest.prp0);
    m_overview->setChannelStatus(module, local,
                                 tr("样本:%1[%2] 反应中")
                                 .arg(run->sample.sampleNumber,
                                      run->currentTest.reagent));
    emit statusChanged(tr("样本 %1 %2 吐试剂完成（PRP0=%3），开始 300s 反应")
                       .arg(run->sample.sampleNumber,
                            run->currentTest.reagent)
                       .arg(run->currentTest.prp0));
}

void TestScheduler::beginReactionTiming(SampleRun *run)
{
    if (!run || run->reactionActive) {
        return;
    }
    run->reactionActive = true;
    startReaction(run);
}

void TestScheduler::finishReaction(int globalChannel)
{
    SampleRun *run = findRunByChannel(globalChannel);
    if (!run) {
        return;
    }
    run->reactionActive = false;
    run->discardPending = true;

    run->currentTest.status = tr("完成");

    const int module = globalToModule(globalChannel);
    const int local = globalToLocal(globalChannel);
    m_overview->setChannelProgress(module, local, 100);
    m_overview->setChannelStatus(module, local,
                                 tr("样本:%1[%2] 完成")
                                 .arg(run->sample.sampleNumber,
                                      run->currentTest.reagent));
    if (run->currentTest.prpPoints.size() < kMaxPrpPoints) {
        qWarning() << "反应点数不足:" << run->sample.sampleNumber
                   << run->currentTest.reagent
                   << run->currentTest.prpPoints.size();
    }
    emit reagentTestFinished(run->sample.sampleNumber,
                             run->currentTest);
    pump();
}

TestScheduler::SampleRun *TestScheduler::findRunByChannel(int globalChannel)
{
    for (SampleRun &run : m_active) {
        if (run.globalChannel == globalChannel) {
            return &run;
        }
    }
    return nullptr;
}

TestScheduler::SampleRun *TestScheduler::findRunBySample(
        const QString &sampleNumber)
{
    for (SampleRun &run : m_active) {
        if (run.sample.sampleNumber == sampleNumber) {
            return &run;
        }
    }
    return nullptr;
}

int TestScheduler::allocateChannel() const
{
    const int count = activeChannelCount();
    QVector<int> enabled;
    enabled.reserve(count);
    for (int channel = 1; channel <= count; ++channel) {
        const int module = globalToModule(channel);
        const int local = globalToLocal(channel);
        if (m_overview->isChannelEnabled(module, local)) {
            enabled.append(channel);
        }
    }
    if (enabled.isEmpty()) {
        return -1;
    }

    int maxTesting = 0;
    for (int channel : m_channelsInUse) {
        maxTesting = qMax(maxTesting, channel);
    }

    if (maxTesting > 0) {
        // 先往大号找，保证最后的通道不会被饿死。
        for (int channel : qAsConst(enabled)) {
            if (channel > maxTesting && channelIdle(channel)) {
                return channel;
            }
        }
    }
    // 后面没有空闲了才回到最小号。
    for (int channel : qAsConst(enabled)) {
        if (channelIdle(channel)) {
            return channel;
        }
    }
    return -1;
}

bool TestScheduler::channelIdle(int globalChannel) const
{
    if (m_channelsInUse.contains(globalChannel)) {
        return false;
    }
    const int module = globalToModule(globalChannel);
    const int local = globalToLocal(globalChannel);
    return m_overview->isChannelEnabled(module, local)
            && m_overview->channelRunState(module, local)
            == ChannelRunState::Idle;
}

int TestScheduler::activeChannelCount() const
{
    return m_overview ? m_overview->activeModuleCount() * 4 : 0;
}

int TestScheduler::globalToModule(int globalChannel)
{
    return (globalChannel - 1) / 4 + 1;
}

int TestScheduler::globalToLocal(int globalChannel)
{
    return (globalChannel - 1) % 4 + 1;
}

bool TestScheduler::allocateCups(SampleRun *run, QString *error)
{
    if (!run || !m_trayModel) {
        if (error) {
            *error = tr("试管盘不可用");
        }
        return false;
    }
    const int need = 1 + run->sample.testItems.size();
    if (need <= 1) {
        if (error) {
            *error = tr("样本没有测试项目");
        }
        return false;
    }

    QVector<int> free;
    free.reserve(need);
    for (int tray = 0; tray < m_trayModel->activeTrayCount(); ++tray) {
        for (int row = 0; row < SampleTrayModel::RowsPerTray; ++row) {
            for (int column = 0; column < SampleTrayModel::ColumnsPerTray;
                 ++column) {
                const int tube = SampleTrayModel::tubeIndex(tray, row, column);
                if (m_trayModel->tubeState(tube)
                        == SampleTrayModel::TubeState::Free) {
                    free.append(tube);
                    if (free.size() >= need) {
                        break;
                    }
                }
            }
            if (free.size() >= need) {
                break;
            }
        }
        if (free.size() >= need) {
            break;
        }
    }

    if (free.size() < need) {
        if (error) {
            *error = tr("需要 %1 个空杯，当前只有 %2 个")
                    .arg(need).arg(free.size());
        }
        return false;
    }

    QVector<int> allocated;
    allocated.reserve(need);
    for (int i = 0; i < need; ++i) {
        QString allocateError;
        if (!m_trayModel->allocateTube(free.at(i),
                                       run->sample.sampleNumber,
                                       &allocateError)) {
            for (int tube : qAsConst(allocated)) {
                m_trayModel->clearTube(tube);
            }
            if (error) {
                *error = allocateError;
            }
            return false;
        }
        allocated.append(free.at(i));
    }

    run->pppTube = allocated.first();
    run->prpTubes = allocated.mid(1);
    return true;
}

void TestScheduler::releaseCups(SampleRun *run)
{
    if (!run || !m_trayModel) {
        return;
    }
    if (run->pppTube >= 0
            && m_trayModel->tubeState(run->pppTube)
            == SampleTrayModel::TubeState::Allocated) {
        m_trayModel->clearTube(run->pppTube);
    }
    for (int tube : qAsConst(run->prpTubes)) {
        if (m_trayModel->tubeState(tube)
                == SampleTrayModel::TubeState::Allocated) {
            m_trayModel->clearTube(tube);
        }
    }
}

void TestScheduler::updateChannelUiForPpp(SampleRun *run)
{
    if (!run) {
        return;
    }
    const int module = globalToModule(run->globalChannel);
    const int local = globalToLocal(run->globalChannel);
    // PPP 只读一个基准值：清掉上一轮曲线，不计时也不画曲线。
    m_overview->clearChannelCurve(module, local);
    m_overview->setChannelTestInfo(module, local,
                                   run->sample.sampleNumber,
                                   QStringLiteral("PPP"), 0, -1);
    m_overview->setChannelStatus(module, local,
                                 tr("样本:%1[PPP]")
                                 .arg(run->sample.sampleNumber));
    m_overview->setChannelRunState(module, local, ChannelRunState::Testing);
    m_overview->setChannelProgress(module, local, 0);
}

void TestScheduler::updateChannelUiForReagent(SampleRun *run)
{
    if (!run) {
        return;
    }
    const int module = globalToModule(run->globalChannel);
    const int local = globalToLocal(run->globalChannel);
    m_overview->clearChannelCurve(module, local);
    m_overview->setChannelTestInfo(module, local,
                                   run->sample.sampleNumber,
                                   run->currentTest.reagent, 0, -1);
    m_overview->setChannelStatus(module, local,
                                 tr("样本:%1[%2]")
                                 .arg(run->sample.sampleNumber,
                                      run->currentTest.reagent));
    m_overview->setChannelRunState(module, local, ChannelRunState::Testing);
    // 只清掉上一个试剂的进度，不在这里启动倒计时：
    // 本轮 300s 进度统一由 startReaction() 在试剂真正吐入反应杯后
    // 从 0 开始累计，否则每次测试剂前会先“开启”一次再归零。
    m_overview->setChannelProgress(module, local, 0);
}

void TestScheduler::setChannelIdleUi(int globalChannel)
{
    if (!m_overview || globalChannel <= 0) {
        return;
    }
    const int module = globalToModule(globalChannel);
    const int local = globalToLocal(globalChannel);
    m_overview->stopMotor(module, local);
    m_overview->setTubeInserted(module, local, false);
    m_overview->setChannelStatus(module, local, tr("空闲"));
    m_overview->setChannelRunState(module, local, ChannelRunState::Idle);
}

void TestScheduler::clearAction()
{
    m_action = ActionType::None;
    m_actionSample.clear();
    m_actionChannel = -1;
    m_actionReagentIndex = -1;
    m_actionInProgress = false;
}

void TestScheduler::stopAllTimers()
{
    // 反应计时按吐试剂后的 300 个采样点计数，不使用独立反应定时器。
}

void TestScheduler::cancelCurrentAction(const QString &reason)
{
    if (m_hasAddRun) {
        releaseCups(&m_addRun);
        m_hasAddRun = false;
        m_addRun = SampleRun();
    }
    m_actionInProgress = false;
    m_running = false;
    m_paused = false;
    m_finalParkDone = false;
    stopAllTimers();
    clearAction();
    if (m_overview) {
        m_overview->setTestingActive(false);
    }
    emit errorOccurred(reason);
    emit statusChanged(tr("测试已中止：%1").arg(reason));
}
