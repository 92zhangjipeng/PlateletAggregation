#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "sampletraymodel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QVariant>
#include <QVector>

namespace {
const char kSettingsKey[] = "sampleTrays/tubes";
}

SampleTrayModel::SampleTrayModel(QObject *parent)
    : QObject(parent)
    , m_activeTrayCount(machineTrayCount(m_model))
{
}

void SampleTrayModel::setMachineModel(MachineModel model)
{
    const int newTrayCount = machineTrayCount(model);
    m_model = model;

    bool changed = false;
    for (int i = 0; i < MaxTubes; ++i) {
        const int tray = trayOf(i);
        if (tray >= newTrayCount && m_tubes[i].state != TubeState::Free) {
            resetTube(m_tubes[i]);
            changed = true;
        }
    }
    m_activeTrayCount = newTrayCount;

    if (changed) {
        saveToSettings();
    }
    emit modelChanged();
}

MachineModel SampleTrayModel::machineModel() const
{
    return m_model;
}

int SampleTrayModel::activeTrayCount() const
{
    return m_activeTrayCount;
}

bool SampleTrayModel::isTrayActive(int trayIndex) const
{
    return trayIndex >= 0 && trayIndex < m_activeTrayCount;
}

SampleTrayModel::TubeInfo SampleTrayModel::tubeInfo(int tubeIndex) const
{
    if (!isValidTubeIndex(tubeIndex)) {
        return TubeInfo();
    }
    return m_tubes[tubeIndex];
}

SampleTrayModel::TubeState SampleTrayModel::tubeState(int tubeIndex) const
{
    return isValidTubeIndex(tubeIndex)
            ? m_tubes[tubeIndex].state : TubeState::Free;
}

int SampleTrayModel::firstFreeTube(int trayIndex) const
{
    if (trayIndex >= 0) {
        if (!isTrayActive(trayIndex)) {
            return -1;
        }
        const int start = trayIndex * TubesPerTray;
        for (int i = start; i < start + TubesPerTray; ++i) {
            if (m_tubes[i].state == TubeState::Free) {
                return i;
            }
        }
        return -1;
    }

    for (int tray = 0; tray < m_activeTrayCount; ++tray) {
        const int start = tray * TubesPerTray;
        for (int i = start; i < start + TubesPerTray; ++i) {
            if (m_tubes[i].state == TubeState::Free) {
                return i;
            }
        }
    }
    return -1;
}

int SampleTrayModel::countState(TubeState state) const
{
    int count = 0;
    for (int tray = 0; tray < m_activeTrayCount; ++tray) {
        count += countStateInTray(tray, state);
    }
    return count;
}

int SampleTrayModel::countStateInTray(int trayIndex, TubeState state) const
{
    if (!isTrayActive(trayIndex)) {
        return 0;
    }
    int count = 0;
    const int start = trayIndex * TubesPerTray;
    for (int i = start; i < start + TubesPerTray; ++i) {
        if (m_tubes[i].state == state) {
            ++count;
        }
    }
    return count;
}

bool SampleTrayModel::setTubeState(int tubeIndex, TubeState state,
                                   const QString &sampleNumber,
                                   const QString &reagent,
                                   QString *error)
{
    if (error) {
        error->clear();
    }
    if (!isValidTubeIndex(tubeIndex)) {
        if (error) {
            *error = tr("试管编号超出范围。");
        }
        return false;
    }
    if (!isTrayActive(trayOf(tubeIndex))) {
        if (error) {
            *error = tr("当前机型未使用该试管盘。");
        }
        return false;
    }

    TubeInfo &tube = m_tubes[tubeIndex];
    if (tube.state == state) {
        bool dataChanged = false;
        if (state != TubeState::Free) {
            if (!sampleNumber.isEmpty()
                    && tube.sampleNumber != sampleNumber) {
                tube.sampleNumber = sampleNumber;
                dataChanged = true;
            }
            if (!reagent.isEmpty() && tube.reagent != reagent) {
                tube.reagent = reagent;
                dataChanged = true;
            }
        }
        if (dataChanged) {
            tube.updatedAt = QDateTime::currentDateTime();
            notifyChanged(tubeIndex);
        }
        return true;
    }
    if (!transitionAllowed(tube.state, state)) {
        if (error) {
            *error = tr("试管 %1 不能从「%2」直接变为「%3」。")
                    .arg(tubeIndex)
                    .arg(stateText(tube.state))
                    .arg(stateText(state));
        }
        return false;
    }

    if (state == TubeState::Free) {
        resetTube(tube);
    } else {
        tube.state = state;
        if (!sampleNumber.isEmpty()) {
            tube.sampleNumber = sampleNumber;
        }
        if (!reagent.isEmpty()) {
            tube.reagent = reagent;
        }
        tube.updatedAt = QDateTime::currentDateTime();
    }
    notifyChanged(tubeIndex);
    return true;
}

