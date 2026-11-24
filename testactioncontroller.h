#ifndef TESTACTIONCONTROLLER_H
#define TESTACTIONCONTROLLER_H

#include "instrumentcoordinates.h"
#include "motionprotocol.h"
#include "testsamplequeue.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

class ModuleOverviewWidget;
class QTimer;

/**
 * TestScheduler 的机械动作层。
 *
 * 调度器只发“加样 / 洗样本针 / PPP 杯 / 吸试剂+PRP 杯 / 洗试剂针 / 丢杯”
 * 六种请求；本类把它们展开成串行运动步骤，收到完成报告后再发下一条。
 * 所有 XY 移动前都先复位三根 Z；抓手带杯移动时用带 grabCup 的抓手复位，
 * 保持空气阀开启。
 */
class TestActionController : public QObject
{
    Q_OBJECT

public:
    explicit TestActionController(InstrumentCoordinates *coordinates,
                                  ModuleOverviewWidget *overview,
                                  QObject *parent = nullptr);

    void setProtocol(platelet::MotionProtocol *protocol);
    bool isBusy() const;

public slots:
    void addSample(const QueuedSample &sample, int pppTube,
                   const QVector<int> &prpTubes);
    void washSampleNeedle();
    void movePppCup(int globalChannel, int pppTube);
    void setupReagent(int globalChannel, int prpTube,
                      const QString &reagent);
    void washReagentNeedle();
    void discardCup(int globalChannel, const QString &reagent);
    /** 测试结束收尾：移动到清洗原点，再做一次 XY 负速度复位 */
    void parkAtOrigin();
    void handleReport(const platelet::MotionProtocol::Frame &frame);
    void cancel();

    /** 抓杯失败后操作员选择“重试”：从当前 XY 重新抓取 */
    void retryPausedGrab();
    /** 抓杯失败后操作员选择“放弃”：结束当前动作并通知调度器跳过 */
    void abandonPausedGrab();

signals:
    void sendRequested(const QByteArray &frame, const QString &description);
    void actionFinished(bool ok, const QString &message);
    /** 抓杯失败、抓手已复位，等待操作员选择重试/放弃 */
    void grabFailurePaused(const QString &message);
    /** 操作员选择放弃：调度器据此跳过当前样本/当前试剂 */
    void actionAbandoned(const QString &message);
    void pppValueRead(int globalChannel, int value);
    void prp0ValueRead(int globalChannel, int value);
    /** 试剂已吐入反应杯：调度器从此刻开始 300s 计时 */
    void reagentDispensed(int globalChannel);
    void statusChanged(const QString &status);
    /**
     * 0x17 动作命令交换开始/结束。true 时通讯层暂停 0x16 周期读命令，
     * 避免读命令冲掉动作完成应答（测试流程中轮询是开着的）。
     */
    void motionExchangeActiveChanged(bool active);
    /** 试杯实际放入/拔出通道时通知调度器刷新主页试管绘制 */
    void cupPlacedInChannel(int globalChannel, bool ppp);
    void cupRemovedFromChannel(int globalChannel);

private:
    enum class PumpUpdate {
        None,
        SamplePump,
        ReagentPump
    };
    enum class StepKind {
        Motion,
        Delay,
        Custom
    };

    struct Step {
        StepKind kind = StepKind::Custom;
        QByteArray frame;
        QString description;
        quint8 slaveAddress = 0;
        platelet::MotionProtocol::Axis axis =
                platelet::MotionProtocol::Axis::Xy;
        int timeoutMs = 30000;
        int delayMs = 0;
        std::function<void()> custom;
        std::function<void(const platelet::MotionProtocol::Frame &)>
                onMotionComplete;
        PumpUpdate pumpUpdate = PumpUpdate::None;
        int pumpPosition = -1;
    };

