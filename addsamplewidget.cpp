#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "addsamplewidget.h"

#include "appconfig.h"
#include "medicalwidgets.h"
#include "testdatastore.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QLinearGradient>
#include <QMenu>
#include <QMessageBox>
#include "MedicalMessageBox.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPushButton>
#include <QRadialGradient>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSet>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextLayout>
#include <QTextLine>
#include <QTextOption>
#include <QToolTip>
#include <QVBoxLayout>
#include <QVector>
#include <QtMath>

#include <functional>
#include <utility>

namespace {

enum SampleColumn {
    StatusColumn = 0,
    SampleNoColumn,
    BarcodeColumn,
    TestItemColumn,
    HeightColumn,
    PositionColumn,
    SampleColumnCount
};

const int kSampleRowHeight = 40;
const int kHeightSourceRole = Qt::UserRole + 1;
const char kPlasmaAutoHeightSource[] = "plasmaAuto";

QPointF lerpPoint(const QPointF &from, const QPointF &to, qreal t)
{
    return from + (to - from) * t;
}

QString groupLetter(int index)
{
    return QString(QChar('A' + index));
}

QString normalizedSampleNumber(QString number)
{
    number = number.trimmed();
    if (number.startsWith(QLatin1Char('t'))) {
        number[0] = QLatin1Char('T');
    }
    return number;
}

QString editorStyleSheet()
{
    return QStringLiteral(
                "QComboBox {"
                "  border: 1px solid #A9C8DF;"
                "  border-radius: 4px;"
                "  background: #FFFFFF;"
                "  color: #1F4E79;"
                "  padding: 1px 4px;"
                "  font-family: 'Microsoft YaHei';"
                "  font-size: 12px;"
                "}");
}

const QStringList &testItemCatalog()
{
    static const QStringList items = {
        QStringLiteral("AA"), QStringLiteral("ADP"),
        QStringLiteral("COL"), QStringLiteral("EPI"),
        QStringLiteral("RIS")
    };
    return items;
}

/** 去重并按 AA/ADP/COL/EPI/RIS 的固定顺序整理 */
QStringList canonicalTestItemList(const QStringList &items)
{
    QStringList picked;
    for (const QString &raw : items) {
        const QString item = raw.trimmed().toUpper();
        if (testItemCatalog().contains(item) && !picked.contains(item)) {
            picked << item;
        }
    }
    QStringList ordered;
    for (const QString &item : testItemCatalog()) {
        if (picked.contains(item)) {
            ordered << item;
        }
    }
    return ordered;
}

QString canonicalTestItemText(const QStringList &items)
{
    return canonicalTestItemList(items).join(QLatin1Char('+'));
}

QStringList splitTestItemText(const QString &text)
{
    return canonicalTestItemList(
                text.split(QLatin1Char('+'), QString::SkipEmptyParts));
}

QStringList loadTestItemCombinations()
{
    QSettings settings;
    const QStringList stored = settings.value(
                QString::fromLatin1(testItemCombinationsSettingsKey()))
            .toStringList();
    QStringList combinations;
    for (const QString &entry : stored) {
        const QString combination = canonicalTestItemText(
                    entry.split(QLatin1Char('+'), QString::SkipEmptyParts));
        if (!combination.isEmpty()
                && !combinations.contains(combination)) {
            combinations << combination;
        }
    }
    return combinations;
}

void saveTestItemCombinations(const QStringList &combinations)
{
    QSettings settings;
    settings.setValue(
                QString::fromLatin1(testItemCombinationsSettingsKey()),
                combinations);
}

QString testItemDialogStyleSheet()
{
    return QStringLiteral(
        "QDialog { background: #F7FAFD; }"
        "QLabel {"
        "  color: #5D7386;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QCheckBox {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "  spacing: 8px;"
        "  padding: 3px;"
        "}"
        "QPushButton {"
        "  min-width: 70px;"
        "  min-height: 28px;"
        "  border: 1px solid #A9C8DF;"
        "  border-radius: 5px;"
        "  background: #FFFFFF;"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  padding: 2px 10px;"
        "}"
        "QPushButton:hover { background: #EAF5FD; }"
        "QPushButton:pressed { background: #D3E6F5; }"
        "QPushButton:disabled { color: #9FB0BF; background: #F0F4F8; }"
        "QPushButton#comboAction { min-width: 70px; min-height: 26px; }"
        "QScrollArea {"
        "  border: 1px solid #D6E6F2;"
        "  border-radius: 6px;"
        "  background: #FFFFFF;"
        "}");
}

/** 新增/修改一个常用组合：只选择组合包含的试剂。 */
class TestItemCombinationDialog : public QDialog
{
public:
    explicit TestItemCombinationDialog(const QStringList &selected,
                                       bool editing,
                                       QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(editing ? tr("修改组合") : tr("添加组合"));
        setModal(true);
        setMinimumWidth(360);
        setStyleSheet(testItemDialogStyleSheet());

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(18, 16, 18, 16);
        layout->setSpacing(10);

        auto *hint = new QLabel(
                    tr("勾选这个组合包含的试剂；组合名称会按固定顺序自动生成。"),
                    this);
        hint->setWordWrap(true);
        layout->addWidget(hint);

        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(16);
        grid->setVerticalSpacing(4);
        int index = 0;
        for (const QString &item : testItemCatalog()) {
            auto *check = new MedicalCheckBox(item, this);
            check->setChecked(selected.contains(item));
            m_checks.append(check);
            grid->addWidget(check, index / 3, index % 3);
            ++index;
            connect(check, &QCheckBox::toggled,
                    this, [this](bool) { updateState(); });
        }
        layout->addLayout(grid);

        m_summary = new QLabel(this);
        m_summary->setStyleSheet(
                    QStringLiteral("color: #2C7BBF; font-weight: 600;"));
        layout->addWidget(m_summary);

        auto *buttons = new QDialogButtonBox(
                    QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                    this);
        m_okButton = buttons->button(QDialogButtonBox::Ok);
        if (m_okButton) {
            m_okButton->setText(tr("确定"));
        }
        if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel)) {
            cancel->setText(tr("取消"));
        }
        connect(buttons, &QDialogButtonBox::accepted,
                this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected,
                this, &QDialog::reject);
        layout->addWidget(buttons);

        updateState();
    }

    QStringList selectedItems() const
    {
        QStringList items;
        for (QCheckBox *check : m_checks) {
            if (check->isChecked()) {
                items << check->text();
            }
        }
        return canonicalTestItemList(items);
    }

private:
    void updateState()
    {
        const QStringList items = selectedItems();
        m_summary->setText(
                    items.isEmpty()
                    ? tr("请至少选择一个试剂")
                    : tr("组合：%1").arg(items.join(QLatin1Char('+'))));
        if (m_okButton) {
            m_okButton->setEnabled(!items.isEmpty());
        }
    }

    QList<QCheckBox *> m_checks;
    QLabel *m_summary = nullptr;
    QPushButton *m_okButton = nullptr;
};

/**
 * 测试项目选择：
 * - 已有组合：勾选一个直接使用，可添加/修改/删除；
 * - 自由组合：直接勾选 AA/ADP/COL/EPI/RIS，组成临时组合；
 * - 两种方式互斥：勾已有组合会清空自由勾选，勾任一自由试剂会取消已有组合。
 */
class TestItemDialog : public QDialog
{
public:
    explicit TestItemDialog(const QStringList &selected,
                            QWidget *parent = nullptr)
        : QDialog(parent)
        , m_combinations(loadTestItemCombinations())
    {
        setWindowTitle(tr("选择测试项目"));
        setModal(true);
        setMinimumSize(520, 470);
        setStyleSheet(testItemDialogStyleSheet());

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(18, 16, 18, 16);
        layout->setSpacing(10);

        auto *hint = new QLabel(
                    tr("可以直接勾选一个已有组合，也可以在下方自由勾选试剂。"
                       "两种方式是互斥的，勾选一边会自动取消另一边。"),
                    this);
        hint->setWordWrap(true);
        layout->addWidget(hint);

        auto *comboHeader = new QHBoxLayout;
        comboHeader->setSpacing(6);
        auto *comboTitle = new QLabel(tr("已有组合"), this);
        comboTitle->setStyleSheet(
                    QStringLiteral("color: #1F4E79; font-weight: 600;"));
        comboHeader->addWidget(comboTitle);
        comboHeader->addStretch(1);

        m_addComboButton = new QPushButton(tr("添加组合"), this);
        m_addComboButton->setObjectName(QStringLiteral("comboAction"));
        m_modifyComboButton = new QPushButton(tr("修改组合"), this);
        m_modifyComboButton->setObjectName(QStringLiteral("comboAction"));
        m_deleteComboButton = new QPushButton(tr("删除组合"), this);
        m_deleteComboButton->setObjectName(QStringLiteral("comboAction"));
        comboHeader->addWidget(m_addComboButton);
        comboHeader->addWidget(m_modifyComboButton);
        comboHeader->addWidget(m_deleteComboButton);
        layout->addLayout(comboHeader);

        m_comboScroll = new QScrollArea(this);
        m_comboScroll->setWidgetResizable(true);
        m_comboScroll->setMinimumHeight(88);
        m_comboScroll->setMaximumHeight(132);
        m_comboScroll->setFrameShape(QFrame::StyledPanel);
        m_comboScroll->setHorizontalScrollBarPolicy(
                    Qt::ScrollBarAlwaysOff);
        layout->addWidget(m_comboScroll);

        auto *freeTitle = new QLabel(tr("自由组合（临时）"), this);
        freeTitle->setStyleSheet(
                    QStringLiteral("color: #1F4E79; font-weight: 600;"));
        layout->addWidget(freeTitle);

        auto *reagentGrid = new QGridLayout;
        reagentGrid->setHorizontalSpacing(16);
        reagentGrid->setVerticalSpacing(4);
        int index = 0;
        for (const QString &item : testItemCatalog()) {
            auto *check = new MedicalCheckBox(item, this);
            m_reagentChecks.append(check);
            reagentGrid->addWidget(check, index / 3, index % 3);
            ++index;
            connect(check, &QCheckBox::toggled,
                    this, [this](bool checked) {
                if (m_updatingSelection) {
                    return;
                }
                if (checked) {
                    m_updatingSelection = true;
                    for (QCheckBox *combo : m_comboChecks) {
                        combo->setChecked(false);
                    }
                    m_updatingSelection = false;
                }
                updateState();
            });
        }
        layout->addLayout(reagentGrid);

        m_summary = new QLabel(this);
        m_summary->setWordWrap(true);
        m_summary->setStyleSheet(
                    QStringLiteral("color: #2C7BBF; font-weight: 600;"
                                   " padding: 4px;"));
        layout->addWidget(m_summary);

        auto *buttons = new QDialogButtonBox(
                    QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                    this);
        m_okButton = buttons->button(QDialogButtonBox::Ok);
        if (m_okButton) {
            m_okButton->setText(tr("确定"));
        }
        if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel)) {
            cancel->setText(tr("取消"));
        }
        connect(buttons, &QDialogButtonBox::accepted,
                this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected,
                this, &QDialog::reject);
        layout->addWidget(buttons);

        connect(m_addComboButton, &QPushButton::clicked,
                this, &TestItemDialog::addCombination);
        connect(m_modifyComboButton, &QPushButton::clicked,
                this, &TestItemDialog::modifyCombination);
        connect(m_deleteComboButton, &QPushButton::clicked,
                this, &TestItemDialog::deleteCombination);

        rebuildComboChecks(QString());
        applyInitialSelection(selected);
        updateState();
    }

    QStringList selectedItems() const
    {
        const int combo = selectedComboIndex();
        if (combo >= 0) {
            return splitTestItemText(m_combinations.at(combo));
        }
        QStringList items;
        for (QCheckBox *check : m_reagentChecks) {
            if (check->isChecked()) {
                items << check->text();
            }
        }
        return canonicalTestItemList(items);
    }

private:
    int selectedComboIndex() const
    {
        for (int i = 0; i < m_comboChecks.size(); ++i) {
            if (m_comboChecks.at(i)->isChecked()) {
                return i;
            }
        }
        return -1;
    }

    void rebuildComboChecks(const QString &selectedCombo)
    {
        m_comboChecks.clear();

        auto *container = new QWidget;
        container->setStyleSheet(QStringLiteral("background: #FFFFFF;"));
        auto *listLayout = new QVBoxLayout(container);
        listLayout->setContentsMargins(8, 8, 8, 8);
        listLayout->setSpacing(2);

        if (m_combinations.isEmpty()) {
            auto *empty = new QLabel(
                        tr("暂无组合。在下方自由勾选后点「添加组合」保存。"),
                        container);
            empty->setWordWrap(true);
            empty->setStyleSheet(
                        QStringLiteral("color: #7B8FA3; font-size: 12px;"));
            listLayout->addWidget(empty);
        } else {
            for (const QString &combination : m_combinations) {
                auto *check = new MedicalCheckBox(combination, container);
                check->setChecked(combination == selectedCombo);
                listLayout->addWidget(check);
                m_comboChecks.append(check);
                connect(check, &QCheckBox::toggled,
                        this, [this, check](bool checked) {
                    if (m_updatingSelection) {
                        return;
                    }
                    if (checked) {
                        m_updatingSelection = true;
                        for (QCheckBox *other : m_comboChecks) {
                            if (other != check) {
                                other->setChecked(false);
                            }
                        }
                        for (QCheckBox *reagent : m_reagentChecks) {
                            reagent->setChecked(false);
                        }
                        m_updatingSelection = false;
                    }
                    updateState();
                });
            }
        }
        listLayout->addStretch(1);
        m_comboScroll->setWidget(container);
        updateState();
    }

    void applyInitialSelection(const QStringList &selected)
    {
        const QString text = canonicalTestItemText(selected);
        if (text.isEmpty()) {
            return;
        }
        const int combo = m_combinations.indexOf(text);
        m_updatingSelection = true;
        if (combo >= 0 && combo < m_comboChecks.size()) {
            m_comboChecks.at(combo)->setChecked(true);
            for (QCheckBox *reagent : m_reagentChecks) {
                reagent->setChecked(false);
            }
        } else {
            const QStringList items = splitTestItemText(text);
            for (QCheckBox *reagent : m_reagentChecks) {
                reagent->setChecked(items.contains(reagent->text()));
            }
        }
        m_updatingSelection = false;
    }

    void clearReagentChecks()
    {
        m_updatingSelection = true;
        for (QCheckBox *reagent : m_reagentChecks) {
            reagent->setChecked(false);
        }
        m_updatingSelection = false;
    }

    void updateState()
    {
        const int combo = selectedComboIndex();
        const QStringList items = selectedItems();
        if (items.isEmpty()) {
            m_summary->setText(tr("当前未选择测试项目"));
        } else {
            m_summary->setText(
                        tr("当前选择：%1（%2）")
                        .arg(items.join(QLatin1Char('+')))
                        .arg(combo >= 0 ? tr("已有组合")
                                        : tr("临时组合")));
        }
        const bool hasCombo = combo >= 0;
        m_modifyComboButton->setEnabled(hasCombo);
        m_deleteComboButton->setEnabled(hasCombo);
        if (m_okButton) {
            m_okButton->setEnabled(!items.isEmpty());
        }
    }

    void addCombination()
    {
        TestItemCombinationDialog dialog(selectedItems(), false, this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        const QString combination = canonicalTestItemText(
                    dialog.selectedItems());
        if (combination.isEmpty()) {
            return;
        }
        if (m_combinations.contains(combination)) {
            MedicalMessageBox::information(
                        this, tr("添加组合"),
                        tr("组合 %1 已存在，已直接选中。")
                        .arg(combination));
        } else {
            m_combinations.append(combination);
            saveTestItemCombinations(m_combinations);
        }
        rebuildComboChecks(combination);
        clearReagentChecks();
        updateState();
    }

    void modifyCombination()
    {
        const int combo = selectedComboIndex();
        if (combo < 0) {
            MedicalMessageBox::information(this, tr("修改组合"),
                                     tr("请先勾选要修改的已有组合。"));
            return;
        }
        const QString oldCombination = m_combinations.at(combo);
        TestItemCombinationDialog dialog(
                    splitTestItemText(oldCombination), true, this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        const QString combination = canonicalTestItemText(
                    dialog.selectedItems());
        if (combination.isEmpty() || combination == oldCombination) {
            return;
        }
        const int duplicate = m_combinations.indexOf(combination);
        if (duplicate >= 0 && duplicate != combo) {
            MedicalMessageBox::warning(
                        this, tr("修改组合"),
                        tr("组合 %1 已存在，请换一个组合。")
                        .arg(combination));
            return;
        }
        m_combinations[combo] = combination;
        saveTestItemCombinations(m_combinations);
        rebuildComboChecks(combination);
        clearReagentChecks();
        updateState();
    }

    void deleteCombination()
    {
        const int combo = selectedComboIndex();
        if (combo < 0) {
            MedicalMessageBox::information(this, tr("删除组合"),
                                     tr("请先勾选要删除的已有组合。"));
            return;
        }
        const QString combination = m_combinations.at(combo);
        if (MedicalMessageBox::question(
                    this, tr("删除组合"),
                    tr("确定删除组合「%1」吗？").arg(combination),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
        const QStringList items = splitTestItemText(combination);
        m_combinations.removeAt(combo);
        saveTestItemCombinations(m_combinations);
        rebuildComboChecks(QString());
        m_updatingSelection = true;
        for (QCheckBox *reagent : m_reagentChecks) {
            reagent->setChecked(items.contains(reagent->text()));
        }
        m_updatingSelection = false;
        updateState();
    }

    QStringList m_combinations;
    QVector<QCheckBox *> m_comboChecks;
    QList<QCheckBox *> m_reagentChecks;
    QScrollArea *m_comboScroll = nullptr;
    QLabel *m_summary = nullptr;
    QPushButton *m_okButton = nullptr;
    QPushButton *m_addComboButton = nullptr;
    QPushButton *m_modifyComboButton = nullptr;
    QPushButton *m_deleteComboButton = nullptr;
    bool m_updatingSelection = false;
};

/** 样本位置：下拉选择当前机型下未被其他样本占用的位置。 */
class PositionDelegate : public QStyledItemDelegate
{
public:
    using PositionsProvider = std::function<QStringList(int)>;

    explicit PositionDelegate(PositionsProvider provider,
                              QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
        , m_provider(std::move(provider))
    {
    }

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &,
                          const QModelIndex &index) const override
    {
        auto *combo = new QComboBox(parent);
        combo->addItems(m_provider(index.row()));
        combo->setStyleSheet(editorStyleSheet());
        return combo;
    }

    void setEditorData(QWidget *editor,
                       const QModelIndex &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (!combo) {
            return;
        }
        combo->setCurrentText(
                    index.model()->data(index, Qt::EditRole).toString());
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (!combo) {
            return;
        }
        model->setData(index, combo->currentText(), Qt::EditRole);
    }

private:
    PositionsProvider m_provider;
};

/** 加样状态列：彩色圆点 + 待定/待测/加样完成（测试状态看结果信息表）。 */
class SampleStatusDelegate : public QStyledItemDelegate
{
public:
    explicit SampleStatusDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QString text = opt.text;
        opt.text.clear();
        QStyle *style = opt.widget ? opt.widget->style()
                                   : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter,
                           opt.widget);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const bool selected = (option.state & QStyle::State_Selected);
        QColor dot(QStringLiteral("#8A9CAB"));   // 待定
        if (text == QStringLiteral("待测")) {
            dot = QColor(QStringLiteral("#E6A23C"));
        } else if (text == QStringLiteral("加样完成")
                   || text == QStringLiteral("已加样")) {
            dot = QColor(QStringLiteral("#22A877"));
        } else if (text == QStringLiteral("测试中")) {
            dot = QColor(QStringLiteral("#2C7BBF"));
        } else if (text == QStringLiteral("完成")) {
            dot = QColor(QStringLiteral("#6C8A5A"));
        }

        const QRect rect = option.rect;
        const qreal diameter = 6.0;
        const qreal left = rect.left() + 8.0;
        painter->setPen(Qt::NoPen);
        painter->setBrush(dot);
        painter->drawEllipse(QPointF(left + diameter / 2.0,
                                     rect.center().y()),
                             diameter / 2.0, diameter / 2.0);

        QFont statusFont = opt.font;
        statusFont.setFamily(QStringLiteral("Microsoft YaHei"));
        statusFont.setPixelSize(11);
        painter->setFont(statusFont);
        painter->setPen(selected
                        ? option.palette.color(QPalette::HighlightedText)
                        : QColor(QStringLiteral("#3A5065")));
        painter->drawText(QRectF(left + 12.0, rect.top(),
                                 rect.width() - 14.0, rect.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, text);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        Q_UNUSED(option);
        Q_UNUSED(index);
        return QSize(70, kSampleRowHeight);
    }
};

/**
 * 普通表格单元格：文本超宽时按字符换行（长数字/AA+ADP 这种无空格文本也会断行），
 * 并保持水平/垂直居中。仅负责绘制，编辑框仍由 QStyledItemDelegate 提供。
 */
class WrappedItemDelegate : public QStyledItemDelegate
{
public:
    explicit WrappedItemDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QString text = opt.text;
        opt.text.clear();

        QStyle *style = opt.widget ? opt.widget->style()
                                   : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter,
                           opt.widget);
        if (text.isEmpty()) {
            return;
        }

        const QRect textRect = style->subElementRect(
                    QStyle::SE_ItemViewItemText, &opt, opt.widget);
        if (textRect.isEmpty()) {
            return;
        }

        painter->save();
        painter->setClipRect(textRect);
        const QPalette::ColorGroup group =
                (opt.state & QStyle::State_Enabled)
                ? QPalette::Normal : QPalette::Disabled;
        const QPalette::ColorRole role =
                (opt.state & QStyle::State_Selected)
                ? QPalette::HighlightedText : QPalette::Text;
        painter->setPen(opt.palette.color(group, role));

        QTextOption textOption;
        textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        textOption.setAlignment(opt.displayAlignment
                                & Qt::AlignHorizontal_Mask);
        QTextLayout layout(text, opt.font);
        layout.setTextOption(textOption);
        layout.beginLayout();
        qreal textHeight = 0.0;
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) {
                break;
            }
            line.setLineWidth(textRect.width());
            line.setPosition(QPointF(0.0, textHeight));
            textHeight += line.height();
        }
        layout.endLayout();

        const qreal y = textRect.top()
                + qMax<qreal>(0.0,
                              (textRect.height() - textHeight) / 2.0);
        layout.draw(painter, QPointF(textRect.left(), y));
        painter->restore();
    }
};

