#ifndef INSTRUMENTPARAMETERS_H
#define INSTRUMENTPARAMETERS_H

#include "mainboardconsumables.h"

#include <QSettings>
#include <QString>
#include <QtGlobal>

/**
 * 仪器运动/泵/抓手/耗材容量参数。
 *
 * 全部保存在 QSettings，设置界面修改后立即生效；动作层在每次动作前读取。
 * 这里只做参数和换算，不发送任何运动命令。
 *
 * 容量换算（主板 512 比例）：
 *   百分比   = ratio / 512 * 100
 *   剩余 µL  = 容量µL * ratio / 512
 *   写回比例 = round(剩余µL / 容量µL * 512)，限制 0~512
 * 换满瓶时写 512（100%），容量只参与软件侧 µL 换算，不写入主板比例字段。
 */

// ---------------- 泵量换算 ----------------

/**
 * 样本泵（血样泵）：2026-11 硬件更新，容量由 1000 µL 改为 2500 µL，
 * 但满行程总步数不变（仍为 7200 步）。
 * 因此默认换算 = 7200 / 2500 = 2.88 步/µL。
 */
constexpr int kSamplePumpFullScaleSteps = 7200;
constexpr double kSamplePumpCapacityUl = 2500.0;

/** 血样泵（大泵）步/µL；7200 步对应 2500 µL，即 2.88。 */
inline const char *bloodPumpStepsPerUlSettingsKey()
{
    return "pumps/bloodStepsPerUl";
}

inline double bloodPumpStepsPerUl()
{
    QSettings settings;
    const double value = settings.value(
                QString::fromLatin1(bloodPumpStepsPerUlSettingsKey()),
                kSamplePumpFullScaleSteps / kSamplePumpCapacityUl)
            .toDouble();
    return qBound(0.1, value, 100.0);
}

inline void setBloodPumpStepsPerUl(double stepsPerUl)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(bloodPumpStepsPerUlSettingsKey()),
                      qBound(0.1, stepsPerUl, 100.0));
}

/** 试剂泵（小泵）步/µL；7200 步对应 100 µL，即 72。 */
inline const char *reagentPumpStepsPerUlSettingsKey()
{
    return "pumps/reagentStepsPerUl";
}

inline double reagentPumpStepsPerUl()
{
    QSettings settings;
    const double value = settings.value(
                QString::fromLatin1(reagentPumpStepsPerUlSettingsKey()), 72.0)
            .toDouble();
    return qBound(1.0, value, 1000.0);
}

inline void setReagentPumpStepsPerUl(double stepsPerUl)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(reagentPumpStepsPerUlSettingsKey()),
                      qBound(1.0, stepsPerUl, 1000.0));
}

/** µL -> 泵步数（四舍五入，动作层再按泵行程 0~7200 做边界检查） */
inline int pumpStepsForUl(double ul, double stepsPerUl)
{
    if (ul <= 0.0 || stepsPerUl <= 0.0) {
        return 0;
    }
    return qRound(ul * stepsPerUl);
}

/** 泵步数 -> µL */
inline double pumpUlForSteps(int steps, double stepsPerUl)
{
    if (steps <= 0 || stepsPerUl <= 0.0) {
        return 0.0;
    }
    return static_cast<double>(steps) / stepsPerUl;
}

// ---------------- 单次吸样量 ----------------

/** 样本针单次吸样量（µL），老软件默认 TaketheSampleSize=250。 */
inline const char *sampleAspirateVolumeSettingsKey()
{
    return "pumps/sampleAspirateUl";
}

inline int sampleAspirateVolumeUl()
{
    QSettings settings;
    return qBound(1, settings.value(
                      QString::fromLatin1(sampleAspirateVolumeSettingsKey()),
                      250).toInt(),
                  static_cast<int>(kSamplePumpCapacityUl));
}

inline void setSampleAspirateVolumeUl(int ul)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(sampleAspirateVolumeSettingsKey()),
                      qBound(1, ul,
                             static_cast<int>(kSamplePumpCapacityUl)));
}

/** PRP 每个测试项目吸的单份量（µL），老软件默认按 45 µL 量级。 */
inline const char *samplePrpDoseSettingsKey()
{
    return "pumps/samplePrpDoseUl";
}

inline int samplePrpDoseUl()
{
    QSettings settings;
    return qBound(1, settings.value(
                      QString::fromLatin1(samplePrpDoseSettingsKey()),
                      45).toInt(), 500);
}

