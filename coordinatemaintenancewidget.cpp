#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "coordinatemaintenancewidget.h"

#include "coordinatefileio.h"

#include <QAbstractButton>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

constexpr double kXySpinMin = -10000.0;
constexpr double kXySpinMax = 10000.0;
constexpr double kZSpinMin = -1000.0;
constexpr double kZSpinMax = 1000.0;

const InstrumentCoordinates::ZAxis kZAxes[] = {
    InstrumentCoordinates::ZAxis::Gripper,
    InstrumentCoordinates::ZAxis::ReagentNeedle,
    InstrumentCoordinates::ZAxis::SampleNeedle
};

/** 坐标输入框：忽略滚轮，避免滚动页面时误改数值 */
class WheelSafeDoubleSpinBox : public QDoubleSpinBox
{
public:
    explicit WheelSafeDoubleSpinBox(QWidget *parent = nullptr)
        : QDoubleSpinBox(parent)
    {
    }

protected:
    void wheelEvent(QWheelEvent *event) override
    {
        event->ignore();   // 不修改数值，交给父级滚动区域
    }
};

QDoubleSpinBox *makeCoordinateSpin(QWidget *parent,
                                   double minimum, double maximum)
{
    auto *spin = new WheelSafeDoubleSpinBox(parent);
    spin->setRange(minimum, maximum);
    spin->setDecimals(3);
    spin->setSingleStep(0.1);
    spin->setKeyboardTracking(false);
    spin->setMinimumWidth(74);
    return spin;
}

QString validMark(bool valid)
{
    return valid
            ? QStringLiteral("<span style='color:#1E7B45;'>已设置</span>")
            : QStringLiteral("<span style='color:#8A9CAB;'>未设置</span>");
}

} // namespace

CoordinateMaintenanceWidget::CoordinateMaintenanceWidget(
        InstrumentCoordinates *coordinates, QWidget *parent)
    : QWidget(parent)
    , m_ownCoordinates()
    , m_coordinates(coordinates ? coordinates : &m_ownCoordinates)
{
    setObjectName(QStringLiteral("coordinateMaintenance"));
    m_lastFileDir =
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (m_lastFileDir.isEmpty()) {
        m_lastFileDir = QDir::homePath();
    }
    buildUi();
    rebuildTree();
}

