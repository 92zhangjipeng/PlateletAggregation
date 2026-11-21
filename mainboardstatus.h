#ifndef MAINBOARDSTATUS_H
#define MAINBOARDSTATUS_H

#include <QByteArray>
#include <QMetaType>
#include <QtGlobal>

/**
 * 主板状态帧（主板信息 V1.0.1，15 字节）解析结果。
 *
 * 字节布局：
 * [0] slave_addr(0x05)
 * [1] func_code(0x16)
 * [2] cmd_num(定时读取为 0xFE)
 * [3] cmd_size
 * [4] type(bit0) / cmd_status(bit1~2) / Clean(bit3) / cup1~4(bit4~7)
 * [5] Waste(bit0) / height(bit1) / fan(bit2) / cool(bit3) /
 *     pump1(bit4) / pump2(bit5) / ThreeWay1(bit6) / ThreeWay2(bit7)
 * [6..7] 暂未使用
 * [8] mfc_status(bit0~3) / reg_type(bit4~7)
 * [9] reg_remain(刷卡瓶数)
 * [10] cons_status(bit0~3) / Types_of_cons(bit4~7)
 * [11..12] consumable（低字节在前）
 * [13] reg_date 年
 * [14] reg_date 月
 */
struct MainBoardStatus
{
    bool valid = false;

    quint8 cmdSize = 0;
    quint8 type = 0;          // 0=命令(W)，1=报告(R)
    quint8 cmdStatus = 0;     // 0等待 1运行中 2执行完成 3命令错误

    bool cleanNormal = true;  // true=清洗液正常
    bool cupPresent[4] = { true, true, true, true };
    bool wasteNormal = true;  // true=废液正常
    bool heightOk = false;
    bool fanOn = false;
    bool coolOn = false;
    bool pump1On = false;
    bool pump2On = false;
    bool threeWay1Open = false;
    bool threeWay2Open = false;

    quint8 mfcStatus = 0;
    quint8 regType = 0;
    quint8 regRemain = 0;
    quint8 consStatus = 0;
    quint8 consType = 0;
    quint16 consumable = 0;
    quint8 regYear = 0;
    quint8 regMonth = 0;

    QByteArray raw;
};

Q_DECLARE_METATYPE(MainBoardStatus)

#endif // MAINBOARDSTATUS_H
