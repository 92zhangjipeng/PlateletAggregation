#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "motioncontroller.h"

#include "CommunicationWorker.h"

#include <QDebug>
#include <QMetaObject>

namespace platelet {

MotionController::MotionController(QObject *parent)
    : QObject(parent)
{
}

void MotionController::setCommunicationWorker(CommunicationWorker *worker)
{
    if (m_worker == worker) {
        return;
    }
    if (m_worker) {
        QObject::disconnect(m_worker, nullptr, this, nullptr);
    }
    m_worker = worker;
    if (!m_worker) {
        return;
    }

    connect(m_worker, &CommunicationWorker::sendResult, this,
            [this](const QString &description, bool ok,
                   const QString &error) {
        if (!description.startsWith(descriptionPrefix())) {
            return;
        }
        if (!ok) {
            qWarning() << "运动命令发送失败:" << description << error;
        }
        emit commandSent(description, ok, error);
    }, Qt::QueuedConnection);

    connect(m_worker, &CommunicationWorker::frameReceived, this,
            [this](const QByteArray &frame) {
        QString parseError;
        const MotionProtocol::Frame parsed =
                MotionProtocol::parse(frame, &parseError);
        if (!parsed.valid || !parsed.isReport()) {
            return;
        }
        if (parsed.slaveAddress != MotionProtocol::ZAxisBoard
                && parsed.slaveAddress != MotionProtocol::MainBoard) {
            return;
        }

        qInfo() << "运动报告: 从机" << parsed.slaveAddress
                << "类型" << (parsed.type == MotionProtocol::FrameType::Report
                              ? "报告" : "命令位回帧")
                << "轴" << MotionProtocol::axisName(parsed.axis)
                << "状态" << MotionProtocol::statusText(parsed.status)
                << "故障" << parsed.faultDescription()
                << "D1" << parsed.distance1
                << "D2" << parsed.distance2;
        emit reportReceived(parsed);
    }, Qt::QueuedConnection);
}

CommunicationWorker *MotionController::communicationWorker() const
{
    return m_worker;
}

MotionProtocol &MotionController::protocol()
{
    return m_protocol;
}

const MotionProtocol &MotionController::protocol() const
{
    return m_protocol;
}

QString MotionController::lastError() const
{
    return m_lastError;
}

bool MotionController::moveXy(int x, int y, MotionProtocol::XyReference reference,
                              MotionProtocol::Area area)
{
    const QByteArray frame = m_protocol.moveXy(x, y, reference, area);
    return queueFrame(
                frame,
                descriptionPrefix()
                + QStringLiteral(" XY 移动到 X=%1 Y=%2（相对%3，%4）")
                .arg(x).arg(y)
                .arg(MotionProtocol::xyReferenceName(reference),
                     MotionProtocol::areaName(area)));
}

bool MotionController::moveGripperTo(int height, MotionProtocol::Area area,
                                     bool grabCup, bool liquidLevel)
{
    const QByteArray frame =
            m_protocol.moveGripperTo(height, area, grabCup, liquidLevel);
    QString flags;
    if (grabCup) {
        flags += QStringLiteral("，抓杯");
    }
    if (liquidLevel) {
        flags += QStringLiteral("，液面探测");
    }
    return queueFrame(
                frame,
                descriptionPrefix()
                + QStringLiteral(" 抓手下降到 %1（%2%3）")
                .arg(height)
                .arg(MotionProtocol::areaName(area), flags));
}

bool MotionController::moveReagentNeedleTo(int height,
                                           MotionProtocol::Area area,
                                           bool liquidLevel)
{
    const QByteArray frame =
            m_protocol.moveReagentNeedleTo(height, area, liquidLevel);
    return queueFrame(
                frame,
                descriptionPrefix()
                + QStringLiteral(" 试剂针下降到 %1（%2%3）")
                .arg(height)
                .arg(MotionProtocol::areaName(area),
                     liquidLevel ? QStringLiteral("，液面探测") : QString()));
}

bool MotionController::moveSampleNeedleTo(int height,
                                          MotionProtocol::Area area,
                                          bool liquidLevel)
{
    const QByteArray frame =
            m_protocol.moveSampleNeedleTo(height, area, liquidLevel);
    return queueFrame(
                frame,
                descriptionPrefix()
                + QStringLiteral(" 样本针下降到 %1（%2%3）")
                .arg(height)
                .arg(MotionProtocol::areaName(area),
                     liquidLevel ? QStringLiteral("，液面探测") : QString()));
}

bool MotionController::resetXy(int speed)
{
    const QByteArray frame = m_protocol.resetXy(speed);
    return queueFrame(
                frame,
                descriptionPrefix()
                + QStringLiteral(" XY 复位（速度 %1）").arg(speed));
}

bool MotionController::resetGripper(int speed, MotionProtocol::Area area)
{
    const QByteArray frame = m_protocol.resetGripper(speed, area);
    return queueFrame(
                frame,
                descriptionPrefix()
                + QStringLiteral(" 抓手复位（速度 %1，%2）")
                .arg(speed)
                .arg(MotionProtocol::areaName(area)));
}

bool MotionController::resetReagentNeedle(int speed,
                                          MotionProtocol::Area area)
{
    const QByteArray frame = m_protocol.resetReagentNeedle(speed, area);
    return queueFrame(
                frame,
                descriptionPrefix()
                + QStringLiteral(" 试剂针复位（速度 %1，%2）")
                .arg(speed)
                .arg(MotionProtocol::areaName(area)));
}

bool MotionController::resetSampleNeedle(int speed,
                                         MotionProtocol::Area area)
{
    const QByteArray frame = m_protocol.resetSampleNeedle(speed, area);
    return queueFrame(
                frame,
                descriptionPrefix()
                + QStringLiteral(" 样本针复位（速度 %1，%2）")
                .arg(speed)
                .arg(MotionProtocol::areaName(area)));
}

bool MotionController::moveBloodPump(int units, bool aspirate, bool clean,
                                     int cleaningTimeMs)
{
    const QByteArray frame =
            m_protocol.moveBloodPump(units, aspirate, clean, cleaningTimeMs);
    return queueFrame(
                frame,
                descriptionPrefix()
                + QStringLiteral(" 血样泵 %1 %2 单位%3")
                .arg(aspirate ? QStringLiteral("吸样")
                              : QStringLiteral("吐样"))
                .arg(units)
                .arg(clean ? QStringLiteral("，清洗 %1ms").arg(cleaningTimeMs)
                           : QString()));
}

bool MotionController::moveReagentPump(int units, bool aspirate, bool clean,
                                       int cleaningTimeMs)
{
    const QByteArray frame =
            m_protocol.moveReagentPump(units, aspirate, clean,
                                       cleaningTimeMs);
    return queueFrame(
                frame,
                descriptionPrefix()
                + QStringLiteral(" 试剂泵 %1 %2 单位%3")
                .arg(aspirate ? QStringLiteral("吸样")
                              : QStringLiteral("吐样"))
                .arg(units)
                .arg(clean ? QStringLiteral("，清洗 %1ms").arg(cleaningTimeMs)
                           : QString()));
}

bool MotionController::resetBloodPump(int speed, bool clean)
{
    const QByteArray frame = m_protocol.resetBloodPump(speed, clean);
    return queueFrame(
                frame,
                descriptionPrefix()
                + QStringLiteral(" 血样泵复位（速度 %1%2）")
                .arg(speed)
                .arg(clean ? QStringLiteral("，清洗") : QString()));
}

bool MotionController::resetReagentPump(int speed, bool clean)
{
    const QByteArray frame = m_protocol.resetReagentPump(speed, clean);
    return queueFrame(
                frame,
                descriptionPrefix()
                + QStringLiteral(" 试剂泵复位（速度 %1%2）")
                .arg(speed)
                .arg(clean ? QStringLiteral("，清洗") : QString()));
}

bool MotionController::sendCommand(const QByteArray &frame,
                                   const QString &description)
{
    if (frame.size() != MotionProtocol::FrameSize) {
        reject(description,
               QStringLiteral("运动命令帧必须是 %1 字节，实际 %2 字节")
               .arg(int(MotionProtocol::FrameSize)).arg(frame.size()));
        return false;
    }
    return queueFrame(frame, description);
}

bool MotionController::queueFrame(const QByteArray &frame,
                                  const QString &description)
{
    m_lastError.clear();

    QString desc = description;
    if (desc.isEmpty()) {
        desc = descriptionPrefix() + QStringLiteral(" 自定义命令");
    } else if (!desc.startsWith(descriptionPrefix())) {
        desc.prepend(descriptionPrefix() + QLatin1Char(' '));
    }

    if (frame.isEmpty()) {
        const QString error = m_protocol.lastError().isEmpty()
                ? QStringLiteral("运动命令构帧失败")
                : m_protocol.lastError();
        reject(desc, error);
        return false;
    }
    if (!m_worker) {
        reject(desc, QStringLiteral("通讯线程未启动"));
        return false;
    }

    QMetaObject::invokeMethod(m_worker, "sendFrameRequested",
                              Qt::QueuedConnection,
                              Q_ARG(QByteArray, frame),
                              Q_ARG(QString, desc));
    qInfo() << desc << frame.toHex(' ').toUpper();
    emit commandQueued(desc);
    return true;
}

void MotionController::reject(const QString &description,
                              const QString &error)
{
    m_lastError = error;
    qWarning() << "运动命令未发送:" << description << error;
    emit commandRejected(description, error);
}

QString MotionController::descriptionPrefix()
{
    return QStringLiteral("运动:");
}

} // namespace platelet