inline void setSamplePrpDoseUl(int ul)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(samplePrpDoseSettingsKey()),
                      qBound(1, ul, 500));
}

/** 试剂单份量（µL），老软件 AA~RIS 默认 10 µL。 */
inline const char *reagentDoseVolumeSettingsKey()
{
    return "pumps/reagentDoseUl";
}

inline int reagentDoseVolumeUl()
{
    QSettings settings;
    return qBound(1, settings.value(
                      QString::fromLatin1(reagentDoseVolumeSettingsKey()),
                      10).toInt(), 100);
}

inline void setReagentDoseVolumeUl(int ul)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(reagentDoseVolumeSettingsKey()),
                      qBound(1, ul, 100));
}

/** 洗样本针吸清洗液量（µL），老软件默认 250。 */
inline const char *sampleWashVolumeSettingsKey()
{
    return "pumps/sampleWashUl";
}

inline int sampleWashVolumeUl()
{
    QSettings settings;
    // 清洗动作必须吸液，0 µL 会导致构帧失败；下限按 1 µL 处理。
    return qBound(1, settings.value(
                      QString::fromLatin1(sampleWashVolumeSettingsKey()),
                      250).toInt(), 2000);
}

inline void setSampleWashVolumeUl(int ul)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(sampleWashVolumeSettingsKey()),
                      qBound(1, ul, 2000));
}

/** 洗试剂针吸清洗液量（µL），老软件默认 12。 */
inline const char *reagentWashVolumeSettingsKey()
{
    return "pumps/reagentWashUl";
}

inline int reagentWashVolumeUl()
{
    QSettings settings;
    // 清洗动作必须吸液，0 µL 会导致构帧失败；下限按 1 µL 处理。
    return qBound(1, settings.value(
                      QString::fromLatin1(reagentWashVolumeSettingsKey()),
                      12).toInt(), 100);
}

inline void setReagentWashVolumeUl(int ul)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(reagentWashVolumeSettingsKey()),
                      qBound(1, ul, 100));
}

// ---------------- 抓手负压 ----------------

/** 抓杯成功：负压值 <= 该阈值；老软件默认 GripperSuctioncupMax=1597。 */
inline const char *gripperGrabVacuumMaxSettingsKey()
{
    return "gripper/grabVacuumMax";
}

inline int gripperGrabVacuumMax()
{
    QSettings settings;
    return qBound(0, settings.value(
                      QString::fromLatin1(
                          gripperGrabVacuumMaxSettingsKey()),
                      1597).toInt(), 65535);
}

inline void setGripperGrabVacuumMax(int value)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(
                          gripperGrabVacuumMaxSettingsKey()),
                      qBound(0, value, 65535));
}

/** 松杯成功：负压值 >= 该阈值；老软件默认 GripperSplitcupMin=2717。 */
inline const char *gripperReleaseVacuumMinSettingsKey()
{
    return "gripper/releaseVacuumMin";
}

inline int gripperReleaseVacuumMin()
{
    QSettings settings;
    return qBound(0, settings.value(
                      QString::fromLatin1(
                          gripperReleaseVacuumMinSettingsKey()),
                      2717).toInt(), 65535);
}

inline void setGripperReleaseVacuumMin(int value)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(
                          gripperReleaseVacuumMinSettingsKey()),
                      qBound(0, value, 65535));
}

/** 开负压后等待多久再判负压（ms），老软件默认 TimelapseSucking=500。 */
inline const char *gripperSuctionDelaySettingsKey()
{
    return "gripper/suctionDelayMs";
}

inline int gripperSuctionDelayMs()
{
    QSettings settings;
    return qBound(0, settings.value(
                      QString::fromLatin1(gripperSuctionDelaySettingsKey()),
                      500).toInt(), 10000);
}

inline void setGripperSuctionDelayMs(int ms)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(gripperSuctionDelaySettingsKey()),
                      qBound(0, ms, 10000));
}

inline bool gripperGrabSucceeded(quint32 vacuum)
{
    return vacuum <= static_cast<quint32>(gripperGrabVacuumMax());
}

inline bool gripperReleaseSucceeded(quint32 vacuum)
{
    return vacuum >= static_cast<quint32>(gripperReleaseVacuumMin());
}