void CoordinateMaintenanceWidget::buildUi()
{
    setStyleSheet(QStringLiteral(
        "QWidget#coordinateMaintenance { background: #F4F8FC; }"
        "QLabel#coordinateHint {"
        "  color: #6B8194;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QTreeWidget#coordinateTree {"
        "  background: #FFFFFF;"
        "  border: 1px solid #DCE7F1;"
        "  border-radius: 8px;"
        "  alternate-background-color: #F8FBFD;"
        "  color: #30475C;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QTreeWidget#coordinateTree::item {"
        "  min-height: 26px;"
        "  padding: 2px 4px;"
        "}"
        "QTreeWidget#coordinateTree::item:selected {"
        "  background: #DCEFFA;"
        "  color: #1F4E79;"
        "}"
        "QHeaderView::section {"
        "  background: #EAF3FA;"
        "  color: #1F4E79;"
        "  border: none;"
        "  padding: 6px 8px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QFrame#coordinateEditor {"
        "  background: #FFFFFF;"
        "  border: 1px solid #DCE7F1;"
        "  border-radius: 10px;"
        "}"
        "QLabel#coordinateEditorTitle {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 15px;"
        "  font-weight: 600;"
        "}"
        "QLabel#coordinateName {"
        "  color: #2C7BBF;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 14px;"
        "  font-weight: 600;"
        "}"
        "QLabel#coordinateState {"
        "  color: #5D7386;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QLabel#coordFeedback {"
        "  color: #1E7B45;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QLabel#poseHeader {"
        "  color: #8A9CAB;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 11px;"
        "}"
        "QDoubleSpinBox {"
        "  background: #FFFFFF;"
        "  border: 1px solid #C9DCEB;"
        "  border-radius: 5px;"
        "  padding: 4px 6px;"
        "  color: #1F2D3D;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QDoubleSpinBox:focus { border: 1px solid #5FA8D8; }"
        "QCheckBox {"
        "  color: #5D7386;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  spacing: 4px;"
        "}"
        "QPushButton#coordPrimary {"
        "  background: #2C7BBF;"
        "  color: #FFFFFF;"
        "  border: 1px solid #24699F;"
        "  border-radius: 6px;"
        "  padding: 7px 14px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QPushButton#coordPrimary:hover { background: #3A8DCE; }"
        "QPushButton#coordPrimary:pressed { background: #24699F; }"
        "QPushButton#coordSecondary {"
        "  background: #FFFFFF;"
        "  color: #1F4E79;"
        "  border: 1px solid #A9C8DF;"
        "  border-radius: 6px;"
        "  padding: 7px 12px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QPushButton#coordSecondary:hover { background: #E8F1FA; }"
        "QFrame#coordinateVerify {"
        "  background: #FFFFFF;"
        "  border: 1px solid #DCE7F1;"
        "  border-radius: 10px;"
        "}"
        "QLabel#verifyTitle {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "}"
        "QLabel#verifyPose {"
        "  color: #1F2D3D;"
        "  font-family: 'Consolas', 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QLabel#verifyHint {"
        "  color: #8A9CAB;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 11px;"
        "}"
        "QComboBox#verifyTool {"
        "  background: #FFFFFF;"
        "  border: 1px solid #C9DCEB;"
        "  border-radius: 5px;"
        "  padding: 3px 8px;"
        "  color: #30475C;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    auto *hint = new QLabel(
                tr("三根 Z 轴水平位置不同，每个工具需要各自的 X/Y："
                   "选择左侧位置后，分别填写抓手、试剂针、样本针的 X、Y "
                   "和下降 Z。勾选“启用”表示该工具参与定位。坐标单位 mm。"
                   "保存后自动写入本地坐标文件，软件启动时自动恢复。"),
                this);
    hint->setObjectName(QStringLiteral("coordinateHint"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto *body = new QHBoxLayout;
    body->setSpacing(14);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("coordinateTree"));
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({ tr("位置"), tr("状态") });
    m_tree->setRootIsDecorated(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setMinimumWidth(360);
    connect(m_tree, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem *, QTreeWidgetItem *) {
        loadSelection();
    });

    auto *leftColumn = new QVBoxLayout;
    leftColumn->setSpacing(10);
    leftColumn->addWidget(m_tree, 1);

    // 定位校验：当前选中目标 + 工具 → 生成 XY/Z 运动目标。
    // 运动控制协议尚未接入，按钮只发信号，不驱动仪器。
    m_verifyPanel = new QFrame(this);
    m_verifyPanel->setObjectName(QStringLiteral("coordinateVerify"));
    auto *verifyLayout = new QVBoxLayout(m_verifyPanel);
    verifyLayout->setContentsMargins(14, 10, 14, 10);
    verifyLayout->setSpacing(6);

    auto *verifyTitleRow = new QHBoxLayout;
    verifyTitleRow->setSpacing(8);
    auto *verifyTitle = new QLabel(tr("定位校验"), m_verifyPanel);
    verifyTitle->setObjectName(QStringLiteral("verifyTitle"));
    verifyTitleRow->addWidget(verifyTitle);
    m_verifyTargetLabel = new QLabel(tr("未选择目标"), m_verifyPanel);
    m_verifyTargetLabel->setObjectName(QStringLiteral("verifyHint"));
    verifyTitleRow->addWidget(m_verifyTargetLabel);
    verifyTitleRow->addStretch(1);
    verifyLayout->addLayout(verifyTitleRow);

    auto *verifyControlRow = new QHBoxLayout;
    verifyControlRow->setSpacing(8);
    auto *toolCaption = new QLabel(tr("工具"), m_verifyPanel);
    toolCaption->setObjectName(QStringLiteral("verifyHint"));
    verifyControlRow->addWidget(toolCaption);

    m_verifyToolCombo = new QComboBox(m_verifyPanel);
    m_verifyToolCombo->setObjectName(QStringLiteral("verifyTool"));
    m_verifyToolCombo->addItem(tr("抓手"),
                               static_cast<int>(ZAxis::Gripper));
    m_verifyToolCombo->addItem(tr("试剂针"),
                               static_cast<int>(ZAxis::ReagentNeedle));
    m_verifyToolCombo->addItem(tr("样本针"),
                               static_cast<int>(ZAxis::SampleNeedle));
    verifyControlRow->addWidget(m_verifyToolCombo);

    m_verifyPoseLabel = new QLabel(tr("X --   Y --   Z --"), m_verifyPanel);
    m_verifyPoseLabel->setObjectName(QStringLiteral("verifyPose"));
    verifyControlRow->addWidget(m_verifyPoseLabel, 1);

    m_verifyButton = new QPushButton(tr("触发移动"), m_verifyPanel);
    m_verifyButton->setObjectName(QStringLiteral("coordPrimary"));
    m_verifyButton->setCursor(Qt::PointingHandCursor);
    m_verifyButton->setToolTip(
                tr("生成当前目标 + 工具的运动目标；运动协议未接入前不会下发"));
    verifyControlRow->addWidget(m_verifyButton);
    verifyLayout->addLayout(verifyControlRow);

    m_verifyHintLabel = new QLabel(
                tr("运动控制协议未接入：点击只计算目标位置，不会驱动仪器。"),
                m_verifyPanel);
    m_verifyHintLabel->setObjectName(QStringLiteral("verifyHint"));
    m_verifyHintLabel->setWordWrap(true);
    verifyLayout->addWidget(m_verifyHintLabel);

    leftColumn->addWidget(m_verifyPanel, 0);
    body->addLayout(leftColumn, 1);

    m_editorPanel = new QFrame(this);
    m_editorPanel->setObjectName(QStringLiteral("coordinateEditor"));
    m_editorPanel->setFixedWidth(470);
    auto *editorLayout = new QVBoxLayout(m_editorPanel);
    editorLayout->setContentsMargins(18, 16, 18, 16);
    editorLayout->setSpacing(10);

    auto *editorTitle = new QLabel(tr("坐标编辑"), m_editorPanel);
    editorTitle->setObjectName(QStringLiteral("coordinateEditorTitle"));
    editorLayout->addWidget(editorTitle);

    m_selectedLabel = new QLabel(tr("未选择位置"), m_editorPanel);
    m_selectedLabel->setObjectName(QStringLiteral("coordinateName"));
    m_selectedLabel->setWordWrap(true);
    editorLayout->addWidget(m_selectedLabel);

    auto *poseGrid = new QGridLayout;
    poseGrid->setContentsMargins(0, 0, 0, 0);
    poseGrid->setHorizontalSpacing(6);
    poseGrid->setVerticalSpacing(8);

    const QStringList headers = {
        tr("工具"), tr("X"), tr("Y"), tr("Z"), tr("启用")
    };
    for (int column = 0; column < headers.size(); ++column) {
        auto *header = new QLabel(headers.at(column), m_editorPanel);
        header->setObjectName(QStringLiteral("poseHeader"));
        poseGrid->addWidget(header, 0, column);
    }

    const QStringList toolLabels = {
        tr("抓手"), tr("试剂针"), tr("样本针")
    };
    for (int axis = 0; axis < 3; ++axis) {
        auto *toolLabel = new QLabel(toolLabels.at(axis), m_editorPanel);
        toolLabel->setObjectName(QStringLiteral("coordinateState"));
        poseGrid->addWidget(toolLabel, axis + 1, 0);

        m_xSpins[axis] = makeCoordinateSpin(
                    m_editorPanel, kXySpinMin, kXySpinMax);
        m_ySpins[axis] = makeCoordinateSpin(
                    m_editorPanel, kXySpinMin, kXySpinMax);
        m_zSpins[axis] = makeCoordinateSpin(
                    m_editorPanel, kZSpinMin, kZSpinMax);
        m_xSpins[axis]->setToolTip(tr("该工具的 X 坐标（mm）"));
        m_ySpins[axis]->setToolTip(tr("该工具的 Y 坐标（mm）"));
        m_zSpins[axis]->setToolTip(tr("该工具在此位置的下降高度 Z（mm）"));

        poseGrid->addWidget(m_xSpins[axis], axis + 1, 1);
        poseGrid->addWidget(m_ySpins[axis], axis + 1, 2);
        poseGrid->addWidget(m_zSpins[axis], axis + 1, 3);

        m_toolChecks[axis] = new QCheckBox(m_editorPanel);
        m_toolChecks[axis]->setToolTip(tr("启用该工具坐标"));
        poseGrid->addWidget(m_toolChecks[axis], axis + 1, 4,
                             Qt::AlignCenter);
    }
    for (int column = 1; column <= 3; ++column) {
        poseGrid->setColumnStretch(column, 1);
    }
    editorLayout->addLayout(poseGrid);

    m_stateLabel = new QLabel(m_editorPanel);
    m_stateLabel->setObjectName(QStringLiteral("coordinateState"));
    m_stateLabel->setWordWrap(true);
    editorLayout->addWidget(m_stateLabel);

    auto *buttonGrid = new QGridLayout;
    buttonGrid->setHorizontalSpacing(8);
    buttonGrid->setVerticalSpacing(8);

    m_saveButton = new QPushButton(tr("保存坐标"), m_editorPanel);
    m_saveButton->setObjectName(QStringLiteral("coordPrimary"));
    m_saveButton->setCursor(Qt::PointingHandCursor);
    m_saveButton->setToolTip(
                tr("写入内存并保存到默认坐标文件：\n%1")
                .arg(QDir::toNativeSeparators(
                         CoordinateFileIo::defaultFilePath())));

    m_reloadButton = new QPushButton(tr("重新读取"), m_editorPanel);
    m_reloadButton->setObjectName(QStringLiteral("coordSecondary"));
    m_reloadButton->setCursor(Qt::PointingHandCursor);

    m_importButton = new QPushButton(tr("导入坐标..."), m_editorPanel);
    m_importButton->setObjectName(QStringLiteral("coordSecondary"));
    m_importButton->setCursor(Qt::PointingHandCursor);
    m_importButton->setToolTip(tr("从 JSON 文件导入坐标并覆盖当前内存坐标"));

    m_exportButton = new QPushButton(tr("导出坐标..."), m_editorPanel);
    m_exportButton->setObjectName(QStringLiteral("coordSecondary"));
    m_exportButton->setCursor(Qt::PointingHandCursor);
    m_exportButton->setToolTip(tr("把当前机型的所有已设置坐标导出为 JSON 文件"));

    buttonGrid->addWidget(m_saveButton, 0, 0);
    buttonGrid->addWidget(m_reloadButton, 0, 1);
    buttonGrid->addWidget(m_importButton, 1, 0);
    buttonGrid->addWidget(m_exportButton, 1, 1);
    editorLayout->addLayout(buttonGrid);

    m_feedbackLabel = new QLabel(m_editorPanel);
    m_feedbackLabel->setObjectName(QStringLiteral("coordFeedback"));
    m_feedbackLabel->setWordWrap(true);
    editorLayout->addWidget(m_feedbackLabel);
    editorLayout->addStretch(1);

    connect(m_saveButton, &QPushButton::clicked,
            this, &CoordinateMaintenanceWidget::saveCurrent);
    connect(m_reloadButton, &QPushButton::clicked,
            this, &CoordinateMaintenanceWidget::loadSelection);
    connect(m_importButton, &QPushButton::clicked,
            this, &CoordinateMaintenanceWidget::importCoordinates);
    connect(m_exportButton, &QPushButton::clicked,
            this, &CoordinateMaintenanceWidget::exportCoordinates);
    connect(m_verifyToolCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateVerificationPanel(); });
    connect(m_verifyButton, &QPushButton::clicked,
            this, &CoordinateMaintenanceWidget::triggerVerificationMove);

    body->addWidget(m_editorPanel, 0);
    root->addLayout(body, 1);
}

