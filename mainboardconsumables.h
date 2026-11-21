#ifndef MAINBOARDCONSUMABLES_H
#define MAINBOARDCONSUMABLES_H

/**
 * 主板耗材读取位置。
 *
 * 串口连接后由 MainBoardPoller 逐个发送读命令（命令号 100~112），
 * 每次读取一个位置的剩余总量：
 * - 0~7：AA、EPI、ADP、COL、RIS、内部清洗液 S1、试管、外部清洗液 S2；
 * - 8~12：AA、EPI、ADP、COL、RIS 的右侧瓶位（AA1~RIS1）。
 *
 * 回复帧沿用 15 字节主板帧：
 * [8] 低 4 位 mfc 工作状态，高 4 位刷卡数据类型；
 * [9] 剩余总量（试剂/清洗液单位为瓶，试管单位为板）；
 * [10] 低 4 位 cons_status，高 4 位 Types_of_cons：
 *      0 AA、1 EPI、2 ADP、3 COL、4 RIS、5 内部清洗剂、6 试管、
 *      7 AA1、8 EPI1、9 ADP1、10 COL1、11 RIS1、12 NONE；
 * [11..12] 当前单瓶剩余比例 0~512（低字节在前）。
 */
namespace MainBoardConsumable {

constexpr int kPositionCount = 13;
constexpr int kCommandBase = 100;
constexpr int kRatioMax = 512;

enum Position {
    AA = 0,
    EPI = 1,
    ADP = 2,
    COL = 3,
    RIS = 4,
    InternalCleanS1 = 5,
    TestTube = 6,
    ExternalCleanS2 = 7,
    AA_Second = 8,
    EPI_Second = 9,
    ADP_Second = 10,
    COL_Second = 11,
    RIS_Second = 12
};

} // namespace MainBoardConsumable

#endif // MAINBOARDCONSUMABLES_H
