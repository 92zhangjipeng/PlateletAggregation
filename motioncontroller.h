#ifndef MOTIONCONTROLLER_H
#define MOTIONCONTROLLER_H

#include "motionprotocol.h"

#include <QObject>
#include <QString>

class CommunicationWorker;

namespace platelet {

/**
 * 单动作运动控制接口。
 *
 * 每个方法只生成并排队发送一帧运动命令，不等待执行完成；
 * 适合“XY 移动到某处”“抓手下降到某高度”“样本针复位”这种单动作。
 * 组合动作（先 XY、再下针、再泵等）后续在流程层组合这些方法即可。
 *
 * 发送路径：
 *   MotionController → CommunicationWorker::sendFrameRequested（queued）
 *   → 通讯线程 SerialPortManager → 仪器串口。
 *
 * 信号：
 *   commandQueued   命令已排入通讯线程
 *   commandSent     串口实际发送结果（成功/失败原因）
 *   commandRejected 未排队（构帧失败或通讯线程未启动）
 *   reportReceived  收到 Z 轴板/主控板的功能码 0x17 报告帧
 *
 * 线程安全：本对象在 GUI 线程使用；串口发送在通讯线程执行。
 */
class MotionController : public QObject
{
    Q_OBJECT

public:
    explicit MotionController(QObject *parent = nullptr);

    /** 绑定通讯线程对象；需在 CommunicationWorker 创建后调用 */
    void setCommunicationWorker(CommunicationWorker *worker);
    CommunicationWorker *communicationWorker() const;

    MotionProtocol &protocol();
    const MotionProtocol &protocol() const;

    /** 最近一次未排队的错误原因 */
    QString lastError() const;

    bool moveXy(int x, int y,
                MotionProtocol::XyReference reference =
                    MotionProtocol::XyReference::BloodNeedle,
                MotionProtocol::Area area = MotionProtocol::Area::Test);

    bool moveGripperTo(int height,
                       MotionProtocol::Area area = MotionProtocol::Area::Test,
                       bool grabCup = false, bool liquidLevel = false);
    bool moveReagentNeedleTo(int height,
                             MotionProtocol::Area area =
                                 MotionProtocol::Area::Test,
                             bool liquidLevel = false);
    bool moveSampleNeedleTo(int height,
                            MotionProtocol::Area area =
                                MotionProtocol::Area::Test,
                            bool liquidLevel = false);

    bool resetXy(int speed = MotionProtocol::DefaultXyResetSpeed);
    bool resetGripper(int speed = MotionProtocol::DefaultZResetSpeed,
                      MotionProtocol::Area area = MotionProtocol::Area::Test);
    bool resetReagentNeedle(int speed = MotionProtocol::DefaultZResetSpeed,
                            MotionProtocol::Area area =
                                MotionProtocol::Area::Test);
    bool resetSampleNeedle(int speed = MotionProtocol::DefaultZResetSpeed,
                           MotionProtocol::Area area =
                               MotionProtocol::Area::Test);

    bool moveBloodPump(int units, bool aspirate, bool clean = false);
    bool moveReagentPump(int units, bool aspirate, bool clean = false);
    bool resetBloodPump(int speed = MotionProtocol::DefaultPumpResetSpeed,
                        bool clean = false);
    bool resetReagentPump(int speed = MotionProtocol::DefaultPumpResetSpeed,
                          bool clean = false);

    /** 直接发送自定义 15 字节帧（组合动作/调试用） */
    bool sendCommand(const QByteArray &frame, const QString &description);

signals:
    void commandQueued(const QString &description);
    void commandSent(const QString &description, bool ok,
                     const QString &error);
    void commandRejected(const QString &description, const QString &error);
    void reportReceived(const MotionProtocol::Frame &frame);

private:
    bool queueFrame(const QByteArray &frame, const QString &description);
    void reject(const QString &description, const QString &error);
    static QString descriptionPrefix();

    CommunicationWorker *m_worker = nullptr;
    MotionProtocol m_protocol;
    QString m_lastError;
};

} // namespace platelet

#endif // MOTIONCONTROLLER_H