QTreeWidgetItem *CoordinateMaintenanceWidget::rootItem(const QString &text)
{
    auto *item = new QTreeWidgetItem(m_tree);
    item->setText(0, text);
    item->setFirstColumnSpanned(true);
    item->setFlags(Qt::ItemIsEnabled);
    return item;
}

QTreeWidgetItem *CoordinateMaintenanceWidget::addTargetItem(
        QTreeWidgetItem *parent, const QString &text,
        TargetKind kind, int index, const QString &displayName)
{
    auto *item = parent
            ? new QTreeWidgetItem(parent)
            : new QTreeWidgetItem(m_tree);
    item->setText(0, text);
    item->setData(0, KindRole, static_cast<int>(kind));
    item->setData(0, IndexRole, index);
    item->setData(0, NameRole, displayName);
    item->setData(0, IsTargetRole, true);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return item;
}

void CoordinateMaintenanceWidget::rebuildTree()
{
    if (!m_coordinates || !m_tree) {
        return;
    }

    TargetKind previousKind = TargetKind::Origin;
    int previousIndex = 0;
    QString previousName;
    const bool hadPrevious = currentTarget(&previousKind, &previousIndex,
                                           &previousName);

    m_tree->clear();

    addTargetItem(nullptr, tr("原点"),
                  TargetKind::Origin, 0, tr("原点"));
    addTargetItem(nullptr, tr("清洗孔位"),
                  TargetKind::CleaningHole, 0, tr("清洗孔位"));
    addTargetItem(nullptr, tr("弃杯孔位"),
                  TargetKind::DiscardCup, 0, tr("弃杯孔位"));

    QTreeWidgetItem *channelRoot = rootItem(tr("通道"));
    const int channelCount = m_coordinates->channelCount();
    for (int i = 0; i < channelCount; ++i) {
        const QString name = tr("CH%1").arg(i + 1);
        addTargetItem(channelRoot, name, TargetKind::Channel, i, name);
    }
    channelRoot->setExpanded(true);

    QTreeWidgetItem *tubeRoot = rootItem(tr("试管"));
    const int trayCount = m_coordinates->tubeTrayCount();
    const int tubeCount = m_coordinates->tubeCount();
    for (int tray = 0; tray < trayCount; ++tray) {
        auto *trayItem = new QTreeWidgetItem(tubeRoot);
        trayItem->setText(0, tr("试管盘 %1").arg(tray + 1));
        trayItem->setFirstColumnSpanned(true);
        trayItem->setFlags(Qt::ItemIsEnabled);

        for (int position = 0;
             position < InstrumentCoordinates::kTubesPerTray; ++position) {
            const int index = tray * InstrumentCoordinates::kTubesPerTray
                    + position;
            if (index >= tubeCount) {
                break;
            }
            addTargetItem(
                        trayItem, tr("管 %1").arg(position + 1),
                        TargetKind::Tube, index,
                        tr("试管盘 %1 · 管 %2")
                        .arg(tray + 1).arg(position + 1));
        }
    }
    tubeRoot->setExpanded(false);

    QTreeWidgetItem *reagentRoot = rootItem(tr("试剂位"));
    const int reagentCount = m_coordinates->reagentCount();
    for (int i = 0; i < reagentCount; ++i) {
        const QString name =
                InstrumentCoordinates::reagentPositionName(i);
        addTargetItem(reagentRoot, name, TargetKind::Reagent, i, name);
    }
    reagentRoot->setExpanded(true);

    QTreeWidgetItem *wellRoot = rootItem(tr("样本孔"));
    const int wellCount = m_coordinates->sampleWellCount();
    for (int i = 0; i < wellCount; ++i) {
        const QString name = tr("样本孔 %1").arg(i + 1);
        addTargetItem(wellRoot, name, TargetKind::SampleWell, i, name);
    }
    wellRoot->setExpanded(false);

    m_lastModel = m_coordinates->machineModel();
    m_lastChannelCount = channelCount;
    m_lastTubeCount = tubeCount;
    m_lastSampleWellCount = wellCount;

    refreshStatuses();
    if (hadPrevious) {
        selectTarget(previousKind, previousIndex);
    }
    if (!m_tree->currentItem()) {
        selectTarget(TargetKind::Origin, 0);
    }
    loadSelection();
}