/**
 * 抓杯过程中“仍然吸牢”的判定。
 * 抓住线贴近吸住时的单次读数，单次读数会略高，所以：
 * - ≤ 抓杯阈值：一定吸牢；
 * - ≥ 松杯阈值：一定没吸住；
 * - 介于两者之间：只把中点以下当成吸牢（老软件 isHandsVacuumStillSealed）。
 */
inline bool gripperStillHolding(quint32 vacuum)
{
    const quint32 grab = static_cast<quint32>(gripperGrabVacuumMax());
    if (vacuum <= grab) {
        return true;
    }
    const quint32 release =
            static_cast<quint32>(gripperReleaseVacuumMin());
    if (release <= grab) {
        return false;
    }
    const quint32 mid = grab + (release - grab) / 2;
    return vacuum <= mid;
}

/** 弃杯关阀后等待负压释放的时间（ms），老软件默认 GripperThrowDelayMs=600。 */
inline const char *gripperReleaseDelaySettingsKey()
{
    return "gripper/releaseDelayMs";
}

inline int gripperReleaseDelayMs()
{
    QSettings settings;
    return qBound(0, settings.value(
                      QString::fromLatin1(gripperReleaseDelaySettingsKey()),
                      600).toInt(), 10000);
}

inline void setGripperReleaseDelayMs(int ms)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(gripperReleaseDelaySettingsKey()),
                      qBound(0, ms, 10000));
}

/** 抓杯/弃杯失败后的重试次数，默认 2（共尝试 3 次）。 */
inline const char *gripperRetryCountSettingsKey()
{
    return "gripper/retryCount";
}

/** 抓手速度模式下压吸杯的速度（1~7200），默认 100（与 Z 复位速度一致）。 */
inline const char *gripperGrabSpeedSettingsKey()
{
    return "gripper/grabSpeed";
}

inline int gripperGrabSpeed()
{
    QSettings settings;
    return qBound(1, settings.value(
                      QString::fromLatin1(gripperGrabSpeedSettingsKey()),
                      100).toInt(), 7200);
}

inline void setGripperGrabSpeed(int speed)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(gripperGrabSpeedSettingsKey()),
                      qBound(1, speed, 7200));
}

inline int gripperRetryCount()
{
    QSettings settings;
    return qBound(0, settings.value(
                      QString::fromLatin1(gripperRetryCountSettingsKey()),
                      2).toInt(), 9);
}

inline void setGripperRetryCount(int count)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(gripperRetryCountSettingsKey()),
                      qBound(0, count, 9));
}

// ---------------- 液面探测 / 空气段 / 延时 ----------------

inline const char *sampleSuckLiftDelaySettingsKey()
{
    return "sample/suckLiftDelayMs";
}

inline int sampleSuckLiftDelayMs()
{
    QSettings settings;
    return qBound(0, settings.value(
                      QString::fromLatin1(sampleSuckLiftDelaySettingsKey()),
                      300).toInt(), 5000);
}

inline void setSampleSuckLiftDelayMs(int ms)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(sampleSuckLiftDelaySettingsKey()),
                      qBound(0, ms, 5000));
}

inline const char *sampleSpitLiftDelaySettingsKey()
{
    return "sample/spitLiftDelayMs";
}

inline int sampleSpitLiftDelayMs()
{
    QSettings settings;
    return qBound(0, settings.value(
                      QString::fromLatin1(sampleSpitLiftDelaySettingsKey()),
                      500).toInt(), 5000);
}

inline void setSampleSpitLiftDelayMs(int ms)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(sampleSpitLiftDelaySettingsKey()),
                      qBound(0, ms, 5000));
}

/** 样本针吸样本液面探测失败高度（mm），老软件 AnaemiaDetectionDownHigh=77。 */
inline const char *sampleDetectionFailHeightSettingsKey()
{
    return "sample/detectionFailHeightMm";
}

inline int sampleDetectionFailHeightMm()
{
    QSettings settings;
    return qBound(0, settings.value(
                      QString::fromLatin1(
                          sampleDetectionFailHeightSettingsKey()),
                      77).toInt(), 150);
}

inline void setSampleDetectionFailHeightMm(int mm)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(
                          sampleDetectionFailHeightSettingsKey()),
                      qBound(0, mm, 150));
}

/** 洗样本针吸清洗液时探液失败高度（mm），老软件默认 73。 */
inline const char *sampleWashDetectionFailHeightSettingsKey()
{
    return "sample/washDetectionFailHeightMm";
}

