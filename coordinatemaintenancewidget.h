#ifndef COORDINATEMAINTENANCEWIDGET_H
#define COORDINATEMAINTENANCEWIDGET_H

#include "instrumentcoordinates.h"
#include "medicalwidgets.h"

#include <QWidget>

class QCheckBox;
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

signals:
    /**
     * 定位校验触发：当前选中目标 + 工具已生成完整 XY/Z。
     * 单动作发送接口见 MotionController；组合动作接入后，
     * 由外部连接此信号按“先 XY、再 Z”下发。
     */
    void motionRequested(const InstrumentCoordinates::MoveTarget &target);

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void loadSelection();
    void saveCurrent();
    void importCoordinates();
    void exportCoordinates();
    void triggerVerificationMove();

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
    void updateVerificationPanel();
    bool saveToDefaultFile(QString *error);

    InstrumentCoordinates m_ownCoordinates;
    InstrumentCoordinates *m_coordinates = nullptr;

    QFrame *m_editorPanel = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_selectedLabel = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_feedbackLabel = nullptr;

    // 下标对应 ZAxis：0=抓手，1=试剂针，2=样本针
    MedicalDoubleSpinBox *m_xSpins[3] = {};
    MedicalDoubleSpinBox *m_ySpins[3] = {};
    MedicalDoubleSpinBox *m_zSpins[3] = {};
    QCheckBox *m_toolChecks[3] = {};

    QPushButton *m_saveButton = nullptr;
    QPushButton *m_reloadButton = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_exportButton = nullptr;

    // 定位校验（运动协议接入前的 UI 骨架）
    QFrame *m_verifyPanel = nullptr;
    QLabel *m_verifyTargetLabel = nullptr;
    QLabel *m_verifyPoseLabel = nullptr;
    QLabel *m_verifyHintLabel = nullptr;
    MedicalComboBox *m_verifyToolCombo = nullptr;
    QPushButton *m_verifyButton = nullptr;

    QString m_lastFileDir;   // 上次导入/导出的目录

    MachineModel m_lastModel = MachineModel::KS1200;
    int m_lastChannelCount = -1;
    int m_lastTubeCount = -1;
    int m_lastSampleWellCount = -1;
};

#endif // COORDINATEMAINTENANCEWIDGET_H