void CoordinateMaintenanceWidget::refreshAll()
{
    if (!m_coordinates || !m_tree) {
        return;
    }

    const bool needsRebuild =
            m_tree->topLevelItemCount() == 0
            || m_lastModel != m_coordinates->machineModel()
            || m_lastChannelCount != m_coordinates->channelCount()
            || m_lastTubeCount != m_coordinates->tubeCount()
            || m_lastSampleWellCount != m_coordinates->sampleWellCount();

    if (needsRebuild) {
        rebuildTree();
    } else {
        refreshStatuses();
        loadSelection();
    }
}

QString CoordinateMaintenanceWidget::statusText(TargetKind kind,
                                                int index) const
{
    if (!m_coordinates) {
        return QString();
    }

    int validCount = 0;
    for (ZAxis axis : kZAxes) {
        if (m_coordinates->toolPose(kind, index, axis).valid()) {
            ++validCount;
        }
    }

    if (validCount == 3) {
        return tr("已设置");
    }
    if (validCount > 0) {
        return tr("部分设置 (%1/3)").arg(validCount);
    }
    return tr("未设置");
}

void CoordinateMaintenanceWidget::refreshStatuses()
{
    if (!m_tree || !m_coordinates) {
        return;
    }

    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        QTreeWidgetItem *item = *it;
        if (item->data(0, IsTargetRole).toBool()) {
            const TargetKind kind = static_cast<TargetKind>(
                        item->data(0, KindRole).toInt());
            const int index = item->data(0, IndexRole).toInt();
            const QString text = statusText(kind, index);
            item->setText(1, text);
            item->setTextAlignment(1, Qt::AlignCenter);

            int validCount = 0;
            QStringList tips;
            for (ZAxis axis : kZAxes) {
                const bool valid = m_coordinates
                        ? m_coordinates->toolPose(kind, index, axis).valid()
                        : false;
                if (valid) {
                    ++validCount;
                }
                tips << tr("%1：%2")
                        .arg(InstrumentCoordinates::zAxisName(axis))
                        .arg(valid ? tr("已设置") : tr("未设置"));
            }
            item->setToolTip(1, tips.join(QLatin1Char('\n')));

            QColor color("#8A9CAB");
            if (validCount == 3) {
                color = QColor("#1E7B45");
            } else if (validCount > 0) {
                color = QColor("#B7791F");
            }
            item->setForeground(1, QBrush(color));
        }
        ++it;
    }
}

