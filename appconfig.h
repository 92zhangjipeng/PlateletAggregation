#ifndef APPCONFIG_H
#define APPCONFIG_H

#include "MachineModel.h"

#include <QSettings>
#include <QString>
#include <QtGlobal>

/** 基础参数中“样本质量”的 QSettings key */
inline const char *sampleQualitySettingsKey()
{
    return "basic/sampleQuality";
}

inline QString sampleQualityFromSettings()
{
    QSettings settings;
    return settings.value(QString::fromLatin1(sampleQualitySettingsKey()),
                          QStringLiteral("wholeBlood"))
            .toString();
}

inline bool sampleQualityIsPlasma()
{
    return sampleQualityFromSettings() == QLatin1String("plasma");
}

/**
 * 血浆模式下样本针吸 PRP 的固定下降高度（mm）。
 * 血浆模式不做测高，加样列表的「吸样高度」直接取这个值。
 */
inline const char *plasmaPrpAspirateHeightSettingsKey()
{
    return "basic/plasmaPrpAspirateHeight";
}

inline int plasmaPrpAspirateHeightFromSettings()
{
    QSettings settings;
    const int value = settings.value(
                QString::fromLatin1(plasmaPrpAspirateHeightSettingsKey()),
                32).toInt();
    return qBound(20, value, 99);
}

inline void setPlasmaPrpAspirateHeightInSettings(int heightMm)
{
    QSettings settings;
    settings.setValue(
                QString::fromLatin1(plasmaPrpAspirateHeightSettingsKey()),
                qBound(20, heightMm, 99));
}

/**
 * 开机灌注时柱塞泵吐回清洗池的冲洗时间（ms），对应老软件 Config.ini 的
 * WashesTime。默认 6000ms，设置范围 500~65535ms。
 */
inline const char *perfusionCleaningTimeSettingsKey()
{
    return "basic/perfusionCleaningTimeMs";
}

inline int perfusionCleaningTimeMsFromSettings()
{
    QSettings settings;
    const int value = settings.value(
                QString::fromLatin1(perfusionCleaningTimeSettingsKey()),
                6000).toInt();
    return qBound(500, value, 65535);
}

inline void setPerfusionCleaningTimeMsInSettings(int ms)
{
    QSettings settings;
    settings.setValue(
                QString::fromLatin1(perfusionCleaningTimeSettingsKey()),
                qBound(500, ms, 65535));
}

/**
 * 各模组的目标温度（℃），默认 35℃。可在维护设置 → 模组控制页
 * 每个模组单独设置；开机升温按该目标判断是否到位。
 */
inline const char *moduleTargetTemperatureSettingsKey(int moduleNumber)
{
    static const char *keys[] = {
        "module/targetTemperature1",
        "module/targetTemperature2",
        "module/targetTemperature3"
    };
    return keys[qBound(1, moduleNumber, 3) - 1];
}

inline double moduleTargetTemperatureFromSettings(int moduleNumber)
{
    QSettings settings;
    const double value = settings.value(
                QString::fromLatin1(
                    moduleTargetTemperatureSettingsKey(moduleNumber)),
                35.0).toDouble();
    return qBound(0.0, value, 100.0);
}

inline void setModuleTargetTemperatureInSettings(int moduleNumber,
                                                 double celsius)
{
    QSettings settings;
    settings.setValue(
                QString::fromLatin1(
                    moduleTargetTemperatureSettingsKey(moduleNumber)),
                qBound(0.0, celsius, 100.0));
}

/**
 * 开机模组升温超时（秒）。默认 60 秒（1 分钟），设置范围 10~3600 秒。
 * 超时后启动校准中止；升温提示框右下角显示剩余时间倒计时。
 */
inline const char *moduleHeatingTimeoutSecondsSettingsKey()
{
    return "basic/moduleHeatingTimeoutSeconds";
}

inline int moduleHeatingTimeoutSecondsFromSettings()
{
    QSettings settings;
    const int value = settings.value(
                QString::fromLatin1(moduleHeatingTimeoutSecondsSettingsKey()),
                60).toInt();
    return qBound(10, value, 3600);
}

