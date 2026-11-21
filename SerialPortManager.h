#ifndef SERIALPORTMANAGER_H
#define SERIALPORTMANAGER_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QSet>
#include <QString>

/**
 * 仪器串口收发管理（QtSerialPort）
 *
 * - 仪器使用固定 15 字节帧：slave_addr(1) + func_code(1) + ... 共 15 字节；
 *   检测模组 func_code=0x17，主板 0x15/0x16，后续运动控制也走同一串口。
 * - 接收时做帧对齐：按“从机地址 1~5 + 已知功能码”找帧头，长度固定 15，
 *   错位时丢字节重新同步。
 * - 目前读写在 GUI 线程完成，15 字节帧足够；后续高频轮询可把本类移入线程。
 *
 * 新增设备类型时：
 *   - 功能码不在默认 {0x15, 0x16, 0x17} 里的，调用 registerFunctionCode()；
 *   - 新设备界面连接 frameReceived() 并按 slave_addr 过滤即可。
 */
class SerialPortManager : public QObject
{
    Q_OBJECT

public:
    struct PortInfo {
        QString portName;
        QString description;
        QString manufacturer;
        bool isInstrument = false;
    };

    static constexpr int kFrameLength = 15;
    static constexpr quint16 kInstrumentVid = 0x0483; // 仪器 USB VID
    static constexpr quint16 kInstrumentPid = 0x5740; // 仪器 USB PID
    static constexpr qint32 kInstrumentBaudRate = 1000000;

    explicit SerialPortManager(QObject *parent = nullptr);
    ~SerialPortManager() override;

    /** 扫描当前可用串口，识别到仪器的排在最前面 */
    static QList<PortInfo> scanPorts();
    static bool isInstrumentPort(const QSerialPortInfo &info);
    /** 按 VID/PID 查找仪器串口，找不到返回空字符串 */
    static QString findInstrumentPortName();

    bool openPort(const QString &portName, qint32 baudRate, QString *error = nullptr);
    void closePort();
    bool isOpen() const;
    QString portName() const;
    qint32 baudRate() const;
    QString lastError() const;

    /** 发送一帧；必须是 15 字节。未连接或长度不对时返回 false 并写 error */
    bool sendFrame(const QByteArray &frame, QString *error = nullptr);

    /** 追加允许接收的功能码（默认 0x15 / 0x16 / 0x17） */
    void registerFunctionCode(quint8 funcCode);
    QSet<quint8> allowedFunctionCodes() const;

    /** 从字节流中提取 15 字节帧；未成帧数据保留在 buffer 中。
     *  droppedBytes 返回本次因错位丢弃的字节数。 */
    static QList<QByteArray> extractFrames(QByteArray *buffer,
                                           const QSet<quint8> &allowedFunctionCodes,
                                           int *droppedBytes = nullptr);
    static bool isValidFrameHeader(quint8 address, quint8 functionCode,
                                   const QSet<quint8> &allowedFunctionCodes);

signals:
    void connectedChanged(bool connected);
    void frameReceived(const QByteArray &frame);
    void frameSent(const QByteArray &frame);
    void errorOccurred(const QString &message);

private slots:
    void handleReadyRead();
    void handleSerialError(QSerialPort::SerialPortError error);

private:
    void processBuffer();
    void setError(const QString &message);

    QSerialPort *m_port = nullptr;
    QByteArray m_buffer;
    QSet<quint8> m_allowedFunctionCodes;
    QString m_portName;
    QString m_lastError;
    qint32 m_baudRate = 1000000;
    bool m_open = false;
};

#endif // SERIALPORTMANAGER_H
