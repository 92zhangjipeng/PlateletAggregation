#ifndef APPCONFIG_H
#define APPCONFIG_H

#include "MachineModel.h"

#include <QSettings>
#include <QString>

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
