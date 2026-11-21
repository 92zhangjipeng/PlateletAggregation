#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "DetectionModule.h"
#include "MachineModel.h"
#include "instrumentcoordinates.h"
#include "mainboardconsumables.h"
#include "mainboardstatus.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QString>

#include <functional>

class AddSampleWidget;
class CommunicationWorker;
class ConsumablesDialog;
class CoordinateVerificationController;
class CustomTitleBar;
class PerfusionController;
class MainBoardStatusIconBar;
class ModuleOverviewWidget;
class QCloseEvent;
class QEvent;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QueryWidget;
class ResultsInfoWidget;
class SampleTrayModel;
class SettingsDialog;
class TestDataStore;
class TubeTrayDialog;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QStackedWidget;

namespace platelet {
class MotionController;
}
class QTabWidget;
class QThread;
class QTimer;
class QToolButton;
class QWidget;
class ReagentBottlePanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message,
                     long *result) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

public slots:
    /** 机型协议接入后调用：KS600/KS800/KS1200 → 1/2/3 个模组 */
    void setMachineModel(MachineModel model);
    /** 全局“开始测试”状态：有通道测试时为 true，全部结束后为 false */
    void setTestingActive(bool active);
    /** 通讯线程解析出的主板状态 → 底部状态栏 */
    void updateMainboardStatus(const MainBoardStatus &status);
    /** 主板逐个试剂位读取的耗材余量 → 耗材管理界面 */
    void updateConsumableAmount(int position, int bottles, int ratio);

public:
    MachineModel machineModel() const;
    bool testingActive() const;
    /** 主界面模组可视化控件，后续测试流程可用来驱动试管/进度/马达 */
    ModuleOverviewWidget *moduleOverview() const;
    /** 模组下方的试剂余量面板，后续可接入真实余量 */
    ReagentBottlePanel *reagentPanel() const;
    /** 测试数据 SQLite 存储（样本 / PPP / 试剂 / 300 个 PRP 点） */
    TestDataStore *testDataStore() const;
    /** 样本试管盘状态模型（最多 4 盘 × 60 支） */
    SampleTrayModel *trayModel() const;
    /** 仪器坐标内存表（原点/清洗孔/通道/试管/试剂位/样本孔） */
    InstrumentCoordinates *instrumentCoordinates();
    /** 单动作运动控制接口（XY/抓手/试剂针/样本针/血样泵/试剂泵） */
    platelet::MotionController *motionController() const;

private slots:
    void handleLogin();
    void returnToLogin();
    void openSettings();
    /** 程序启动后创建通讯线程 + 30 秒超时计时 */
    void startInstrumentConnection();
    void handleStartupTimeout();
    void updateSerialStatus(bool connected, const QString &portName,
                            qint32 baudRate);
    void handleSerialError(const QString &message, bool connected);

