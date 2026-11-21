#ifndef INSTRUMENTCOORDINATES_H
#define INSTRUMENTCOORDINATES_H

#include "MachineModel.h"

#include <QString>
#include <QVector>

/**
 * 仪器坐标表（仅保存在内存中）。
 *
 * 三根 Z 轴（抓手、试剂针、样本针）在水平方向上是错位安装的：
 * 同一个目标位置，要让不同的工具垂直对准它，XY 坐标是不一样的。
 * 因此每个目标位置都保存 3 组工具坐标，每组包含：
 * - 该工具对准目标时的 X / Y；
 * - 该工具在这个 XY 位置的 Z 下降高度。
 *
 * 坐标单位统一为毫米（mm），数组下标统一从 0 开始：
 * - 通道：0 ~ channelCount()-1，对应 CH1 ~ CH12；
 * - 试管：0 ~ tubeCount()-1，按盘连续编号（盘1=0~59，盘2=60~119 ...）；
 * - 试剂位：0 ~ 9，顺序见 reagentPositionName()；
 * - 样本孔：0 ~ sampleWellCount()-1。
 *
 * 有效数量随机型变化：
 * - KS600 ：4 通道、2 个试管盘 120 试管、60 个样本孔；
 * - KS800 ：8 通道、3 个试管盘 180 试管、80 个样本孔；
 * - KS1200：12 通道、4 个试管盘 240 试管、120 个样本孔；
 * - 试剂位固定 10 个。
 *
 * 试剂位命名（下标 0~9）：
 *   AA左、ADP左、EPI左、COL左、RIS左、AA右、ADP右、EPI右、COL右、RIS右。
 *
 * 线程安全：本类只做内存数据保存，不提供锁；请在同一个线程内使用，
 * 或由调用方自行加锁。
 *
 * 示例：
 * @code
 * InstrumentCoordinates coords(MachineModel::KS1200);
 *
 * // 抓手对准 CH1
 * coords.setChannelXy(0, InstrumentCoordinates::ZAxis::Gripper,
 *                     100.0, 200.0);
 * coords.setChannelZ(0, InstrumentCoordinates::ZAxis::Gripper, 35.0);
 *
 * // 样本针对准 CH1（XY 和抓手不同）
 * coords.setChannelXy(0, InstrumentCoordinates::ZAxis::SampleNeedle,
 *                     102.5, 198.0);
 * coords.setChannelZ(0, InstrumentCoordinates::ZAxis::SampleNeedle, 42.0);
 *
 * const auto target = coords.locateChannel(
 *             0, InstrumentCoordinates::ZAxis::SampleNeedle);
 * if (target.valid()) {
 *     // XY 移动到 target.x / target.y，样本针下降到 target.z
 * }
 * @endcode
 */
class InstrumentCoordinates
{
public:
    enum class ZAxis {
        Gripper = 0,        // 抓手
        ReagentNeedle = 1,  // 试剂针
        SampleNeedle = 2,   // 样本针
        AxisCount = 3
    };

    enum class TargetKind {
        Origin,         // 原点
        CleaningHole,   // 清洗孔位
        DiscardCup,     // 弃杯孔位
        Channel,        // 检测通道
        Tube,           // 试管盘中的试管位
        Reagent,        // 试剂位
        SampleWell      // 样本孔
    };

    /** 某一个工具（Z 轴）在一个目标位置上的坐标 */
    struct ToolPose {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;       // 该工具在此 XY 位置的下降高度
        bool xyValid = false;
        bool zValid = false;

        bool valid() const { return xyValid && zValid; }
    };

    /** 一个目标位置的 3 组工具坐标 */
    struct TargetPosition {
        ToolPose tools[3];

        bool hasTool(ZAxis axis) const;
        const ToolPose &tool(ZAxis axis) const;
    };

    /** locate() 返回的定位结果：指定工具的 XY + Z */
    struct MoveTarget {
        TargetKind kind = TargetKind::Origin;
        int index = -1;
        ZAxis axis = ZAxis::Gripper;

        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        bool xyValid = false;
        bool zValid = false;

        bool valid() const { return xyValid && zValid; }
    };

    static constexpr int kMaxChannels = 12;
    static constexpr int kMaxTubeTrays = 4;
    static constexpr int kTubesPerTray = 60;
    static constexpr int kMaxTubes = kMaxTubeTrays * kTubesPerTray;
    static constexpr int kReagentPositions = 10;
    static constexpr int kMaxSampleWells = 120;

    explicit InstrumentCoordinates(
            MachineModel model = defaultMachineModel());

    MachineModel machineModel() const;
    void setMachineModel(MachineModel model);

    int channelCount() const;
    int tubeTrayCount() const;
    int tubeCount() const;
    int reagentCount() const;
    int sampleWellCount() const;

