#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "coordinatefileio.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtGlobal>

#include <cmath>

namespace {

const char kFormatId[] = "PlateletAggregationCoordinates";
constexpr int kFormatVersion = 1;
constexpr double kXyLimit = 10000.0;
constexpr double kZLimit = 1000.0;

const char kToolKeys[3][16] = {
    "gripper",
    "reagentNeedle",
    "sampleNeedle"
};

using TargetKind = InstrumentCoordinates::TargetKind;

void setError(QString *error, const QString &text)
{
    if (error) {
        *error = text;
    }
}

QString kindKey(TargetKind kind)
{
    switch (kind) {
    case TargetKind::Origin:       return QStringLiteral("origin");
    case TargetKind::CleaningHole: return QStringLiteral("cleaningHole");
    case TargetKind::DiscardCup:   return QStringLiteral("discardCup");
    case TargetKind::Channel:      return QStringLiteral("channel");
    case TargetKind::Tube:         return QStringLiteral("tube");
    case TargetKind::Reagent:      return QStringLiteral("reagent");
    case TargetKind::SampleWell:   return QStringLiteral("sampleWell");
    }
    return QString();
}

bool kindFromKey(const QString &key, TargetKind *kind)
{
    if (!kind) {
        return false;
    }
    if (key == QLatin1String("origin")) {
        *kind = TargetKind::Origin;
    } else if (key == QLatin1String("cleaningHole")) {
        *kind = TargetKind::CleaningHole;
    } else if (key == QLatin1String("discardCup")) {
        *kind = TargetKind::DiscardCup;
    } else if (key == QLatin1String("channel")) {
        *kind = TargetKind::Channel;
    } else if (key == QLatin1String("tube")) {
        *kind = TargetKind::Tube;
    } else if (key == QLatin1String("reagent")) {
        *kind = TargetKind::Reagent;
    } else if (key == QLatin1String("sampleWell")) {
        *kind = TargetKind::SampleWell;
    } else {
        return false;
    }
    return true;
}

int maxIndex(TargetKind kind)
{
    switch (kind) {
    case TargetKind::Origin:
    case TargetKind::CleaningHole:
    case TargetKind::DiscardCup:
        return 0;
    case TargetKind::Channel:
        return InstrumentCoordinates::kMaxChannels - 1;
    case TargetKind::Tube:
        return InstrumentCoordinates::kMaxTubes - 1;
    case TargetKind::Reagent:
        return InstrumentCoordinates::kReagentPositions - 1;
    case TargetKind::SampleWell:
        return InstrumentCoordinates::kMaxSampleWells - 1;
    }
    return -1;
}

QString targetName(TargetKind kind, int index)
{
    switch (kind) {
    case TargetKind::Origin:
        return QStringLiteral("原点");
    case TargetKind::CleaningHole:
        return QStringLiteral("清洗孔位");
    case TargetKind::DiscardCup:
        return QStringLiteral("弃杯孔位");
    case TargetKind::Channel:
        return QStringLiteral("CH%1").arg(index + 1);
    case TargetKind::Tube:
        return QStringLiteral("试管盘 %1 · 管 %2")
                .arg(index / InstrumentCoordinates::kTubesPerTray + 1)
                .arg(index % InstrumentCoordinates::kTubesPerTray + 1);
    case TargetKind::Reagent:
        return InstrumentCoordinates::reagentPositionName(index);
    case TargetKind::SampleWell:
        return QStringLiteral("样本孔 %1").arg(index + 1);
    }
    return QString();
}

QJsonObject toolToJson(const InstrumentCoordinates::ToolPose &pose)
{
    QJsonObject object;
    object.insert(QStringLiteral("xyValid"), pose.xyValid);
    object.insert(QStringLiteral("zValid"), pose.zValid);
    object.insert(QStringLiteral("x"), pose.x);
    object.insert(QStringLiteral("y"), pose.y);
    object.insert(QStringLiteral("z"), pose.z);
    return object;
}

bool readDouble(const QJsonObject &object, const QString &key,
                const QString &context, double *value, QString *error)
{
    const QJsonValue jsonValue = object.value(key);
    if (!jsonValue.isDouble()) {
        setError(error, QStringLiteral("%1：字段 %2 必须是数字")
                 .arg(context, key));
        return false;
    }
    const double number = jsonValue.toDouble();
    if (!qIsFinite(number)) {
        setError(error, QStringLiteral("%1：字段 %2 不是有效数字")
                 .arg(context, key));
        return false;
    }
    if (value) {
        *value = number;
    }
    return true;
}

bool readTool(const QJsonValue &value, const QString &context,
              CoordinateDocument::ToolData *tool, QString *error)
{
    if (!tool) {
        return false;
    }
    if (!value.isObject()) {
        setError(error, QStringLiteral("%1：工具坐标必须是对象").arg(context));
        return false;
    }

    const QJsonObject object = value.toObject();
    tool->xyValid = object.value(QStringLiteral("xyValid")).toBool(false);
    tool->zValid = object.value(QStringLiteral("zValid")).toBool(false);

    if (tool->xyValid
            && (!readDouble(object, QStringLiteral("x"), context,
                            &tool->x, error)
                || !readDouble(object, QStringLiteral("y"), context,
                               &tool->y, error))) {
        return false;
    }
    if (tool->zValid
            && !readDouble(object, QStringLiteral("z"), context,
                           &tool->z, error)) {
        return false;
    }
    return true;
}

} // namespace