private:
    enum class StartupCalibrationState {
        Idle,
        Heating,
        Perfusion,
        Dimming,
        ReadingLedCurrent,
        LedTuning,
        Saving,
        Done,
        Failed
    };

    QWidget *createLoginPage();
    QWidget *createMainPage();
    void setupCommunicationThread();
    void shutdownCommunicationThread();
    void setConnectionStatus(const QString &text, const char *state,
                             const QString &toolTip = QString());
    bool startupConnectionAbnormal() const;

    /** 联机升温 → 灌注清洗（超时）→ 调光 → LED 电流微调 → 保存参数 */
    void createStartupMask();
    void setMainPageInteractionEnabled(bool enabled);
    /** 登录且已连接时调用，开始“灌注 → 升温 → 调光”整套启动流程 */
    void maybeStartStartupCalibration();
    void startStartupCalibration();
    void abortStartupCalibration(const QString &reason = QString(),
                                 bool userVisible = true);
    void handleStartupTick();
    void beginStartupPerfusion();
    void beginStartupHeating();
    void beginStartupDimming();
    void checkStartupDimming();
    /** 调光后等待各通道 AD 稳定（慢慢爬升过程中不判断 ±100） */
    bool dimmingAdSettled();
    void promptResidualCups(const QStringList &failedChannels);
    /** 微调前先重新读取一次各通道 LED 电流 */
    void beginLedCurrentRead();
    void beginLedCurrentTuning();
    void processLedTuningData(int moduleNumber);
    void finishLedCurrentTuning();
    void handleStartupLedCurrentFrame(const QByteArray &frame);
    void setStartupMaskStatus(const QString &title, const QString &detail,
                              bool progressVisible = false,
                              int progress = 0);
    void startSimulatedProgress(int durationMs,
                                const std::function<void()> &onDone);
    void handleStartupProgressTick();
    void sendModuleFrame(int moduleNumber, const QByteArray &frame,
                         const QString &description);
    bool allModulesAtTargetTemperature() const;

    void handleStartTest();
    void handlePauseResume();
    void handleResetTest();
    void showHomeTab();
    void createConsumablesTab();
    void createTubeTrayTab();
    void createResultTabs();
    void createSettingsTab();
    void createEmptyPlaceholderTabs();
    QWidget *createPlaceholderPage(const QString &title,
                                   const QString &description);
    void refreshConsumablesTab();
    void openConsumables();
    void openTubeTrays();
    /** 更换试管盘：测试中禁止，二次确认后余量减 1 并写入主板 */
    void handleTrayReplaceRequested(int trayIndex);
    void openResults();
    void openQuery();
    void openQuality();
    void openAddSample();
    void createLogTab();
    void openLogs();
    void refreshLogView();
    void showPendingFunction(const QString &name);
    void showActionHint(const QString &text);
    void applyChannelSettings();
    void saveCompletedSimulationData();
    void updatePauseButtonAppearance();
    void applyMainboardStatus(const MainBoardStatus &status);
    void setMainboardStatusText(const QString &summary, const char *state,
                                const QString &toolTip = QString());
    /** 启动时从默认坐标文件恢复仪器坐标 */
    void loadInstrumentCoordinates();

    CommunicationWorker *m_commWorker = nullptr;
    QThread *m_commThread = nullptr;
    CustomTitleBar *m_titleBar = nullptr;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_loginPage = nullptr;
    QWidget *m_mainPage = nullptr;
    QTabWidget *m_mainTabs = nullptr;
    int m_homeTabIndex = -1;
    int m_consumablesTabIndex = -1;
    int m_tubeTrayTabIndex = -1;
    int m_resultsTabIndex = -1;
    int m_queryTabIndex = -1;
    int m_qualityTabIndex = -1;
    int m_addSampleTabIndex = -1;
    int m_settingsTabIndex = -1;
    int m_logTabIndex = -1;
    QPlainTextEdit *m_logView = nullptr;
    QLabel *m_logPathLabel = nullptr;
    QLineEdit *m_usernameEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QLabel *m_connectionStatus = nullptr;
    QLabel *m_mainboardStatus = nullptr;
    QLabel *m_actionHint = nullptr;
    QTimer *m_connectDeadlineTimer = nullptr;
    QTimer *m_actionHintTimer = nullptr;
    QToolButton *m_startButton = nullptr;
    QToolButton *m_pauseButton = nullptr;
    ModuleOverviewWidget *m_moduleOverview = nullptr;
    ReagentBottlePanel *m_reagentPanel = nullptr;
    MainBoardStatusIconBar *m_boardIndicators = nullptr;
    ConsumablesDialog *m_consumablesDialog = nullptr;
    SettingsDialog *m_settingsDialog = nullptr;
    ResultsInfoWidget *m_resultsInfoWidget = nullptr;
    QueryWidget *m_queryWidget = nullptr;
    TestDataStore *m_testDataStore = nullptr;
    platelet::MotionController *m_motionController = nullptr;
    CoordinateVerificationController *m_verificationController = nullptr;
    PerfusionController *m_perfusionController = nullptr;
    AddSampleWidget *m_addSampleWidget = nullptr;
    SampleTrayModel *m_trayModel = nullptr;
    TubeTrayDialog *m_tubeTrayDialog = nullptr;
    MainBoardStatus m_latestMainboardStatus;
    bool m_hasMainboardStatus = false;
    bool m_lastHeightDetectionTrigger = false;
    bool m_heightTriggerBaselineSet = false;
    int m_consumableBottles[MainBoardConsumable::kPositionCount] = {};
    int m_consumableRatios[MainBoardConsumable::kPositionCount] = {};
    bool m_consumableValid[MainBoardConsumable::kPositionCount] = {};
    bool m_channelOneDemoPaused = false;
    bool m_hasLoggedIn = false;

    MachineModel m_machineModel = defaultMachineModel();
    InstrumentCoordinates m_instrumentCoordinates;

    bool m_connectionStarted = false;
    bool m_serialConnected = false;       // 当前串口是否连接
    bool m_startupConnected = false;      // 30 秒内成功连上过
    bool m_startupTimedOut = false;       // 30 秒超时
    bool m_lateRestartPromptShown = false;

    // 联机启动校准：升温 → 灌注清洗（超时）→ 调光 → LED 电流微调 → 保存
    platelet::DetectionModule m_moduleProtocols[3];
    QWidget *m_startupMask = nullptr;
    QLabel *m_startupMaskTitle = nullptr;
    QLabel *m_startupMaskDetail = nullptr;
    QProgressBar *m_startupMaskProgress = nullptr;
    QTimer *m_startupCheckTimer = nullptr;
    QTimer *m_startupProgressTimer = nullptr;
    QElapsedTimer m_startupHeatingClock;
    QElapsedTimer m_startupProgressClock;
    std::function<void()> m_startupProgressDone;
    StartupCalibrationState m_startupState = StartupCalibrationState::Idle;
    bool m_startupCalibrationAttempted = false;
    int m_startupProgressDurationMs = 0;
    int m_dimmingCheckWaitCount = 0;
    int m_cupRetryCount = 0;
    QElapsedTimer m_dimmingSettleClock;
    int m_dimmingPreviousAd[3][4] = {};
    bool m_dimmingPreviousValid = false;
    int m_dimmingStableSamples = 0;
    int m_ledCurrent[3][4] = {};
    bool m_ledCurrentValid[3][4] = {};
    bool m_ledCurrentReadLocked = false;
    QElapsedTimer m_ledTuningSettleClock[3];
    bool m_ledTuningConverged[3] = {};
    int m_invalidAdCount[3][4] = {};
    double m_latestTemperature[3] = {};
    bool m_latestTemperatureValid[3] = {};
    int m_latestChannelAd[3][4] = {};
    bool m_latestModuleDataValid[3] = {};
};

#endif // MAINWINDOW_H
