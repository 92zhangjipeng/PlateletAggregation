#ifndef PERFUSIONCONTROLLER_H
#define PERFUSIONCONTROLLER_H

#include "instrumentcoordinates.h"
#include "motionprotocol.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

class QTimer;

/**
 * 开机灌注流程（移植自老软件 QUIUtils::perfusionAction 的主流程）。
 *
 * 动作顺序：
 *   1. 三根 Z 轴依次复位（样本针 → 试剂针 → 抓手）；
 *   2. XY 负速度复位 → 到 (0,0) → 到样本针原点 XY；
 *   3. 样本针下到清洗高度，血样泵排空 → 复位 → 吸 60% 满量程 →
 *      下针 → 排空并清洗 → 复位 → 回吸空气；
 *   4. XY 到试剂针原点 XY，试剂针重复同样的排空/吸满/清洗/回吸空气流程；
 *   5. 结束。
 *
 * 安全约束：
 *   - 所有步骤严格串行，收到上一条“执行完成”报告后才发下一条；
 *   - 前三条是 Z 轴复位，因此 XY 移动一定发生在三根 Z 都复位完成之后；
 *   - 任一步报错、故障码非 0 或超时立即中止，不再执行后续动作。
 *
 * 本类不直接操作串口：通过 sendRequested 把帧交给 MotionController，
 * 报告帧由 handleReport() 注入。
 */
class PerfusionController : public QObject
{
    Q_OBJECT

public:
    explicit PerfusionController(InstrumentCoordinates *coordinates,
                                 QObject *parent = nullptr);

    /** 与 MotionController 共用命令编号，避免两条序列重号 */
    void setProtocol(platelet::MotionProtocol *protocol);

    /** 泵清洗时间，默认 6000ms（老软件 Config.ini 的 WashesTime） */
    void setCleaningTimeMs(int ms);
    /** 开机清洗下针高度，默认 40mm */
    void setWashDownHeightMm(int mm);

    /** 各阶段超时，默认 Z 复位 20s、XY 30s、Z 30s、泵 30s、带清洗泵 45s */
    void setStageTimeouts(int zResetMs, int xyMs, int zMoveMs,
                          int pumpMs, int pumpCleanMs);

    bool isBusy() const;

public slots:
    void start();
    void handleReport(const platelet::MotionProtocol::Frame &frame);
    void cancel();

signals:
    void sendRequested(const QByteArray &frame, const QString &description);
    void progressChanged(int completed, int total, const QString &status);
    void statusChanged(const QString &status);
    void finished(bool ok, const QString &message);

private:
    struct Step {
        QByteArray frame;
        QString description;
        quint8 slaveAddress = 0;
        platelet::MotionProtocol::Axis axis =
                platelet::MotionProtocol::Axis::Xy;
        int timeoutMs = 30000;
    };

    bool buildSequence(QString *error);
    bool appendStep(const QByteArray &frame, const QString &description,
                    quint8 slaveAddress,
                    platelet::MotionProtocol::Axis axis,
                    int timeoutMs, QString *error);
    void sendNextStep();
    void finish(bool ok, const QString &message);
    bool isReportFor(const platelet::MotionProtocol::Frame &frame,
                     const Step &step) const;

    InstrumentCoordinates *m_coordinates = nullptr;
    QTimer *m_timeoutTimer = nullptr;
    platelet::MotionProtocol m_ownProtocol;
    platelet::MotionProtocol *m_protocol = nullptr;

    QVector<Step> m_steps;
    int m_currentStep = -1;
    bool m_busy = false;

    int m_cleaningTimeMs = 6000;
    int m_washDownHeightMm = 40;
    int m_zResetTimeoutMs = 20000;
    int m_xyTimeoutMs = 30000;
    int m_zMoveTimeoutMs = 30000;
    int m_pumpTimeoutMs = 30000;
    int m_pumpCleanTimeoutMs = 45000;
};

#endif // PERFUSIONCONTROLLER_H
