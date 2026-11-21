#ifndef COORDINATEVERIFICATIONCONTROLLER_H
#define COORDINATEVERIFICATIONCONTROLLER_H

#include "instrumentcoordinates.h"
#include "motionprotocol.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

class QTimer;

/**
 * 坐标维护页「触发移动」的定位校验状态机。
 *
 * 安全门禁（必须严格遵守）：
 *   1. 同时下发三个 Z 轴（抓手 / 试剂针 / 样本针）复位命令；
 *   2. 只有三个复位都收到“执行完成”报告后，才允许发送 XY 移动；
 *   3. XY 收到“执行完成”后，才让所选工具下降到目标 Z。
 * 任一命令报错、故障码非 0 或超时都会立即中止；Z 轴没有全部复位时
 * 绝不会发送 XY 命令。
 *
 * 本类不直接操作串口：
 *   - 通过 sendRequested 把 15 字节帧交给 MotionController；
 *   - 仪器报告帧由 handleReport() 注入（MainWindow 把
 *     MotionController::reportReceived 连到该槽）。
 */
class CoordinateVerificationController : public QObject
{
    Q_OBJECT

public:
    explicit CoordinateVerificationController(QObject *parent = nullptr);

    /**
     * 使用外部 MotionProtocol 构帧，与 MotionController 共用命令编号，
     * 避免两条独立的 1~255 序列出现重号、把别的动作回执误判为复位完成。
     * 传 nullptr 表示恢复使用内部实例。
     */
    void setProtocol(platelet::MotionProtocol *protocol);

    bool isBusy() const;

    /** 调整各阶段超时（毫秒），默认 Z 复位 20s、XY 30s、Z 下降 30s */
    void setStageTimeouts(int zResetMs, int xyMs, int zMoveMs);

public slots:
    void start(const InstrumentCoordinates::MoveTarget &target);
    void handleReport(const platelet::MotionProtocol::Frame &frame);
    void cancel();

signals:
    void sendRequested(const QByteArray &frame, const QString &description);
    void statusChanged(const QString &status);
    void finished(bool ok, const QString &message);

private:
    enum class Stage {
        Idle,
        ResettingZ,
        MovingXy,
        MovingZ
    };

    struct PendingReset {
        quint8 cmdNum = 0;
        InstrumentCoordinates::ZAxis axis =
                InstrumentCoordinates::ZAxis::Gripper;
    };

    bool sendZResets();
    bool sendXyMove();
    bool sendZMove();
    void beginStage(Stage stage, int timeoutMs);
    void armTimeout(int timeoutMs);
    void stopTimeout();
    void handleStageTimeout();
    void finish(bool ok, const QString &message);

    bool isReportFor(const platelet::MotionProtocol::Frame &frame,
                     quint8 commandNumber, quint8 slaveAddress,
                     platelet::MotionProtocol::Axis axis) const;
    static quint8 commandNumberOf(const QByteArray &frame);
    static platelet::MotionProtocol::Axis protocolAxis(
            InstrumentCoordinates::ZAxis axis);
    static platelet::MotionProtocol::XyReference xyReference(
            InstrumentCoordinates::ZAxis axis);
    static QString targetName(const InstrumentCoordinates::MoveTarget &target);
    static QString axisDescription(InstrumentCoordinates::ZAxis axis);

    QTimer *m_timeoutTimer = nullptr;
    platelet::MotionProtocol m_ownProtocol;
    platelet::MotionProtocol *m_protocol = nullptr;
    InstrumentCoordinates::MoveTarget m_target;
    Stage m_stage = Stage::Idle;
    bool m_busy = false;

    QVector<PendingReset> m_pendingResets;
    quint8 m_xyCommandNumber = 0;
    quint8 m_zCommandNumber = 0;

    int m_zResetTimeoutMs = 20000;
    int m_xyTimeoutMs = 30000;
    int m_zMoveTimeoutMs = 30000;
};

#endif // COORDINATEVERIFICATIONCONTROLLER_H
