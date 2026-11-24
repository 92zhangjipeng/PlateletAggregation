#ifndef MAINBOARDCONSUMABLES_H
#define MAINBOARDCONSUMABLES_H

#include <QString>

/**
 * 主板耗材读取位置。
 *
 * 串口连接后由 MainBoardPoller 逐个发送读命令（命令号 100~112），
 * 每次读取一个位置的剩余总量：
 * - 0~7：AA、EPI、ADP、COL、RIS、内部清洗液 S1、试管、外部清洗液 S2；
 * - 8~12：AA、EPI、ADP、COL、RIS 的右侧瓶位（AA1~RIS1）。
 *
 * 已与主板确认：
 * - 读写下标（命令号 100+下标、总量瓶/板 ≤255、单瓶比例 ≤512 共用）：
 *   0 AA、1 EPI、2 ADP、3 COL、4 RIS、5 内部清洗剂 S1、6 试管、
 *   7 外部清洗液 S2、8~12 右侧瓶位 AA1~RIS1；
 * - 应答帧 Types_of_cons 字段是另一套编号：
 *   0 AA、1 EPI、2 ADP、3 COL、4 RIS、5 内部清洗剂、6 试管、
 *   7 AA1、8 EPI1、9 ADP1、10 COL1、11 RIS1、12 NONE；
 *   即右侧瓶位 8~12 在应答里报 7~11，需要 +1 映射回去；
 *   外部清洗液 S2 在该字段中没有定义。
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

/** 是否为 AA~RIS 的试剂位（左侧 0~4 或右侧 8~12） */
inline bool isReagentPosition(int position)
{
    return (position >= AA && position <= RIS)
            || (position >= AA_Second && position <= RIS_Second);
}

/**
 * 同一试剂的左侧/右侧共享“总瓶数”，主位置固定为左侧 0~4。
 * 非试剂位（S1/试管/S2）返回自身。
 */
inline int mainPositionForReagent(int position)
{
    if (position >= AA_Second && position <= RIS_Second) {
        return position - AA_Second;
    }
    return position;
}

/** 界面显示名：AA 左 / AA 右 / 内部清洗液 S1 … */
inline QString positionDisplayName(int position)
{
    switch (position) {
    case AA:              return QStringLiteral("AA 左");
    case EPI:             return QStringLiteral("EPI 左");
    case ADP:             return QStringLiteral("ADP 左");
    case COL:             return QStringLiteral("COL 左");
    case RIS:             return QStringLiteral("RIS 左");
    case InternalCleanS1: return QStringLiteral("内部清洗液 S1");
    case TestTube:        return QStringLiteral("试管盘");
    case ExternalCleanS2: return QStringLiteral("外部清洗液 S2");
    case AA_Second:       return QStringLiteral("AA 右");
    case EPI_Second:      return QStringLiteral("EPI 右");
    case ADP_Second:      return QStringLiteral("ADP 右");
    case COL_Second:      return QStringLiteral("COL 右");
    case RIS_Second:      return QStringLiteral("RIS 右");
    default:              return QStringLiteral("未知位置(%1)").arg(position);
    }
}

/**
 * 刷卡 reg_type -> 主板耗材位置。
 *
 * 协议 0~5：AA、EPI、ADP、COL、RIS、内部清洗剂 S1；
 * 6/7：试管、外部清洗液 S2（沿用老软件编码）；
 * 8~12：AA1~RIS1，右侧瓶位（命令位置 8~12）。
 * 映射集中在此处，固件编码若有调整只需改这一处。
 */
inline int positionForCardType(int cardType)
{
    switch (cardType) {
    case 0:  return AA;
    case 1:  return EPI;
    case 2:  return ADP;
    case 3:  return COL;
    case 4:  return RIS;
    case 5:  return InternalCleanS1;
    case 6:  return TestTube;
    case 7:  return ExternalCleanS2;
    case 8:  return AA_Second;
    case 9:  return EPI_Second;
    case 10: return ADP_Second;
    case 11: return COL_Second;
    case 12: return RIS_Second;
    default: return -1;
    }
}

} // namespace MainBoardConsumable

#endif // MAINBOARDCONSUMABLES_H
