#ifndef MODULEOVERVIEWWIDGET_H
#define MODULEOVERVIEWWIDGET_H

#include "MachineModel.h"
#include "channelrunstate.h"

#include <QPointer>
#include <QString>
#include <QWidget>

class ModuleRackWidget;
class QHBoxLayout;
class QLabel;
class QResizeEvent;
class QTimer;
class TestCurveDialog;

/**
 * 主界面的检测模组总览：按机型横向显示 1~3 个 ModuleRackWidget。
 *
 * 默认机型 KS1200 显示 3 个模组；MainWindow::setMachineModel() 或
 * setActiveModuleCount() 可以在读取到真实机型后调整显示数量。
 *
 * 测试流程接口使用 1 为起始的编号：
 * moduleNumber = 1~3，localChannel = 1~4，globalChannel = (module-1)*4+channel。
 */
class ModuleOverviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ModuleOverviewWidget(QWidget *parent = nullptr);

    void setMachineModel(MachineModel model);
    MachineModel machineModel() const;
    int activeModuleCount() const;

    /** moduleNumber 为 1~3，找不到返回 nullptr */
    ModuleRackWidget *moduleRack(int moduleNumber) const;
    bool isChannelEnabled(int moduleNumber, int localChannel) const;
    /** 通道运行状态：空闲 / 测试中 / 异常 / 禁用 */
    ChannelRunState channelRunState(int moduleNumber, int localChannel) const;

    /** 全局“开始测试”状态：true 表示有通道正在测试 */
    void setTestingActive(bool active);
    bool testingActive() const;

    void setTubeInserted(int moduleNumber, int localChannel,
                         bool inserted, bool animated = true);
    void setChannelProgress(int moduleNumber, int localChannel, int percent);
    void setChannelStatus(int moduleNumber, int localChannel,
                          const QString &status);
    void setChannelEnabled(int moduleNumber, int localChannel, bool enabled);
    /** 设置通道运行状态；Disabled 同时会禁用该通道 */
    void setChannelRunState(int moduleNumber, int localChannel,
                            ChannelRunState state);
    /** 模组顶部居中的温度显示；celsius < 0 显示“温度 --” */
    void setModuleTemperature(int moduleNumber, double celsius);
    void startMotor(int moduleNumber, int localChannel);
    void stopMotor(int moduleNumber, int localChannel);
    void setMotorRunning(int moduleNumber, int localChannel, bool running);

    /** 曲线窗口顶部的样本/试剂/PRP/AD 信息 */
    void setChannelTestInfo(int moduleNumber, int localChannel,
                            const QString &sampleNumber,
                            const QString &reagentName,
                            int elapsedSeconds,
                            int prpValue);
    /** 每秒 AD 值；测试状态下会同时追加到该通道的测试曲线 */
    void setChannelAdValue(int moduleNumber, int localChannel, int adValue);
    /** 显式追加测试曲线/集聚曲线数据（x 轴单位为秒） */
    void appendChannelTestPoint(int moduleNumber, int localChannel,
                                double seconds, double value);
    void appendChannelAggregationPoint(int moduleNumber, int localChannel,
                                       double seconds, double value);
    void clearChannelCurve(int moduleNumber, int localChannel);

    /** 临时演示：模拟模组1 CH1 正在测试（PRP、AD 每秒刷新、集聚曲线、马达旋转） */
    void startChannelOneSimulation();
    void stopChannelOneSimulation();
    void setChannelOneSimulationPaused(bool paused);
    bool isChannelOneSimulationRunning() const;
    bool isChannelOneSimulationPaused() const;

    void resetAll();

signals:
    /** 通道1模拟测试自然结束，或模拟被停止/复位 */
    void channelOneSimulationFinished();

public slots:
    /** 按数量显示前 count 个模组（1~3） */
    void setActiveModuleCount(int count);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void updateHeaderText();
    void updateLayoutSizes();
    void updateGlobalTestState(bool active);
    void openCurveDialog(int moduleNumber, int localChannel);
    void advanceChannelOneSimulation();

    ModuleRackWidget *m_modules[3] = {};
    QLabel *m_titleLabel = nullptr;
    QLabel *m_subtitleLabel = nullptr;
    QLabel *m_testStateLabel = nullptr;
    QLabel *m_legendLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QWidget *m_rowHost = nullptr;
    QHBoxLayout *m_rowLayout = nullptr;

    QPointer<TestCurveDialog> m_curveDialog;

    QTimer *m_channelOneDemoTimer = nullptr;
    int m_channelOneDemoSecond = 0;
    bool m_channelOneDemoRunning = false;
    bool m_channelOneDemoPaused = false;

    MachineModel m_model = defaultMachineModel();
    int m_activeCount = 3;
};

#endif // MODULEOVERVIEWWIDGET_H
