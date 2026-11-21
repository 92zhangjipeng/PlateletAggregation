#ifndef COMMUNICATIONWORKER_H
#define COMMUNICATIONWORKER_H

#include "MachineModel.h"
#include "ModuleAdPoller.h"
#include "mainboardstatus.h"

#include <QByteArray>
#include <QObject>
#include <QString>

class MainBoardPoller;
class QTimer;
class SerialPortManager;

/**
 * 仪器通讯线程里的工作对象：
 * - 拥有 SerialPortManager（QSerialPort）、ModuleAdPoller 和 MainBoardPoller；
 * - 自动按 VID/PID 连接仪器，2 秒重试；
 * - 每 100ms 轮询模组 AD，每秒输出一次滤波值；
 * - 串口连接后每秒读取一次主板状态；
 * - 所有界面交互通过 queued 信号槽完成，后续主板/运动控制的轮询
 *   也放到这个对象/线程里，避免占用 GUI 线程。
 *
 * 该对象必须在 QThread 中创建并 moveToThread，startCommunication()
 * 在 QThread::started 信号里调用。
 */
class CommunicationWorker : public QObject
{
    Q_OBJECT

public:
    explicit CommunicationWorker(QObject *parent = nullptr);
    ~CommunicationWorker() override;

public slots:
    void startCommunication();
    void stopCommunication();

    /** 界面请求发送一帧（queued 调用） */
    void sendFrameRequested(const QByteArray &frame, const QString &description);

    /** 机型协议接入后调用 */
    void setMachineModel(MachineModel model);

    /** 界面打开时请求最近一次滤波结果和活动模组数 */
    void requestLatestModuleData();

    /** 界面打开时主动上报一次当前串口连接状态 */
    void requestConnectionState();

signals:
    void connectedChanged(bool connected, const QString &portName, qint32 baudRate);
    /** 非周期轮询帧（用于模组页日志/手动读回） */
    void frameReceived(const QByteArray &frame);
    void errorOccurred(const QString &message, bool connected);
    void sendResult(const QString &description, bool ok, const QString &error);

    void moduleAdUpdated(const ModuleAdPoller::ModuleAdData &data);
    void moduleCountChanged(int count);
    /** 每秒主板状态报告解析结果 */
    void mainBoardStatusUpdated(const MainBoardStatus &status);
    /** 主板逐个试剂位读取的耗材剩余总量（试管单位为板，其余为瓶） */
    void mainBoardConsumableUpdated(int position, int bottles, int ratio);

private slots:
    void attemptConnect();

private:
    SerialPortManager *m_serial = nullptr;
    ModuleAdPoller *m_adPoller = nullptr;
    MainBoardPoller *m_boardPoller = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    bool m_started = false;
};

#endif // COMMUNICATIONWORKER_H
