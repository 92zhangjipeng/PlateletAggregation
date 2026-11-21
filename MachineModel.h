#ifndef MACHINEMODEL_H
#define MACHINEMODEL_H

#include <QMetaType>
#include <QString>
#include <QtGlobal>

/** 仪器机型；机型读取协议接入前先用默认值 */
enum class MachineModel {
    KS600 = 0,
    KS800,
    KS1200
};

Q_DECLARE_METATYPE(MachineModel)

/** 机型显示名，例如 "KS1200" */
inline const char *machineModelName(MachineModel model)
{
    switch (model) {
        case MachineModel::KS600:  return "KS600";
        case MachineModel::KS800:  return "KS800";
        case MachineModel::KS1200: return "KS1200";
    }
    return "KS1200";
}

/** KS600 = 1 个模组，KS800 = 2 个，KS1200 = 3 个 */
inline int machineModuleCount(MachineModel model)
{
    switch (model) {
        case MachineModel::KS600:  return 1;
        case MachineModel::KS800:  return 2;
        case MachineModel::KS1200: return 3;
    }
    return 3;
}

/** KS600 = 2 个试管盘，KS800 = 3 个，KS1200 = 4 个；每盘 60 个试管 */
inline int machineTrayCount(MachineModel model)
{
    switch (model) {
        case MachineModel::KS600:  return 2;
        case MachineModel::KS800:  return 3;
        case MachineModel::KS1200: return 4;
    }
    return 4;
}

inline int machineTubeCount(MachineModel model)
{
    return machineTrayCount(model) * 60;
}

inline MachineModel defaultMachineModel()
{
    // 机型读取协议尚未接入，暂时按 KS1200（3 个模组）运行
    return MachineModel::KS1200;
}

#endif // MACHINEMODEL_H
