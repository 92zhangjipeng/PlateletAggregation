#ifndef RESETCONTROLLER_H
#define RESETCONTROLLER_H

#include "instrumentcoordinates.h"
#include "motionprotocol.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

class QTimer;

/**
 * 主界面「复位」动作：
 *   1. 三根 Z 轴依次复位（样本针 → 试剂针 → 抓手）；
 *   2. XY 负速度模式复位；
 *   3. XY 移动到坐标表「原点」的样本针 XY（只移动 XY，Z 保持复位后的抬起状态）。
 *
 * 每一步都必须收到对应从机、轴、命令号的“执行完成”报告才发下一条；
 * 任一步报错、故障码非 0 或超时立即中止。
 */
class ResetController : public QObject
{
    Q_OBJECT

public:
    explicit ResetController(InstrumentCoordinates *coordinates,
                             QObject *parent = nullptr);

    void setProtocol(platelet::MotionProtocol *protocol);
    bool isBusy() const;

public slots:
    void start();
    void handleReport(const platelet::MotionProtocol::Frame &frame);
    void cancel();

signals:
    void sendRequested(const QByteArray &frame, const QString &description);
    void statusChanged(const QString &status);
    void finished(bool ok, const QString &message);

private:
    struct Step {
        QByteArray frame;
        QString description;
        quint8 slaveAddress = platelet::MotionProtocol::ZAxisBoard;
        platelet::MotionProtocol::Axis axis =
                platelet::MotionProtocol::Axis::SampleNeedle;
        int timeoutMs = 45000;
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

    int m_zResetTimeoutMs = 45000;
    int m_xyTimeoutMs = 45000;
};

#endif // RESETCONTROLLER_H