bool CoordinateMaintenanceWidget::currentTarget(TargetKind *kind,
                                                int *index,
                                                QString *name) const
{
    if (!m_tree) {
        return false;
    }
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item || !item->data(0, IsTargetRole).toBool()) {
        return false;
    }
    if (kind) {
        *kind = static_cast<TargetKind>(item->data(0, KindRole).toInt());
    }
    if (index) {
        *index = item->data(0, IndexRole).toInt();
    }
    if (name) {
        *name = item->data(0, NameRole).toString();
    }
    return true;
}

void CoordinateMaintenanceWidget::setStateLabel(TargetKind kind, int index)
{
    if (!m_stateLabel || !m_coordinates) {
        return;
    }

    const auto toolMark = [this, kind, index](ZAxis axis) {
        return validMark(m_coordinates->toolPose(kind, index, axis).valid());
    };

    m_stateLabel->setText(
                tr("抓手：%1　试剂针：%2　样本针：%3")
                .arg(toolMark(ZAxis::Gripper))
                .arg(toolMark(ZAxis::ReagentNeedle))
                .arg(toolMark(ZAxis::SampleNeedle)));
}

void CoordinateMaintenanceWidget::updateEditorEnabled()
{
    if (!m_editorPanel) {
        return;
    }
    TargetKind kind;
    int index = -1;
    QString name;
    m_editorPanel->setEnabled(currentTarget(&kind, &index, &name));
}

