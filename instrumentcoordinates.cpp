#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "instrumentcoordinates.h"

namespace {

bool isValidAxis(InstrumentCoordinates::ZAxis axis)
{
    const int value = static_cast<int>(axis);
    return value >= 0
            && value < static_cast<int>(
                InstrumentCoordinates::ZAxis::AxisCount);
}

} // namespace

bool InstrumentCoordinates::TargetPosition::hasTool(ZAxis axis) const
{
    return tool(axis).valid();
}

const InstrumentCoordinates::ToolPose &
InstrumentCoordinates::TargetPosition::tool(ZAxis axis) const
{
    static const ToolPose emptyPose;
    if (!isValidAxis(axis)) {
        return emptyPose;
    }
    return tools[static_cast<int>(axis)];
}

InstrumentCoordinates::InstrumentCoordinates(MachineModel model)
    : m_model(model)
{
    // 按最大机型分配，切换机型时只限制可访问数量，不清掉已有坐标。
    m_channels.resize(kMaxChannels);
    m_tubes.resize(kMaxTubes);
    m_reagents.resize(kReagentPositions);
    m_sampleWells.resize(kMaxSampleWells);
}

MachineModel InstrumentCoordinates::machineModel() const
{
    return m_model;
}

void InstrumentCoordinates::setMachineModel(MachineModel model)
{
    m_model = model;
}

int InstrumentCoordinates::channelCount() const
{
    return machineModuleCount(m_model) * 4;
}

int InstrumentCoordinates::tubeTrayCount() const
{
    return machineTrayCount(m_model);
}

int InstrumentCoordinates::tubeCount() const
{
    return machineTubeCount(m_model);
}

int InstrumentCoordinates::reagentCount() const
{
    return kReagentPositions;
}

int InstrumentCoordinates::sampleWellCount() const
{
    switch (m_model) {
    case MachineModel::KS600:
        return 60;
    case MachineModel::KS800:
        return 80;
    case MachineModel::KS1200:
        return 120;
    }
    return kMaxSampleWells;
}

bool InstrumentCoordinates::setXy(TargetKind kind, int index, ZAxis axis,
                                  double x, double y)
{
    if (!isValidAxis(axis)) {
        return false;
    }
    TargetPosition *target = position(kind, index);
    if (!target) {
        return false;
    }

    ToolPose &pose = target->tools[static_cast<int>(axis)];
    pose.x = x;
    pose.y = y;
    pose.xyValid = true;
    return true;
}

bool InstrumentCoordinates::setZ(TargetKind kind, int index,
                                 ZAxis axis, double z)
{
    if (!isValidAxis(axis)) {
        return false;
    }
    TargetPosition *target = position(kind, index);
    if (!target) {
        return false;
    }

    ToolPose &pose = target->tools[static_cast<int>(axis)];
    pose.z = z;
    pose.zValid = true;
    return true;
}

bool InstrumentCoordinates::xy(TargetKind kind, int index, ZAxis axis,
                               double *x, double *y) const
{
    if (!isValidAxis(axis)) {
        return false;
    }
    const TargetPosition *target = position(kind, index);
    if (!target) {
        return false;
    }

    const ToolPose &pose = target->tools[static_cast<int>(axis)];
    if (!pose.xyValid) {
        return false;
    }
    if (x) {
        *x = pose.x;
    }
    if (y) {
        *y = pose.y;
    }
    return true;
}

bool InstrumentCoordinates::z(TargetKind kind, int index, ZAxis axis,
                              double *z) const
{
    if (!isValidAxis(axis)) {
        return false;
    }
    const TargetPosition *target = position(kind, index);
    if (!target) {
        return false;
    }

    const ToolPose &pose = target->tools[static_cast<int>(axis)];
    if (!pose.zValid) {
        return false;
    }
    if (z) {
        *z = pose.z;
    }
    return true;
}