inline int sampleWashDetectionFailHeightMm()
{
    QSettings settings;
    return qBound(0, settings.value(
                      QString::fromLatin1(
                          sampleWashDetectionFailHeightSettingsKey()),
                      73).toInt(), 150);
}

inline void setSampleWashDetectionFailHeightMm(int mm)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(
                          sampleWashDetectionFailHeightSettingsKey()),
                      qBound(0, mm, 150));
}

/** 清洗双针在机器原点吐液时双针下降高度（mm），老软件 CLEANING_DOWNHIGN=45。 */
inline const char *cleaningDumpHeightSettingsKey()
{
    return "cleaning/dumpHeightMm";
}

inline int cleaningDumpHeightMm()
{
    QSettings settings;
    return qBound(0, settings.value(
                      QString::fromLatin1(cleaningDumpHeightSettingsKey()),
                      45).toInt(), 150);
}

inline void setCleaningDumpHeightMm(int mm)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(cleaningDumpHeightSettingsKey()),
                      qBound(0, mm, 150));
}

/** 采血管口到针尖原点的偏移（mm），老软件 BloodNeedleTubeMouthMm=12。 */
inline const char *sampleTubeMouthSettingsKey()
{
    return "sample/tubeMouthMm";
}

inline int sampleTubeMouthMm()
{
    QSettings settings;
    return qBound(0, settings.value(
                      QString::fromLatin1(sampleTubeMouthSettingsKey()),
                      12).toInt(), 99);
}

inline void setSampleTubeMouthMm(int mm)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(sampleTubeMouthSettingsKey()),
                      qBound(0, mm, 99));
}

inline const char *reagentSuckLiftDelaySettingsKey()
{
    return "reagent/suckLiftDelayMs";
}

inline int reagentSuckLiftDelayMs()
{
    QSettings settings;
    return qBound(0, settings.value(
                      QString::fromLatin1(reagentSuckLiftDelaySettingsKey()),
                      300).toInt(), 5000);
}

inline void setReagentSuckLiftDelayMs(int ms)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(reagentSuckLiftDelaySettingsKey()),
                      qBound(0, ms, 5000));
}

inline const char *reagentSpitLiftDelaySettingsKey()
{
    return "reagent/spitLiftDelayMs";
}

inline int reagentSpitLiftDelayMs()
{
    QSettings settings;
    return qBound(0, settings.value(
                      QString::fromLatin1(reagentSpitLiftDelaySettingsKey()),
                      300).toInt(), 5000);
}

inline void setReagentSpitLiftDelayMs(int ms)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(reagentSpitLiftDelaySettingsKey()),
                      qBound(0, ms, 5000));
}

/** 洗试剂针吸清洗液时探液失败高度（mm），老软件默认 78。 */
inline const char *reagentWashDetectionFailHeightSettingsKey()
{
    return "reagent/washDetectionFailHeightMm";
}

inline int reagentWashDetectionFailHeightMm()
{
    QSettings settings;
    return qBound(0, settings.value(
                      QString::fromLatin1(
                          reagentWashDetectionFailHeightSettingsKey()),
                      78).toInt(), 150);
}

inline void setReagentWashDetectionFailHeightMm(int mm)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(
                          reagentWashDetectionFailHeightSettingsKey()),
                      qBound(0, mm, 150));
}

/** 试剂瓶口到针尖原点的偏移（mm），老软件 ReagentBottleMouthMm=20。 */
inline const char *reagentBottleMouthSettingsKey()
{
    return "reagent/bottleMouthMm";
}

inline int reagentBottleMouthMm()
{
    QSettings settings;
    return qBound(0, settings.value(
                      QString::fromLatin1(reagentBottleMouthSettingsKey()),
                      20).toInt(), 99);
}

inline void setReagentBottleMouthMm(int mm)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(reagentBottleMouthSettingsKey()),
                      qBound(0, mm, 99));
}

// ---------------- 单试剂吸样量 / 探液失败高度 ----------------
// UI 顺序：AA、ADP、EPI、COL、RIS（与主板 AA/EPI/ADP 顺序不同，单独映射）。

inline int clampedReagentUiIndex(int index)
{
    return qBound(0, index, 4);
}

inline const char *reagentUiSettingsSegment(int index)
{
    static const char *segments[] = { "AA", "ADP", "EPI", "COL", "RIS" };
    return segments[clampedReagentUiIndex(index)];
}

