#ifndef MODULEADPOLLER_H
#define MODULEADPOLLER_H

#include "DetectionModule.h"
#include "MachineModel.h"

#include <QByteArray>
#include <QMetaType>
#include <QObject>
#include <QTimer>
#include <QVector>

class SerialPortManager;

/**
 * 每 100ms（每秒 10 次）轮询所有活动模组的温度和 4 路通道 AD 值，
 * 把 1 秒内收到的 10 个样本滤波后，每秒输出一个通道 AD 值。
 *
 * - 活动模组数由机型决定：KS600=1，KS800=2，KS1200=3；
 * - 每 100ms 发送一次 readTemperatureAndChannels()（每个活动模组一帧）；
 * - 每累计 10 次采样（约 1 秒）执行一次滤波输出：
 *   样本数 1~2 取均值，>=3 取中位数；无新样本则保持上次值；
 * - 只接受协议范围内 2000~3300 的 AD 样本；
 * - 周期轮询的应答可通过 isPeriodicReply() 识别，界面据此避免日志刷屏。
 */
class ModuleAdPoller : public QObject
{
    Q_OBJECT

public:
    struct ModuleAdData {
        int moduleNumber = 0;            // 1~3
        QVector<quint16> channels;       // 4 路滤波后的 AD 值
        bool channelsValid = false;      // 至少有一路曾经收到有效值
        double temperatureCelsius = 0.0;
        bool temperatureValid = false;
    };

    explicit ModuleAdPoller(SerialPortManager *serial, QObject *parent = nullptr);
    ~ModuleAdPoller() override;

    void setMachineModel(MachineModel model);
    MachineModel machineModel() const;
    int activeModuleCount() const;

    void start();
    void stop();
    bool isRunning() const;

    /** 是否为每秒轮询请求对应的应答帧（用于过滤界面日志） */
    bool isPeriodicReply(const QByteArray &frame) const;

    /** 最近一次每秒滤波输出；界面打开时可先取一次 */
    ModuleAdData latestModuleData(int moduleNumber) const;

    /** 采样周期：100ms（每秒 10 次） */
    static constexpr int kPollIntervalMs = 100;
    /** 累计 10 次采样输出一次滤波值（1 秒一个值） */
    static constexpr int kPollsPerOutput = 10;

signals:
    void moduleAdUpdated(const ModuleAdPoller::ModuleAdData &data);
    void moduleCountChanged(int count);
    void machineModelChanged(MachineModel model);

private slots:
    void onPollTick();
    void onFrameReceived(const QByteArray &frame);

private:
    struct ChannelFilter {
        QVector<quint16> samples;
        quint16 lastValue = 0;
        bool hasValue = false;

        void add(quint16 value);
        quint16 finalize();
        void reset();
    };

    struct ModuleState {
        ChannelFilter channels[4];
        double lastTemperature = 0.0;
        bool hasTemperature = false;
        quint8 lastPollCmdNum = 0;
    };

    void pollModule(int moduleIndex);
    void emitSecondValues();
    void resetState();

    SerialPortManager *m_serial = nullptr;
    QTimer m_timer;
    MachineModel m_model = MachineModel::KS1200;
    platelet::DetectionModule *m_readers[3] = {};
    ModuleState m_states[3];
    ModuleAdData m_latest[3];
    int m_tickCounter = 0;
    bool m_running = false;
};

Q_DECLARE_METATYPE(ModuleAdPoller::ModuleAdData)

#endif // MODULEADPOLLER_H