    // ---------------- 通用读写 ----------------
    // Origin / CleaningHole / DiscardCup 的 index 参数会被忽略。
    bool setXy(TargetKind kind, int index, ZAxis axis,
               double x, double y);
    bool setZ(TargetKind kind, int index, ZAxis axis, double z);
    bool xy(TargetKind kind, int index, ZAxis axis,
            double *x, double *y) const;
    bool z(TargetKind kind, int index, ZAxis axis, double *z) const;
    /** 返回某个工具在目标位置的完整坐标（无效时 xyValid/zValid=false） */
    ToolPose toolPose(TargetKind kind, int index, ZAxis axis) const;
    /** 当前机型下该目标位置的 index 是否有效（特殊位置的 index 忽略） */
    bool indexValid(TargetKind kind, int index) const;

    // ---------------- 特殊位置快捷接口 ----------------
    bool setOriginXy(ZAxis axis, double x, double y);
    bool setOriginZ(ZAxis axis, double z);
    bool setCleaningHoleXy(ZAxis axis, double x, double y);
    bool setCleaningHoleZ(ZAxis axis, double z);
    bool setDiscardCupXy(ZAxis axis, double x, double y);
    bool setDiscardCupZ(ZAxis axis, double z);

    // ---------------- 通道 ----------------
    bool setChannelXy(int channelIndex, ZAxis axis, double x, double y);
    bool setChannelZ(int channelIndex, ZAxis axis, double z);
    bool channelXy(int channelIndex, ZAxis axis,
                   double *x, double *y) const;
    bool channelZ(int channelIndex, ZAxis axis, double *z) const;

    // ---------------- 试管 ----------------
    bool setTubeXy(int tubeIndex, ZAxis axis, double x, double y);
    bool setTubeZ(int tubeIndex, ZAxis axis, double z);
    bool tubeXy(int tubeIndex, ZAxis axis, double *x, double *y) const;
    bool tubeZ(int tubeIndex, ZAxis axis, double *z) const;

    // ---------------- 试剂位 ----------------
    bool setReagentXy(int reagentIndex, ZAxis axis, double x, double y);
    bool setReagentZ(int reagentIndex, ZAxis axis, double z);
    bool reagentXy(int reagentIndex, ZAxis axis,
                   double *x, double *y) const;
    bool reagentZ(int reagentIndex, ZAxis axis, double *z) const;

    // ---------------- 样本孔 ----------------
    bool setSampleWellXy(int wellIndex, ZAxis axis, double x, double y);
    bool setSampleWellZ(int wellIndex, ZAxis axis, double z);
    bool sampleWellXy(int wellIndex, ZAxis axis,
                      double *x, double *y) const;
    bool sampleWellZ(int wellIndex, ZAxis axis, double *z) const;

    // ---------------- 定位 ----------------
    /** 返回指定目标 + 指定工具的 XY/Z 定位结果 */
    MoveTarget locate(TargetKind kind, int index, ZAxis axis) const;
    MoveTarget locateOrigin(ZAxis axis) const;
    MoveTarget locateCleaningHole(ZAxis axis) const;
    MoveTarget locateDiscardCup(ZAxis axis) const;
    MoveTarget locateChannel(int channelIndex, ZAxis axis) const;
    MoveTarget locateTube(int tubeIndex, ZAxis axis) const;
    MoveTarget locateReagent(int reagentIndex, ZAxis axis) const;
    MoveTarget locateSampleWell(int wellIndex, ZAxis axis) const;

    // ---------------- 清空 ----------------
    void clear();
    void clear(TargetKind kind, int index);
    /** 清空某个工具在该位置的 XY 和 Z */
    void clearPose(TargetKind kind, int index, ZAxis axis);
    /** 只清空某个工具的 Z，保留 XY */
    void clearZ(TargetKind kind, int index, ZAxis axis);

    static QString zAxisName(ZAxis axis);
    static QString targetKindName(TargetKind kind);
    /** 试剂位显示名：AA左、ADP左、EPI左、COL左、RIS左、AA右... */
    static QString reagentPositionName(int reagentIndex);

private:
    TargetPosition *position(TargetKind kind, int index);
    const TargetPosition *position(TargetKind kind, int index) const;
    bool validIndex(TargetKind kind, int index) const;

    MachineModel m_model = defaultMachineModel();

    TargetPosition m_origin;
    TargetPosition m_cleaningHole;
    TargetPosition m_discardCup;
    QVector<TargetPosition> m_channels;
    QVector<TargetPosition> m_tubes;
    QVector<TargetPosition> m_reagents;
    QVector<TargetPosition> m_sampleWells;
};

#endif // INSTRUMENTCOORDINATES_H