/** 测试项目名 AA/ADP/EPI/COL/RIS -> 试剂 UI 序号 0~4；未知返回 -1 */
inline int reagentNameUiIndex(const QString &name)
{
    const QString key = name.trimmed().toUpper();
    if (key == QLatin1String("AA"))  return 0;
    if (key == QLatin1String("ADP")) return 1;
    if (key == QLatin1String("EPI")) return 2;
    if (key == QLatin1String("COL")) return 3;
    if (key == QLatin1String("RIS")) return 4;
    return -1;
}

/** UI 试剂序号 -> 主板耗材位置 */
inline int reagentUiToConsumablePosition(int index)
{
    static const int positions[] = {
        MainBoardConsumable::AA,
        MainBoardConsumable::ADP,
        MainBoardConsumable::EPI,
        MainBoardConsumable::COL,
        MainBoardConsumable::RIS
    };
    return positions[clampedReagentUiIndex(index)];
}

inline const char *reagentVolumeSettingsKey(int index)
{
    static const char *keys[] = {
        "reagent/AA/volumeUl",
        "reagent/ADP/volumeUl",
        "reagent/EPI/volumeUl",
        "reagent/COL/volumeUl",
        "reagent/RIS/volumeUl"
    };
    return keys[clampedReagentUiIndex(index)];
}

inline int reagentVolumeUl(int index)
{
    QSettings settings;
    return qBound(1, settings.value(
                      QString::fromLatin1(reagentVolumeSettingsKey(index)),
                      10).toInt(), 100);
}

inline void setReagentVolumeUl(int index, int ul)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(reagentVolumeSettingsKey(index)),
                      qBound(1, ul, 100));
}

inline const char *reagentDetectionFailHeightSettingsKey(int index)
{
    static const char *keys[] = {
        "reagent/AA/detectionFailHeightMm",
        "reagent/ADP/detectionFailHeightMm",
        "reagent/EPI/detectionFailHeightMm",
        "reagent/COL/detectionFailHeightMm",
        "reagent/RIS/detectionFailHeightMm"
    };
    return keys[clampedReagentUiIndex(index)];
}

inline int reagentDetectionFailHeightMm(int index)
{
    QSettings settings;
    return qBound(0, settings.value(
                      QString::fromLatin1(
                          reagentDetectionFailHeightSettingsKey(index)),
                      73).toInt(), 150);
}

inline void setReagentDetectionFailHeightMm(int index, int mm)
{
    QSettings settings;
    settings.setValue(
                QString::fromLatin1(
                    reagentDetectionFailHeightSettingsKey(index)),
                qBound(0, mm, 150));
}

// ---------------- 液面探测防挂珠判定 ----------------

/**
 * 老软件防挂珠逻辑：液面探测成功返回一个高度。
 * - 探到的高度比「针尖到瓶口/管口」浅 → 判为针尖挂珠，
 *   不能在这里吸，要下到探液失败高度再吸；
 * - 探到瓶口/管口或更深 → 就在探到的液面吸；
 * - 一直探不到（detectedHeightMm <= 0）→ 下到失败高度吸。
 *
 * 高度方向：数值越大越靠下。mouthHeightMm 为 0 时不判挂珠。
 */
inline bool liquidLevelIsHangingDrop(int detectedHeightMm,
                                     int mouthHeightMm)
{
    // 与老软件一致：回包高度 <= 瓶口/管口偏移即判为针尖挂珠。
    // 调用方应先确认“液面探测成功”（Z 轴回包 axisByte bit7 = 1）。
    return detectedHeightMm >= 0
            && mouthHeightMm > 0
            && detectedHeightMm <= mouthHeightMm;
}

inline int liquidLevelAspirateHeightMm(int detectedHeightMm,
                                       int mouthHeightMm,
                                       int failHeightMm)
{
    if (detectedHeightMm <= 0) {
        return failHeightMm;
    }
    if (liquidLevelIsHangingDrop(detectedHeightMm, mouthHeightMm)) {
        return failHeightMm;
    }
    return detectedHeightMm;
}

// ---------------- 预备试剂位 ----------------

/**
 * 启用预备试剂位：当前使用的试剂位单瓶余量降到该试剂的
 * 单瓶%报警限后，自动切换到另一侧（左 <-> 右）继续测试。
 * 关闭时测试暂停并提示更换试剂。默认关闭。
 */