QString CoordinateFileIo::defaultFilePath()
{
    QString directory = QStandardPaths::writableLocation(
                QStandardPaths::AppDataLocation);
    if (directory.isEmpty()) {
        directory = QDir(QCoreApplication::applicationDirPath())
                .filePath(QStringLiteral("data"));
    }
    return QDir(directory).filePath(
                QStringLiteral("platelet_coordinates.json"));
}

bool CoordinateFileIo::save(const InstrumentCoordinates &coordinates,
                            const QString &filePath,
                            QString *error)
{
    if (filePath.isEmpty()) {
        setError(error, QStringLiteral("未指定导出文件"));
        return false;
    }

    const QString directory = QFileInfo(filePath).absolutePath();
    if (!directory.isEmpty() && !QDir().mkpath(directory)) {
        setError(error, QStringLiteral("无法创建目录 %1").arg(directory));
        return false;
    }

    QJsonArray targets;
    const auto appendTarget = [&coordinates, &targets](TargetKind kind,
                                                       int index) {
        QJsonObject tools;
        bool anyTool = false;
        for (int axis = 0; axis < 3; ++axis) {
            const auto pose = coordinates.toolPose(
                        kind, index,
                        static_cast<InstrumentCoordinates::ZAxis>(axis));
            if (!pose.xyValid && !pose.zValid) {
                continue;
            }
            tools.insert(QLatin1String(kToolKeys[axis]), toolToJson(pose));
            anyTool = true;
        }
        if (!anyTool) {
            return;
        }

        QJsonObject target;
        target.insert(QStringLiteral("kind"), kindKey(kind));
        target.insert(QStringLiteral("index"), index);
        target.insert(QStringLiteral("name"), targetName(kind, index));
        target.insert(QStringLiteral("tools"), tools);
        targets.append(target);
    };

    const TargetKind specials[] = {
        TargetKind::Origin, TargetKind::CleaningHole, TargetKind::DiscardCup
    };
    for (TargetKind kind : specials) {
        appendTarget(kind, 0);
    }
    for (int i = 0; i < coordinates.channelCount(); ++i) {
        appendTarget(TargetKind::Channel, i);
    }
    for (int i = 0; i < coordinates.tubeCount(); ++i) {
        appendTarget(TargetKind::Tube, i);
    }
    for (int i = 0; i < coordinates.reagentCount(); ++i) {
        appendTarget(TargetKind::Reagent, i);
    }
    for (int i = 0; i < coordinates.sampleWellCount(); ++i) {
        appendTarget(TargetKind::SampleWell, i);
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), QLatin1String(kFormatId));
    root.insert(QStringLiteral("version"), kFormatVersion);
    root.insert(QStringLiteral("machineModel"),
                QLatin1String(machineModelName(coordinates.machineModel())));
    root.insert(QStringLiteral("unit"), QStringLiteral("mm"));
    root.insert(QStringLiteral("exportedAt"),
                QDateTime::currentDateTime().toString(Qt::ISODate));
    root.insert(QStringLiteral("targets"), targets);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("无法打开文件 %1：%2")
                 .arg(filePath, file.errorString()));
        return false;
    }
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size()) {
        setError(error, QStringLiteral("写入文件 %1 失败：%2")
                 .arg(filePath, file.errorString()));
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        setError(error, QStringLiteral("保存文件 %1 失败：%2")
                 .arg(filePath, file.errorString()));
        return false;
    }
    return true;
}

