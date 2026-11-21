#ifndef MODULECONTROLWIDGET_H
#define MODULECONTROLWIDGET_H

#include "DetectionModule.h"
#include "ModuleAdPoller.h"

#include <QWidget>

class CommunicationWorker;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPlainTextEdit;
class QSpinBox;
class QTabWidget;

/** 单个模组的控制页：温度、调光、LED 电流、4 路转速、读回 */
class ModulePanel : public QWidget
{
    Q_OBJECT

public:
    explicit ModulePanel(int moduleNumber, QWidget *parent = nullptr);

    quint8 slaveAddress() const;

public slots:
    /** 把收到的 15 字节应答帧交给本页显示 */
    void handleIncomingFrame(const QByteArray &frame);

public:
    /** 往本页日志追加一条系统提示（串口未连接、发送失败等） */
    void appendSystemMessage(const QString &text);
    /** 每秒滤波后的通道 AD 数据（模块号 1~3） */
    void applyAdData(const ModuleAdPoller::ModuleAdData &data);

signals:
    /** 需要发送的命令帧；接到串口 / 主控发送接口即可 */
    void frameReady(const QByteArray &frame, const QString &description);

private:
    void buildUi();
    QGroupBox *createTemperatureGroup();
    QGroupBox *createDimmingGroup();
    QGroupBox *createCurrentGroup();
    QGroupBox *createSpeedGroup();
    QGroupBox *createReadbackGroup();
    QGroupBox *createLogGroup();

    void sendCommand(const QByteArray &frame, const QString &description);
    void appendLog(const QString &text);
    int globalChannelNumber(int localChannel1Based) const;

    platelet::DetectionModule m_module;
    int m_moduleNumber = 1;

    QDoubleSpinBox *m_targetTempSpin = nullptr;
    QLabel *m_currentTempLabel = nullptr;
    QSpinBox *m_dimmingSpin = nullptr;
    QSpinBox *m_currentSpins[4] = {};
    QSpinBox *m_speedSpins[4] = {};
    QLabel *m_channelValueLabels[4] = {};
    QLabel *m_speedReadLabels[4] = {};
    QLabel *m_currentReadLabels[4] = {};
    QPlainTextEdit *m_log = nullptr;
};

/** 3 个模组的控制容器（Tab：模组1 / 模组2 / 模组3） */
class ModuleControlWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ModuleControlWidget(QWidget *parent = nullptr);

public slots:
    /** 收到任意模组的应答帧后调用，内部按从机地址分发到对应 Tab */
    void handleIncomingFrame(const QByteArray &frame);
    /** 串口连接状态变化，更新页首提示 */
    void setSerialConnected(bool connected, const QString &detail = QString());
    /** 在当前 Tab 的日志里追加系统提示 */
    void appendSystemMessage(const QString &text);
    /** 每秒滤波后的 AD 数据分发到对应模组页 */
    void onModuleAdUpdated(const ModuleAdPoller::ModuleAdData &data);
    /** 按机型启用 1~3 个模组 Tab */
    void setActiveModuleCount(int count);

public:
    /** 绑定通讯线程：接收每秒滤波值/活动模组数并显示 */
    void bindCommunicationWorker(CommunicationWorker *worker);

signals:
    void frameReady(const QByteArray &frame, const QString &description);

private:
    CommunicationWorker *m_commWorker = nullptr;
    QTabWidget *m_tabs = nullptr;
    ModulePanel *m_panels[3] = {};
    QLabel *m_hint = nullptr;
};

#endif // MODULECONTROLWIDGET_H
