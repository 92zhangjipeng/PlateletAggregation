#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "SerialPortManager.h"

#include <QDebug>
#include <algorithm>

namespace {
constexpr int kMaxReceiveBuffer = 64 * 1024;
}

SerialPortManager::SerialPortManager(QObject *parent)
    : QObject(parent)
    , m_port(new QSerialPort(this))
{
    m_allowedFunctionCodes << 0x15 << 0x16 << 0x17;

    connect(m_port, &QSerialPort::readyRead,
            this, &SerialPortManager::handleReadyRead);
    connect(m_port, &QSerialPort::errorOccurred,
            this, &SerialPortManager::handleSerialError);
}

SerialPortManager::~SerialPortManager()
{
    if (m_port && m_port->isOpen()) {
        m_port->close();
    }
}

QList<SerialPortManager::PortInfo> SerialPortManager::scanPorts()
{
    QList<PortInfo> result;
    const QList<QSerialPortInfo> infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos) {
        PortInfo port;
        port.portName = info.portName();
        port.description = info.description();
        port.manufacturer = info.manufacturer();
        port.isInstrument = isInstrumentPort(info);
        result.append(port);
    }

    std::sort(result.begin(), result.end(),
              [](const PortInfo &left, const PortInfo &right) {
        if (left.isInstrument != right.isInstrument) {
            return left.isInstrument;
        }
        return left.portName < right.portName;
    });
    return result;
}

bool SerialPortManager::isInstrumentPort(const QSerialPortInfo &info)
{
    return info.hasVendorIdentifier() && info.hasProductIdentifier()
            && info.vendorIdentifier() == kInstrumentVid
            && info.productIdentifier() == kInstrumentPid;
}

QString SerialPortManager::findInstrumentPortName()
{
    const QList<QSerialPortInfo> infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos) {
        if (isInstrumentPort(info)) {
            return info.portName();
        }
    }
    return QString();
}

bool SerialPortManager::openPort(const QString &portName, qint32 baudRate, QString *error)
{
    const QString trimmedName = portName.trimmed();
    if (trimmedName.isEmpty()) {
        setError(tr("未选择串口"));
        if (error) {
            *error = m_lastError;
        }
        return false;
    }
    if (baudRate <= 0) {
        setError(tr("波特率无效：%1").arg(baudRate));
        if (error) {
            *error = m_lastError;
        }
        return false;
    }

    closePort();

    m_port->setPortName(trimmedName);
    m_port->setBaudRate(baudRate);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setFlowControl(QSerialPort::NoFlowControl);
    m_port->setReadBufferSize(kMaxReceiveBuffer);

    if (!m_port->open(QIODevice::ReadWrite)) {
        setError(tr("打开串口 %1 失败：%2").arg(trimmedName, m_port->errorString()));
        if (error) {
            *error = m_lastError;
        }
        return false;
    }

    // 部分驱动在 open 之后才真正应用参数，这里再设置一次并检查
    m_port->setBaudRate(baudRate);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setFlowControl(QSerialPort::NoFlowControl);
    m_port->setDataTerminalReady(true);
    m_port->setRequestToSend(true);
    m_port->clear();

    if (m_port->baudRate() != baudRate) {
        setError(tr("串口 %1 波特率设置失败：期望 %2，实际 %3")
                 .arg(trimmedName)
                 .arg(baudRate)
                 .arg(m_port->baudRate()));
        m_port->close();
        if (error) {
            *error = m_lastError;
        }
        return false;
    }

    m_buffer.clear();
    m_portName = m_port->portName();
    m_baudRate = m_port->baudRate();
    m_open = true;
    m_lastError.clear();
    emit connectedChanged(true);
    return true;
}

void SerialPortManager::closePort()
{
    const bool wasOpen = m_open;
    if (m_port && m_port->isOpen()) {
        m_port->clear();
        m_port->close();
    }
    m_buffer.clear();
    m_open = false;
    m_portName.clear();
    if (wasOpen) {
        emit connectedChanged(false);
    }
}

bool SerialPortManager::isOpen() const
{
    return m_open && m_port && m_port->isOpen();
}

QString SerialPortManager::portName() const
{
    return m_portName;
}

qint32 SerialPortManager::baudRate() const
{
    return m_baudRate;
}

QString SerialPortManager::lastError() const
{
    return m_lastError;
}

