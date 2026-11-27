#ifndef TESTSCHEDULER_H
#define TESTSCHEDULER_H

#include "testdatastore.h"
#include "testsamplequeue.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

class AddSampleWidget;
class ModuleOverviewWidget;
class QTimer;
class SampleTrayModel;

/**
 * 同通道测试调度器（骨架）。
 *
 * 资源模型：
 *  - 加样：样本针串行；一个样本加样完成后立刻洗样本针，洗完才允许下一个加样；
 *  - 试管盘：一个样本提前占 1 个 PPP 杯 + RN 个 PRP 杯（RN=测试项目数）；
 *  - 通道：同一通道模式下一个样本的 PPP + 所有试剂固定在一个通道；
 *  - 机械动作：通过 xxxRequested 信号交给动作层执行，完成后动作层调用
 *    handleActionFinished()；同一时间只允许一个机械动作；
 *  - 通道反应：300s 独立计时，不占机械臂；反应期间机械臂可继续加样/移杯。
 *
 * 当前只实现调度和资源分配，具体加样/移杯/吸试剂/洗针动作由动作层接入：
 * 动作层需要连接各 xxxRequested 信号，并在完成后回调 handleActionFinished()；
 * 读到的 PPP/PRP0 分别通过 setPppValue()/setPrp0Value() 回填。
 *
 * 职责边界：试管盘状态（Allocated/Sampled/Used）由调度器维护，动作层只负责
 * 机械动作；动作层不要重复调用 markSampled()/markUsed()。
 */
class TestScheduler : public QObject
{
    Q_OBJECT

public:
    enum class ActionType {
        None,
        AddSample,
        WashSampleNeedle,
        MovePppCup,
        SetupReagent,
        WashReagentNeedle,
        DiscardCup,
        Park
    };

    explicit TestScheduler(AddSampleWidget *addSampleWidget,
                           SampleTrayModel *trayModel,
                           ModuleOverviewWidget *overview,
                           QObject *parent = nullptr);
    ~TestScheduler() override;

    /** 动作层是否已接入；未接入时 start() 只提示，不进入调度 */
    void setActionLayerReady(bool ready);
    bool isActionLayerReady() const;

    bool isRunning() const;
    bool isPaused() const;

    /**
     * 开始吸某个试剂前，由主窗口按余量和“启用预备试剂位”设置
     * 选择本次使用的试剂坐标位（0~4 左、5~9 右）。
     * 返回 <0 表示当前不能开始（余量到报警限等），reason 为提示文本。
     */
    using ReagentPositionSelector =
            std::function<int(const QString &reagent, QString *reason)>;
    void setReagentPositionSelector(ReagentPositionSelector selector);

public slots:
    void start();
    void pause();
    void resume();
    void reset();

    /** 动作层完成一个机械动作后调用 */
    void handleActionFinished(bool ok, const QString &message);
    /**
     * 动作序列在构建阶段失败（坐标未设置等），机械臂未动：
     * 只放弃当前样本/当前试剂，其他通道已开始的反应继续采样。
     * blocksTest=true 时（泵位置未知等全局条件）暂停下发新动作，
     * 但保留 m_running，让已在反应的通道继续计时。
     */
    void handleActionRejected(const QString &message, bool blocksTest);
    /**
     * 操作员在“抓杯失败”提示框里选择放弃：
     * - PPP 杯抓取失败：放弃整个样本；
     * - PRP 杯抓取失败：跳过当前试剂，继续下一个试剂/下一个样本。
     */
    void handleActionAbandoned(const QString &message);

    /** 主窗口每秒 AD 更新时转发；反应中的通道会记录 PRP1~PRP300 */
    void handleChannelAd(int moduleNumber, int localChannel, int adValue);

    /**
     * 测试进行中新样本确认入队后调用：立即唤醒调度器处理队列，
     * 不需要再次点击「开始」。
     */
    void notifyWorkAvailable();

    /** 动作层读到 PPP/PRP0 后回填；PRP0 只作集聚计算基准，不启动计时 */
    void setPppValue(int globalChannel, int value);
    void setPrp0Value(int globalChannel, int value);

    /** 试杯实际放入/拔出通道时刷新主页试管绘制 */
    void handleCupPlaced(int globalChannel, bool ppp);
    void handleCupRemoved(int globalChannel);
    /** 动作层在试剂吐入反应杯时立即通知，300s 反应从此刻开始计时 */
    void handleReagentDispensed(int globalChannel);

signals:
    void statusChanged(const QString &status);
    void errorOccurred(const QString &message);
    void finished();

    /** 单个试剂被放弃/异常：结果详情需把它标记为独立状态 */
    void reagentAbandoned(const QString &sampleNumber,
                          const QString &reagent);

    /** 测试流程洗针成功消耗的内部清洗液 S1（µL），主窗口据此扣减并写回主板 */
    void internalCleanUsed(double ul);

    /**
     * 试剂余量到报警限且无法继续（未启用预备位或左右都到限）：
     * 调度器已自动暂停，主窗口负责提示更换试剂。
     */
    void reagentAlarm(const QString &reagent, const QString &message);