/** 条码列编辑框只允许数字。 */
class BarcodeDelegate : public WrappedItemDelegate
{
public:
    explicit BarcodeDelegate(QObject *parent = nullptr)
        : WrappedItemDelegate(parent)
    {
    }

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *edit = new QLineEdit(parent);
        edit->setValidator(new QRegularExpressionValidator(
                               QRegularExpression(QStringLiteral("^\\d*$")),
                               edit));
        edit->setFrame(false);
        return edit;
    }
};

} // namespace

/**
 * 血样区：按机型绘制可插采血管的 3D 采血管架。
 *
 * 每组 5 行 × 2 列 = 10 孔，组数按机型：
 * KS600=6 组（A~F）、KS800=8 组（A~H）、KS1200=12 组（A~L）。
 * 每个孔内画一支已插入的采血管：
 * - 全血模式：管底 60% 红色血液，上部 40% 淡黄色；
 * - 血浆模式：整管淡黄色。
 */
class BloodSampleAreaWidget : public QWidget
{
public:
    explicit BloodSampleAreaWidget(MachineModel model, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_model(model)
    {
        setMouseTracking(true);
        setMinimumSize(600, 440);
        setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setMachineModel(MachineModel model)
    {
        if (m_model == model) {
            return;
        }
        m_model = model;
        m_hoverWell = -1;
        update();
    }

    void setPlasma(bool plasma)
    {
        if (m_plasma == plasma) {
            return;
        }
        m_plasma = plasma;
        update();
    }

    void setSamplePositions(const QStringList &positions)
    {
        QSet<QString> next;
        for (const QString &position : positions) {
            const QString trimmed = position.trimmed();
            if (!trimmed.isEmpty()) {
                next.insert(trimmed);
            }
        }
        if (next == m_samplePositions) {
            return;
        }
        m_samplePositions = next;
        update();
    }

    void setPppPositions(const QStringList &positions)
    {
        QSet<QString> next;
        for (const QString &position : positions) {
            const QString trimmed = position.trimmed();
            if (!trimmed.isEmpty()) {
                next.insert(trimmed);
            }
        }
        if (next == m_pppPositions) {
            return;
        }
        m_pppPositions = next;
        update();
    }

    using MoveHandler = std::function<void(const QStringList &)>;
    using CancelHandler = std::function<void()>;

    void setMoveHandlers(MoveHandler commit, CancelHandler cancel)
    {
        m_moveCommit = std::move(commit);
        m_moveCancel = std::move(cancel);
    }

    void startMoving(const QStringList &positions)
    {
        QStringList cleaned;
        for (const QString &position : positions) {
            const QString trimmed = position.trimmed();
            if (!trimmed.isEmpty()) {
                cleaned << trimmed;
            }
        }
        if (cleaned.isEmpty()) {
            return;
        }
        m_moving = true;
        m_movingPositions = cleaned;
        m_previewPositions = cleaned;
        m_previewPppPositions.clear();
        if (cleaned.size() >= 2) {
            m_previewPppPositions.insert(cleaned.first());
        }
        m_previewValid = true;
        m_hasPreviewTarget = false;
        m_hoverWell = -1;
        QToolTip::hideText();
        setFocus(Qt::MouseFocusReason);
        setCursor(Qt::ClosedHandCursor);
        update();
    }

    void cancelMoving()
    {
        if (!m_moving) {
            return;
        }
        resetMoving();
        if (m_moveCancel) {
            m_moveCancel();
        }
    }

    bool isMoving() const
    {
        return m_moving;
    }

    void setSelectedPositions(const QStringList &positions)
    {
        QSet<QString> next;
        for (const QString &position : positions) {
            const QString trimmed = position.trimmed();
            if (!trimmed.isEmpty()) {
                next.insert(trimmed);
            }
        }
        if (next == m_selectedPositions) {
            return;
        }
        m_selectedPositions = next;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        const Geometry geometry = rackGeometry();
        if (!geometry.valid()) {
            return;
        }

        paintRack(p, geometry);
        paintHoles(p, geometry);
        paintRowLabels(p, geometry);

        // 后排先画，前排后画，形成遮挡关系；只有已加样的孔才插管。
        for (int row = 0; row < geometry.rows; ++row) {
            for (int column = 0; column < geometry.columns; ++column) {
                const int index = row * geometry.columns + column;
                const Well &well = geometry.wells.at(index);
                if (!m_samplePositions.contains(well.position)) {
                    continue;
                }
                if (m_moving
                        && m_movingPositions.contains(well.position)) {
                    continue;   // 正在移动的管从原位隐藏
                }
                paintTube(p, well, index == m_hoverWell,
                          m_selectedPositions.contains(well.position));
            }
        }

        // 拖动预览：目标孔处画高亮管（PPP+PRP 两根一起）。
        if (m_moving) {
            for (int row = 0; row < geometry.rows; ++row) {
                for (int column = 0; column < geometry.columns;
                     ++column) {
                    const int index = row * geometry.columns + column;
                    const Well &well = geometry.wells.at(index);
                    if (!m_previewPositions.contains(well.position)) {
                        continue;
                    }
                    paintTube(p, well, false, true, true,
                              m_previewValid);
                }
            }
        }

        paintGroupLabels(p, geometry);
        if (m_moving) {
            paintMoveBadge(p, geometry);
            paintMoveBanner(p, geometry);
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_moving) {
            const int index = wellAt(event->pos());
            if (m_hasPreviewTarget != (index >= 0)) {
                m_hasPreviewTarget = index >= 0;
                update();
            }
            if (index >= 0) {
                const Geometry geometry = rackGeometry();
                const Well &well = geometry.wells.at(index);
                QStringList target;
                if (m_movingPositions.size() >= 2) {
                    const int base = well.group * 10 + well.row * 2;
                    target << QString::number(base)
                           << QString::number(base + 1);
                } else {
                    target << well.position;
                }

                bool valid = true;
                for (const QString &position : target) {
                    if (m_samplePositions.contains(position)
                            && !m_movingPositions.contains(position)) {
                        valid = false;
                    }
                }
                if (target != m_previewPositions
                        || valid != m_previewValid) {
                    m_previewPositions = target;
                    m_previewPppPositions.clear();
                    if (target.size() >= 2) {
                        m_previewPppPositions.insert(target.first());
                    }
                    m_previewValid = valid;
                    update();
                }
            }
            QToolTip::hideText();
            QWidget::mouseMoveEvent(event);
            return;
        }

        const int index = wellAt(event->pos());
        if (index != m_hoverWell) {
            m_hoverWell = index;
            update();
            if (index >= 0) {
                const Geometry geometry = rackGeometry();
                const Well &well = geometry.wells.at(index);
                const QString side = well.column == 0
                        ? tr("左侧") : tr("右侧");
                const bool occupied = m_samplePositions.contains(
                            well.position);
                if (occupied) {
                    const bool pppMode = !m_pppPositions.isEmpty();
                    const bool isPpp = m_pppPositions.contains(
                                well.position);
                    QString title = well.position;
                    if (pppMode) {
                        title += isPpp ? tr("（PPP）") : tr("（PRP）");
                    }
                    QString liquid;
                    if (isPpp) {
                        liquid = m_plasma
                                ? tr("PPP：透亮淡黄色")
                                : tr("PPP：下层红色血液 / 上层透亮淡黄");
                    } else if (m_plasma) {
                        liquid = tr("血浆：整管淡黄色");
                    } else {
                        liquid = tr("全血：下层红色 60% / "
                                    "上层淡黄色 40%");
                    }
                    QToolTip::showText(
                                event->globalPos(),
                                tr("采血管 %1\n%2 组 · 第 %3 行%4\n%5")
                                .arg(title)
                                .arg(groupLetter(well.group))
                                .arg(well.row + 1)
                                .arg(side)
                                .arg(liquid),
                                this);
                } else {
                    QToolTip::showText(
                                event->globalPos(),
                                tr("空孔 %1\n%2 组 · 第 %3 行%4\n等待加样")
                                .arg(well.position)
                                .arg(groupLetter(well.group))
                                .arg(well.row + 1)
                                .arg(side),
                                this);
                }
            } else {
                QToolTip::hideText();
            }
        }
        QWidget::mouseMoveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (m_moving) {
            if (event->button() == Qt::LeftButton) {
                if (m_hasPreviewTarget && m_previewValid
                        && !m_previewPositions.isEmpty()) {
                    const QStringList positions = m_previewPositions;
                    resetMoving();
                    if (m_moveCommit) {
                        m_moveCommit(positions);
                    }
                }
                event->accept();
                return;
            }
            if (event->button() == Qt::RightButton) {
                cancelMoving();
                event->accept();
                return;
            }
        }
        QWidget::mousePressEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (m_moving && event->key() == Qt::Key_Escape) {
            cancelMoving();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hoverWell = -1;
        QToolTip::hideText();
        update();
        QWidget::leaveEvent(event);
    }

private:
    void resetMoving()
    {
        m_moving = false;
        m_movingPositions.clear();
        m_previewPositions.clear();
        m_previewPppPositions.clear();
        m_previewValid = true;
        m_hasPreviewTarget = false;
        m_hoverWell = -1;
        unsetCursor();
        QToolTip::hideText();
        update();
    }

    struct Well {
        QPointF center;      // 上层平板孔中心
        qreal holeRx = 0.0;
        qreal holeRy = 0.0;
        qreal bodyRadius = 0.0; // 采血管半径
        qreal tubeHeight = 0.0; // 露出上层平板的管高
        qreal tubeBottomY = 0.0; // 下层托板支撑点 Y
        qreal lowerTopY = 0.0;   // 下层可见段顶部（上层平板前缘下方）
        int group = 0;
        int row = 0;
        int column = 0;      // 组内列号 0/1
        int holeNumber = 0;  // 组内孔号 0~9
        QString position;    // 例如 A-0
    };

    struct Geometry {
        QPolygonF topFace;          // 上层平板台面
        QPolygonF frontFace;        // 上层平板前缘
        QPolygonF leftSide;
        QPolygonF rightSide;
        QPolygonF bottomFace;       // 下层托板台面
        QPolygonF bottomFrontFace;  // 下层托板前挡边
        QPolygonF bottomLeftSide;
        QPolygonF bottomRightSide;
        QPolygonF leftWall;         // 上下层之间的左侧板
        QPolygonF rightWall;
        QPolygonF backWall;         // 上下层之间的后侧板
        QVector<Well> wells;
        QVector<QPointF> groupLabelPoints;
        QVector<QLineF> groupDividers;
        int rows = 5;
        int columns = 0;
        int groups = 0;
        qreal backY = 0.0;
        qreal frontY = 0.0;
        qreal plateThickness = 0.0;
        qreal gap = 0.0;            // 上下两层之间的间距
        qreal trayLipHeight = 0.0;
        qreal bottomWidth = 0.0;
        qreal topWidth = 0.0;
        qreal frontLeft = 0.0;
        qreal pitch = 0.0;

        bool valid() const
        {
            return groups > 0 && columns == groups * 2
                    && wells.size() == rows * columns;
        }
    };

    Geometry rackGeometry() const
    {
        Geometry geometry;
        const QRectF area = QRectF(rect()).adjusted(10.0, 8.0, -10.0, -8.0);
        const int groups = machineBloodSampleGroupCount(m_model);
        const int columns = groups * 2;
        const int rows = 5;
        if (area.width() < 240.0 || area.height() < 220.0) {
            return geometry;
        }

        geometry.groups = groups;
        geometry.columns = columns;
        geometry.rows = rows;

        const qreal headerSpace = 64.0;  // 组名标签 + 后排敞口管顶部空间
        const qreal shadowSpace = 16.0;
        const qreal trayLipHeight = qBound<qreal>(
                    12.0, area.height() * 0.025, 18.0);
        const qreal availableLayers = area.height() - headerSpace
                - trayLipHeight - shadowSpace;
        // 上下两层错开得比上层平板更深，保证下层托板、托底和管身下段都可见。
        const qreal gap = qBound<qreal>(140.0,
                                        availableLayers * 0.52,
                                        240.0);
        const qreal faceHeight = qBound<qreal>(150.0,
                                               availableLayers - gap,
                                               230.0);
        const qreal plateThickness = qBound<qreal>(
                    8.0, faceHeight * 0.045, 12.0);

        const qreal blockHeight = headerSpace + faceHeight + gap
                + trayLipHeight + shadowSpace;
        const qreal blockTop = area.top()
                + qMax<qreal>(0.0, (area.height() - blockHeight) / 2.0);
        geometry.backY = blockTop + headerSpace;
        geometry.frontY = geometry.backY + faceHeight;
        geometry.gap = gap;
        geometry.plateThickness = plateThickness;
        geometry.trayLipHeight = trayLipHeight;

        // 左右各留出 28px，左侧放行号，避免满宽时行号被裁掉。
        const qreal maxWidth = area.width() - 56.0;

        // 组间距尽量大；窗口较窄时自动收小间距，保证采血管仍有足够宽度。
        qreal groupGapFactor = 0.85;
        qreal pitch = maxWidth
                / (columns + (groups - 1) * groupGapFactor + 1.25);
        if (pitch < 28.0 && groups > 1) {
            groupGapFactor = qMax<qreal>(
                        0.35,
                        (maxWidth / 28.0 - columns - 1.25)
                        / (groups - 1));
            pitch = maxWidth
                    / (columns + (groups - 1) * groupGapFactor + 1.25);
        }
        pitch = qBound<qreal>(22.0, pitch, 48.0);
        const qreal groupGap = pitch * groupGapFactor;
        const qreal sideMargin = pitch * 0.62;

        geometry.pitch = pitch;
        geometry.bottomWidth = columns * pitch
                + (groups - 1) * groupGap + 2.0 * sideMargin;
        geometry.topWidth = geometry.bottomWidth * 0.975;
        const qreal centerX = area.center().x();
        geometry.frontLeft = centerX - geometry.bottomWidth / 2.0;
        const qreal backLeft = centerX - geometry.topWidth / 2.0;
        const qreal frontRight = geometry.frontLeft + geometry.bottomWidth;
        const qreal backRight = backLeft + geometry.topWidth;

        geometry.topFace
                << QPointF(backLeft, geometry.backY)
                << QPointF(backRight, geometry.backY)
                << QPointF(frontRight, geometry.frontY)
                << QPointF(geometry.frontLeft, geometry.frontY);

        // 上层平板：薄板 + 前缘 + 薄侧边
        const QPointF plateDepth(0.0, plateThickness);
        geometry.frontFace
                << geometry.topFace.at(3) << geometry.topFace.at(2)
                << geometry.topFace.at(2) + plateDepth
                << geometry.topFace.at(3) + plateDepth;

        const QPointF plateSideDepth(0.0, plateThickness * 0.82);
        geometry.leftSide
                << geometry.topFace.at(0) << geometry.topFace.at(3)
                << geometry.topFace.at(3) + plateDepth
                << geometry.topFace.at(0) + plateSideDepth;
        geometry.rightSide
                << geometry.topFace.at(1) << geometry.topFace.at(2)
                << geometry.topFace.at(2) + plateDepth
                << geometry.topFace.at(1) + plateSideDepth;

        // 下层托板：与上层平行的台面，向下偏移 gap；前/侧挡边托住采血管。
        const QPointF trayOffset(0.0, gap);
        geometry.bottomFace
                << geometry.topFace.at(0) + trayOffset
                << geometry.topFace.at(1) + trayOffset
                << geometry.topFace.at(2) + trayOffset
                << geometry.topFace.at(3) + trayOffset;

        const QPointF lipDepth(0.0, trayLipHeight);
        geometry.bottomFrontFace
                << geometry.bottomFace.at(3) << geometry.bottomFace.at(2)
                << geometry.bottomFace.at(2) + lipDepth
                << geometry.bottomFace.at(3) + lipDepth;
        geometry.bottomLeftSide
                << geometry.bottomFace.at(0) << geometry.bottomFace.at(3)
                << geometry.bottomFace.at(3) + lipDepth
                << geometry.bottomFace.at(0) + lipDepth;
        geometry.bottomRightSide
                << geometry.bottomFace.at(1) << geometry.bottomFace.at(2)
                << geometry.bottomFace.at(2) + lipDepth
                << geometry.bottomFace.at(1) + lipDepth;

        // 上下层之间的侧板与后板。
        geometry.leftWall
                << geometry.topFace.at(0) << geometry.topFace.at(3)
                << geometry.bottomFace.at(3) << geometry.bottomFace.at(0);
        geometry.rightWall
                << geometry.topFace.at(1) << geometry.topFace.at(2)
                << geometry.bottomFace.at(2) << geometry.bottomFace.at(1);
        geometry.backWall
                << geometry.topFace.at(0) << geometry.topFace.at(1)
                << geometry.bottomFace.at(1) << geometry.bottomFace.at(0);

        const qreal rowFractions[5] = {0.10, 0.30, 0.50, 0.70, 0.90};
        const qreal rowPitch = faceHeight / qreal(rows);

        // 按“行 → 组 → 组内列”存放，保证 wells 是行优先：
        // index = row * columns + column，绘制和行号都依赖这个顺序。
        for (int row = 0; row < rows; ++row) {
            const qreal fraction = rowFractions[row];
            const qreal scale = 0.84 + 0.04 * row;

            for (int group = 0; group < groups; ++group) {
                for (int localColumn = 0; localColumn < 2; ++localColumn) {
                    const int column = group * 2 + localColumn;
                    const qreal frontX = geometry.frontLeft + sideMargin
                            + column * pitch + group * groupGap
                            + pitch / 2.0;
                    const qreal u = (frontX - geometry.frontLeft)
                            / geometry.bottomWidth;
                    const qreal xTop = backLeft + u * geometry.topWidth;
                    const qreal xBottom = geometry.frontLeft
                            + u * geometry.bottomWidth;

                    Well well;
                    well.center = QPointF(
                                lerpPoint(QPointF(xTop, geometry.backY),
                                          QPointF(xBottom, geometry.frontY),
                                          fraction).x(),
                                geometry.backY + fraction * faceHeight);
                    well.holeRx = pitch * 0.36 * scale;
                    well.holeRy = well.holeRx * 0.42;
                    well.bodyRadius = well.holeRx * 0.72;
                    // 露出上层平板的管高；下层还有一段延伸到托板。
                    const qreal naturalHeight = qMin<qreal>(
                                rowPitch * 0.94,
                                well.bodyRadius * 2.0 * 4.6) * scale;
                    well.tubeHeight = qMax<qreal>(46.0, naturalHeight);
                    well.tubeBottomY = well.center.y() + gap;
                    well.lowerTopY = geometry.frontY + plateThickness;
                    well.group = group;
                    well.row = row;
                    well.column = localColumn;
                    // 孔号跨组连续编号：A组0~9、B组10~19、C组20~29……
                    well.holeNumber = group * 10 + row * 2 + localColumn;
                    well.position = QString::number(well.holeNumber);
                    geometry.wells.append(well);
                }
            }
        }

        // 组名标在架子上方，位于该组两列的正中间。
        const qreal labelY = geometry.backY - 44.0;
        for (int group = 0; group < groups; ++group) {
            const qreal leftX = geometry.frontLeft + sideMargin
                    + group * 2 * pitch + group * groupGap + pitch / 2.0;
            const qreal rightX = leftX + pitch;
            const qreal uLeft = (leftX - geometry.frontLeft)
                    / geometry.bottomWidth;
            const qreal uRight = (rightX - geometry.frontLeft)
                    / geometry.bottomWidth;
            const qreal xTopLeft = backLeft + uLeft * geometry.topWidth;
            const qreal xTopRight = backLeft + uRight * geometry.topWidth;
            geometry.groupLabelPoints.append(
                        QPointF((xTopLeft + xTopRight) / 2.0, labelY));
        }

        // 组与组之间在台面上画一条分隔槽。
        for (int group = 1; group < groups; ++group) {
            const qreal offset = sideMargin + group * 2 * pitch
                    + (group - 1) * groupGap + groupGap / 2.0;
            const qreal u = offset / geometry.bottomWidth;
            geometry.groupDividers.append(
                        QLineF(QPointF(backLeft + u * geometry.topWidth,
                                       geometry.backY + 2.0),
                               QPointF(geometry.frontLeft
                                       + u * geometry.bottomWidth,
                                       geometry.frontY - 2.0)));
        }

        return geometry;
    }

    void paintRack(QPainter &p, const Geometry &geometry) const
    {
        const qreal bottomY = geometry.bottomFace.at(3).y()
                + geometry.trayLipHeight;

        // 桌面阴影
        const QRectF shadowRect(geometry.frontLeft + 12.0, bottomY + 2.0,
                                geometry.bottomWidth - 24.0, 18.0);
        QRadialGradient shadow(shadowRect.center(),
                               qMax<qreal>(20.0, shadowRect.width() / 2.0));
        shadow.setColorAt(0.0, QColor(31, 78, 121, 55));
        shadow.setColorAt(1.0, QColor(31, 78, 121, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(shadow);
        p.drawEllipse(shadowRect);

        // 后侧板（上下层之间）
        QLinearGradient backGradient(0.0, geometry.topFace.at(0).y(),
                                     0.0, geometry.bottomFace.at(0).y());
        backGradient.setColorAt(0.0, QColor(126, 149, 166));
        backGradient.setColorAt(1.0, QColor(84, 105, 122));
        p.setPen(QPen(QColor(82, 103, 120), 1.0));
        p.setBrush(backGradient);
        p.drawPolygon(geometry.backWall);

        // 左右侧板（上下层之间）
        QLinearGradient sideGradient(0.0, geometry.topFace.at(0).y(),
                                     0.0, bottomY);
        sideGradient.setColorAt(0.0, QColor(167, 187, 202));
        sideGradient.setColorAt(1.0, QColor(129, 152, 171));
        p.setPen(QPen(QColor(110, 134, 153), 1.0));
        p.setBrush(sideGradient);
        p.drawPolygon(geometry.leftWall);
        p.drawPolygon(geometry.rightWall);

        // 下层托板台面
        QLinearGradient trayGradient(0.0, geometry.bottomFace.at(0).y(),
                                     0.0, geometry.bottomFace.at(3).y());
        trayGradient.setColorAt(0.0, QColor(221, 231, 239));
        trayGradient.setColorAt(0.55, QColor(201, 216, 227));
        trayGradient.setColorAt(1.0, QColor(181, 199, 213));
        p.setPen(QPen(QColor(143, 167, 185), 1.0));
        p.setBrush(trayGradient);
        p.drawPolygon(geometry.bottomFace);

        // 下层托板前挡边和侧挡边
        QLinearGradient lipGradient(0.0, geometry.bottomFace.at(3).y(),
                                    0.0, bottomY);
        lipGradient.setColorAt(0.0, QColor(178, 197, 211));
        lipGradient.setColorAt(1.0, QColor(135, 159, 177));
        p.setPen(QPen(QColor(113, 138, 157), 1.0));
        p.setBrush(lipGradient);
        p.drawPolygon(geometry.bottomLeftSide);
        p.drawPolygon(geometry.bottomRightSide);
        p.drawPolygon(geometry.bottomFrontFace);

        // 托板台面前缘、挡边上沿高光
        p.setPen(QPen(QColor(255, 255, 255, 175), 1.0));
        p.drawLine(geometry.bottomFace.at(3), geometry.bottomFace.at(2));
        p.setPen(QPen(QColor(255, 255, 255, 120), 0.8));
        p.drawLine(geometry.bottomFrontFace.at(0),
                   geometry.bottomFrontFace.at(1));

        // 托板台面高光
        p.save();
        QPainterPath trayClip;
        trayClip.addPolygon(geometry.bottomFace);
        trayClip.closeSubpath();
        p.setClipPath(trayClip);
        p.setPen(QPen(QColor(255, 255, 255, 38), 10.0));
        p.drawLine(QPointF(geometry.frontLeft + 30.0,
                           geometry.bottomFace.at(3).y() - 4.0),
                   QPointF(geometry.frontLeft + geometry.topWidth * 0.35,
                           geometry.bottomFace.at(0).y() + 4.0));
        p.restore();

        // 下层托板的托底支撑凹槽；上层平板会遮住后排。
        paintBottomSupports(p, geometry);

        // 上层平板台面
        QLinearGradient topGradient(0.0, geometry.backY,
                                    0.0, geometry.frontY);
        topGradient.setColorAt(0.0, QColor(245, 249, 252));
        topGradient.setColorAt(0.45, QColor(226, 237, 244));
        topGradient.setColorAt(1.0, QColor(205, 220, 231));
        p.setPen(QPen(QColor(158, 182, 199), 1.1));
        p.setBrush(topGradient);
        p.drawPolygon(geometry.topFace);

        p.setPen(QPen(QColor(255, 255, 255, 235), 1.2));
        p.drawLine(geometry.topFace.at(0), geometry.topFace.at(1));
        p.setPen(QPen(QColor(255, 255, 255, 130), 0.9));
        p.drawLine(geometry.topFace.at(0), geometry.topFace.at(3));
        p.drawLine(geometry.topFace.at(1), geometry.topFace.at(2));
        p.setPen(QPen(QColor(112, 140, 162, 120), 0.8));
        p.drawLine(geometry.topFace.at(3), geometry.topFace.at(2));

        // 上层平板高光
        p.save();
        QPainterPath clip;
        clip.addPolygon(geometry.topFace);
        clip.closeSubpath();
        p.setClipPath(clip);
        p.setPen(QPen(QColor(255, 255, 255, 45), 12.0));
        p.drawLine(QPointF(geometry.frontLeft + 30.0,
                           geometry.frontY - 6.0),
                   QPointF(geometry.frontLeft + geometry.topWidth * 0.38,
                           geometry.backY + 6.0));
        p.restore();

        // 上层平板前缘（薄边）
        QLinearGradient plateEdge(0.0, geometry.frontY,
                                  0.0, geometry.frontY
                                  + geometry.plateThickness);
        plateEdge.setColorAt(0.0, QColor(201, 218, 230));
        plateEdge.setColorAt(1.0, QColor(158, 181, 200));
        p.setPen(QPen(QColor(133, 158, 178), 1.0));
        p.setBrush(plateEdge);
        p.drawPolygon(geometry.frontFace);

        // 上层平板侧边
        QLinearGradient plateSide(0.0, geometry.topFace.at(0).y(),
                                  0.0, geometry.topFace.at(3).y());
        plateSide.setColorAt(0.0, QColor(177, 196, 211));
        plateSide.setColorAt(1.0, QColor(151, 174, 193));
        p.setPen(QPen(QColor(129, 154, 174), 0.9));
        p.setBrush(plateSide);
        p.drawPolygon(geometry.leftSide);
        p.drawPolygon(geometry.rightSide);

        // 组间分隔槽
        for (const QLineF &line : geometry.groupDividers) {
            p.setPen(QPen(QColor(86, 113, 134, 45), 1.6));
            p.drawLine(line);
            p.setPen(QPen(QColor(255, 255, 255, 120), 0.7));
            p.drawLine(line.translated(1.0, 0.0));
        }

        // 下层托板前挡边两端螺钉
        const qreal screwY = geometry.bottomFace.at(3).y()
                + geometry.trayLipHeight * 0.5;
        const qreal screwXs[2] = {geometry.frontLeft + 8.0,
                                  geometry.frontLeft
                                  + geometry.bottomWidth - 8.0};
        for (qreal x : screwXs) {
            p.setPen(QPen(QColor(125, 149, 168), 0.8));
            p.setBrush(QColor(190, 204, 216));
            p.drawEllipse(QPointF(x, screwY), 2.2, 2.2);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 170));
            p.drawEllipse(QPointF(x - 0.6, screwY - 0.7), 0.8, 0.8);
        }
    }

    void paintBottomSupports(QPainter &p, const Geometry &geometry) const
    {
        for (const Well &well : geometry.wells) {
            const QPointF support(well.center.x(), well.tubeBottomY);
            const qreal rx = well.bodyRadius * 1.18;
            const qreal ry = well.bodyRadius * 0.34;

            QRadialGradient dimple(support - QPointF(0.0, ry * 0.25),
                                   qMax<qreal>(4.0, rx * 1.25));
            dimple.setColorAt(0.0, QColor(101, 123, 140, 170));
            dimple.setColorAt(0.65, QColor(154, 177, 194, 130));
            dimple.setColorAt(1.0, QColor(210, 225, 236, 70));
            p.setPen(QPen(QColor(140, 164, 181, 140), 0.8));
            p.setBrush(dimple);
            p.drawEllipse(support, rx, ry);

            p.setPen(QPen(QColor(255, 255, 255, 95), 0.7));
            p.setBrush(Qt::NoBrush);
            p.drawArc(QRectF(support.x() - rx, support.y() - ry,
                             rx * 2.0, ry * 2.0),
                      200 * 16, 140 * 16);
        }
    }

    void paintHoles(QPainter &p, const Geometry &geometry) const
    {
        for (int i = 0; i < geometry.wells.size(); ++i) {
            const Well &well = geometry.wells.at(i);

            // 孔外圈凸台
            p.setPen(QPen(QColor(255, 255, 255, 160), 0.8));
            p.setBrush(QColor(188, 207, 221, 95));
            p.drawEllipse(well.center, well.holeRx * 1.12,
                          well.holeRy * 1.12);

            // 孔内深色
            QRadialGradient hole(well.center
                                 + QPointF(-well.holeRx * 0.16,
                                           -well.holeRy * 0.18),
                                 qMax<qreal>(5.0, well.holeRx * 1.35));
            hole.setColorAt(0.0, QColor(39, 58, 72, 235));
            hole.setColorAt(0.62, QColor(79, 103, 120, 210));
            hole.setColorAt(1.0, QColor(151, 176, 194, 180));
            p.setPen(QPen(QColor(115, 141, 160, 190), 0.9));
            p.setBrush(hole);
            p.drawEllipse(well.center, well.holeRx, well.holeRy);

            // 孔口内沿阴影
            p.setPen(QPen(QColor(24, 40, 52, 110), 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawArc(QRectF(well.center.x() - well.holeRx * 0.92,
                             well.center.y() - well.holeRy * 0.92,
                             well.holeRx * 1.84, well.holeRy * 1.84),
                      20 * 16, 140 * 16);

            const bool occupied = m_samplePositions.contains(
                        well.position);
            if (!occupied) {
                // 空孔内显示组内孔号 0~9；插管后孔号改画在管帽上。
                QFont numberFont(QStringLiteral("Microsoft YaHei"));
                numberFont.setPixelSize(
                            qBound(6, int(well.holeRy * 1.6), 9));
                numberFont.setBold(true);
                p.setFont(numberFont);
                p.setPen(QColor(222, 236, 247, 225));
                p.drawText(QRectF(well.center.x() - well.holeRx,
                                  well.center.y() - well.holeRy * 0.95,
                                  well.holeRx * 2.0,
                                  well.holeRy * 1.9),
                           Qt::AlignCenter,
                           QString::number(well.holeNumber));
            }

            const bool selected = m_selectedPositions.contains(
                        well.position)
                    && !(m_moving
                         && m_movingPositions.contains(well.position));
            if (i == m_hoverWell || selected) {
                p.setPen(QPen(QColor(44, 123, 191,
                                     selected ? 235 : 200),
                              selected ? 2.0 : 1.6));
                p.setBrush(Qt::NoBrush);
                const qreal ring = selected ? 1.34 : 1.28;
                p.drawEllipse(well.center, well.holeRx * ring,
                              well.holeRy * ring);
            }
        }
    }

    void paintRowLabels(QPainter &p, const Geometry &geometry) const
    {
        QFont font(QStringLiteral("Microsoft YaHei"));
        font.setPixelSize(9);
        font.setBold(true);
        p.setFont(font);

        const qreal rackLeft = qMin(geometry.topFace.at(0).x(),
                                    geometry.topFace.at(3).x());
        for (int row = 0; row < geometry.rows; ++row) {
            const Well &well = geometry.wells.at(row * geometry.columns);
            const qreal y = well.center.y();
            const QRectF pill(rackLeft - 26.0, y - 8.0, 17.0, 16.0);
            p.setPen(QPen(QColor(174, 196, 212), 0.9));
            p.setBrush(QColor(255, 255, 255, 225));
            p.drawRoundedRect(pill, 3.0, 3.0);
            p.setPen(QColor("#5D7386"));
            p.drawText(pill, Qt::AlignCenter, QString::number(row + 1));

            p.setPen(QPen(QColor(128, 154, 173, 120), 0.8));
            p.drawLine(QPointF(pill.right() + 1.0, y),
                       QPointF(rackLeft - 3.0, y));
        }
    }

    void paintGroupLabels(QPainter &p, const Geometry &geometry) const
    {
        QFont font(QStringLiteral("Microsoft YaHei"));
        font.setPixelSize(11);
        font.setBold(true);
        p.setFont(font);

        for (int group = 0; group < geometry.groupLabelPoints.size();
             ++group) {
            const QPointF point = geometry.groupLabelPoints.at(group);
            const QRectF pill(point.x() - 11.0, point.y() - 8.0,
                              22.0, 16.0);
            p.setPen(QPen(QColor(169, 200, 223), 0.9));
            p.setBrush(QColor(232, 241, 250, 235));
            p.drawRoundedRect(pill, 4.0, 4.0);
            p.setPen(QColor("#1F4E79"));
            p.drawText(pill, Qt::AlignCenter, groupLetter(group));
        }
    }

    void paintMoveBadge(QPainter &p, const Geometry &geometry) const
    {
        if (!m_moving || m_previewPositions.isEmpty()) {
            return;
        }
        qreal left = 0.0;
        qreal right = 0.0;
        qreal top = 0.0;
        bool found = false;
        for (const Well &well : geometry.wells) {
            if (!m_previewPositions.contains(well.position)) {
                continue;
            }
            const qreal wellLeft = well.center.x() - well.bodyRadius;
            const qreal wellRight = well.center.x() + well.bodyRadius;
            const qreal wellTop = well.center.y() - well.tubeHeight;
            if (!found) {
                left = wellLeft;
                right = wellRight;
                top = wellTop;
                found = true;
            } else {
                left = qMin(left, wellLeft);
                right = qMax(right, wellRight);
                top = qMin(top, wellTop);
            }
        }
        if (!found) {
            return;
        }

        const QString text = m_previewValid
                ? tr("移动中") : tr("目标孔被占用");
        QFont font(QStringLiteral("Microsoft YaHei"));
        font.setPixelSize(10);
        font.setBold(true);
        p.setFont(font);
        const QFontMetrics metrics(font);
        const qreal width = metrics.width(text) + 14.0;
        const qreal height = 16.0;
        const qreal x = (left + right) / 2.0 - width / 2.0;
        const qreal y = qMax<qreal>(2.0, top - height - 5.0);
        const QRectF rect(x, y, width, height);
        const QColor color = m_previewValid
                ? QColor(QStringLiteral("#2C7BBF"))
                : QColor(QStringLiteral("#D9534F"));
        p.setPen(QPen(color, 1.0));
        p.setBrush(QColor(255, 255, 255, 238));
        p.drawRoundedRect(rect, 5.0, 5.0);
        p.setPen(color);
        p.drawText(rect, Qt::AlignCenter, text);
    }

    void paintMoveBanner(QPainter &p, const Geometry &geometry) const
    {
        if (!m_moving) {
            return;
        }
        QString text;
        if (!m_hasPreviewTarget) {
            text = tr("移动样本：把鼠标移到目标孔，左键放置，右键取消");
        } else if (m_previewValid) {
            text = tr("移动样本：左键放到目标孔，右键取消");
        } else {
            text = tr("目标孔被占用：请换一个孔，右键取消");
        }
        QFont font(QStringLiteral("Microsoft YaHei"));
        font.setPixelSize(11);
        font.setBold(true);
        p.setFont(font);
        const QFontMetrics metrics(font);
        const qreal width = metrics.width(text) + 24.0;
        const qreal height = 20.0;
        const qreal x = geometry.frontLeft
                + (geometry.bottomWidth - width) / 2.0;
        const qreal y = geometry.bottomFace.at(3).y() - height - 8.0;
        const QRectF rect(x, y, width, height);

        const QColor color = m_previewValid
                ? QColor(QStringLiteral("#2C7BBF"))
                : QColor(QStringLiteral("#D9534F"));
        p.setPen(QPen(color, 1.2));
        p.setBrush(QColor(255, 255, 255, 238));
        p.drawRoundedRect(rect, 6.0, 6.0);
        p.setPen(color);
        p.drawText(rect, Qt::AlignCenter, text);
    }

    void paintTube(QPainter &p, const Well &well, bool hovered,
                   bool selected, bool preview = false,
                   bool previewValid = true) const
    {
        const qreal bodyRadius = well.bodyRadius;
        // 敞口采血管：顶部没有管帽，直接在管口画玻璃壁和内腔。
        const qreal bodyTop = well.center.y() - well.tubeHeight;
        // 上层可见段到孔口为止，孔口前侧壁盖住管身下段。
        const qreal bodyBottom = well.center.y() + well.holeRy * 0.45;
        const qreal bodyHeight = bodyBottom - bodyTop;
        if (bodyHeight <= 2.0) {
            return;
        }

        const bool previewInvalid = preview && !previewValid;
        if (hovered || selected || preview) {
            const QColor glowColor = previewInvalid
                    ? QColor(217, 83, 79) : QColor(44, 123, 191);
            const int centerAlpha = preview
                    ? (previewInvalid ? 115 : 150)
                    : (selected ? 120 : 95);
            QRadialGradient glow(well.center
                                 + QPointF(0.0, -well.tubeHeight * 0.45),
                                 qMax<qreal>(14.0,
                                             well.tubeHeight * 0.62));
            glow.setColorAt(0.0, QColor(glowColor.red(), glowColor.green(),
                                        glowColor.blue(), centerAlpha));
            glow.setColorAt(0.65, QColor(glowColor.red(), glowColor.green(),
                                         glowColor.blue(),
                                         preview ? 55 : (selected ? 42 : 30)));
            glow.setColorAt(1.0, QColor(glowColor.red(), glowColor.green(),
                                        glowColor.blue(), 0));
            p.setPen(Qt::NoPen);
            p.setBrush(glow);
            p.drawEllipse(well.center + QPointF(0.0,
                                                -well.tubeHeight * 0.45),
                          bodyRadius * (preview ? 2.45
                                                : (selected ? 2.1 : 1.9)),
                          well.tubeHeight * 0.72);
        }

        // 采血管在台面上的投影
        QRadialGradient tubeShadow(
                    QPointF(well.center.x() + bodyRadius * 0.5,
                            well.center.y() + well.holeRy * 0.25),
                    bodyRadius * 1.7);
        tubeShadow.setColorAt(0.0, QColor(31, 52, 68, 70));
        tubeShadow.setColorAt(1.0, QColor(31, 52, 68, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(tubeShadow);
        p.drawEllipse(QPointF(well.center.x() + bodyRadius * 0.5,
                              well.center.y() + well.holeRy * 0.25),
                      bodyRadius * 1.5, well.holeRy * 1.3);

        const QRectF bodyRect(well.center.x() - bodyRadius, bodyTop,
                              bodyRadius * 2.0, bodyHeight);
        const qreal left = well.center.x() - bodyRadius;
        const qreal right = well.center.x() + bodyRadius;
        const qreal corner = bodyRadius * 0.95;
        QPainterPath bodyPath;
        bodyPath.moveTo(left, bodyTop);
        bodyPath.lineTo(right, bodyTop);
        bodyPath.lineTo(right, bodyBottom - corner);
        bodyPath.quadTo(right, bodyBottom, right - corner, bodyBottom);
        bodyPath.lineTo(left + corner, bodyBottom);
        bodyPath.quadTo(left, bodyBottom, left, bodyBottom - corner);
        bodyPath.closeSubpath();

        // 管内液体
        p.save();
        p.setClipPath(bodyPath);
        // 全血不要装太满，管口下方留一段空管；血浆保持较高液位。
        const qreal emptyFraction = m_plasma ? 0.10 : 0.32;
        const qreal liquidTop = bodyTop + bodyHeight * emptyFraction;
        const qreal liquidBottom = bodyBottom - bodyHeight * 0.05;
        const qreal liquidHeight = liquidBottom - liquidTop;
        const qreal boundary = liquidBottom - liquidHeight * 0.60;

        const QColor redTop(196, 58, 70);
        const QColor redBottom(158, 31, 43);
        const QColor yellowTop(247, 233, 181);
        const QColor yellowBottom(233, 212, 140);

        const bool isPpp = m_pppPositions.contains(well.position)
                || m_previewPppPositions.contains(well.position);
        if (m_plasma) {
            QLinearGradient yellow(0.0, liquidTop, 0.0, liquidBottom);
            if (isPpp) {
                yellow.setColorAt(0.0, QColor(255, 250, 208, 225));
                yellow.setColorAt(0.55, QColor(255, 238, 164, 225));
                yellow.setColorAt(1.0, QColor(255, 226, 126, 220));
            } else {
                yellow.setColorAt(0.0, yellowTop);
                yellow.setColorAt(1.0, yellowBottom);
            }
            p.fillRect(QRectF(well.center.x() - bodyRadius, liquidTop,
                              bodyRadius * 2.0, liquidHeight),
                       yellow);
        } else {
            // 全血：两支管底部都是血液；PPP 管上部用更透亮的 PPP 黄色。
            QLinearGradient red(0.0, boundary, 0.0, liquidBottom);
            red.setColorAt(0.0, redTop);
            red.setColorAt(1.0, redBottom);
            p.fillRect(QRectF(well.center.x() - bodyRadius, boundary,
                              bodyRadius * 2.0, liquidBottom - boundary),
                       red);

            QLinearGradient yellow(0.0, liquidTop, 0.0, boundary);
            if (isPpp) {
                yellow.setColorAt(0.0, QColor(255, 250, 208, 230));
                yellow.setColorAt(1.0, QColor(255, 229, 132, 225));
            } else {
                yellow.setColorAt(0.0, yellowTop);
                yellow.setColorAt(1.0, QColor(240, 218, 154));
            }
            p.fillRect(QRectF(well.center.x() - bodyRadius, liquidTop,
                              bodyRadius * 2.0, boundary - liquidTop),
                       yellow);

            p.setPen(QPen(QColor(255, 238, 198, 150), 0.9));
            p.drawLine(QPointF(well.center.x() - bodyRadius, boundary),
                       QPointF(well.center.x() + bodyRadius, boundary));
        }

        // 液面
        p.setPen(QPen(QColor(255, 255, 255, 150), 0.7));
        p.setBrush(QColor(255, 255, 255, 38));
        p.drawEllipse(QPointF(well.center.x(), liquidTop),
                      bodyRadius * 0.90, bodyRadius * 0.20);
        p.restore();

        // 玻璃管高光/边缘
        p.save();
        p.setClipPath(bodyPath);
        QLinearGradient glass(well.center.x() - bodyRadius, 0.0,
                              well.center.x() + bodyRadius, 0.0);
        glass.setColorAt(0.0, QColor(255, 255, 255, 165));
        glass.setColorAt(0.20, QColor(255, 255, 255, 22));
        glass.setColorAt(0.50, QColor(255, 255, 255, 6));
        glass.setColorAt(0.78, QColor(255, 255, 255, 28));
        glass.setColorAt(1.0, QColor(255, 255, 255, 175));
        p.fillRect(bodyRect, glass);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 105));
        p.drawRoundedRect(QRectF(well.center.x() - bodyRadius * 0.55,
                                 bodyTop + bodyHeight * 0.05,
                                 bodyRadius * 0.20,
                                 bodyHeight * 0.88),
                          bodyRadius * 0.10, bodyRadius * 0.10);
        p.restore();

        p.setPen(QPen(QColor(112, 144, 166, 180), 0.9));
        p.setBrush(Qt::NoBrush);
        p.drawPath(bodyPath);
        p.setPen(QPen(QColor(70, 100, 120, 80), 0.7));
        p.drawLine(QPointF(well.center.x() + bodyRadius * 0.72, bodyTop + 3.0),
                   QPointF(well.center.x() + bodyRadius * 0.72,
                           bodyBottom - 3.0));

        // 敞口管口：外圈玻璃壁 + 深色管口内腔，不画管帽。
        const qreal rimRy = qMax<qreal>(2.0, bodyRadius * 0.30);
        const QRectF rimRect(well.center.x() - bodyRadius,
                             bodyTop - rimRy,
                             bodyRadius * 2.0, rimRy * 2.0);
        QLinearGradient rimGradient(0.0, rimRect.top(),
                                    0.0, rimRect.bottom());
        rimGradient.setColorAt(0.0, QColor(247, 251, 253, 235));
        rimGradient.setColorAt(0.55, QColor(224, 237, 245, 235));
        rimGradient.setColorAt(1.0, QColor(193, 212, 226, 235));
        p.setPen(QPen(QColor(150, 176, 194, 220), 0.9));
        p.setBrush(rimGradient);
        p.drawEllipse(rimRect);

        const QRectF innerRect(well.center.x() - bodyRadius * 0.78,
                               bodyTop - rimRy * 0.78,
                               bodyRadius * 1.56, rimRy * 1.56);
        QRadialGradient mouth(innerRect.center() - QPointF(0.0, rimRy * 0.18),
                              qMax<qreal>(3.0, bodyRadius * 0.78));
        mouth.setColorAt(0.0, QColor(43, 63, 78, 235));
        mouth.setColorAt(0.70, QColor(79, 102, 118, 225));
        mouth.setColorAt(1.0, QColor(120, 143, 159, 210));
        p.setPen(QPen(QColor(230, 241, 248, 180), 0.8));
        p.setBrush(mouth);
        p.drawEllipse(innerRect);

        p.setPen(QPen(QColor(255, 255, 255, 130), 0.6));
        p.setBrush(Qt::NoBrush);
        p.drawArc(innerRect, 200 * 16, 140 * 16);

        // 孔内前侧壁：盖住管身下段，形成采血管插进孔内的深度感。
        const QRectF holeRect(well.center.x() - well.holeRx,
                              well.center.y() - well.holeRy,
                              well.holeRx * 2.0, well.holeRy * 2.0);
        QPainterPath holePath;
        holePath.addEllipse(holeRect);
        p.save();
        p.setClipPath(holePath);
        QLinearGradient innerWall(0.0, well.center.y(),
                                  0.0, well.center.y()
                                  + well.holeRy * 1.2);
        innerWall.setColorAt(0.0, QColor(32, 49, 62, 245));
        innerWall.setColorAt(0.55, QColor(55, 76, 92, 245));
        innerWall.setColorAt(1.0, QColor(115, 141, 159, 245));
        p.setPen(Qt::NoPen);
        p.setBrush(innerWall);
        p.drawRect(QRectF(well.center.x() - well.holeRx,
                          well.center.y(),
                          well.holeRx * 2.0, well.holeRy * 1.2));
        p.restore();

        // 孔口前沿高光
        p.setPen(QPen(QColor(205, 225, 239, 230), 1.3));
        p.setBrush(Qt::NoBrush);
        p.drawArc(holeRect, 195 * 16, 150 * 16);

        if (preview) {
            const QColor color = previewValid
                    ? QColor(QStringLiteral("#2C7BBF"))
                    : QColor(QStringLiteral("#D9534F"));
            QPen outlinePen(color, 2.2, Qt::DashLine);
            outlinePen.setCapStyle(Qt::RoundCap);
            p.setPen(outlinePen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(well.center.x() - bodyRadius - 2.0,
                                     bodyTop - 2.0,
                                     bodyRadius * 2.0 + 4.0,
                                     bodyHeight + 4.0),
                              bodyRadius * 0.95, bodyRadius * 0.95);
            p.setPen(QPen(color, 2.6, Qt::DashLine));
            p.drawEllipse(well.center, well.holeRx * 1.55,
                          well.holeRy * 1.55);
            if (!previewValid) {
                p.save();
                p.setClipPath(bodyPath);
                p.fillRect(bodyRect, QColor(217, 83, 79, 55));
                p.restore();
            }
        }

        // 下层托板上的可见管段：从上层平板前缘下方延伸到托底支撑。
        const qreal lowerTop = well.lowerTopY;
        const qreal lowerBottom = well.tubeBottomY - 1.0;
        if (lowerBottom - lowerTop > 3.0) {
            paintLowerTubeSegment(p, well, lowerTop, lowerBottom,
                                  selected, preview, previewValid);
        }
    }

    void paintLowerTubeSegment(QPainter &p, const Well &well,
                               qreal top, qreal bottom,
                               bool selected, bool preview = false,
                               bool previewValid = true) const
    {
        const qreal r = well.bodyRadius;
        const qreal left = well.center.x() - r;
        const qreal right = well.center.x() + r;
        const qreal height = bottom - top;

        // 下层托板上的投影
        QRadialGradient shadow(
                    QPointF(well.center.x() + r * 0.4, bottom),
                    qMax<qreal>(8.0, r * 1.8));
        shadow.setColorAt(0.0, QColor(31, 52, 68, 65));
        shadow.setColorAt(1.0, QColor(31, 52, 68, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(shadow);
        p.drawEllipse(QPointF(well.center.x() + r * 0.4, bottom),
                      r * 1.5, r * 0.45);

        // 平顶、圆底的管身路径
        QPainterPath bodyPath;
        bodyPath.moveTo(left, top);
        bodyPath.lineTo(right, top);
        bodyPath.lineTo(right, bottom - r * 0.65);
        bodyPath.quadTo(right, bottom, right - r * 0.65, bottom);
        bodyPath.lineTo(left + r * 0.65, bottom);
        bodyPath.quadTo(left, bottom, left, bottom - r * 0.65);
        bodyPath.closeSubpath();

        p.save();
        p.setClipPath(bodyPath);
        QLinearGradient liquid(0.0, top, 0.0, bottom);
        if (m_plasma) {
            if (m_pppPositions.contains(well.position)
                    || m_previewPppPositions.contains(well.position)) {
                liquid.setColorAt(0.0, QColor(255, 250, 208, 225));
                liquid.setColorAt(0.55, QColor(255, 238, 164, 225));
                liquid.setColorAt(1.0, QColor(255, 226, 126, 220));
            } else {
                liquid.setColorAt(0.0, QColor(247, 233, 181));
                liquid.setColorAt(1.0, QColor(233, 212, 140));
            }
        } else {
            // 全血 PPP/PRP 管下层都是血液。
            liquid.setColorAt(0.0, QColor(196, 58, 70));
            liquid.setColorAt(1.0, QColor(158, 31, 43));
        }
        p.fillRect(QRectF(left, top, r * 2.0, height), liquid);

        // 上层平板在前缘投下的阴影
        QLinearGradient topShade(0.0, top, 0.0,
                                 top + qMin<qreal>(8.0, height * 0.3));
        topShade.setColorAt(0.0, QColor(38, 55, 70, 120));
        topShade.setColorAt(1.0, QColor(38, 55, 70, 0));
        p.fillRect(QRectF(left, top, r * 2.0, height * 0.3), topShade);
        p.restore();

        // 玻璃高光/边缘
        p.save();
        p.setClipPath(bodyPath);
        QLinearGradient glass(left, 0.0, right, 0.0);
        glass.setColorAt(0.0, QColor(255, 255, 255, 150));
        glass.setColorAt(0.20, QColor(255, 255, 255, 20));
        glass.setColorAt(0.50, QColor(255, 255, 255, 5));
        glass.setColorAt(0.78, QColor(255, 255, 255, 24));
        glass.setColorAt(1.0, QColor(255, 255, 255, 160));
        p.fillRect(QRectF(left, top, r * 2.0, height), glass);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 95));
        p.drawRoundedRect(QRectF(well.center.x() - r * 0.55,
                                 top + 2.0, r * 0.18,
                                 qMax<qreal>(1.0, height - 4.0)),
                          r * 0.10, r * 0.10);
        p.restore();

        p.setPen(QPen(QColor(112, 144, 166, 180), 0.9));
        p.setBrush(Qt::NoBrush);
        p.drawPath(bodyPath);
        if (preview) {
            const QColor color = previewValid
                    ? QColor(QStringLiteral("#2C7BBF"))
                    : QColor(QStringLiteral("#D9534F"));
            p.setPen(QPen(color, 2.2, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(left, top, r * 2.0, height)
                              .adjusted(-2.0, -2.0, 2.0, 2.0),
                              r * 0.7, r * 0.7);
            if (!previewValid) {
                p.save();
                p.setClipPath(bodyPath);
                p.fillRect(QRectF(left, top, r * 2.0, height),
                           QColor(217, 83, 79, 55));
                p.restore();
            }
        } else if (selected) {
            p.setPen(QPen(QColor(44, 123, 191, 200), 1.4));
            p.drawRoundedRect(QRectF(left, top, r * 2.0, height)
                              .adjusted(-1.0, -1.0, 1.0, 1.0),
                              r * 0.7, r * 0.7);
        }

        // 托底支撑前沿：盖住管底，显示采血管坐在托板上。
        const qreal supportRx = r * 1.18;
        const qreal supportRy = r * 0.34;
        const QPointF support(well.center.x(), well.tubeBottomY);
        const QRectF supportRect(support.x() - supportRx,
                                 support.y() - supportRy,
                                 supportRx * 2.0, supportRy * 2.0);
        QPainterPath supportPath;
        supportPath.addEllipse(supportRect);
        p.save();
        p.setClipPath(supportPath);
        QLinearGradient inner(0.0, support.y(),
                              0.0, support.y() + supportRy * 1.2);
        inner.setColorAt(0.0, QColor(60, 80, 96, 235));
        inner.setColorAt(1.0, QColor(150, 174, 191, 235));
        p.setPen(Qt::NoPen);
        p.setBrush(inner);
        p.drawRect(QRectF(support.x() - supportRx, support.y(),
                          supportRx * 2.0, supportRy * 1.2));
        p.restore();

        p.setPen(QPen(QColor(225, 238, 247, 210), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawArc(supportRect, 200 * 16, 140 * 16);
    }

    int wellAt(const QPoint &position) const
    {
        const Geometry geometry = rackGeometry();
        if (!geometry.valid()) {
            return -1;
        }

        // 从前排往后排命中，重叠时优先选前排。
        for (int i = geometry.wells.size() - 1; i >= 0; --i) {
            const Well &well = geometry.wells.at(i);
            if (m_samplePositions.contains(well.position)) {
                const qreal hitTop = well.center.y()
                        - well.tubeHeight - 3.0;
                const QRectF hitRect(
                            well.center.x() - well.bodyRadius * 1.25,
                            hitTop,
                            well.bodyRadius * 2.5,
                            well.tubeBottomY - hitTop + 6.0);
                if (hitRect.contains(position)) {
                    return i;
                }
            } else {
                const qreal dx = (position.x() - well.center.x())
                        / (well.holeRx * 1.35);
                const qreal dy = (position.y() - well.center.y())
                        / (well.holeRy * 1.9);
                if (dx * dx + dy * dy <= 1.0) {
                    return i;
                }
            }
        }
        return -1;
    }

    MachineModel m_model = defaultMachineModel();
    bool m_plasma = false;
    QSet<QString> m_samplePositions;
    QSet<QString> m_selectedPositions;
    QSet<QString> m_pppPositions;   // PPP+PRP 模式下每对的第一个孔为 PPP
    QSet<QString> m_previewPppPositions;
    QStringList m_movingPositions;
    QStringList m_previewPositions;
    MoveHandler m_moveCommit;
    CancelHandler m_moveCancel;
    bool m_moving = false;
    bool m_previewValid = true;
    bool m_hasPreviewTarget = false;
    int m_hoverWell = -1;
};

AddSampleWidget::AddSampleWidget(MachineModel model,
                                 TestDataStore *dataStore,
                                 QWidget *parent)
    : QWidget(parent)
    , m_model(model)
    , m_dataStore(dataStore)
{
    setObjectName(QStringLiteral("addSamplePage"));
    setStyleSheet(QStringLiteral(
        "QWidget#addSamplePage { background: #F4F8FC; }"
        "QLabel#addSampleTitle {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 21px;"
        "  font-weight: 700;"
        "}"
        "QLabel#addSampleSubtitle {"
        "  color: #7A8FA3;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QFrame#sampleCard, QFrame#bloodCard {"
        "  background: #FFFFFF;"
        "  border: 1px solid #DCE7F1;"
        "  border-radius: 10px;"
        "}"
        "QLabel#cardTitle {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 14px;"
        "  font-weight: 600;"
        "}"
        "QLabel#cardNote {"
        "  color: #7A8FA3;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 11px;"
        "}"
        "QLabel#tableHint {"
        "  color: #8A9CAB;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 11px;"
        "}"
        "QLabel#quickLabel {"
        "  color: #5D7386;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QLineEdit#quickInput {"
        "  background: #FFFFFF;"
        "  color: #1F2D3D;"
        "  border: 1px solid #A9C8DF;"
        "  border-radius: 6px;"
        "  padding: 4px 8px;"
        "  min-height: 28px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  selection-background-color: #2C7BBF;"
        "  selection-color: #FFFFFF;"
        "}"
        "QLineEdit#quickInput:focus {"
        "  border: 2px solid #2C7BBF;"
        "  padding: 3px 7px;"
        "}"
        "QLineEdit#quickInput:disabled {"
        "  background: #F2F5F8;"
        "  color: #9AA9B5;"
        "}"
        "QPushButton#sampleAction {"
        "  background: #FFFFFF;"
        "  color: #1F4E79;"
        "  border: 1px solid #A9C8DF;"
        "  border-radius: 6px;"
        "  padding: 5px 12px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QPushButton#sampleAction:hover { background: #EAF4FC; }"
        "QPushButton#sampleAction:pressed { background: #DCEBFA; }"
        "QPushButton#sampleAction:disabled {"
        "  color: #AFC0CC;"
        "  border-color: #DCE7F1;"
        "  background: #F7FAFC;"
        "}"
        "QPushButton#sampleAddPrimary {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "      stop:0 #FFFFFF, stop:0.45 #F0F9FF, stop:1 #D9EDFB);"
        "  color: #174A6D;"
        "  border: 2px solid #7FB3D3;"
        "  border-radius: 8px;"
        "  padding: 9px 18px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 14px;"
        "  font-weight: 700;"
        "}"
        "QPushButton#sampleAddPrimary:hover {"
        "  background: #E8F6FF;"
        "  border-color: #4E97C6;"
        "}"
        "QPushButton#sampleAddPrimary:pressed {"
        "  background: #CFE8F8;"
        "  border-color: #2C7BBF;"
        "}"
        "QPushButton#sampleAddPrimary:disabled {"
        "  color: #AFC0CC;"
        "  border-color: #DCE7F1;"
        "  background: #F7FAFC;"
        "}"
        "QPushButton#sampleConfirm {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "      stop:0 #4A9AD8, stop:0.5 #2C7BBF, stop:1 #23649B);"
        "  color: #FFFFFF;"
        "  border: 2px solid #2C7BBF;"
        "  border-radius: 8px;"
        "  padding: 9px 20px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 14px;"
        "  font-weight: 700;"
        "}"
        "QPushButton#sampleConfirm:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "      stop:0 #5AA9E2, stop:1 #2F78B5);"
        "}"
        "QPushButton#sampleConfirm:pressed {"
        "  background: #2468A3;"
        "}"
        "QPushButton#sampleConfirm:disabled {"
        "  color: #EAF2F8;"
        "  border-color: #A9C8DF;"
        "  background: #A9C8DF;"
        "}"
        "QTableWidget#sampleTable {"
        "  background: #FFFFFF;"
        "  alternate-background-color: #F7FAFD;"
        "  border: 1px solid #D8E5EF;"
        "  border-radius: 6px;"
        "  gridline-color: #E6EEF5;"
        "  selection-background-color: #DCEBFA;"
        "  selection-color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QTableWidget#sampleTable::item { padding: 2px 5px; }"
        "QTableWidget#sampleTable QHeaderView::section {"
        "  background: #EAF2F9;"
        "  color: #1F4E79;"
        "  border: none;"
        "  border-right: 1px solid #D8E5EF;"
        "  border-bottom: 1px solid #D8E5EF;"
        "  padding: 4px 6px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QTableCornerButton::section {"
        "  background: #EAF2F9;"
        "  border: none;"
        "}"));

    buildUi();
    updateSampleQuality();
}

void AddSampleWidget::setMachineModel(MachineModel model)
{
    const bool changed = (m_model != model);
    m_model = model;
    if (m_bloodArea) {
        m_bloodArea->setMachineModel(model);
    }
    if (changed && m_sampleTable && m_sampleTable->rowCount() > 0) {
        reallocatePositionsForMode();
    }
    updateSampleQuality();
}

void AddSampleWidget::refreshMode()
{
    updateSampleQuality();
}

TestQueue *AddSampleWidget::testQueue()
{
    return &m_testQueue;
}

const TestQueue *AddSampleWidget::testQueue() const
{
    return &m_testQueue;
}

int AddSampleWidget::activeSampleCount() const
{
    if (!m_sampleTable) {
        return 0;
    }
    int count = 0;
    for (int row = 0; row < m_sampleTable->rowCount(); ++row) {
        QTableWidgetItem *status = m_sampleTable->item(row, StatusColumn);
        if (status
                && (status->text() == tr("待测")
                    || status->text() == tr("加样完成")
                    || status->text() == tr("已加样")
                    || status->text() == tr("测试中")
                    || status->text() == tr("完成")
                    || status->text() == tr("异常结果"))) {
            ++count;
        }
    }
    return count;
}

void AddSampleWidget::setSampleTesting(const QString &sampleNumber,
                                       bool testing)
{
    if (!m_sampleTable) {
        return;
    }
    const QString trimmed = sampleNumber.trimmed();
    for (int row = 0; row < m_sampleTable->rowCount(); ++row) {
        QTableWidgetItem *sampleNo = m_sampleTable->item(row,
                                                         SampleNoColumn);
        if (!sampleNo || sampleNo->text().trimmed() != trimmed) {
            continue;
        }
        // 加样页表格状态固定在“加样完成”，测试状态只在结果信息表变化；
        // 这里只维护内部“测试中”标记（用于禁止删除正在测试的样本）。
        if (testing) {
            m_testingSamples.insert(trimmed);
            m_testQueue.removeBySampleNumber(trimmed);
        } else {
            m_testingSamples.remove(trimmed);
            QueuedSample sample;
            if (queuedSampleFromRow(row, &sample)
                    && !m_testQueue.contains(sample.sampleNumber)) {
                QString error;
                if (!m_testQueue.enqueue(sample, &error)) {
                    qWarning() << "Re-enqueue sample failed:" << error;
                }
            }
        }
        updateRowEditability(row);
        if (testing) {
            m_sampleTable->selectRow(row);
        }
        emit sampleStatusChanged(trimmed, testing ? tr("测试中")
                                                  : tr("待测"));
        return;
    }
}

bool AddSampleWidget::setSampleStatus(const QString &sampleNumber,
                                      const QString &status)
{
    if (!m_sampleTable) {
        return false;
    }
    const QString trimmed = sampleNumber.trimmed();
    for (int row = 0; row < m_sampleTable->rowCount(); ++row) {
        QTableWidgetItem *sampleNo = m_sampleTable->item(row,
                                                         SampleNoColumn);
        if (!sampleNo || sampleNo->text().trimmed() != trimmed) {
            continue;
        }
        if (QTableWidgetItem *item =
                m_sampleTable->item(row, StatusColumn)) {
            m_updatingTable = true;
            item->setText(status);
            m_updatingTable = false;
        }
        updateRowEditability(row);
        updateSampleCount();
        syncBloodAreaSamples();
        return true;
    }
    return false;
}

void AddSampleWidget::setSampleAdded(const QString &sampleNumber)
{
    // 加样页状态到此为止：之后测试中/测试结束/异常都只在结果信息表显示。
    setSampleStatus(sampleNumber, tr("加样完成"));
    emit sampleStatusChanged(sampleNumber, tr("测试中"));
}

void AddSampleWidget::setSampleCompleted(const QString &sampleNumber)
{
    m_testingSamples.remove(sampleNumber.trimmed());
    emit sampleStatusChanged(sampleNumber, tr("测试结束"));
}

void AddSampleWidget::setSampleAbnormal(const QString &sampleNumber)
{
    m_testingSamples.remove(sampleNumber.trimmed());
    emit sampleStatusChanged(sampleNumber, tr("异常结果"));
}

void AddSampleWidget::clearTestingSamples()
{
    m_testingSamples.clear();
}

bool AddSampleWidget::isRowPositionReleased(int row) const
{
    if (!m_sampleTable || row < 0
            || row >= m_sampleTable->rowCount()) {
        return false;
    }
    QTableWidgetItem *status = m_sampleTable->item(row, StatusColumn);
    if (!status) {
        return false;
    }
    const QString text = status->text();
    return text == tr("加样完成")
            || text == tr("已加样")
            || text == tr("测试中")
            || text == tr("完成")
            || text == tr("异常结果");
}

void AddSampleWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateSampleQuality();
    if (m_quickBarcodeEdit) {
        m_quickBarcodeEdit->setFocus();
        m_quickBarcodeEdit->selectAll();
    }
}

void AddSampleWidget::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 18);
    root->setSpacing(12);

    auto *header = new QVBoxLayout;
    header->setSpacing(2);
    auto *title = new QLabel(tr("加样"), this);
    title->setObjectName(QStringLiteral("addSampleTitle"));
    m_subtitle = new QLabel(this);
    m_subtitle->setObjectName(QStringLiteral("addSampleSubtitle"));
    header->addWidget(title);
    header->addWidget(m_subtitle);
    root->addLayout(header);

    auto *content = new QHBoxLayout;
    content->setSpacing(12);
    content->addWidget(createBloodAreaCard(), 1);

    auto *rightColumn = new QWidget(this);
    rightColumn->setFixedWidth(500);
    auto *rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);

    auto *rightButtons = new QHBoxLayout;
    rightButtons->setSpacing(10);
    m_addSampleButton = new QPushButton(tr("＋ 添加"), rightColumn);
    m_addSampleButton->setObjectName(QStringLiteral("sampleAddPrimary"));
    m_addSampleButton->setCursor(Qt::PointingHandCursor);
    m_addSampleButton->setMinimumHeight(44);
    m_addSampleButton->setSizePolicy(
                QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_confirmButton = new QPushButton(tr("确认"), rightColumn);
    m_confirmButton->setObjectName(QStringLiteral("sampleConfirm"));
    m_confirmButton->setCursor(Qt::PointingHandCursor);
    m_confirmButton->setMinimumHeight(44);
    m_confirmButton->setSizePolicy(
                QSizePolicy::Expanding, QSizePolicy::Fixed);
    rightButtons->addWidget(m_addSampleButton, 1);
    rightButtons->addWidget(m_confirmButton, 1);
    rightLayout->addLayout(rightButtons);
    rightLayout->addWidget(createSampleTableCard(), 1);
    content->addWidget(rightColumn, 0);

    connect(m_addSampleButton, &QPushButton::clicked,
            this, &AddSampleWidget::addSample);
    connect(m_confirmButton, &QPushButton::clicked,
            this, &AddSampleWidget::confirmSamples);

    root->addLayout(content, 1);
}

QFrame *AddSampleWidget::createSampleTableCard()
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("sampleCard"));
    card->setFixedWidth(500);
    card->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);

    auto *titleRow = new QHBoxLayout;
    auto *title = new QLabel(tr("加样列表"), card);
    title->setObjectName(QStringLiteral("cardTitle"));
    m_countLabel = new QLabel(card);
    m_countLabel->setObjectName(QStringLiteral("cardNote"));
    m_countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    titleRow->addWidget(title);
    titleRow->addStretch(1);
    titleRow->addWidget(m_countLabel);
    layout->addLayout(titleRow);

    // 快速加样：样本号 + 条码 一次录入，条码栏可直接扫码/手动输入。
    auto *quickGrid = new QGridLayout;
    quickGrid->setHorizontalSpacing(8);
    quickGrid->setVerticalSpacing(6);

    auto *sampleNoCaption = new QLabel(tr("样本号"), card);
    sampleNoCaption->setObjectName(QStringLiteral("quickLabel"));
    m_quickSampleNoEdit = new QLineEdit(card);
    m_quickSampleNoEdit->setObjectName(QStringLiteral("quickInput"));
    m_quickSampleNoEdit->setPlaceholderText(tr("下一个样本号"));
    m_quickSampleNoEdit->setToolTip(
                tr("默认按 T+8位日期+4位序号生成，共 13 位；"
                   "可修改，但必须保持 13 位长度"));
    m_quickSampleNoEdit->setValidator(new QRegularExpressionValidator(
                QRegularExpression(QStringLiteral("^[Tt]?\\d{0,12}$")),
                m_quickSampleNoEdit));

    auto *barcodeCaption = new QLabel(tr("条码"), card);
    barcodeCaption->setObjectName(QStringLiteral("quickLabel"));
    m_quickBarcodeEdit = new QLineEdit(card);
    m_quickBarcodeEdit->setObjectName(QStringLiteral("quickInput"));
    m_quickBarcodeEdit->setPlaceholderText(tr("扫描或手动输入数字条码"));
    m_quickBarcodeEdit->setClearButtonEnabled(true);
    m_quickBarcodeEdit->setToolTip(
                tr("扫码枪直接扫入；只能输入数字，回车或点上方「添加」加入列表"));
    m_quickBarcodeEdit->setValidator(new QRegularExpressionValidator(
                QRegularExpression(QStringLiteral("^\\d*$")),
                m_quickBarcodeEdit));

    quickGrid->addWidget(sampleNoCaption, 0, 0);
    quickGrid->addWidget(m_quickSampleNoEdit, 0, 1);
    quickGrid->addWidget(barcodeCaption, 1, 0);
    quickGrid->addWidget(m_quickBarcodeEdit, 1, 1);
    quickGrid->setColumnStretch(1, 1);
    layout->addLayout(quickGrid);

    auto *toolRow = new QHBoxLayout;
    toolRow->setSpacing(8);
    m_removeSampleButton = new QPushButton(tr("删除选中"), card);
    m_modifySampleButton = new QPushButton(tr("修改样本号"), card);
    for (QPushButton *button : {m_removeSampleButton,
                                m_modifySampleButton}) {
        button->setObjectName(QStringLiteral("sampleAction"));
        button->setCursor(Qt::PointingHandCursor);
        toolRow->addWidget(button);
    }
    toolRow->addStretch(1);
    layout->addLayout(toolRow);

    m_tableHint = new QLabel(card);
    m_tableHint->setObjectName(QStringLiteral("tableHint"));
    m_tableHint->setWordWrap(true);
    layout->addWidget(m_tableHint);

    m_sampleTable = new QTableWidget(card);
    m_sampleTable->setObjectName(QStringLiteral("sampleTable"));
    m_sampleTable->setColumnCount(SampleColumnCount);
    m_sampleTable->setHorizontalHeaderLabels(QStringList()
        << tr("状态") << tr("样本号") << tr("条码") << tr("测试项目")
        << tr("识别高度") << tr("样本位置"));
    m_sampleTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_sampleTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_sampleTable->setEditTriggers(QAbstractItemView::DoubleClicked
                                   | QAbstractItemView::EditKeyPressed);
    m_sampleTable->setAlternatingRowColors(true);
    m_sampleTable->setShowGrid(true);
    m_sampleTable->setWordWrap(true);
    m_sampleTable->setTextElideMode(Qt::ElideNone);
    m_sampleTable->setFrameShape(QFrame::NoFrame);
    m_sampleTable->setFocusPolicy(Qt::StrongFocus);
    m_sampleTable->setCornerButtonEnabled(false);
    m_sampleTable->verticalHeader()->setVisible(false);
    m_sampleTable->verticalHeader()->setDefaultSectionSize(kSampleRowHeight);
    m_sampleTable->horizontalHeader()->setHighlightSections(false);
    m_sampleTable->horizontalHeader()->setSectionsClickable(false);
    m_sampleTable->horizontalHeader()->setFixedHeight(30);
    m_sampleTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_sampleTable->horizontalHeader()->setSectionResizeMode(
                TestItemColumn, QHeaderView::Stretch);
    m_sampleTable->setColumnWidth(StatusColumn, 65);
    m_sampleTable->setColumnWidth(SampleNoColumn, 105);
    m_sampleTable->setColumnWidth(BarcodeColumn, 85);
    m_sampleTable->setColumnWidth(HeightColumn, 70);
    m_sampleTable->setColumnWidth(PositionColumn, 65);
    m_sampleTable->setMinimumHeight(150);

    connect(m_sampleTable->horizontalHeader(),
            &QHeaderView::sectionResized, this,
            [this](int, int, int) {
        adjustAllSampleRowHeights();
    });

    m_sampleTable->setItemDelegate(
                new WrappedItemDelegate(m_sampleTable));
    m_sampleTable->setItemDelegateForColumn(
                StatusColumn, new SampleStatusDelegate(m_sampleTable));
    m_sampleTable->setItemDelegateForColumn(
                BarcodeColumn, new BarcodeDelegate(m_sampleTable));

    auto *statusHeader = m_sampleTable->horizontalHeaderItem(StatusColumn);
    if (statusHeader) {
        statusHeader->setToolTip(
                    tr("待测 / 测试中；测试中的样本不能删除"));
    }
    auto *sampleNoHeader = m_sampleTable->horizontalHeaderItem(
                SampleNoColumn);
    if (sampleNoHeader) {
        sampleNoHeader->setToolTip(
                    tr("T + 日期(yyyyMMdd) + 4位序号；只允许 T 和数字"));
    }
    auto *barcodeHeader = m_sampleTable->horizontalHeaderItem(
                BarcodeColumn);
    if (barcodeHeader) {
        barcodeHeader->setToolTip(tr("只允许数字条码"));
    }
    auto *testItemHeader = m_sampleTable->horizontalHeaderItem(
                TestItemColumn);
    if (testItemHeader) {
        testItemHeader->setToolTip(
                    tr("双击选择测试项目，可单选或多选自由组合"));
    }
    auto *heightHeader = m_sampleTable->horizontalHeaderItem(HeightColumn);
    if (heightHeader) {
        heightHeader->setToolTip(
                    tr("全血为识别高度，血浆为吸样高度；由识别结果自动填入"));
    }
    auto *positionHeader = m_sampleTable->horizontalHeaderItem(
                PositionColumn);
    if (positionHeader) {
        positionHeader->setToolTip(
                    tr("血样孔号，例如 10；PPP+PRP 模式为 10+11"));
    }

    connect(m_quickSampleNoEdit, &QLineEdit::textEdited,
            this, [this]() { m_quickSampleNoEdited = true; });
    connect(m_quickBarcodeEdit, &QLineEdit::returnPressed,
            this, &AddSampleWidget::addSample);
    connect(m_quickSampleNoEdit, &QLineEdit::returnPressed,
            this, [this]() {
        if (m_quickBarcodeEdit) {
            m_quickBarcodeEdit->setFocus();
            m_quickBarcodeEdit->selectAll();
        }
    });
    connect(m_removeSampleButton, &QPushButton::clicked,
            this, &AddSampleWidget::removeSelectedSamples);
    connect(m_modifySampleButton, &QPushButton::clicked,
            this, &AddSampleWidget::modifySampleNumber);
    connect(m_sampleTable, &QTableWidget::itemChanged,
            this, &AddSampleWidget::handleTableItemChanged);
    connect(m_sampleTable, &QTableWidget::cellDoubleClicked,
            this, [this](int row, int column) {
        if (column == SampleNoColumn) {
            m_sampleTable->selectRow(row);
            m_sampleTable->setCurrentCell(row, SampleNoColumn);
            modifySampleNumber();
        } else if (column == TestItemColumn) {
            m_sampleTable->selectRow(row);
            m_sampleTable->setCurrentCell(row, TestItemColumn);
            modifyTestItems(row);
        }
    });
    connect(m_sampleTable, &QTableWidget::cellClicked,
            this, [this](int row, int column) {
        if (column != PositionColumn) {
            return;
        }
        QTableWidgetItem *status = m_sampleTable->item(row, StatusColumn);
        if (!status || status->text() != tr("待定")) {
            if (m_movingRow >= 0) {
                m_movingRow = -1;
                m_bloodArea->cancelMoving();
            }
            MedicalMessageBox::information(
                        this, tr("移动样本位置"),
                        tr("只有待定样本可以移动孔位；"
                           "已确认或测试中的样本不能移动。"));
            return;
        }
        QTableWidgetItem *position = m_sampleTable->item(row,
                                                         PositionColumn);
        if (!position) {
            return;
        }
        const QStringList parts = positionParts(position->text());
        if (parts.isEmpty()) {
            return;
        }
        m_movingRow = row;
        m_sampleTable->selectRow(row);
        m_sampleTable->setCurrentCell(row, PositionColumn);
        m_bloodArea->startMoving(parts);
    });
    connect(m_sampleTable, &QTableWidget::itemSelectionChanged,
            this, [this]() {
        if (m_movingRow >= 0
                && m_sampleTable->currentRow() != m_movingRow) {
            m_movingRow = -1;
            m_bloodArea->cancelMoving();
        }
        syncBloodAreaSamples();
    });

    m_sampleTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_sampleTable, &QTableWidget::customContextMenuRequested,
            this, [this](const QPoint &pos) {
        const QModelIndex index = m_sampleTable->indexAt(pos);
        if (!index.isValid()) {
            return;
        }
        if (!m_sampleTable->selectionModel()->isRowSelected(
                    index.row(), QModelIndex())) {
            m_sampleTable->selectRow(index.row());
        }

        QMenu menu(this);
        QAction *deleteAction = menu.addAction(tr("删除选中"));
        QAction *chosen = menu.exec(
                    m_sampleTable->viewport()->mapToGlobal(pos));
        if (chosen == deleteAction) {
            removeSelectedSamples();
        }
    });

    layout->addWidget(m_sampleTable, 1);
    return card;
}

QFrame *AddSampleWidget::createBloodAreaCard()
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("bloodCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 12, 16, 14);
    layout->setSpacing(6);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel(tr("血样区"), card);
    title->setObjectName(QStringLiteral("cardTitle"));
    m_bloodCaption = new QLabel(card);
    m_bloodCaption->setObjectName(QStringLiteral("cardNote"));
    m_bloodCaption->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(m_bloodCaption);
    layout->addLayout(header);

    m_bloodArea = new BloodSampleAreaWidget(m_model, card);
    m_bloodArea->setObjectName(QStringLiteral("bloodArea"));
    m_bloodArea->setMoveHandlers(
                [this](const QStringList &positions) {
        handleBloodAreaMoveCommitted(positions);
    },
                [this]() {
        handleBloodAreaMoveCancelled();
    });
    layout->addWidget(m_bloodArea, 1);
    return card;
}

void AddSampleWidget::updateSampleQuality()
{
    if (!m_subtitle || !m_bloodCaption || !m_bloodArea) {
        return;
    }

    const bool plasma = sampleQualityIsPlasma();
    const bool scanBarcode = addSampleScanBarcodeEnabled();
    const bool pppPrp = testModeIsPppPrp();
    if (pppPrp != m_lastPppPrpMode) {
        m_lastPppPrpMode = pppPrp;
        reallocatePositionsForMode();
    }
    updatePositionEditability();

    m_bloodArea->setMachineModel(m_model);
    m_bloodArea->setPlasma(plasma);
    m_bloodArea->update();
    if (m_addSampleButton) {
        m_addSampleButton->setEnabled(plasma);
        m_addSampleButton->setToolTip(
                    plasma
                    ? tr("手动添加一个血浆样本")
                    : tr("全血模式：由主板测高检测自动添加样本，"
                         "每次触发加 1 个"));
    }
    updateHeightHeader();
    updateSampleHeightValues();
    updateSampleCount();
    updateScanHint();
    updateQuickSampleNumber();
    syncBloodAreaSamples();

    const int wells = machineBloodSampleWellCount(m_model);
    m_subtitle->setText(
                tr("%1 · %2 · 测试模式：%3 · 扫码：%4 · 血样区 %5 孔")
                .arg(QString::fromLatin1(machineModelName(m_model)))
                .arg(plasma ? tr("血浆") : tr("全血"))
                .arg(pppPrp ? tr("PPP+PRP") : tr("PRP"))
                .arg(scanBarcode ? tr("开") : tr("关"))
                .arg(wells));

    m_bloodCaption->setText(
                tr("孔号跨组连续：A组0-9、B组10-19…… 每组5行×2列"));
}

void AddSampleWidget::updateHeightHeader()
{
    if (!m_sampleTable) {
        return;
    }
    auto *item = m_sampleTable->horizontalHeaderItem(HeightColumn);
    if (!item) {
        item = new QTableWidgetItem;
        m_sampleTable->setHorizontalHeaderItem(HeightColumn, item);
    }
    const bool plasma = sampleQualityIsPlasma();
    item->setText(plasma ? tr("吸样高度") : tr("识别高度"));
    item->setToolTip(
                plasma
                ? tr("血浆模式：吸样高度取基础设置中的固定吸PRP高度")
                : tr("全血模式：识别高度由测高结果填入"));
}

void AddSampleWidget::updateSampleHeightValues()
{
    if (!m_sampleTable) {
        return;
    }
    const bool plasma = sampleQualityIsPlasma();
    const QString height = plasma
            ? QString::number(plasmaPrpAspirateHeightFromSettings())
            : QString();
    m_updatingTable = true;
    for (int row = 0; row < m_sampleTable->rowCount(); ++row) {
        QTableWidgetItem *status = m_sampleTable->item(row, StatusColumn);
        // 已确认/测试中的样本保留确认时的高度，避免队列与实际不一致。
        if (!status || status->text() != tr("待定")) {
            continue;
        }
        QTableWidgetItem *item = m_sampleTable->item(row, HeightColumn);
        if (!item) {
            continue;
        }
        if (plasma) {
            if (item->text() != height
                    || item->data(kHeightSourceRole).toString()
                       != QLatin1String(kPlasmaAutoHeightSource)) {
                item->setText(height);
                item->setData(Qt::UserRole, height);
                item->setData(kHeightSourceRole,
                              QString::fromLatin1(kPlasmaAutoHeightSource));
            }
            item->setToolTip(
                        tr("血浆模式：吸样高度取基础设置中的固定值，不可编辑"));
        } else if (item->data(kHeightSourceRole).toString()
                   == QLatin1String(kPlasmaAutoHeightSource)) {
            // 切回全血时只清掉血浆自动填的值，保留后续测高结果。
            item->setText(QString());
            item->setData(Qt::UserRole, QString());
            item->setData(kHeightSourceRole, QVariant());
            item->setToolTip(tr("由识别/测高结果自动填入，不可编辑"));
        }
    }
    m_updatingTable = false;
}

void AddSampleWidget::adjustSampleRowHeight(int row)
{
    if (!m_sampleTable || row < 0
            || row >= m_sampleTable->rowCount()) {
        return;
    }
    // Qt 5.10 的 QTableWidget 对长数字/无空格文本的自动换行高度计算不完整，
    // 这里按每列文字宽度估算所需行数，保证内容能换行且不被裁掉。
    const QFontMetrics metrics(m_sampleTable->font());
    int lines = 1;
    for (int column = 0; column < SampleColumnCount; ++column) {
        QTableWidgetItem *item = m_sampleTable->item(row, column);
        if (!item || item->text().isEmpty()) {
            continue;
        }
        const int available = qMax(20,
                                   m_sampleTable->columnWidth(column) - 12);
        const int textWidth = qMax(1, metrics.width(item->text()));
        const int columnLines =
                (textWidth + available - 1) / available;
        lines = qMax(lines, qMax(1, columnLines));
    }
    const int height = qMax(kSampleRowHeight,
                            lines * metrics.lineSpacing() + 12);
    if (m_sampleTable->rowHeight(row) != height) {
        m_sampleTable->setRowHeight(row, height);
    }
}

void AddSampleWidget::adjustAllSampleRowHeights()
{
    if (!m_sampleTable) {
        return;
    }
    for (int row = 0; row < m_sampleTable->rowCount(); ++row) {
        adjustSampleRowHeight(row);
    }
}

void AddSampleWidget::updateSampleCount()
{
    if (!m_countLabel || !m_sampleTable) {
        return;
    }
    const int rows = m_sampleTable->rowCount();
    m_countLabel->setText(
                testModeIsPppPrp()
                ? tr("共 %1 个样本 · 每样本2孔").arg(rows)
                : tr("共 %1 个样本 · 每样本1孔").arg(rows));
}

void AddSampleWidget::updateScanHint()
{
    if (!m_tableHint) {
        return;
    }
    if (!sampleQualityIsPlasma()) {
        m_tableHint->setText(
                    addSampleScanBarcodeEnabled()
                    ? tr("全血：主板测高触发自动加样 · 触发前先扫描/输入条码 · "
                         "双击测试项目多选")
                    : tr("全血：主板测高触发自动加样 · 每次触发加 1 个 · "
                         "双击测试项目多选"));
        return;
    }
    if (addSampleScanBarcodeEnabled()) {
        m_tableHint->setText(
                    tr("扫码开：添加后需立即扫描/输入条码 · "
                       "双击测试项目多选"));
    } else {
        m_tableHint->setText(
                    tr("扫码关：条码可留空/手动输入 · "
                       "双击测试项目多选"));
    }
}

void AddSampleWidget::updateQuickSampleNumber()
{
    if (!m_quickSampleNoEdit || m_quickSampleNoEdited) {
        return;
    }
    QString error;
    const QString next = generateNextSampleNumber(&error);
    m_quickSampleNoEdit->setText(next);
    m_quickSampleNoEdit->setPlaceholderText(
                next.isEmpty()
                ? (error.isEmpty() ? tr("无法生成样本号") : error)
                : tr("下一个样本号"));
    m_quickSampleNoEdit->setToolTip(
                next.isEmpty()
                ? (error.isEmpty() ? tr("无法生成样本号") : error)
                : tr("默认按 T+8位日期+4位序号生成，共 13 位；"
                     "修改后也必须保持 13 位且不能重复"));
}

void AddSampleWidget::updatePositionEditability()
{
    if (!m_sampleTable) {
        return;
    }
    for (int row = 0; row < m_sampleTable->rowCount(); ++row) {
        updateRowEditability(row);
    }
    if (QTableWidgetItem *header =
            m_sampleTable->horizontalHeaderItem(PositionColumn)) {
        header->setToolTip(
                    tr("待定样本点击位置单元格后，在血样区把采血管拖到目标孔"));
    }
}

bool AddSampleWidget::queuedSampleFromRow(int row, QueuedSample *sample) const
{
    if (!m_sampleTable || !sample || row < 0
            || row >= m_sampleTable->rowCount()) {
        return false;
    }
    QTableWidgetItem *sampleNo = m_sampleTable->item(row, SampleNoColumn);
    if (!sampleNo || sampleNo->text().trimmed().isEmpty()) {
        return false;
    }

    sample->sampleNumber = sampleNo->text().trimmed();
    QTableWidgetItem *barcode = m_sampleTable->item(row, BarcodeColumn);
    sample->barcode = barcode ? barcode->text().trimmed() : QString();
    sample->testItems.clear();
    if (QTableWidgetItem *testItem =
            m_sampleTable->item(row, TestItemColumn)) {
        const QStringList items = testItem->text().split(
                    QLatin1Char('+'), QString::SkipEmptyParts);
        for (const QString &item : items) {
            const QString trimmed = item.trimmed();
            if (!trimmed.isEmpty()) {
                sample->testItems << trimmed;
            }
        }
    }
    QTableWidgetItem *height = m_sampleTable->item(row, HeightColumn);
    sample->height = height ? height->text().trimmed() : QString();
    QTableWidgetItem *position = m_sampleTable->item(row, PositionColumn);
    sample->positions = position ? positionParts(position->text())
                                 : QStringList();
    sample->pppPrp = testModeIsPppPrp();
    sample->queuedAt = QDateTime::currentDateTime();
    return true;
}

void AddSampleWidget::rebuildTestQueueFromConfirmedRows()
{
    if (!m_sampleTable) {
        return;
    }
    m_testQueue.clear();
    for (int row = 0; row < m_sampleTable->rowCount(); ++row) {
        QTableWidgetItem *status = m_sampleTable->item(row, StatusColumn);
        if (!status || status->text() != tr("待测")) {
            continue;
        }
        QueuedSample sample;
        if (!queuedSampleFromRow(row, &sample)) {
            continue;
        }
        QString error;
        if (!m_testQueue.enqueue(sample, &error)) {
            qWarning() << "Rebuild test queue failed:" << error;
        }
    }
}

void AddSampleWidget::updateRowEditability(int row)
{
    if (!m_sampleTable || row < 0
            || row >= m_sampleTable->rowCount()) {
        return;
    }
    QTableWidgetItem *status = m_sampleTable->item(row, StatusColumn);
    const bool draft = status && status->text() == tr("待定");

    auto setEditable = [this](QTableWidgetItem *item, bool editable) {
        if (!item) {
            return;
        }
        Qt::ItemFlags flags = item->flags();
        if (editable) {
            flags |= Qt::ItemIsEditable;
        } else {
            flags &= ~Qt::ItemIsEditable;
        }
        if (flags != item->flags()) {
            m_updatingTable = true;
            item->setFlags(flags);
            m_updatingTable = false;
        }
    };

    setEditable(m_sampleTable->item(row, BarcodeColumn), draft);
    // 位置列不再通过下拉框编辑，改成点击后在血样区拖动。
    setEditable(m_sampleTable->item(row, PositionColumn), false);
    setEditable(m_sampleTable->item(row, SampleNoColumn), false);
    setEditable(m_sampleTable->item(row, TestItemColumn), false);
    setEditable(m_sampleTable->item(row, HeightColumn), false);
}

void AddSampleWidget::reallocatePositionsForMode()
{
    if (!m_sampleTable) {
        return;
    }
    const bool pppPrp = testModeIsPppPrp();
    const int holes = machineBloodSampleWellCount(m_model);
    QSet<int> used;

    m_updatingTable = true;
    for (int row = 0; row < m_sampleTable->rowCount(); ++row) {
        QString position;
        if (pppPrp) {
            for (int hole = 0; hole + 1 < holes; ++hole) {
                if (!used.contains(hole) && !used.contains(hole + 1)) {
                    position = QStringLiteral("%1+%2")
                            .arg(hole).arg(hole + 1);
                    used.insert(hole);
                    used.insert(hole + 1);
                    break;
                }
            }
        } else {
            for (int hole = 0; hole < holes; ++hole) {
                if (!used.contains(hole)) {
                    position = QString::number(hole);
                    used.insert(hole);
                    break;
                }
            }
        }
        if (QTableWidgetItem *item =
                m_sampleTable->item(row, PositionColumn)) {
            item->setText(position);
            item->setData(Qt::UserRole, position);
        }
    }
    m_updatingTable = false;

    updatePositionEditability();
    rebuildTestQueueFromConfirmedRows();
    updateSampleCount();
    updateQuickSampleNumber();
    syncBloodAreaSamples();

    if (pppPrp && m_sampleTable->rowCount() > holes / 2) {
        MedicalMessageBox::warning(
                    this, tr("测试模式已切换"),
                    tr("当前加样列表有 %1 个样本，PPP+PRP 模式最多 %2 个；"
                       "超出部分没有分配孔位。")
                    .arg(m_sampleTable->rowCount())
                    .arg(holes / 2));
    }
}

void AddSampleWidget::focusBarcodeCell(int row)
{
    if (!m_sampleTable || row < 0 || row >= m_sampleTable->rowCount()) {
        return;
    }
    m_sampleTable->setFocus();
    m_sampleTable->selectRow(row);
    m_sampleTable->setCurrentCell(row, BarcodeColumn);
    if (QTableWidgetItem *item = m_sampleTable->item(row, BarcodeColumn)) {
        m_sampleTable->editItem(item);
    }
}

void AddSampleWidget::modifyTestItems(int row)
{
    if (!m_sampleTable || row < 0
            || row >= m_sampleTable->rowCount()) {
        return;
    }
    QTableWidgetItem *status = m_sampleTable->item(row, StatusColumn);
    if (!status || status->text() != tr("待定")) {
        MedicalMessageBox::information(
                    this, tr("测试项目"),
                    tr("只有待定样本可以修改测试项目；"
                       "已确认或测试中的样本不能修改。"));
        return;
    }
    QTableWidgetItem *item = m_sampleTable->item(row, TestItemColumn);
    if (!item) {
        return;
    }

    const QStringList current = item->text().split(
                QLatin1Char('+'), QString::SkipEmptyParts);
    TestItemDialog dialog(current, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString text = dialog.selectedItems().join(QLatin1Char('+'));
    m_updatingTable = true;
    item->setText(text);
    item->setData(Qt::UserRole, text);
    m_updatingTable = false;
    adjustSampleRowHeight(row);
}

void AddSampleWidget::syncBloodAreaSamples()
{
    if (!m_bloodArea || !m_sampleTable) {
        return;
    }

    QStringList positions;
    QStringList pppPositions;
    const bool pppMode = testModeIsPppPrp();
    for (int row = 0; row < m_sampleTable->rowCount(); ++row) {
        if (isRowPositionReleased(row)) {
            continue;
        }
        if (QTableWidgetItem *item =
                m_sampleTable->item(row, PositionColumn)) {
            const QStringList parts = positionParts(item->text());
            positions << parts;
            if (pppMode && !parts.isEmpty()) {
                pppPositions << parts.first();
            }
        }
    }
    m_bloodArea->setSamplePositions(positions);
    m_bloodArea->setPppPositions(pppPositions);

    QStringList selectedPositions;
    const QModelIndexList selected =
            m_sampleTable->selectionModel()->selectedRows();
    if (!selected.isEmpty()) {
        int row = selected.first().row();
        const int currentRow = m_sampleTable->currentRow();
        for (const QModelIndex &index : selected) {
            if (index.row() == currentRow) {
                row = currentRow;
                break;
            }
        }
        if (!isRowPositionReleased(row)) {
            if (QTableWidgetItem *item =
                    m_sampleTable->item(row, PositionColumn)) {
                selectedPositions = positionParts(item->text());
            }
        }
    }
    m_bloodArea->setSelectedPositions(selectedPositions);
}

void AddSampleWidget::handleBloodAreaMoveCommitted(
        const QStringList &positions)
{
    const int row = m_movingRow;
    m_movingRow = -1;

    if (!m_sampleTable || row < 0
            || row >= m_sampleTable->rowCount()) {
        syncBloodAreaSamples();
        return;
    }
    QTableWidgetItem *status = m_sampleTable->item(row, StatusColumn);
    if (!status || status->text() != tr("待定")) {
        syncBloodAreaSamples();
        return;
    }

    const QString position = positions.join(QLatin1Char('+'));
    if (!positionStringValid(position, row)) {
        MedicalMessageBox::warning(
                    this, tr("移动样本位置"),
                    tr("目标孔位无效或已被其他样本占用。"));
        syncBloodAreaSamples();
        return;
    }

    if (QTableWidgetItem *item =
            m_sampleTable->item(row, PositionColumn)) {
        const QString oldPosition = item->text();
        m_updatingTable = true;
        item->setText(position);
        item->setData(Qt::UserRole, position);
        m_updatingTable = false;
        qInfo() << "Move sample position:" << oldPosition
                << "->" << position;
        adjustSampleRowHeight(row);
    }
    syncBloodAreaSamples();
}

void AddSampleWidget::handleBloodAreaMoveCancelled()
{
    m_movingRow = -1;
    syncBloodAreaSamples();
}

void AddSampleWidget::addSample()
{
    // 全血模式不使用手动添加，等待主板测高上升沿触发。
    if (!sampleQualityIsPlasma()) {
        return;
    }
    addSampleInternal(false);
}

void AddSampleWidget::handleHeightDetectionTrigger()
{
    if (!m_sampleTable || sampleQualityIsPlasma()) {
        return;
    }
    addSampleInternal(true);
}

void AddSampleWidget::addSampleInternal(bool fromHeightTrigger)
{
    if (!m_sampleTable) {
        return;
    }

    // 开启加样扫码时，手动添加必须先把已有样本的条码扫/输完。
    // 全血测高触发不受此限制：先加待定行，条码可稍后补扫。
    if (!fromHeightTrigger && addSampleScanBarcodeEnabled()) {
        for (int row = 0; row < m_sampleTable->rowCount(); ++row) {
            QTableWidgetItem *barcode = m_sampleTable->item(row,
                                                            BarcodeColumn);
            if (barcode && !barcode->text().trimmed().isEmpty()) {
                continue;
            }

            QString number = tr("当前样本");
            if (QTableWidgetItem *sampleNo =
                    m_sampleTable->item(row, SampleNoColumn)) {
                number = sampleNo->text();
            }
            MedicalMessageBox::information(
                        this, tr("请先扫描条码"),
                        tr("加样扫码已开启。\n请先为样本 %1 扫描或输入"
                           "条码，再添加下一个样本。").arg(number));
            focusBarcodeCell(row);
            return;
        }

        if (!m_quickBarcodeEdit
                || m_quickBarcodeEdit->text().trimmed().isEmpty()) {
            MedicalMessageBox::information(
                        this, tr("请扫描条码"),
                        tr("加样扫码已开启，请先在条码栏扫描或输入条码，"
                           "再点「添加」。"));
            if (m_quickBarcodeEdit) {
                m_quickBarcodeEdit->setFocus();
                m_quickBarcodeEdit->selectAll();
            }
            return;
        }
    }

    const QString position = nextFreePosition();
    if (position.isEmpty()) {
        MedicalMessageBox::information(
                    this, tr("无法添加样本"),
                    testModeIsPppPrp()
                    ? tr("没有连续的两个空闲血样孔；PPP+PRP 模式最多 %1 "
                         "个样本。")
                      .arg(machineBloodSampleWellCount(m_model) / 2)
                    : tr("血样区已满：当前机型最多 %1 个样本。")
                      .arg(machineBloodSampleWellCount(m_model)));
        return;
    }

    QString sampleNumber = m_quickSampleNoEdit
            ? normalizedSampleNumber(m_quickSampleNoEdit->text())
            : QString();
    if (sampleNumber.isEmpty()) {
        QString error;
        sampleNumber = generateNextSampleNumber(&error);
        if (sampleNumber.isEmpty()) {
            MedicalMessageBox::warning(
                        this, tr("无法添加样本"),
                        error.isEmpty()
                        ? tr("无法生成当天样本号，请检查测试数据库。")
                        : error);
            return;
        }
        if (m_quickSampleNoEdit) {
            m_quickSampleNoEdit->setText(sampleNumber);
        }
    }

    QString reason;
    if (!validateSampleNumber(sampleNumber, -1, &reason)) {
        MedicalMessageBox::warning(this, tr("样本号不可用"), reason);
        if (m_quickSampleNoEdit) {
            m_quickSampleNoEdit->setFocus();
            m_quickSampleNoEdit->selectAll();
        }
        return;
    }

    const QString barcode = m_quickBarcodeEdit
            ? m_quickBarcodeEdit->text().trimmed() : QString();

    appendSampleRow(sampleNumber, barcode, QString(), QString(), position);
    qInfo() << (fromHeightTrigger ? "Height trigger add sample:"
                                  : "Add sample:")
            << sampleNumber << "position" << position;
    if (fromHeightTrigger && addSampleScanBarcodeEnabled()
            && barcode.isEmpty()) {
        qWarning() << "Height trigger added sample without barcode:"
                   << sampleNumber;
    }

    const int row = m_sampleTable->rowCount() - 1;
    if (!fromHeightTrigger) {
        m_sampleTable->scrollToBottom();
        m_sampleTable->selectRow(row);
        m_sampleTable->setCurrentCell(row, BarcodeColumn);
    }

    if (m_quickBarcodeEdit) {
        m_quickBarcodeEdit->clear();
    }
    m_quickSampleNoEdited = false;
    updateQuickSampleNumber();
    if (!fromHeightTrigger && m_quickBarcodeEdit) {
        m_quickBarcodeEdit->setFocus();
        m_quickBarcodeEdit->selectAll();
    }
    updateSampleCount();
    syncBloodAreaSamples();
}

void AddSampleWidget::confirmSamples()
{
    if (!m_sampleTable) {
        return;
    }
    if (m_sampleTable->rowCount() == 0) {
        MedicalMessageBox::information(this, tr("确认加样"),
                                 tr("加样列表为空。"));
        return;
    }

    const bool needBarcode = addSampleScanBarcodeEnabled();
    QStringList problems;
    int firstProblemRow = -1;
    QList<int> draftRows;

    for (int row = 0; row < m_sampleTable->rowCount(); ++row) {
        QTableWidgetItem *status = m_sampleTable->item(row, StatusColumn);
        if (!status || status->text() != tr("待定")) {
            continue;
        }

        QTableWidgetItem *sampleNo = m_sampleTable->item(row,
                                                         SampleNoColumn);
        QTableWidgetItem *barcode = m_sampleTable->item(row,
                                                        BarcodeColumn);
        QTableWidgetItem *testItem = m_sampleTable->item(row,
                                                         TestItemColumn);
        QTableWidgetItem *position = m_sampleTable->item(row,
                                                         PositionColumn);
        QString reason;
        if (!sampleNo
                || !validateSampleNumber(sampleNo->text(), row, &reason)) {
            problems << tr("第 %1 行：%2")
                        .arg(row + 1)
                        .arg(reason.isEmpty()
                             ? tr("样本号无效") : reason);
            if (firstProblemRow < 0) {
                firstProblemRow = row;
            }
            continue;
        }
        if (needBarcode
                && (!barcode || barcode->text().trimmed().isEmpty())) {
            problems << tr("第 %1 行：条码不能为空").arg(row + 1);
            if (firstProblemRow < 0) {
                firstProblemRow = row;
            }
            continue;
        }
        if (!testItem || testItem->text().trimmed().isEmpty()) {
            problems << tr("第 %1 行：测试项目不能为空").arg(row + 1);
            if (firstProblemRow < 0) {
                firstProblemRow = row;
            }
            continue;
        }
        if (!position
                || !positionStringValid(position->text(), row)) {
            problems << tr("第 %1 行：样本位置无效").arg(row + 1);
            if (firstProblemRow < 0) {
                firstProblemRow = row;
            }
            continue;
        }
        if (m_testQueue.contains(sampleNo->text())) {
            problems << tr("第 %1 行：待测队列中已存在该样本号")
                        .arg(row + 1);
            if (firstProblemRow < 0) {
                firstProblemRow = row;
            }
            continue;
        }
        draftRows << row;
    }

    if (!problems.isEmpty()) {
        MedicalMessageBox::warning(this, tr("确认加样"),
                             problems.join(QLatin1Char('\n')));
        if (firstProblemRow >= 0) {
            m_sampleTable->selectRow(firstProblemRow);
            m_sampleTable->setCurrentCell(firstProblemRow, StatusColumn);
        }
        return;
    }

    if (draftRows.isEmpty()) {
        MedicalMessageBox::information(this, tr("确认加样"),
                                 tr("当前没有待定样本。"));
        return;
    }

    // 先全部入队，再改状态；入队失败则不改任何行。
    for (int row : draftRows) {
        QueuedSample sample;
        if (!queuedSampleFromRow(row, &sample)) {
            MedicalMessageBox::warning(this, tr("确认加样"),
                                 tr("第 %1 行样本数据不完整。")
                                 .arg(row + 1));
            return;
        }
        QString error;
        if (!m_testQueue.enqueue(sample, &error)) {
            MedicalMessageBox::warning(this, tr("确认加样"), error);
            return;
        }
    }

    for (int row : draftRows) {
        if (QTableWidgetItem *status =
                m_sampleTable->item(row, StatusColumn)) {
            m_updatingTable = true;
            status->setText(tr("待测"));
            m_updatingTable = false;
        }
        updateRowEditability(row);
        // 通知结果信息表插入一条“待测”占位记录
        if (QTableWidgetItem *sampleNo =
                m_sampleTable->item(row, SampleNoColumn)) {
            emit sampleQueued(sampleNo->text().trimmed(),
                              sampleQualityIsPlasma()
                              ? tr("血浆") : tr("全血"));
        }
    }

    qInfo() << "Confirm samples:" << draftRows.size()
            << "queue size:" << m_testQueue.size();
    MedicalMessageBox::information(
                this, tr("确认加样"),
                tr("已确认 %1 个样本进入待测队列（当前队列 %2 个）。")
                .arg(draftRows.size())
                .arg(m_testQueue.size()));
}

void AddSampleWidget::removeSelectedSamples()
{
    if (!m_sampleTable) {
        return;
    }
    const QModelIndexList selected =
            m_sampleTable->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        MedicalMessageBox::information(this, tr("删除样本"),
                                 tr("请先选择要删除的样本行。"));
        return;
    }

    QSet<int> rows;
    for (const QModelIndex &index : selected) {
        rows.insert(index.row());
    }
    for (int row : rows) {
        QTableWidgetItem *sampleNo = m_sampleTable->item(row,
                                                         SampleNoColumn);
        if (sampleNo
                && m_testingSamples.contains(sampleNo->text().trimmed())) {
            MedicalMessageBox::warning(
                        this, tr("删除样本"),
                        tr("选中的样本中有正在测试的样本，不能删除。"));
            m_sampleTable->selectRow(row);
            return;
        }
    }

    if (MedicalMessageBox::question(
                this, tr("删除样本"),
                tr("确定删除选中的 %1 个样本？").arg(selected.size()),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    for (int row : rows) {
        QTableWidgetItem *status = m_sampleTable->item(row, StatusColumn);
        if (status && status->text() == tr("待测")) {
            if (QTableWidgetItem *sampleNo =
                    m_sampleTable->item(row, SampleNoColumn)) {
                m_testQueue.removeBySampleNumber(sampleNo->text());
            }
        }
    }

    m_updatingTable = true;
    for (int row = m_sampleTable->rowCount() - 1; row >= 0; --row) {
        if (rows.contains(row)) {
            m_sampleTable->removeRow(row);
        }
    }
    m_updatingTable = false;
    qInfo() << "Removed sample rows:" << rows.size();
    updateSampleCount();
    updateQuickSampleNumber();
    syncBloodAreaSamples();
}

void AddSampleWidget::modifySampleNumber()
{
    if (!m_sampleTable) {
        return;
    }

    int row = m_sampleTable->currentRow();
    if (row < 0) {
        const QModelIndexList selected =
                m_sampleTable->selectionModel()->selectedRows();
        if (!selected.isEmpty()) {
            row = selected.first().row();
        }
    }
    if (row < 0 || row >= m_sampleTable->rowCount()) {
        MedicalMessageBox::information(this, tr("修改样本号"),
                                 tr("请先选择一行样本。"));
        return;
    }

    QTableWidgetItem *status = m_sampleTable->item(row, StatusColumn);
    if (!status || status->text() != tr("待定")) {
        MedicalMessageBox::information(
                    this, tr("修改样本号"),
                    tr("只有待定样本可以修改样本号；"
                       "已确认或测试中的样本不能修改。"));
        return;
    }

    QTableWidgetItem *item = m_sampleTable->item(row, SampleNoColumn);
    if (!item) {
        return;
    }

    QString value = item->text();
    while (true) {
        bool ok = false;
        const QString entered = QInputDialog::getText(
                    this, tr("修改样本号"),
                    tr("样本号（13 位：T + 8 位日期 + 4 位序号）："),
                    QLineEdit::Normal, value, &ok);
        if (!ok) {
            return;
        }

        value = normalizedSampleNumber(entered);
        if (value == item->text()) {
            return;
        }

        QString reason;
        if (validateSampleNumber(value, row, &reason)) {
            const QString oldNumber = item->text();
            m_updatingTable = true;
            item->setText(value);
            item->setData(Qt::UserRole, value);
            m_updatingTable = false;
            qInfo() << "Sample number changed:" << oldNumber << "->"
                    << value;
            adjustSampleRowHeight(row);
            updateQuickSampleNumber();
            m_sampleTable->setCurrentCell(row, SampleNoColumn);
            return;
        }

        MedicalMessageBox::warning(this, tr("样本号不可用"), reason);
    }
}

void AddSampleWidget::handleTableItemChanged(QTableWidgetItem *item)
{
    if (!item || m_updatingTable) {
        return;
    }

    const int column = item->column();
    if (column == BarcodeColumn || column == PositionColumn) {
        QTableWidgetItem *status = m_sampleTable->item(item->row(),
                                                       StatusColumn);
        if (!status || status->text() != tr("待定")) {
            restoreTableItem(item);
            return;
        }
    }
    if (column == PositionColumn) {
        const QString text = item->text().trimmed();
        if (!positionStringValid(text, item->row())) {
            MedicalMessageBox::warning(
                        this, tr("样本位置无效"),
                        testModeIsPppPrp()
                        ? tr("PPP+PRP 模式每个样本占用两个孔，"
                             "孔号不能重复。\n格式：10+11。")
                        : tr("该孔号不存在或已被其他样本占用。\n"
                             "格式：孔号，例如 10。"));
            restoreTableItem(item);
            return;
        }
        const QString canonical = positionParts(text).join(QLatin1Char('+'));
        if (item->text() != canonical) {
            m_updatingTable = true;
            item->setText(canonical);
            m_updatingTable = false;
        }
    }

    m_updatingTable = true;
    item->setData(Qt::UserRole, item->text());
    m_updatingTable = false;
    adjustSampleRowHeight(item->row());

    if (column == PositionColumn) {
        syncBloodAreaSamples();
    }
}

QStringList AddSampleWidget::allPositions() const
{
    QStringList result;
    const int holes = machineBloodSampleWellCount(m_model);
    for (int hole = 0; hole < holes; ++hole) {
        result << QString::number(hole);
    }
    return result;
}

QStringList AddSampleWidget::positionsForRow(int row) const
{
    QString current;
    if (m_sampleTable && row >= 0
            && row < m_sampleTable->rowCount()) {
        if (QTableWidgetItem *item =
                m_sampleTable->item(row, PositionColumn)) {
            current = item->text().trimmed();
        }
    }

    const QStringList all = allPositions();
    QStringList result;
    for (const QString &hole : all) {
        if (hole == current || !holeInTable(hole, row)) {
            result << hole;
        }
    }
    if (!current.isEmpty() && !all.contains(current)
            && !result.contains(current)) {
        result.prepend(current);
    }
    return result;
}

QString AddSampleWidget::nextFreePosition() const
{
    const QStringList positions = allPositions();
    if (testModeIsPppPrp()) {
        for (int i = 0; i + 1 < positions.size(); ++i) {
            if (!holeInTable(positions.at(i))
                    && !holeInTable(positions.at(i + 1))) {
                return positions.at(i) + QLatin1Char('+')
                        + positions.at(i + 1);
            }
        }
        return QString();
    }

    for (const QString &hole : positions) {
        if (!holeInTable(hole)) {
            return hole;
        }
    }
    return QString();
}

QString AddSampleWidget::generateNextSampleNumber(QString *error) const
{
    if (error) {
        error->clear();
    }

    const QDate today = QDate::currentDate();
    const QString prefix = QStringLiteral("T%1")
            .arg(today.toString(QStringLiteral("yyyyMMdd")));

    QString databaseNext;
    if (m_dataStore) {
        if (!m_dataStore->isOpen()) {
            if (error) {
                *error = tr("测试数据库未打开，无法生成当天样本号。");
            }
            return QString();
        }
        QString databaseError;
        databaseNext = m_dataStore->nextSampleNumber(today, &databaseError);
        if (databaseNext.isEmpty()) {
            if (error) {
                *error = databaseError.isEmpty()
                        ? tr("无法读取测试数据库中的当天样本号。")
                        : databaseError;
            }
            return QString();
        }
    }

    int sequence = 1;
    if (!databaseNext.isEmpty()) {
        bool ok = false;
        const int parsed = databaseNext.mid(9).toInt(&ok);
        if (ok && parsed > 0) {
            sequence = parsed;
        }
    }

    for (int number = sequence; number <= 9999; ++number) {
        const QString candidate = prefix + QStringLiteral("%1")
                .arg(number, 4, 10, QLatin1Char('0'));
        if (!sampleNumberInTable(candidate)) {
            // databaseNext 已是数据库当天最大号 + 1，比它大的号不会重复。
            return candidate;
        }
    }

    if (error) {
        *error = tr("当天样本号已用完（0001~9999）。");
    }
    return QString();
}

bool AddSampleWidget::sampleNumberInTable(const QString &number,
                                          int exceptRow) const
{
    if (!m_sampleTable) {
        return false;
    }
    const QString trimmed = number.trimmed();
    for (int row = 0; row < m_sampleTable->rowCount(); ++row) {
        if (row == exceptRow) {
            continue;
        }
        if (QTableWidgetItem *item =
                m_sampleTable->item(row, SampleNoColumn)) {
            if (item->text().trimmed() == trimmed) {
                return true;
            }
        }
    }
    return false;
}

QStringList AddSampleWidget::positionParts(const QString &position) const
{
    QStringList result;
    const QStringList parts = position.split(QLatin1Char('+'),
                                             QString::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) {
            result << trimmed;
        }
    }
    return result;
}

bool AddSampleWidget::holeInTable(const QString &hole,
                                  int exceptRow) const
{
    if (!m_sampleTable) {
        return false;
    }
    const QString trimmed = hole.trimmed();
    for (int row = 0; row < m_sampleTable->rowCount(); ++row) {
        if (row == exceptRow) {
            continue;
        }
        if (isRowPositionReleased(row)) {
            continue;
        }
        if (QTableWidgetItem *item =
                m_sampleTable->item(row, PositionColumn)) {
            if (positionParts(item->text()).contains(trimmed)) {
                return true;
            }
        }
    }
    return false;
}

bool AddSampleWidget::positionStringValid(const QString &position,
                                          int exceptRow) const
{
    const QStringList parts = positionParts(position);
    if (testModeIsPppPrp()) {
        if (parts.size() != 2) {
            return false;
        }
    } else if (parts.size() != 1) {
        return false;
    }

    const QStringList all = allPositions();
    QSet<QString> seen;
    for (const QString &part : parts) {
        if (!all.contains(part) || seen.contains(part)
                || holeInTable(part, exceptRow)) {
            return false;
        }
        seen.insert(part);
    }
    return true;
}

bool AddSampleWidget::validateSampleNumber(const QString &number,
                                           int exceptRow,
                                           QString *reason) const
{
    if (reason) {
        reason->clear();
    }
    const QString trimmed = number.trimmed();
    if (trimmed.isEmpty()) {
        if (reason) {
            *reason = tr("样本号不能为空。");
        }
        return false;
    }
    const QString canonical = normalizedSampleNumber(trimmed);
    const QRegularExpression sampleNoPattern(
                QStringLiteral("^T\\d{12}$"));
    if (!sampleNoPattern.match(canonical).hasMatch()) {
        if (reason) {
            *reason = tr("样本号必须为 13 位：T + 8 位日期 + 4 位序号"
                         "（例如 T202611220001）。");
        }
        return false;
    }
    if (sampleNumberInTable(canonical, exceptRow)) {
        if (reason) {
            *reason = tr("表格中已存在该样本号，请更换。");
        }
        return false;
    }
    if (m_dataStore) {
        if (!m_dataStore->isOpen()) {
            if (reason) {
                *reason = tr("测试数据库未打开，无法校验样本号是否重复。");
            }
            return false;
        }
        if (m_dataStore->sampleNumberExists(canonical)) {
            if (reason) {
                *reason = tr("样本数据库中已存在该样本号，请更换。");
            }
            return false;
        }
    }
    return true;
}

void AddSampleWidget::appendSampleRow(const QString &sampleNumber,
                                      const QString &barcode,
                                      const QString &testItem,
                                      const QString &height,
                                      const QString &position)
{
    if (!m_sampleTable) {
        return;
    }

    m_updatingTable = true;
    const int row = m_sampleTable->rowCount();
    m_sampleTable->insertRow(row);

    const auto makeItem = [](const QString &text, bool editable) {
        auto *item = new QTableWidgetItem(text);
        item->setTextAlignment(Qt::AlignCenter);
        Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
        if (editable) {
            flags |= Qt::ItemIsEditable;
        }
        item->setFlags(flags);
        item->setData(Qt::UserRole, text);
        return item;
    };

    m_sampleTable->setItem(row, StatusColumn,
                           makeItem(tr("待定"), false));
    m_sampleTable->setItem(row, SampleNoColumn,
                           makeItem(sampleNumber, false));
    m_sampleTable->setItem(row, BarcodeColumn, makeItem(barcode, true));
    m_sampleTable->setItem(row, TestItemColumn,
                           makeItem(testItem, false));
    QString rowHeight = height;
    const bool plasmaAutoHeight = rowHeight.isEmpty()
            && sampleQualityIsPlasma();
    if (plasmaAutoHeight) {
        rowHeight = QString::number(plasmaPrpAspirateHeightFromSettings());
    }
    m_sampleTable->setItem(row, HeightColumn,
                           makeItem(rowHeight, false));
    if (plasmaAutoHeight) {
        if (QTableWidgetItem *item =
                m_sampleTable->item(row, HeightColumn)) {
            item->setData(kHeightSourceRole,
                          QString::fromLatin1(kPlasmaAutoHeightSource));
        }
    }
    m_sampleTable->setItem(row, PositionColumn,
                           makeItem(position, false));

    if (QTableWidgetItem *item = m_sampleTable->item(row, StatusColumn)) {
        item->setToolTip(
                    tr("待定：可编辑/移动孔位；待测：已确认进入队列；"
                       "加样完成：已进入测试流程，测试状态请在结果信息表查看"));
    }
    if (QTableWidgetItem *item = m_sampleTable->item(row, SampleNoColumn)) {
        item->setToolTip(tr("双击可修改样本号"));
    }
    if (QTableWidgetItem *item = m_sampleTable->item(row, TestItemColumn)) {
        item->setToolTip(tr("双击选择测试项目，可多选自由组合"));
    }
    if (QTableWidgetItem *item = m_sampleTable->item(row, HeightColumn)) {
        item->setToolTip(
                    sampleQualityIsPlasma()
                    ? tr("血浆模式：吸样高度取基础设置中的固定值，不可编辑")
                    : tr("由识别/测高结果自动填入，不可编辑"));
    }
    if (QTableWidgetItem *item = m_sampleTable->item(row, PositionColumn)) {
        item->setToolTip(tr("血样孔号，例如 10；PPP+PRP 模式为 10+11"));
    }

    m_updatingTable = false;
    adjustSampleRowHeight(row);
    updateSampleCount();
}

void AddSampleWidget::restoreTableItem(QTableWidgetItem *item)
{
    if (!item) {
        return;
    }
    m_updatingTable = true;
    item->setText(item->data(Qt::UserRole).toString());
    m_updatingTable = false;
    adjustSampleRowHeight(item->row());
}