void CoordinateMaintenanceWidget::loadSelection()
{
    if (!m_coordinates || !m_editorPanel) {
        return;
    }
    if (m_feedbackLabel) {
        m_feedbackLabel->clear();
    }

    TargetKind kind;
    int index = -1;
    QString name;
    if (!currentTarget(&kind, &index, &name)) {
        m_selectedLabel->setText(tr("未选择位置"));
        m_stateLabel->clear();
        m_editorPanel->setEnabled(false);
        updateVerificationPanel();
        return;
    }

    const ZAxis axes[3] = {
        ZAxis::Gripper, ZAxis::ReagentNeedle, ZAxis::SampleNeedle
    };
    for (int i = 0; i < 3; ++i) {
        const auto pose = m_coordinates->toolPose(kind, index, axes[i]);
        m_toolChecks[i]->setChecked(pose.xyValid || pose.zValid);
        m_xSpins[i]->setValue(pose.xyValid ? pose.x : 0.0);
        m_ySpins[i]->setValue(pose.xyValid ? pose.y : 0.0);
        m_zSpins[i]->setValue(pose.zValid ? pose.z : 0.0);
    }

    m_selectedLabel->setText(name);
    setStateLabel(kind, index);
    m_editorPanel->setEnabled(true);
    updateVerificationPanel();
}

void CoordinateMaintenanceWidget::saveCurrent()
{
    TargetKind kind;
    int index = -1;
    QString name;
    if (!m_coordinates || !currentTarget(&kind, &index, &name)) {
        return;
    }

    const ZAxis axes[3] = {
        ZAxis::Gripper, ZAxis::ReagentNeedle, ZAxis::SampleNeedle
    };
    bool ok = true;
    for (int i = 0; i < 3; ++i) {
        if (m_toolChecks[i]->isChecked()) {
            ok = m_coordinates->setXy(
                        kind, index, axes[i],
                        m_xSpins[i]->value(), m_ySpins[i]->value()) && ok;
            ok = m_coordinates->setZ(
                        kind, index, axes[i],
                        m_zSpins[i]->value()) && ok;
        } else {
            m_coordinates->clearPose(kind, index, axes[i]);
        }
    }

    QString persistError;
    const bool persisted = ok && saveToDefaultFile(&persistError);

    refreshStatuses();
    loadSelection();

    if (!ok) {
        setFeedback(tr("保存失败：位置索引超出当前机型范围"), true);
    } else if (persisted) {
        setFeedback(tr("已保存：%1（已写入坐标文件）").arg(name), false);
    } else {
        setFeedback(tr("已保存到内存，但写入坐标文件失败：%1")
                    .arg(persistError), true);
    }
}

