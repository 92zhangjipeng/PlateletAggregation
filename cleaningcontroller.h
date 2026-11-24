#ifndef CLEANINGCONTROLLER_H
#define CLEANINGCONTROLLER_H

#include "instrumentcoordinates.h"
#include "motionprotocol.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

class QTimer;

/**
 * 主页「清洗」按钮：清洗双针。
 *
 * 流程移植老软件 QUIUtils::cleaningDoubleNeedleAction：
 *   1. 三根 Z 复位；
 *   2. 样本针到清洗位，液面探测，吸洗样本针量；
 *   3. 三根 Z 复位后 XY 到试剂针清洗位，液面探测，吸洗试剂针量；
 *   4. 三根 Z 复位后 XY 回原点，双针下到清洗高度，
 *      两泵吐 0 + 清洗时间；
 *   5. 双针复位，回吸空气，XY 回原点。
 *
 * 所有 XY 移动前都先发三根 Z 复位，满足主板“针未复位不能动 XY”的联锁。
 */
class CleaningController : public QObject
{
    Q_OBJECT

public:
    explicit CleaningController(InstrumentCoordinates *coordinates,
                                QObject *parent = nullptr);

    void setProtocol(platelet::MotionProtocol *protocol);
    void setCleaningTimeMs(int ms);
    void setStageTimeouts(int zResetMs, int xyMs, int zMoveMs,
                          int pumpMs, int pumpCleanMs);

    bool isBusy() const;

    /** 本次清洗实际吸走的清洗液量（µL），用于完成后扣 S1。 */
    double sampleWashVolumeUl() const;
    double reagentWashVolumeUl() const;

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
    enum class PumpUpdate {
        None,
        SamplePump,
        ReagentPump
    };

    struct Step {
        QByteArray frame;
        QString description;
        quint8 slaveAddress = 0;
        platelet::MotionProtocol::Axis axis =
                platelet::MotionProtocol::Axis::Xy;
        int timeoutMs = 45000;
        PumpUpdate pumpUpdate = PumpUpdate::None;
        int pumpPosition = -1;
    };

    bool buildSequence(QString *error);
    bool appendStep(const QByteArray &frame, const QString &description,
                    quint8 slaveAddress,
                    platelet::MotionProtocol::Axis axis,
                    int timeoutMs, QString *error,
                    PumpUpdate pumpUpdate = PumpUpdate::None,
                    int pumpPosition = -1);
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
    int m_zResetTimeoutMs = 45000;
    int m_xyTimeoutMs = 45000;
    int m_zMoveTimeoutMs = 45000;
    int m_pumpTimeoutMs = 45000;
    int m_pumpCleanTimeoutMs = 45000;

    double m_sampleWashUl = 0.0;
    double m_reagentWashUl = 0.0;
};

#endif // CLEANINGCONTROLLER_H
