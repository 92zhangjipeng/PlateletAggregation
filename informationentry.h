#ifndef INFORMATIONENTRY_H
#define INFORMATIONENTRY_H

#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVector>

/**
 * 维护设置「信息录入」的固定信息根和子项存取。
 *
 * 根是医院报告/样本信息里的常用字段：科别、性别、病区、床号、送检医生、
 * 检验医生、审核医生、临床诊断、备注；每个根下面维护一组可选子项。
 * 子项保存在 QSettings 的 informationEntry/<key>（QStringList），
 * 修改后立即 sync，供后续样本信息、报告打印等界面读取。
 */
struct InformationEntryCategory
{
    const char *key;          // QSettings 子键，ASCII
    const char *title;        // 界面显示名
    const char *placeholder;  // 输入框示例
};

inline QVector<InformationEntryCategory> informationEntryCategories()
{
    return {
        { "department",     "科别",     "例如：内科" },
        { "gender",         "性别",     "例如：男" },
        { "ward",           "病区",     "例如：一病区" },
        { "bed",            "床号",     "例如：12" },
        { "sendingDoctor",  "送检医生", "例如：张三" },
        { "testingDoctor",  "检验医生", "例如：李四" },
        { "reviewingDoctor","审核医生", "例如：王五" },
        { "diagnosis",      "临床诊断", "例如：高血压" },
        { "remark",         "备注",     "例如：空腹" }
    };
}

inline QString informationEntrySettingsKey(const QString &categoryKey)
{
    return QStringLiteral("informationEntry/%1").arg(categoryKey);
}

/** 首次使用时写入的默认子项：性别预置男/女，其余类别为空。 */
inline QStringList informationEntryDefaults(const QString &categoryKey)
{
    if (categoryKey == QLatin1String("gender")) {
        return QStringList() << QStringLiteral("男") << QStringLiteral("女");
    }
    return QStringList();
}

/** 去掉首尾空白、空项和重复项，保留原顺序。 */
inline QStringList normalizedInformationEntryValues(const QStringList &source)
{
    QStringList result;
    for (const QString &raw : source) {
        const QString value = raw.trimmed();
        if (!value.isEmpty() && !result.contains(value)) {
            result.append(value);
        }
    }
    return result;
}

inline QStringList informationEntryValues(const QString &categoryKey)
{
    QSettings settings;
    const QString key = informationEntrySettingsKey(categoryKey);
    if (!settings.contains(key)) {
        return informationEntryDefaults(categoryKey);
    }
    return normalizedInformationEntryValues(
                settings.value(key).toStringList());
}

inline void setInformationEntryValues(const QString &categoryKey,
                                      const QStringList &values)
{
    QSettings settings;
    settings.setValue(informationEntrySettingsKey(categoryKey),
                      normalizedInformationEntryValues(values));
    settings.sync();
}

/** 打开页面时为每个根建立键，保证“删空”后不会被默认值重新填回。 */
inline void ensureInformationEntryDefaults()
{
    QSettings settings;
    const QVector<InformationEntryCategory> categories =
            informationEntryCategories();
    for (const InformationEntryCategory &category : categories) {
        const QString key = informationEntrySettingsKey(
                    QString::fromLatin1(category.key));
        if (!settings.contains(key)) {
            settings.setValue(
                        key,
                        informationEntryDefaults(
                            QString::fromLatin1(category.key)));
        }
    }
    settings.sync();
}

#endif // INFORMATIONENTRY_H
