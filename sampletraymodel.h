#ifndef SAMPLETRAYMODEL_H
#define SAMPLETRAYMODEL_H

#include "MachineModel.h"

#include <QDateTime>
#include <QObject>
#include <QString>

/**
 * 样本试管盘状态模型。
 *
 * 最多 4 个试管盘；每盘 10 行 × 6 列 = 60 个试管，全局编号：
 *   盘1：0~59
 *   盘2：60~119
 *   盘3：120~179
 *   盘4：180~239
 *
 * 机型：
 *   KS600  = 2 个盘（0~119）
 *   KS800  = 3 个盘（0~179）
 *   KS1200 = 4 个盘（0~239）
 *
 * 每个试管状态：空闲 → 分配 → 已加样 → 已使用；也支持释放回空闲。
 * 状态通过 QSettings（JSON）持久化，界面关闭/软件重启后仍可恢复。
 */
class SampleTrayModel : public QObject
{
    Q_OBJECT

public:
    enum class TubeState {
        Free = 0,       // 空闲
        Allocated = 1,  // 分配
        Sampled = 2,    // 已加样
        Used = 3        // 已使用
    };

    struct TubeInfo {
        TubeState state = TubeState::Free;
        QString sampleNumber;
        QString reagent;
        QDateTime updatedAt;
    };

    enum {
        MaxTrays = 4,
        RowsPerTray = 10,
        ColumnsPerTray = 6,
        TubesPerTray = 60,
        MaxTubes = 240
    };

    explicit SampleTrayModel(QObject *parent = nullptr);

    void setMachineModel(MachineModel model);
    MachineModel machineModel() const;
    int activeTrayCount() const;
    bool isTrayActive(int trayIndex) const;

    TubeInfo tubeInfo(int tubeIndex) const;
    TubeState tubeState(int tubeIndex) const;
    int firstFreeTube(int trayIndex = -1) const;
    int countState(TubeState state) const;
    int countStateInTray(int trayIndex, TubeState state) const;

    bool setTubeState(int tubeIndex, TubeState state,
                      const QString &sampleNumber = QString(),
                      const QString &reagent = QString(),
                      QString *error = nullptr);
    bool allocateTube(int tubeIndex, const QString &sampleNumber,
                      QString *error = nullptr);
    bool markSampled(int tubeIndex, const QString &reagent = QString(),
                     QString *error = nullptr);
    bool markUsed(int tubeIndex, QString *error = nullptr);
    bool clearTube(int tubeIndex, QString *error = nullptr);
    void clearTray(int trayIndex);
    void clearAll();

    bool loadFromSettings(QString *error = nullptr);
    bool saveToSettings(QString *error = nullptr) const;

    static bool isValidTubeIndex(int tubeIndex);
    static int trayOf(int tubeIndex);
    static int rowOf(int tubeIndex);
    static int columnOf(int tubeIndex);
    static int tubeIndex(int trayIndex, int row, int column);
    static QString stateText(TubeState state);

signals:
    void tubeStateChanged(int tubeIndex);
    void modelChanged();

private:
    bool transitionAllowed(TubeState from, TubeState to) const;
    void resetTube(TubeInfo &tube);
    void notifyChanged(int tubeIndex = -1);
    bool saveInternal(QString *error) const;

    TubeInfo m_tubes[MaxTubes];
    MachineModel m_model = defaultMachineModel();
    int m_activeTrayCount = 4;
};

#endif // SAMPLETRAYMODEL_H