bool CoordinateFileIo::load(const QString &filePath,
                            CoordinateDocument *document,
                            QString *error)
{
    if (!document) {
        setError(error, QStringLiteral("内部错误：未提供文档对象"));
        return false;
    }
    *document = CoordinateDocument();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("无法打开文件 %1：%2")
                 .arg(filePath, file.errorString()));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument json =
            QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(error, QStringLiteral("JSON 解析失败（偏移 %1）：%2")
                 .arg(parseError.offset)
                 .arg(parseError.errorString()));
        return false;
    }
    if (!json.isObject()) {
        setError(error, QStringLiteral("文件内容不是 JSON 对象"));
        return false;
    }

    const QJsonObject root = json.object();
    if (root.value(QStringLiteral("format")).toString()
            != QLatin1String(kFormatId)) {
        setError(error, QStringLiteral("不是血小板聚集仪坐标文件"));
        return false;
    }

    const QJsonValue versionValue = root.value(QStringLiteral("version"));
    if (!versionValue.isDouble()
            || versionValue.toDouble() != kFormatVersion) {
        setError(error, QStringLiteral("不支持的文件版本：%1（当前支持 %2）")
                 .arg(versionValue.isDouble()
                      ? QString::number(versionValue.toDouble())
                      : QStringLiteral("无"))
                 .arg(kFormatVersion));
        return false;
    }
    document->version = kFormatVersion;
    document->machineModel =
            root.value(QStringLiteral("machineModel")).toString();

    const QJsonValue targetsValue = root.value(QStringLiteral("targets"));
    if (!targetsValue.isArray()) {
        setError(error, QStringLiteral("缺少 targets 数组"));
        return false;
    }

    const QJsonArray targets = targetsValue.toArray();
    document->entries.reserve(targets.size());
    for (int i = 0; i < targets.size(); ++i) {
        const QJsonValue targetValue = targets.at(i);
        const QString context = QStringLiteral("targets[%1]").arg(i);
        if (!targetValue.isObject()) {
            setError(error, QStringLiteral("%1：必须是对象").arg(context));
            return false;
        }

        const QJsonObject targetObject = targetValue.toObject();
        CoordinateDocument::Entry entry;
        if (!kindFromKey(
                    targetObject.value(QStringLiteral("kind")).toString(),
                    &entry.kind)) {
            setError(error, QStringLiteral("%1：未知的位置类型 kind=%2")
                     .arg(context,
                          targetObject.value(QStringLiteral("kind"))
                          .toString()));
            return false;
        }

        const QJsonValue indexValue =
                targetObject.value(QStringLiteral("index"));
        const double indexNumber = indexValue.isDouble()
                ? indexValue.toDouble() : -1.0;
        if (!indexValue.isDouble() || !qIsFinite(indexNumber)
                || indexNumber != std::floor(indexNumber)
                || indexNumber < 0.0
                || indexNumber > maxIndex(entry.kind)) {
            setError(error, QStringLiteral("%1：index 必须是 0~%2 的整数")
                     .arg(context)
                     .arg(maxIndex(entry.kind)));
            return false;
        }
        entry.index = static_cast<int>(indexNumber);

        const QJsonValue toolsValue =
                targetObject.value(QStringLiteral("tools"));
        if (!toolsValue.isUndefined()) {
            if (!toolsValue.isObject()) {
                setError(error, QStringLiteral("%1：tools 必须是对象")
                         .arg(context));
                return false;
            }
            const QJsonObject toolsObject = toolsValue.toObject();
            for (int axis = 0; axis < 3; ++axis) {
                const QJsonValue toolValue =
                        toolsObject.value(QLatin1String(kToolKeys[axis]));
                if (toolValue.isUndefined()) {
                    continue;
                }
                const QString toolContext = QStringLiteral("%1.%2")
                        .arg(context, QLatin1String(kToolKeys[axis]));
                if (!readTool(toolValue, toolContext,
                              &entry.tools[axis], error)) {
                    return false;
                }
            }
        }

        document->entries.append(entry);
    }
    return true;
}

CoordinateFileIo::ApplyResult CoordinateFileIo::apply(
        const CoordinateDocument &document,
        InstrumentCoordinates *coordinates)
{
    ApplyResult result;
    if (!coordinates) {
        return result;
    }

    coordinates->clear();
    for (const CoordinateDocument::Entry &entry : document.entries) {
        if (!coordinates->indexValid(entry.kind, entry.index)) {
            ++result.skipped;
            continue;
        }

        for (int axis = 0; axis < 3; ++axis) {
            const CoordinateDocument::ToolData &tool = entry.tools[axis];
            const auto zAxis =
                    static_cast<InstrumentCoordinates::ZAxis>(axis);
            bool ignored = false;

            if (tool.xyValid) {
                if (tool.x >= -kXyLimit && tool.x <= kXyLimit
                        && tool.y >= -kXyLimit && tool.y <= kXyLimit) {
                    coordinates->setXy(entry.kind, entry.index, zAxis,
                                       tool.x, tool.y);
                } else {
                    ignored = true;
                }
            }
            if (tool.zValid) {
                if (tool.z >= -kZLimit && tool.z <= kZLimit) {
                    coordinates->setZ(entry.kind, entry.index, zAxis,
                                      tool.z);
                } else {
                    ignored = true;
                }
            }
            if (ignored) {
                ++result.ignoredTools;
            }
        }
        ++result.applied;
    }
    return result;
}
