#ifndef REFERENCEINTERVAL_H
#define REFERENCEINTERVAL_H

#include <QSettings>
#include <QString>
#include <QtGlobal>

/**
 * 聚集率参考区间（%）。
 *
 * 每个试剂 AA/ADP/EPI/COL/RIS 分别保存：
 *   - 男/女
 *   - 60S / 180S / 300S / MAX 四个时间点
 *   - 每个时间点的下限、上限
 *
 * MAX 默认值沿用老软件打印报告：
 *   AA 30~70、ADP 40~80、EPI 35~75、COL 50~90、RIS 45~85；
 * 60S/180S/300S 默认 0~0，表示未设置、不参与异常判断，可在维护设置中修改。
 */
namespace ReferenceInterval {

enum class Timepoint {
    S60 = 0,
    S180 = 1,
    S300 = 2,
    Max = 3
};

inline int clampedIndex(int index)
{
    return qBound(0, index, 4);
}

inline const char *reagentName(int index)
{
    static const char *names[] = { "AA", "ADP", "EPI", "COL", "RIS" };
    return names[clampedIndex(index)];
}

inline QString timepointName(Timepoint timepoint)
{
    switch (timepoint) {
    case Timepoint::S60:  return QStringLiteral("60S");
    case Timepoint::S180: return QStringLiteral("180S");
    case Timepoint::S300: return QStringLiteral("300S");
    case Timepoint::Max:  return QStringLiteral("MAX");
    }
    return QStringLiteral("MAX");
}

inline const char *timepointKey(Timepoint timepoint)
{
    switch (timepoint) {
    case Timepoint::S60:  return "60";
    case Timepoint::S180: return "180";
    case Timepoint::S300: return "300";
    case Timepoint::Max:  return "max";
    }
    return "max";
}

inline int defaultLow(int index, Timepoint timepoint)
{
    static const int lows[] = { 30, 40, 35, 50, 45 };
    if (timepoint != Timepoint::Max) {
        return 0;
    }
    return lows[clampedIndex(index)];
}

inline int defaultHigh(int index, Timepoint timepoint)
{
    static const int highs[] = { 70, 80, 75, 90, 85 };
    if (timepoint != Timepoint::Max) {
        return 0;
    }
    return highs[clampedIndex(index)];
}

inline QString settingsKey(int index, bool male, Timepoint timepoint,
                           bool high)
{
    return QStringLiteral("reference/%1/%2/%3/%4")
            .arg(QString::fromLatin1(reagentName(index)),
                 male ? QStringLiteral("male") : QStringLiteral("female"),
                 QString::fromLatin1(timepointKey(timepoint)),
                 high ? QStringLiteral("high") : QStringLiteral("low"));
}

/** 上一版只有 MAX 时的旧键，作为 MAX 的兼容回退。 */
inline QString legacyMaxSettingsKey(int index, bool male, bool high)
{
    return QStringLiteral("reference/%1/%2%3")
            .arg(QString::fromLatin1(reagentName(index)),
                 male ? QStringLiteral("male") : QStringLiteral("female"),
                 high ? QStringLiteral("High") : QStringLiteral("Low"));
}

inline int low(int index, bool male, Timepoint timepoint)
{
    QSettings settings;
    const QString key = settingsKey(index, male, timepoint, false);
    int fallback = defaultLow(index, timepoint);
    if (timepoint == Timepoint::Max) {
        fallback = settings.value(
                    legacyMaxSettingsKey(index, male, false), fallback).toInt();
    }
    return qBound(0, settings.value(key, fallback).toInt(), 100);
}

inline int high(int index, bool male, Timepoint timepoint)
{
    QSettings settings;
    const QString key = settingsKey(index, male, timepoint, true);
    int fallback = defaultHigh(index, timepoint);
    if (timepoint == Timepoint::Max) {
        fallback = settings.value(
                    legacyMaxSettingsKey(index, male, true), fallback).toInt();
    }
    return qBound(0, settings.value(key, fallback).toInt(), 100);
}

inline void setLow(int index, bool male, Timepoint timepoint, int value)
{
    QSettings settings;
    settings.setValue(settingsKey(index, male, timepoint, false),
                      qBound(0, value, 100));
}

inline void setHigh(int index, bool male, Timepoint timepoint, int value)
{
    QSettings settings;
    settings.setValue(settingsKey(index, male, timepoint, true),
                      qBound(0, value, 100));
}

/** 兼容旧调用：不传时间点时按 MAX 处理。 */
inline int low(int index, bool male)
{
    return low(index, male, Timepoint::Max);
}

inline int high(int index, bool male)
{
    return high(index, male, Timepoint::Max);
}

/** 该时间点是否设置了参考区间（上限 > 0 视为已设置）。 */
inline bool isSet(int index, bool male, Timepoint timepoint)
{
    return high(index, male, timepoint) > 0;
}

/**
 * 判断聚集率是否在参考区间内。
 * 未设置参考区间时返回 true，避免把结果误判为异常。
 */
inline bool contains(int index, bool male, Timepoint timepoint,
                     double aggregationPercent)
{
    if (!isSet(index, male, timepoint)) {
        return true;
    }
    return aggregationPercent >= low(index, male, timepoint)
            && aggregationPercent <= high(index, male, timepoint);
}

inline bool contains(int index, bool male, double aggregationPercent)
{
    return contains(index, male, Timepoint::Max, aggregationPercent);
}

} // namespace ReferenceInterval

#endif // REFERENCEINTERVAL_H