inline const char *reagentBackupPositionSettingsKey()
{
    return "consumables/enableBackupReagent";
}

inline bool reagentBackupPositionEnabled()
{
    QSettings settings;
    return settings.value(
                QString::fromLatin1(reagentBackupPositionSettingsKey()),
                false).toBool();
}

inline void setReagentBackupPositionEnabled(bool enabled)
{
    QSettings settings;
    settings.setValue(
                QString::fromLatin1(reagentBackupPositionSettingsKey()),
                enabled);
}

// ---------------- 单瓶 / 总瓶数报警限 ----------------

// 容量分组函数定义在本文件后部，这里先声明，供报警限 key 映射使用。
inline int consumableCapacityIndex(int position);
inline double mainboardRatioToUl(int ratio, double capacityUl);

inline const char *consumableSingleAlarmSettingsKey(int position)
{
    const int index = consumableCapacityIndex(position);
    static const char *keys[] = {
        "consumables/limitPercent/AA",
        "consumables/limitPercent/EPI",
        "consumables/limitPercent/ADP",
        "consumables/limitPercent/COL",
        "consumables/limitPercent/RIS",
        "consumables/limitPercent/S1",
        "consumables/limitPercent/S2"
    };
    return index < 0 ? "consumables/limitPercent/none" : keys[index];
}

inline int consumableSingleAlarmPercent(int position)
{
    if (consumableCapacityIndex(position) < 0) {
        return 0;
    }
    QSettings settings;
    return qBound(0, settings.value(
                      QString::fromLatin1(
                          consumableSingleAlarmSettingsKey(position)),
                      5).toInt(), 100);
}

inline void setConsumableSingleAlarmPercent(int position, int percent)
{
    if (consumableCapacityIndex(position) < 0) {
        return;
    }
    QSettings settings;
    settings.setValue(
                QString::fromLatin1(
                    consumableSingleAlarmSettingsKey(position)),
                qBound(0, percent, 100));
}

inline const char *consumableBottleAlarmSettingsKey(int position)
{
    const int index = consumableCapacityIndex(position);
    static const char *keys[] = {
        "consumables/bottleLimit/AA",
        "consumables/bottleLimit/EPI",
        "consumables/bottleLimit/ADP",
        "consumables/bottleLimit/COL",
        "consumables/bottleLimit/RIS",
        "consumables/bottleLimit/S1",
        "consumables/bottleLimit/S2"
    };
    return index < 0 ? "consumables/bottleLimit/none" : keys[index];
}

inline int consumableBottleAlarm(int position)
{
    if (consumableCapacityIndex(position) < 0) {
        return 0;
    }
    QSettings settings;
    return qBound(0, settings.value(
                      QString::fromLatin1(
                          consumableBottleAlarmSettingsKey(position)),
                      5).toInt(), 255);
}

inline void setConsumableBottleAlarm(int position, int bottles)
{
    if (consumableCapacityIndex(position) < 0) {
        return;
    }
    QSettings settings;
    settings.setValue(
                QString::fromLatin1(
                    consumableBottleAlarmSettingsKey(position)),
                qBound(0, bottles, 255));
}

/**
 * 总剩余百分比报警限。
 * 主板刷卡满量按 100 瓶计：总剩余% = 剩余整瓶数 + 当前瓶比例/512，
 * 例如 2 瓶 + 当前瓶 50% = 2.5%（满 100 瓶为 100%）。
 */
inline const char *consumableTotalPercentAlarmSettingsKey(int position)
{
    const int index = consumableCapacityIndex(position);
    static const char *keys[] = {
        "consumables/totalPercentLimit/AA",
        "consumables/totalPercentLimit/EPI",
        "consumables/totalPercentLimit/ADP",
        "consumables/totalPercentLimit/COL",
        "consumables/totalPercentLimit/RIS",
        "consumables/totalPercentLimit/S1",
        "consumables/totalPercentLimit/S2"
    };
    return index < 0 ? "consumables/totalPercentLimit/none" : keys[index];
}

inline int consumableTotalPercentAlarm(int position)
{
    if (consumableCapacityIndex(position) < 0) {
        return 0;
    }
    QSettings settings;
    return qBound(0, settings.value(
                      QString::fromLatin1(
                          consumableTotalPercentAlarmSettingsKey(position)),
                      5).toInt(), 100);
}