void CoordinateMaintenanceWidget::importCoordinates()
{
    if (!m_coordinates) {
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
                this, tr("导入坐标文件"), m_lastFileDir,
                tr("坐标文件 (*.json);;所有文件 (*)"));
    if (path.isEmpty()) {
        return;
    }
    m_lastFileDir = QFileInfo(path).absolutePath();

    CoordinateDocument document;
    QString error;
    if (!CoordinateFileIo::load(path, &document, &error)) {
        setFeedback(tr("导入失败：%1").arg(error), true);
        QMessageBox::warning(
                    this, tr("导入坐标"),
                    tr("无法读取坐标文件：\n%1").arg(error));
        return;
    }

    const QString fileModel = document.machineModel.isEmpty()
            ? tr("未标注") : document.machineModel;
    const QString currentModel =
            QString::fromLatin1(machineModelName(m_coordinates->machineModel()));

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("导入坐标"));
    box.setText(tr("确定用该文件覆盖当前机型的全部坐标吗？"));
    box.setInformativeText(
                tr("文件：%1\n文件机型：%2\n当前机型：%3\n坐标项：%4 个\n\n"
                   "导入前会清空当前坐标并写回坐标文件；"
                   "超出当前机型范围的位置将被跳过。")
                .arg(QFileInfo(path).fileName())
                .arg(fileModel)
                .arg(currentModel)
                .arg(document.entries.size()));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    if (QAbstractButton *yesButton = box.button(QMessageBox::Yes)) {
        yesButton->setText(tr("确定导入"));
    }
    if (QAbstractButton *noButton = box.button(QMessageBox::No)) {
        noButton->setText(tr("取消"));
    }
    if (box.exec() != QMessageBox::Yes) {
        return;
    }

    const CoordinateFileIo::ApplyResult result =
            CoordinateFileIo::apply(document, m_coordinates);
    QString persistError;
    const bool persisted = saveToDefaultFile(&persistError);
    refreshStatuses();
    loadSelection();

    QString message = tr("已导入 %1 个坐标项").arg(result.applied);
    if (result.skipped > 0) {
        message += tr("，跳过 %1 个超出当前机型范围的位置")
                .arg(result.skipped);
    }
    if (result.ignoredTools > 0) {
        message += tr("，忽略 %1 组越界数值").arg(result.ignoredTools);
    }

    bool hasError = result.skipped > 0 || result.ignoredTools > 0;
    if (persisted) {
        message += tr("，已写入坐标文件");
    } else {
        message += tr("，但写入坐标文件失败：%1").arg(persistError);
        hasError = true;
    }
    setFeedback(message, hasError);
}

void CoordinateMaintenanceWidget::exportCoordinates()
{
    if (!m_coordinates) {
        return;
    }

    const QString model =
            QString::fromLatin1(machineModelName(m_coordinates->machineModel()));
    const QString defaultName = QStringLiteral("coordinates_%1_%2.json")
            .arg(model,
                 QDateTime::currentDateTime().toString(
                     QStringLiteral("yyyyMMdd_HHmmss")));
    const QString initialPath = m_lastFileDir.isEmpty()
            ? defaultName
            : QDir(m_lastFileDir).filePath(defaultName);

    QString path = QFileDialog::getSaveFileName(
                this, tr("导出坐标文件"), initialPath,
                tr("坐标文件 (*.json);;所有文件 (*)"));
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
        path += QStringLiteral(".json");
    }
    m_lastFileDir = QFileInfo(path).absolutePath();

    QString error;
    if (!CoordinateFileIo::save(*m_coordinates, path, &error)) {
        setFeedback(tr("导出失败：%1").arg(error), true);
        QMessageBox::warning(
                    this, tr("导出坐标"),
                    tr("无法写入坐标文件：\n%1").arg(error));
        return;
    }

    setFeedback(tr("已导出坐标：%1")
                .arg(QDir::toNativeSeparators(path)), false);
}