InstrumentCoordinates::ToolPose InstrumentCoordinates::toolPose(
        TargetKind kind, int index, ZAxis axis) const
{
    if (!isValidAxis(axis)) {
        return ToolPose();
    }
    const TargetPosition *target = position(kind, index);
    if (!target) {
        return ToolPose();
    }
    return target->tools[static_cast<int>(axis)];
}

bool InstrumentCoordinates::setOriginXy(ZAxis axis, double x, double y)
{
    return setXy(TargetKind::Origin, 0, axis, x, y);
}

bool InstrumentCoordinates::setOriginZ(ZAxis axis, double z)
{
    return setZ(TargetKind::Origin, 0, axis, z);
}

bool InstrumentCoordinates::setCleaningHoleXy(ZAxis axis,
                                              double x, double y)
{
    return setXy(TargetKind::CleaningHole, 0, axis, x, y);
}

bool InstrumentCoordinates::setCleaningHoleZ(ZAxis axis, double z)
{
    return setZ(TargetKind::CleaningHole, 0, axis, z);
}

bool InstrumentCoordinates::setDiscardCupXy(ZAxis axis,
                                            double x, double y)
{
    return setXy(TargetKind::DiscardCup, 0, axis, x, y);
}

bool InstrumentCoordinates::setDiscardCupZ(ZAxis axis, double z)
{
    return setZ(TargetKind::DiscardCup, 0, axis, z);
}

bool InstrumentCoordinates::setChannelXy(int channelIndex, ZAxis axis,
                                         double x, double y)
{
    return setXy(TargetKind::Channel, channelIndex, axis, x, y);
}

bool InstrumentCoordinates::setChannelZ(int channelIndex,
                                        ZAxis axis, double z)
{
    return setZ(TargetKind::Channel, channelIndex, axis, z);
}

bool InstrumentCoordinates::channelXy(int channelIndex, ZAxis axis,
                                      double *x, double *y) const
{
    return xy(TargetKind::Channel, channelIndex, axis, x, y);
}

bool InstrumentCoordinates::channelZ(int channelIndex, ZAxis axis,
                                     double *zValue) const
{
    return z(TargetKind::Channel, channelIndex, axis, zValue);
}

bool InstrumentCoordinates::setTubeXy(int tubeIndex, ZAxis axis,
                                      double x, double y)
{
    return setXy(TargetKind::Tube, tubeIndex, axis, x, y);
}

bool InstrumentCoordinates::setTubeZ(int tubeIndex, ZAxis axis, double z)
{
    return setZ(TargetKind::Tube, tubeIndex, axis, z);
}

bool InstrumentCoordinates::tubeXy(int tubeIndex, ZAxis axis,
                                   double *x, double *y) const
{
    return xy(TargetKind::Tube, tubeIndex, axis, x, y);
}

bool InstrumentCoordinates::tubeZ(int tubeIndex, ZAxis axis,
                                  double *zValue) const
{
    return z(TargetKind::Tube, tubeIndex, axis, zValue);
}

bool InstrumentCoordinates::setReagentXy(int reagentIndex, ZAxis axis,
                                         double x, double y)
{
    return setXy(TargetKind::Reagent, reagentIndex, axis, x, y);
}

bool InstrumentCoordinates::setReagentZ(int reagentIndex,
                                        ZAxis axis, double z)
{
    return setZ(TargetKind::Reagent, reagentIndex, axis, z);
}

bool InstrumentCoordinates::reagentXy(int reagentIndex, ZAxis axis,
                                      double *x, double *y) const
{
    return xy(TargetKind::Reagent, reagentIndex, axis, x, y);
}

bool InstrumentCoordinates::reagentZ(int reagentIndex, ZAxis axis,
                                     double *zValue) const
{
    return z(TargetKind::Reagent, reagentIndex, axis, zValue);
}

bool InstrumentCoordinates::setSampleWellXy(int wellIndex, ZAxis axis,
                                            double x, double y)
{
    return setXy(TargetKind::SampleWell, wellIndex, axis, x, y);
}

bool InstrumentCoordinates::setSampleWellZ(int wellIndex,
                                           ZAxis axis, double z)
{
    return setZ(TargetKind::SampleWell, wellIndex, axis, z);
}

