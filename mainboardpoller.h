#ifndef MAINBOARDPOLLER_H
#define MAINBOARDPOLLER_H

#include "mainboardstatus.h"

#include <QObject>
#include <QTimer>

class SerialPortManager;

/**
 * 主板状态轮询器。
 *
 * - 串口连接后每秒发送一次主板状态读取命令：
 *   05 16 FE 00 01 00 00 00 00 00 00 00 00 00 00
 * - 只解析从机地址 0x05、功能码 0x16、命令编号 0xFE 的状态报告帧；
 * - 解析结果通过 statusUpdated() 发给 CommunicationWorker，再 queued 到主界面。
 */
class MainBoardPoller : public QObject
{
    Q_OBJECT

public:
    explicit MainBoardPoller(SerialPortManager *serial,
                             QObject *parent = nullptr);
    ~MainBoardPoller() override;

    /** 主机状态轮询应答（用于过滤通讯日志） */
    bool isStatusReply(const QByteArray &frame) const;

    /** 耗材余量读取应答（命令号 100~112，用于过滤通讯日志） */
    bool isConsumableReply(const QByteArray &frame) const;

    /** 每秒发送的主板状态读取命令 */
    static QByteArray statusRequestFrame();

    /** 读取指定位置耗材余量的命令（position 0~12） */
    static QByteArray consumableRequestFrame(int position);

    /** 按主板信息 V1.0.1 解析 15 字节状态帧 */
    static MainBoardStatus parseStatusFrame(const QByteArray &frame,
                                            bool *ok = nullptr);

signals:
    void statusUpdated(const MainBoardStatus &status);
    /** 某个耗材位置的剩余总量（试管单位为板，其余为瓶） */
    void consumableUpdated(int position, int bottles, int ratio);

private:
    void startPolling();
    void stopPolling();
    void pollStatus();
    void pollConsumable();
    void handleFrame(const QByteArray &frame);
    void handleConsumableFrame(const QByteArray &frame);

    SerialPortManager *m_serial = nullptr;
    QTimer m_timer;
    QTimer m_consumableTimer;
    int m_consumablePosition = 0;
};

#endif // MAINBOARDPOLLER_H