inline void setModuleHeatingTimeoutSecondsInSettings(int seconds)
{
    QSettings settings;
    settings.setValue(
                QString::fromLatin1(moduleHeatingTimeoutSecondsSettingsKey()),
                qBound(10, seconds, 3600));
}

/** 基础参数中“加样扫码”的 QSettings key：开启后每添加一个样本必须扫码 */
inline const char *addSampleScanBarcodeSettingsKey()
{
    return "basic/addSampleScanBarcode";
}

inline bool addSampleScanBarcodeEnabled()
{
    QSettings settings;
    return settings.value(
                QString::fromLatin1(addSampleScanBarcodeSettingsKey()), false)
            .toBool();
}

inline void setAddSampleScanBarcodeEnabled(bool enabled)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(addSampleScanBarcodeSettingsKey()),
                      enabled);
}

/** 测试项目常用组合：QStringList，每项为 "AA+ADP" 形式 */
inline const char *testItemCombinationsSettingsKey()
{
    return "basic/testItemCombinations";
}

/** 仪器参数：测试模式。pppPrp=PPP+PRP（每样本2孔），prp=PRP（每样本1孔）。 */
inline const char *testModeSettingsKey()
{
    return "instrument/testMode";
}

inline bool testModeIsPppPrp()
{
    QSettings settings;
    return settings.value(QString::fromLatin1(testModeSettingsKey()),
                          QStringLiteral("prp"))
            .toString() == QLatin1String("pppPrp");
}

inline void setTestModePppPrp(bool enabled)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(testModeSettingsKey()),
                      enabled ? QStringLiteral("pppPrp")
                              : QStringLiteral("prp"));
}

/** 仪器参数：机型（KS600 / KS800 / KS1200） */
inline const char *machineModelSettingsKey()
{
    return "instrument/machineModel";
}

inline MachineModel machineModelFromSettings()
{
    QSettings settings;
    const QString name = settings.value(
                QString::fromLatin1(machineModelSettingsKey()),
                QString::fromLatin1(machineModelName(defaultMachineModel())))
            .toString();
    if (name == QLatin1String("KS600")) {
        return MachineModel::KS600;
    }
    if (name == QLatin1String("KS800")) {
        return MachineModel::KS800;
    }
    if (name == QLatin1String("KS1200")) {
        return MachineModel::KS1200;
    }
    return defaultMachineModel();
}

inline void setMachineModelInSettings(MachineModel model)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(machineModelSettingsKey()),
                      QString::fromLatin1(machineModelName(model)));
}

/** 仪器参数：通道测试模式。同通道 / 不同通道（目前只保存，测试逻辑后续接入）。 */
inline const char *channelTestModeSettingsKey()
{
    return "instrument/channelTestMode";
}

inline bool channelTestModeIsSameChannel()
{
    QSettings settings;
    return settings.value(QString::fromLatin1(channelTestModeSettingsKey()),
                          QStringLiteral("same")).toString()
            == QLatin1String("same");
}

inline void setChannelTestModeSameChannel(bool sameChannel)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(channelTestModeSettingsKey()),
                      sameChannel ? QStringLiteral("same")
                                  : QStringLiteral("different"));
}

/** 按机型保存每个全局通道的启用/禁用状态 */
inline QString channelStateSettingsKey(MachineModel model, int globalChannel)
{
    return QStringLiteral("channelStates/%1/CH%2")
            .arg(QString::fromLatin1(machineModelName(model)))
            .arg(globalChannel);
}

inline bool channelEnabledFromSettings(MachineModel model, int globalChannel)
{
    QSettings settings;
    return settings.value(channelStateSettingsKey(model, globalChannel), true)
            .toBool();
}

inline void setChannelEnabledInSettings(MachineModel model, int globalChannel,
                                        bool enabled)
{
    QSettings settings;
    settings.setValue(channelStateSettingsKey(model, globalChannel), enabled);
}

#endif // APPCONFIG_H