bool InstrumentCoordinates::sampleWellXy(int wellIndex, ZAxis axis,
                                         double *x, double *y) const
{
    return xy(TargetKind::SampleWell, wellIndex, axis, x, y);
}

bool InstrumentCoordinates::sampleWellZ(int wellIndex, ZAxis axis,
                                        double *zValue) const
{
    return z(TargetKind::SampleWell, wellIndex, axis, zValue);
}

InstrumentCoordinates::MoveTarget InstrumentCoordinates::locate(
        TargetKind kind, int index, ZAxis axis) const
{
    MoveTarget result;
    result.kind = kind;
    result.index = index;
    result.axis = axis;

    if (!isValidAxis(axis)) {
        return result;
    }
    const TargetPosition *target = position(kind, index);
    if (!target) {
        return result;
    }

    const ToolPose &pose = target->tools[static_cast<int>(axis)];
    result.xyValid = pose.xyValid;
    if (pose.xyValid) {
        result.x = pose.x;
        result.y = pose.y;
    }
    result.zValid = pose.zValid;
    if (pose.zValid) {
        result.z = pose.z;
    }
    return result;
}

InstrumentCoordinates::MoveTarget InstrumentCoordinates::locateOrigin(
        ZAxis axis) const
{
    return locate(TargetKind::Origin, 0, axis);
}

InstrumentCoordinates::MoveTarget InstrumentCoordinates::locateCleaningHole(
        ZAxis axis) const
{
    return locate(TargetKind::CleaningHole, 0, axis);
}

InstrumentCoordinates::MoveTarget InstrumentCoordinates::locateDiscardCup(
        ZAxis axis) const
{
    return locate(TargetKind::DiscardCup, 0, axis);
}

InstrumentCoordinates::MoveTarget InstrumentCoordinates::locateChannel(
        int channelIndex, ZAxis axis) const
{
    return locate(TargetKind::Channel, channelIndex, axis);
}

InstrumentCoordinates::MoveTarget InstrumentCoordinates::locateTube(
        int tubeIndex, ZAxis axis) const
{
    return locate(TargetKind::Tube, tubeIndex, axis);
}

InstrumentCoordinates::MoveTarget InstrumentCoordinates::locateReagent(
        int reagentIndex, ZAxis axis) const
{
    return locate(TargetKind::Reagent, reagentIndex, axis);
}

InstrumentCoordinates::MoveTarget InstrumentCoordinates::locateSampleWell(
        int wellIndex, ZAxis axis) const
{
    return locate(TargetKind::SampleWell, wellIndex, axis);
}

void InstrumentCoordinates::clear()
{
    m_origin = TargetPosition();
    m_cleaningHole = TargetPosition();
    m_discardCup = TargetPosition();
    m_channels.fill(TargetPosition());
    m_tubes.fill(TargetPosition());
    m_reagents.fill(TargetPosition());
    m_sampleWells.fill(TargetPosition());
}

void InstrumentCoordinates::clear(TargetKind kind, int index)
{
    TargetPosition *target = position(kind, index);
    if (target) {
        *target = TargetPosition();
    }
}

void InstrumentCoordinates::clearPose(TargetKind kind, int index,
                                      ZAxis axis)
{
    if (!isValidAxis(axis)) {
        return;
    }
    TargetPosition *target = position(kind, index);
    if (!target) {
        return;
    }
    target->tools[static_cast<int>(axis)] = ToolPose();
}

void InstrumentCoordinates::clearZ(TargetKind kind, int index, ZAxis axis)
{
    if (!isValidAxis(axis)) {
        return;
    }
    TargetPosition *target = position(kind, index);
    if (!target) {
        return;
    }
    ToolPose &pose = target->tools[static_cast<int>(axis)];
    pose.z = 0.0;
    pose.zValid = false;
}