bool SampleTrayModel::allocateTube(int tubeIndex, const QString &sampleNumber,
                                   QString *error)
{
    return setTubeState(tubeIndex, TubeState::Allocated, sampleNumber,
                        QString(), error);
}

bool SampleTrayModel::markSampled(int tubeIndex, const QString &reagent,
                                  QString *error)
{
    return setTubeState(tubeIndex, TubeState::Sampled, QString(), reagent,
                        error);
}

bool SampleTrayModel::markUsed(int tubeIndex, QString *error)
{
    return setTubeState(tubeIndex, TubeState::Used, QString(), QString(),
                        error);
}

bool SampleTrayModel::clearTube(int tubeIndex, QString *error)
{
    if (!isValidTubeIndex(tubeIndex)) {
        if (error) {
            *error = tr("试管编号超出范围。");
        }
        return false;
    }
    if (m_tubes[tubeIndex].state == TubeState::Free) {
        return true;
    }
    return setTubeState(tubeIndex, TubeState::Free, QString(), QString(),
                        error);
}

void SampleTrayModel::clearTray(int trayIndex)
{
    if (!isTrayActive(trayIndex)) {
        return;
    }
    QVector<int> changed;
    const int start = trayIndex * TubesPerTray;
    for (int i = start; i < start + TubesPerTray; ++i) {
        if (m_tubes[i].state != TubeState::Free) {
            resetTube(m_tubes[i]);
            changed.append(i);
        }
    }
    if (changed.isEmpty()) {
        return;
    }
    for (int index : changed) {
        emit tubeStateChanged(index);
    }
    emit modelChanged();
    saveToSettings();
}

void SampleTrayModel::clearAll()
{
    QVector<int> changed;
    for (int tray = 0; tray < m_activeTrayCount; ++tray) {
        const int start = tray * TubesPerTray;
        for (int i = start; i < start + TubesPerTray; ++i) {
            if (m_tubes[i].state != TubeState::Free) {
                resetTube(m_tubes[i]);
                changed.append(i);
            }
        }
    }
    if (changed.isEmpty()) {
        return;
    }
    for (int index : changed) {
        emit tubeStateChanged(index);
    }
    emit modelChanged();
    saveToSettings();
}

bool SampleTrayModel::loadFromSettings(QString *error)
{
    if (error) {
        error->clear();
    }

    for (TubeInfo &tube : m_tubes) {
        resetTube(tube);
    }

    QSettings settings;
    const QByteArray raw = settings.value(QLatin1String(kSettingsKey))
            .toByteArray();
    if (!raw.isEmpty()) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
            if (error) {
                *error = tr("试管盘状态文件格式错误：%1")
                        .arg(parseError.errorString());
            }
            return false;
        }
        const QJsonArray array = doc.array();
        for (const QJsonValue &value : array) {
            const QJsonObject object = value.toObject();
            const int index = object.value(QStringLiteral("index")).toInt(-1);
            const int stateValue = object.value(QStringLiteral("state")).toInt(
                        int(TubeState::Free));
            if (!isValidTubeIndex(index)
                    || !isTrayActive(trayOf(index))
                    || stateValue < int(TubeState::Free)
                    || stateValue > int(TubeState::Used)) {
                continue;
            }
            TubeInfo &tube = m_tubes[index];
            tube.state = static_cast<TubeState>(stateValue);
            if (tube.state != TubeState::Free) {
                tube.sampleNumber = object.value(
                            QStringLiteral("sample")).toString();
                tube.reagent = object.value(
                            QStringLiteral("reagent")).toString();
                tube.updatedAt = QDateTime::fromString(
                            object.value(QStringLiteral("updated")).toString(),
                            Qt::ISODate);
            }
        }
    }

    emit modelChanged();
    return true;
}