void CoordinateMaintenanceWidget::updateVerificationPanel()
{
    if (!m_verifyPanel || !m_verifyTargetLabel || !m_verifyPoseLabel
            || !m_verifyToolCombo || !m_verifyButton) {
        return;
    }

    TargetKind kind;
    int index = -1;
    QString name;
    const bool hasTarget =
            m_coordinates && currentTarget(&kind, &index, &name);

    m_verifyToolCombo->setEnabled(hasTarget);
    if (!hasTarget) {
        m_verifyTargetLabel->setText(tr("未选择目标"));
        m_verifyPoseLabel->setText(tr("X --   Y --   Z --"));
        m_verifyButton->setEnabled(false);
        return;
    }

    const ZAxis axis = static_cast<ZAxis>(
                m_verifyToolCombo->currentData().toInt());
    const auto target = m_coordinates->locate(kind, index, axis);
    m_verifyTargetLabel->setText(tr("目标：%1").arg(name));

    const QString x = target.xyValid
            ? QString::number(target.x, 'f', 3) : QStringLiteral("--");
    const QString y = target.xyValid
            ? QString::number(target.y, 'f', 3) : QStringLiteral("--");
    const QString z = target.zValid
            ? QString::number(target.z, 'f', 3) : QStringLiteral("--");
    m_verifyPoseLabel->setText(tr("X %1   Y %2   Z %3").arg(x, y, z));

    m_verifyButton->setEnabled(target.valid());
    m_verifyButton->setToolTip(
                target.valid()
                ? tr("生成当前目标 + 工具的运动目标；运动协议未接入前不会下发")
                : tr("该工具的 XY 或 Z 尚未设置完整"));
}

void CoordinateMaintenanceWidget::triggerVerificationMove()
{
    TargetKind kind;
    int index = -1;
    QString name;
    if (!m_coordinates || !currentTarget(&kind, &index, &name)) {
        return;
    }

    const ZAxis axis = static_cast<ZAxis>(
                m_verifyToolCombo->currentData().toInt());
    const auto target = m_coordinates->locate(kind, index, axis);
    if (!target.valid()) {
        setFeedback(tr("定位校验：%1 的%2坐标未设置完整")
                    .arg(name,
                         InstrumentCoordinates::zAxisName(axis)), true);
        return;
    }

    setFeedback(
                tr("已生成运动目标：%1 · %2 → X %3, Y %4, Z %5；"
                   "运动协议未接入，未下发")
                .arg(name, InstrumentCoordinates::zAxisName(axis))
                .arg(target.x, 0, 'f', 3)
                .arg(target.y, 0, 'f', 3)
                .arg(target.z, 0, 'f', 3),
                false);
    emit motionRequested(target);
}

bool CoordinateMaintenanceWidget::saveToDefaultFile(QString *error)
{
    if (!m_coordinates) {
        if (error) {
            *error = tr("坐标表不可用");
        }
        return false;
    }
    return CoordinateFileIo::save(
                *m_coordinates,
                CoordinateFileIo::defaultFilePath(),
                error);
}

QTreeWidgetItem *CoordinateMaintenanceWidget::findTargetItem(
        TargetKind kind, int index) const
{
    if (!m_tree) {
        return nullptr;
    }
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        if (QTreeWidgetItem *item =
                findTargetItem(m_tree->topLevelItem(i), kind, index)) {
            return item;
        }
    }
    return nullptr;
}

QTreeWidgetItem *CoordinateMaintenanceWidget::findTargetItem(
        QTreeWidgetItem *root, TargetKind kind, int index) const
{
    if (!root) {
        return nullptr;
    }
    if (root->data(0, IsTargetRole).toBool()
            && root->data(0, KindRole).toInt() == static_cast<int>(kind)
            && root->data(0, IndexRole).toInt() == index) {
        return root;
    }
    for (int i = 0; i < root->childCount(); ++i) {
        if (QTreeWidgetItem *item =
                findTargetItem(root->child(i), kind, index)) {
            return item;
        }
    }
    return nullptr;
}

void CoordinateMaintenanceWidget::selectTarget(TargetKind kind, int index)
{
    QTreeWidgetItem *item = findTargetItem(kind, index);
    if (!item) {
        return;
    }
    for (QTreeWidgetItem *parent = item->parent();
         parent; parent = parent->parent()) {
        parent->setExpanded(true);
    }
    m_tree->setCurrentItem(item);
    item->setSelected(true);
}

void CoordinateMaintenanceWidget::setFeedback(const QString &text,
                                              bool error)
{
    if (!m_feedbackLabel) {
        return;
    }
    m_feedbackLabel->setText(text);
    m_feedbackLabel->setStyleSheet(
                error
                ? QStringLiteral("color: #B03A2E;")
                : QStringLiteral("color: #1E7B45;"));
}

void CoordinateMaintenanceWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshAll();
}