bool SerialPortManager::sendFrame(const QByteArray &frame, QString *error)
{
    if (!isOpen()) {
        setError(tr("串口未连接"));
        if (error) {
            *error = m_lastError;
        }
        return false;
    }
    if (frame.size() != kFrameLength) {
        setError(tr("帧长度错误：必须 %1 字节，实际 %2 字节")
                 .arg(kFrameLength).arg(frame.size()));
        if (error) {
            *error = m_lastError;
        }
        return false;
    }

    const qint64 written = m_port->write(frame);
    if (written != frame.size()) {
        setError(tr("串口写入失败：%1").arg(m_port->errorString()));
        if (error) {
            *error = m_lastError;
        }
        return false;
    }

    emit frameSent(frame);
    return true;
}

void SerialPortManager::registerFunctionCode(quint8 funcCode)
{
    m_allowedFunctionCodes.insert(funcCode);
}

QSet<quint8> SerialPortManager::allowedFunctionCodes() const
{
    return m_allowedFunctionCodes;
}

bool SerialPortManager::isValidFrameHeader(quint8 address, quint8 functionCode,
                                            const QSet<quint8> &allowedFunctionCodes)
{
    // 从机地址：1~3 检测模组，4 Z 轴，5 主板
    return address >= 1 && address <= 5 && allowedFunctionCodes.contains(functionCode);
}

QList<QByteArray> SerialPortManager::extractFrames(QByteArray *buffer,
                                                    const QSet<quint8> &allowedFunctionCodes,
                                                    int *droppedBytes)
{
    QList<QByteArray> frames;
    int dropped = 0;
    if (!buffer) {
        if (droppedBytes) {
            *droppedBytes = 0;
        }
        return frames;
    }

    while (buffer->size() >= kFrameLength) {
        int start = -1;
        for (int i = 0; i + 1 < buffer->size(); ++i) {
            if (isValidFrameHeader(static_cast<quint8>(buffer->at(i)),
                                   static_cast<quint8>(buffer->at(i + 1)),
                                   allowedFunctionCodes)) {
                start = i;
                break;
            }
        }

        if (start < 0) {
            // 找不到帧头：保留最后一个字节（可能是下一帧的第一个字节）
            if (buffer->size() > 1) {
                dropped += buffer->size() - 1;
                const char lastByte = buffer->at(buffer->size() - 1);
                buffer->clear();
                buffer->append(lastByte);
            }
            break;
        }

        if (start > 0) {
            dropped += start;
            buffer->remove(0, start);
        }
        if (buffer->size() < kFrameLength) {
            break;
        }

        frames.append(buffer->left(kFrameLength));
        buffer->remove(0, kFrameLength);
    }

    if (droppedBytes) {
        *droppedBytes = dropped;
    }
    return frames;
}

void SerialPortManager::handleReadyRead()
{
    if (!m_port) {
        return;
    }

    const QByteArray data = m_port->readAll();
    if (data.isEmpty()) {
        return;
    }

    m_buffer.append(data);
    if (m_buffer.size() > kMaxReceiveBuffer) {
        qWarning() << "Serial receive buffer overflow, resync. size=" << m_buffer.size();
        m_buffer = m_buffer.right(1);
        emit errorOccurred(tr("串口接收缓存溢出，已清空重新同步"));
    }

    processBuffer();
}

void SerialPortManager::processBuffer()
{
    int dropped = 0;
    const QList<QByteArray> frames =
            extractFrames(&m_buffer, m_allowedFunctionCodes, &dropped);
    if (dropped > 0) {
        qWarning() << "Serial frame alignment: dropped" << dropped << "bytes";
    }
    for (const QByteArray &frame : frames) {
        emit frameReceived(frame);
    }
}

void SerialPortManager::handleSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || !m_port) {
        return;
    }

    const QString message = m_port->errorString();
    m_lastError = message;

    const bool fatal = (error == QSerialPort::ResourceError)
            || (error == QSerialPort::DeviceNotFoundError)
            || (error == QSerialPort::PermissionError);
    if (fatal && m_open) {
        m_port->close();
        m_buffer.clear();
        m_open = false;
        m_portName.clear();
        emit connectedChanged(false);
    }

    emit errorOccurred(tr("串口错误：%1").arg(message));
}

void SerialPortManager::setError(const QString &message)
{
    m_lastError = message;
    emit errorOccurred(message);
}