    bool beginAction(const QString &name);
    void finishAction(bool ok, const QString &message);
    void appendStep(const Step &step);
    void appendMotion(
            const QByteArray &frame, const QString &description,
            quint8 slaveAddress, platelet::MotionProtocol::Axis axis,
            int timeoutMs, PumpUpdate pumpUpdate = PumpUpdate::None,
            int pumpPosition = -1,
            const std::function<void(
                const platelet::MotionProtocol::Frame &)> &onComplete = {});
    void appendDelay(int ms, const QString &description);
    void appendCustom(const std::function<void()> &custom,
                      const QString &description);
    void insertNextStep(const Step &step);
    void runNextStep();
    void setMotionExchangeActive(bool active);
    bool isReportFor(const platelet::MotionProtocol::Frame &frame,
                     const Step &step) const;

    void appendResetSampleReagentZ(const QString &prefix);
    void appendResetAllZ(const QString &prefix);
    void appendGripperLiftHoldingCup(
            const QString &prefix,
            const std::function<void()> &retryBuilder = {});
    void appendXyHome(const QString &prefix);

    bool buildAddSample(const QueuedSample &sample, int pppTube,
                        const QVector<int> &prpTubes);
    bool buildWashSampleNeedle();
    bool buildWashReagentNeedle();
    bool buildMovePppCup(int globalChannel, int pppTube);
    bool buildSetupReagent(int globalChannel, int prpTube,
                           const QString &reagent);
    bool buildDiscardCup(int globalChannel);

    int globalToModule(int globalChannel) const;
    int globalToLocal(int globalChannel) const;
    int reagentUiIndex(const QString &reagent) const;
    int reagentCoordinateIndex(const QString &reagent) const;
    static int sampleHoleIndex(const QString &position);
    bool pumpTarget(int current, int steps, int *target,
                    QString *error) const;
    void appendGrabFromTube(int tubeIndex, const QString &prefix);
    void appendPlaceInChannel(int globalChannel, const QString &prefix);
    void appendGrabFromChannel(int globalChannel, const QString &prefix);
    void appendDiscardHeldCup(const QString &prefix);
    /** 速度模式下压吸杯（行程上限=试管/通道位 z），失败自动重试一次 */
    void appendGripperSpeedGrab(int limitHeight, const QString &prefix);
    void appendGripperVacuumCheck(
            bool grab, const QString &description,
            const std::function<void()> &retryBuilder = {});
    /**
     * 可恢复抓杯：保存重试序列构建器，插入“抓手复位 → 暂停”两步，
     * 等操作员在提示框里选择重试或放弃。
     */
    void pauseForGrabFailure(const QString &message,
                             const std::function<void()> &retryBuilder);
    /** 结束动作并通知调度器“放弃”，不按普通失败中止整个测试 */
    void finishAbandoned(const QString &message);
    /** 生成“从当前 XY 再次开阀下压抓杯”的重试构建器 */
    std::function<void()> grabRetryBuilder(int z, const QString &prefix,
                                           bool includeLift);
    void appendGrabRetrySequence(int z, const QString &prefix,
                                 bool includeLift);
    void appendReadChannelAd(int globalChannel, bool ppp,
                             const QString &prefix);

    InstrumentCoordinates *m_coordinates = nullptr;
    ModuleOverviewWidget *m_overview = nullptr;
    platelet::MotionProtocol m_ownProtocol;
    platelet::MotionProtocol *m_protocol = nullptr;
    QTimer *m_timeoutTimer = nullptr;

    QVector<Step> m_steps;
    int m_currentStep = -1;
    bool m_busy = false;
    quint64 m_actionId = 0;
    QString m_actionName;
    QString m_error;

    int m_globalChannel = -1;
    int m_sampleStart = 0;
    int m_reagentStart = 0;
    bool m_gripperRetried = false;
    quint32 m_lastGripperVacuum = 0;
    quint32 m_idleGripperVacuum = 0;   // 抓杯前原位读的空载负压
    bool m_motionExchangeActive = false;
    bool m_grabFailurePaused = false;
    QString m_grabFailureMessage;
    std::function<void()> m_grabRetryAppend;
};

#endif // TESTACTIONCONTROLLER_H
