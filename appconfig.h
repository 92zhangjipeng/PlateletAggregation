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