bool SampleTrayModel::saveToSettings(QString *error) const
{
    return saveInternal(error);
}

bool SampleTrayModel::saveInternal(QString *error) const
{
    if (error) {
        error->clear();
    }

    QJsonArray array;
    for (int i = 0; i < MaxTubes; ++i) {
        const TubeInfo &tube = m_tubes[i];
        if (tube.state == TubeState::Free
                || !isTrayActive(trayOf(i))) {
            continue;
        }
        QJsonObject object;
        object.insert(QStringLiteral("index"), i);
        object.insert(QStringLiteral("state"), int(tube.state));
        object.insert(QStringLiteral("sample"), tube.sampleNumber);
        object.insert(QStringLiteral("reagent"), tube.reagent);
        object.insert(QStringLiteral("updated"),
                      tube.updatedAt.toString(Qt::ISODate));
        array.append(object);
    }

    QSettings settings;
    settings.setValue(QLatin1String(kSettingsKey),
                      QJsonDocument(array).toJson(QJsonDocument::Compact));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error) {
            *error = tr("保存试管盘状态失败。");
        }
        return false;
    }
    return true;
}

bool SampleTrayModel::isValidTubeIndex(int tubeIndex)
{
    return tubeIndex >= 0 && tubeIndex < MaxTubes;
}

int SampleTrayModel::trayOf(int tubeIndex)
{
    return isValidTubeIndex(tubeIndex) ? tubeIndex / TubesPerTray : -1;
}

int SampleTrayModel::rowOf(int tubeIndex)
{
    return isValidTubeIndex(tubeIndex)
            ? (tubeIndex % TubesPerTray) / ColumnsPerTray : -1;
}

int SampleTrayModel::columnOf(int tubeIndex)
{
    return isValidTubeIndex(tubeIndex)
            ? (tubeIndex % TubesPerTray) % ColumnsPerTray : -1;
}

int SampleTrayModel::tubeIndex(int trayIndex, int row, int column)
{
    if (trayIndex < 0 || trayIndex >= MaxTrays
            || row < 0 || row >= RowsPerTray
            || column < 0 || column >= ColumnsPerTray) {
        return -1;
    }
    return trayIndex * TubesPerTray + row * ColumnsPerTray + column;
}

QString SampleTrayModel::stateText(TubeState state)
{
    switch (state) {
    case TubeState::Free: return QObject::tr("空闲");
    case TubeState::Allocated: return QObject::tr("分配");
    case TubeState::Sampled: return QObject::tr("已加样");
    case TubeState::Used: return QObject::tr("已使用");
    }
    return QObject::tr("未知");
}

bool SampleTrayModel::transitionAllowed(TubeState from, TubeState to) const
{
    if (to == TubeState::Free) {
        return from != TubeState::Free;
    }
    if (from == TubeState::Free && to == TubeState::Allocated) {
        return true;
    }
    if (from == TubeState::Allocated && to == TubeState::Sampled) {
        return true;
    }
    if (from == TubeState::Sampled && to == TubeState::Used) {
        return true;
    }
    return false;
}

void SampleTrayModel::resetTube(TubeInfo &tube)
{
    tube.state = TubeState::Free;
    tube.sampleNumber.clear();
    tube.reagent.clear();
    tube.updatedAt = QDateTime();
}

void SampleTrayModel::notifyChanged(int tubeIndex)
{
    if (tubeIndex >= 0) {
        emit tubeStateChanged(tubeIndex);
    }
    emit modelChanged();
    saveToSettings();
}