QString InstrumentCoordinates::zAxisName(ZAxis axis)
{
    switch (axis) {
    case ZAxis::Gripper:       return QStringLiteral("抓手Z轴");
    case ZAxis::ReagentNeedle: return QStringLiteral("试剂针Z轴");
    case ZAxis::SampleNeedle:  return QStringLiteral("样本针Z轴");
    case ZAxis::AxisCount:     break;
    }
    return QStringLiteral("未知Z轴");
}

QString InstrumentCoordinates::targetKindName(TargetKind kind)
{
    switch (kind) {
    case TargetKind::Origin:       return QStringLiteral("原点");
    case TargetKind::CleaningHole: return QStringLiteral("清洗孔位");
    case TargetKind::DiscardCup:   return QStringLiteral("弃杯孔位");
    case TargetKind::Channel:      return QStringLiteral("通道");
    case TargetKind::Tube:         return QStringLiteral("试管");
    case TargetKind::Reagent:      return QStringLiteral("试剂位");
    case TargetKind::SampleWell:   return QStringLiteral("样本孔");
    }
    return QStringLiteral("未知位置");
}

QString InstrumentCoordinates::reagentPositionName(int reagentIndex)
{
    switch (reagentIndex) {
    case 0: return QStringLiteral("AA左");
    case 1: return QStringLiteral("ADP左");
    case 2: return QStringLiteral("EPI左");
    case 3: return QStringLiteral("COL左");
    case 4: return QStringLiteral("RIS左");
    case 5: return QStringLiteral("AA右");
    case 6: return QStringLiteral("ADP右");
    case 7: return QStringLiteral("EPI右");
    case 8: return QStringLiteral("COL右");
    case 9: return QStringLiteral("RIS右");
    default: break;
    }
    return QStringLiteral("试剂位 %1").arg(reagentIndex + 1);
}

bool InstrumentCoordinates::validIndex(TargetKind kind, int index) const
{
    switch (kind) {
    case TargetKind::Origin:
    case TargetKind::CleaningHole:
    case TargetKind::DiscardCup:
        // 特殊位置只有一个，index 参数忽略。
        return true;
    case TargetKind::Channel:
        return index >= 0 && index < channelCount();
    case TargetKind::Tube:
        return index >= 0 && index < tubeCount();
    case TargetKind::Reagent:
        return index >= 0 && index < reagentCount();
    case TargetKind::SampleWell:
        return index >= 0 && index < sampleWellCount();
    }
    return false;
}

InstrumentCoordinates::TargetPosition *InstrumentCoordinates::position(
        TargetKind kind, int index)
{
    switch (kind) {
    case TargetKind::Origin:
        return &m_origin;
    case TargetKind::CleaningHole:
        return &m_cleaningHole;
    case TargetKind::DiscardCup:
        return &m_discardCup;
    case TargetKind::Channel:
        return validIndex(kind, index) ? &m_channels[index] : nullptr;
    case TargetKind::Tube:
        return validIndex(kind, index) ? &m_tubes[index] : nullptr;
    case TargetKind::Reagent:
        return validIndex(kind, index) ? &m_reagents[index] : nullptr;
    case TargetKind::SampleWell:
        return validIndex(kind, index) ? &m_sampleWells[index] : nullptr;
    }
    return nullptr;
}

const InstrumentCoordinates::TargetPosition *InstrumentCoordinates::position(
        TargetKind kind, int index) const
{
    switch (kind) {
    case TargetKind::Origin:
        return &m_origin;
    case TargetKind::CleaningHole:
        return &m_cleaningHole;
    case TargetKind::DiscardCup:
        return &m_discardCup;
    case TargetKind::Channel:
        return validIndex(kind, index) ? &m_channels[index] : nullptr;
    case TargetKind::Tube:
        return validIndex(kind, index) ? &m_tubes[index] : nullptr;
    case TargetKind::Reagent:
        return validIndex(kind, index) ? &m_reagents[index] : nullptr;
    case TargetKind::SampleWell:
        return validIndex(kind, index) ? &m_sampleWells[index] : nullptr;
    }
    return nullptr;
}