    /** 某个试剂开始测试（吐试剂前）：结果详情可先落一条“测试中”占位行 */
    void reagentTestStarted(const QString &sampleNumber,
                            const QString &reagent);

    /** 机械动作请求：动作层实现后连接这些信号 */
    void addSampleRequested(const QueuedSample &sample, int pppTube,
                            const QVector<int> &prpTubes);
    void washSampleNeedleRequested();
    void movePppCupRequested(int globalChannel, int pppTube);
    void setupReagentRequested(int globalChannel, int prpTube,
                               const QString &reagent, int coordinateIndex);
    void washReagentNeedleRequested();
    void discardCupRequested(int globalChannel, const QString &reagent);
    /** 复位时要求动作层取消当前机械动作 */
    void cancelActionRequested();
    /** 测试结束、机械臂无后续任务：移动到清洗原点并做一次 XY 负速度复位 */
    void parkAtOriginRequested();

    /** PPP 基线读完：PPP 是样本级基准值，与 PRP0 一样单独保存，不算试剂 */
    void pppBaselineFinished(const QString &sampleNumber, int pppValue);
    /** 一个试剂 300s 反应结束：动作层/主窗口据此立即落库 */
    void reagentTestFinished(const QString &sampleNumber,
                             const TestCurveData &test);
    /** PPP 基准值 < 1000：样本应标记为“异常结果” */
    void sampleMarkedAbnormal(const QString &sampleNumber);

private:
    struct SampleRun {
        QueuedSample sample;
        int pppTube = -1;
        QVector<int> prpTubes;
        int globalChannel = -1;
        int nextReagentIndex = 0;
        int pppValue = -1;
        bool cupsAllocated = false;
        bool pppDone = false;
        bool reagentPending = false;
        bool reactionActive = false;
        bool discardPending = false;
        bool abandoned = false;      // 有杯/试剂被操作员放弃，最终结果记异常
        qint64 lastAdSampleMs = 0;
        TestCurveData currentTest;
    };

    bool pump();
    bool startAddSample();
    bool startWashSampleNeedle();
    bool startMovePppCup();
    bool startSetupReagent();
    bool startWashReagentNeedle();
    bool startDiscardCup();

    void finishAddSample();
    void finishMovePppCup();
    void finishDiscardCup();
    void beginReagentReaction(SampleRun *run);
    /** PPP 杯抓取失败放弃：作废样本、释放通道、结果记异常 */
    void abandonPppSample(const QString &reason);
    /** PRP 杯抓取失败放弃：跳过当前试剂；没有后续试剂则完成样本 */
    void abandonCurrentReagent(const QString &reason);
    /** 加样动作预检失败：释放已分配试管、标异常并跳过该样本 */
    void abandonAddSample(const QString &reason);
    /**
     * 丢杯动作预检失败：试杯仍留在通道里，不能释放通道给下一个样本；
     * 只结束该样本并保持通道占用，等待人工处理。
     */
    void abandonStuckChannel(const QString &reason);
    /** 放弃样本时处理已分配/已加样的托盘杯 */
    void discardTrayTube(int tube);

    void assignChannel(SampleRun *run, int globalChannel);
    void releaseChannel(int globalChannel);
    void completeSample(SampleRun *run);
    void startReaction(SampleRun *run);
    /** 试剂吐入反应杯完成时启动 300s 计时和 PRP 曲线采集 */
    void beginReactionTiming(SampleRun *run);
    void finishReaction(int globalChannel);

    SampleRun *findRunByChannel(int globalChannel);
    SampleRun *findRunBySample(const QString &sampleNumber);

    int allocateChannel() const;
    bool channelIdle(int globalChannel) const;
    int activeChannelCount() const;
    static int globalToModule(int globalChannel);
    static int globalToLocal(int globalChannel);

    bool allocateCups(SampleRun *run, QString *error);
    void releaseCups(SampleRun *run);

    void updateChannelUiForPpp(SampleRun *run);
    void updateChannelUiForReagent(SampleRun *run);
    void setChannelIdleUi(int globalChannel);
    void clearAction();
    void stopAllTimers();
    void cancelCurrentAction(const QString &reason);

    AddSampleWidget *m_addSampleWidget = nullptr;
    SampleTrayModel *m_trayModel = nullptr;
    ModuleOverviewWidget *m_overview = nullptr;

    bool m_actionLayerReady = false;
    bool m_running = false;
    bool m_paused = false;
    bool m_pumping = false;
    bool m_actionInProgress = false;
    bool m_finalParkDone = false;
    bool m_sampleNeedleDirty = false;
    bool m_reagentNeedleDirty = false;

    ReagentPositionSelector m_reagentPositionSelector;

    ActionType m_action = ActionType::None;
    QString m_actionSample;
    int m_actionChannel = -1;
    int m_actionReagentIndex = -1;

    bool m_hasAddRun = false;
    SampleRun m_addRun;
    QVector<SampleRun> m_waitingChannel;
    QVector<SampleRun> m_active;
    QSet<int> m_channelsInUse;
    QHash<int, QTimer *> m_reactionTimers;   // 已废弃：反应按点数计数
};

#endif // TESTSCHEDULER_H