inline void setConsumableTotalPercentAlarm(int position, int percent)
{
    if (consumableCapacityIndex(position) < 0) {
        return;
    }
    QSettings settings;
    settings.setValue(
                QString::fromLatin1(
                    consumableTotalPercentAlarmSettingsKey(position)),
                qBound(0, percent, 100));
}

/** 总剩余%：满量按 100 瓶计，返回 0~100。 */
inline double consumableTotalRemainingPercent(int bottles, int ratio)
{
    const double wholeBottles = qMax(0, bottles);
    const double partial = qBound(0, ratio, MainBoardConsumable::kRatioMax)
            / static_cast<double>(MainBoardConsumable::kRatioMax);
    return qBound(0.0, wholeBottles + partial, 100.0);
}

// ---------------- 试剂/清洗液容量 ----------------

/**
 * 容量分组：同一种试剂的左瓶/右瓶共用容量。
 * AA/AA1=0、EPI/EPI1=1、ADP/ADP1=2、COL/COL1=3、RIS/RIS1=4、
 * 内部清洗液 S1=5、外部清洗液 S2=6；试管盘返回 -1（单位是板）。
 */
inline int consumableCapacityIndex(int position)
{
    switch (position) {
    case MainBoardConsumable::AA:
    case MainBoardConsumable::AA_Second:      return 0;
    case MainBoardConsumable::EPI:
    case MainBoardConsumable::EPI_Second:     return 1;
    case MainBoardConsumable::ADP:
    case MainBoardConsumable::ADP_Second:     return 2;
    case MainBoardConsumable::COL:
    case MainBoardConsumable::COL_Second:     return 3;
    case MainBoardConsumable::RIS:
    case MainBoardConsumable::RIS_Second:     return 4;
    case MainBoardConsumable::InternalCleanS1: return 5;
    case MainBoardConsumable::ExternalCleanS2: return 6;
    default:                                   return -1;
    }
}

inline const char *consumableCapacitySettingsKey(int position)
{
    const int index = consumableCapacityIndex(position);
    if (index < 0) {
        return "consumables/capacity/none";
    }
    static const char *keys[] = {
        "consumables/capacity/AA",
        "consumables/capacity/EPI",
        "consumables/capacity/ADP",
        "consumables/capacity/COL",
        "consumables/capacity/RIS",
        "consumables/capacity/S1",
        "consumables/capacity/S2"
    };
    return keys[index];
}

/** 单瓶容量默认值：AA~RIS 1000 µL，S1/S2 60000 µL。 */
inline double defaultConsumableCapacityUl(int position)
{
    const int index = consumableCapacityIndex(position);
    if (index < 0) {
        return 0.0;
    }
    return index <= 4 ? 1000.0 : 60000.0;
}

inline double consumableCapacityUl(int position)
{
    if (consumableCapacityIndex(position) < 0) {
        return 0.0;
    }
    QSettings settings;
    const double value = settings.value(
                QString::fromLatin1(consumableCapacitySettingsKey(position)),
                defaultConsumableCapacityUl(position)).toDouble();
    return qBound(0.0, value, 60000.0);
}

inline void setConsumableCapacityUl(int position, double ul)
{
    if (consumableCapacityIndex(position) < 0) {
        return;
    }
    QSettings settings;
    settings.setValue(
                QString::fromLatin1(consumableCapacitySettingsKey(position)),
                qBound(0.0, ul, 60000.0));
}

/** 主板 0~512 比例 -> 百分比 */
inline double mainboardRatioPercent(int ratio)
{
    return qBound(0, ratio, MainBoardConsumable::kRatioMax)
            * 100.0 / MainBoardConsumable::kRatioMax;
}

/** 主板比例 -> 当前单瓶剩余 µL */
inline double mainboardRatioToUl(int ratio, double capacityUl)
{
    if (capacityUl <= 0.0) {
        return 0.0;
    }
    return capacityUl
            * qBound(0, ratio, MainBoardConsumable::kRatioMax)
            / MainBoardConsumable::kRatioMax;
}

/** 按微升扣减后写回主板的 0~512 比例 */
inline int mainboardUlToRatio(double remainUl, double capacityUl)
{
    if (capacityUl <= 0.0 || remainUl <= 0.0) {
        return 0;
    }
    const int ratio = qRound(remainUl * MainBoardConsumable::kRatioMax
                             / capacityUl);
    return qBound(0, ratio, MainBoardConsumable::kRatioMax);
}

#endif // INSTRUMENTPARAMETERS_H
