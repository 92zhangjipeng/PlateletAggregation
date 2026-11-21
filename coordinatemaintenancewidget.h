#ifndef COORDINATEMAINTENANCEWIDGET_H
#define COORDINATEMAINTENANCEWIDGET_H

#include "instrumentcoordinates.h"

#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QPushButton;
class QShowEvent;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * 坐标维护页：
 * - 左侧树：原点/清洗孔/弃杯孔、通道、试管盘/试管、试剂位、样本孔；
 * - 右侧编辑：X/Y 和 3 个 Z 轴高度，带“启用”勾选；
 * - 保存到 InstrumentCoordinates 内存表，并实时刷新左侧状态。
 */
class CoordinateMaintenanceWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CoordinateMaintenanceWidget(
            InstrumentCoordinates *coordinates,
            QWidget *parent = nullptr);

    /** 按当前机型重建树并刷新状态 */
    void refreshAll();

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void loadSelection();
    void saveCurrent();
    void clearCurrent();
    void clearAll();
    void importCoordinates();
    void exportCoordinates();

private:
    using TargetKind = InstrumentCoordinates::TargetKind;
    using ZAxis = InstrumentCoordinates::ZAxis;

    enum ItemRole {
        KindRole = Qt::UserRole,
        IndexRole = Qt::UserRole + 1,
        NameRole = Qt::UserRole + 2,
        IsTargetRole = Qt::UserRole + 3
    };

    void buildUi();
    void rebuildTree();
    void refreshStatuses();
    void updateEditorEnabled();
    bool currentTarget(TargetKind *kind, int *index,
                       QString *name) const;
    QTreeWidgetItem *addTargetItem(QTreeWidgetItem *parent,
                                   const QString &text,
                                   TargetKind kind, int index,
                                   const QString &displayName);
    QTreeWidgetItem *rootItem(const QString &text);
    QTreeWidgetItem *findTargetItem(TargetKind kind, int index) const;
    QTreeWidgetItem *findTargetItem(QTreeWidgetItem *root,
                                    TargetKind kind, int index) const;
    void selectTarget(TargetKind kind, int index);
    QString statusText(TargetKind kind, int index) const;
    void setStateLabel(TargetKind kind, int index);
    void setFeedback(const QString &text, bool error = false);

    InstrumentCoordinates m_ownCoordinates;
    InstrumentCoordinates *m_coordinates = nullptr;

    QFrame *m_editorPanel = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_selectedLabel = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_feedbackLabel = nullptr;

    // 下标对应 ZAxis：0=抓手，1=试剂针，2=样本针
    QDoubleSpinBox *m_xSpins[3] = {};
    QDoubleSpinBox *m_ySpins[3] = {};
    QDoubleSpinBox *m_zSpins[3] = {};
    QCheckBox *m_toolChecks[3] = {};

    QPushButton *m_saveButton = nullptr;
    QPushButton *m_reloadButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QPushButton *m_clearAllButton = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_exportButton = nullptr;

    QString m_lastFileDir;   // 上次导入/导出的目录

    MachineModel m_lastModel = MachineModel::KS1200;
    int m_lastChannelCount = -1;
    int m_lastTubeCount = -1;
    int m_lastSampleWellCount = -1;
};

#endif // COORDINATEMAINTENANCEWIDGET_H
