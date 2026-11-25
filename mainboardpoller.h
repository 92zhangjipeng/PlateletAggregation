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
 * - 解析从机地址 0x05、功能码 0x16 的状态帧（0xFE）和耗材读取应答
 *   （100~112），结果通过 statusUpdated()/consumableUpdated() 上报；
 * - 刷卡充值确认命令沿用 0xFE，写卡期间可暂停轮询，并解析 5/6 回执。
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

    /**
     * 写耗材总量（充值/换瓶登记总瓶数）。
     * position 0~12；totalBottles 为瓶/板数（≤255）。
     * 按主板信息 V1.0.1：byte7 高4位 reg_type、低4位 mfc_status=01 更新耗材总量，
     * byte9 写总量。余量字段保持不动。
     */
    static QByteArray consumableTotalWriteFrame(int position, int totalBottles);

    /**
     * 写耗材余量（每次扣减写单瓶 0~100% 比例）。
     * position 0~12；ratio 为 0~512。
     * 按主板信息 V1.0.1：byte9 高4位 Types_of_cons、低4位 cons_status=0001 更新余量，
     * byte11~12 写余量（低位在前）。
     */
    static QByteArray consumableRatioWriteFrame(int position, quint16 ratio);

    /** 按主板信息 V1.0.1 解析 15 字节状态帧 */
    static MainBoardStatus parseStatusFrame(const QByteArray &frame,
                                            bool *ok = nullptr);

    /**
     * 刷卡充值确认帧（老协议 ConfirmReadthecardRecharge）。
     * cmd_num=0xFE（沿用主板状态命令号）；byte8 高 4 位卡类型、
     * 低 4 位 0x02（读卡成功）；byte9 为写入后的总瓶数；比例字段写 0。
     */
    static QByteArray cardRechargeConfirmFrame(int cardType, int newTotal);

public slots:
    /** 刷卡写卡期间暂停状态帧/耗材读取，避免读命令冲掉 RFID 写窗口 */
    void pausePolling();
    void resumePolling();
    /**
     * 是否启用 100~112 耗材读取。新软件在开机灌注完成前先关闭，
     * 与老软件“灌注结束后才开始采集主板/耗材”一致；灌注结束后再打开。
     * 状态帧 0xFE 轮询不受影响。
     */
    void setConsumablePollingEnabled(bool enabled);
    /**
     * 开机灌注完成前整体推迟主板轮询（状态 0xFE 和耗材 100~112 都不发），
     * 与老软件“灌注结束后才启动主板采集”一致。断线重连后仍保持推迟，
     * 直到调用 setPollingDeferred(false)。
     */
    void setPollingDeferred(bool deferred);
    /**
     * 0x17 动作交换期间静默：定时器继续走，只是不发 0x16 状态/耗材读命令，
     * 避免读命令冲掉动作完成应答。
     */
    void setBusQuiet(bool quiet);
    /** 发送充值确认前登记期望的回执卡类型 */
    void expectCardRechargeResult(int cardType);

signals:
    void statusUpdated(const MainBoardStatus &status);
    /** 某个耗材位置的剩余总量（试管单位为板，其余为瓶） */
    void consumableUpdated(int position, int bottles, int ratio);
    /** 充值确认的 0xFE 回执：mfcStatus 5=成功、6=失败、3/4=读卡异常 */
    void cardRechargeResult(int mfcStatus, int cardType, int cardQty,
                            int year, int month);

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
    bool m_paused = false;
    bool m_pollingDeferred = false;
    bool m_busQuiet = false;
    bool m_consumablePollingEnabled = true;
    bool m_pendingCardResult = false;
    quint8 m_pendingCardType = 0;
};

#endif // MAINBOARDPOLLER_H
