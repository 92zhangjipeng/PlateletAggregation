#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "resultstablewidget.h"

#include "informationentry.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QFontMetrics>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QResizeEvent>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>

namespace {

// 列顺序
enum Column {
    StatusColumn = 0,      // 测试状态
    SampleNoColumn,        // 样本号
    NameColumn,            // 姓名（可编辑）
    TypeColumn,            // 样本类型
    ReagentCountColumn,    // 试剂数
    TimeColumn,            // 检测时间
    DepartmentColumn,      // 科别
    GenderColumn,          // 性别
    WardColumn,            // 病区
    SendingDoctorColumn,   // 送检医生
    TestingDoctorColumn,   // 检验医生
    ReviewingDoctorColumn, // 审核医生
    DiagnosisColumn,       // 临床诊断
    RemarkColumn,          // 备注
    ColumnCount
};

// 列宽比例
constexpr double kColumnRatios[ColumnCount] = {
    0.07,  // 状态
    0.11,  // 样本号
    0.07,  // 姓名
    0.05,  // 类型
    0.045, // 试剂数
    0.075, // 时间
    0.07,  // 科别
    0.045, // 性别
    0.07,  // 病区
    0.075, // 送检医生
    0.075, // 检验医生
    0.075, // 审核医生
    0.085, // 临床诊断
    0.075, // 备注
};

// 信息录入列 → (列号, 数据库列名, 信息录入类别键)
struct EntryColumnInfo {
    int column;
    const char *dbKey;
    const char *categoryKey;
};

const EntryColumnInfo kEntryColumns[] = {
    { DepartmentColumn,      "category",         "department" },
    { GenderColumn,          "gender_entry",     "gender" },
    { WardColumn,            "ward",             "ward" },
    { SendingDoctorColumn,   "sending_doctor",   "sendingDoctor" },
    { TestingDoctorColumn,   "testing_doctor",   "testingDoctor" },
    { ReviewingDoctorColumn, "reviewing_doctor", "reviewingDoctor" },
    { DiagnosisColumn,       "diagnosis",        "diagnosis" },
    { RemarkColumn,          "remark",           "remark" },
};

int entryColumnCount()
{
    return int(sizeof(kEntryColumns) / sizeof(kEntryColumns[0]));
}

const EntryColumnInfo *entryColumnAt(int column)
{
    for (int i = 0; i < entryColumnCount(); ++i) {
        if (kEntryColumns[i].column == column) {
            return &kEntryColumns[i];
        }
    }
    return nullptr;
}

bool isEditableColumn(int column)
{
    return column == NameColumn || entryColumnAt(column) != nullptr;
}

QString entryValue(const SampleSummary &row, int column)
{
    switch (column) {
    case DepartmentColumn:      return row.department;
    case GenderColumn:          return row.gender;
    case WardColumn:            return row.ward;
    case SendingDoctorColumn:   return row.sendingDoctor;
    case TestingDoctorColumn:   return row.testingDoctor;
    case ReviewingDoctorColumn: return row.reviewingDoctor;
    case DiagnosisColumn:       return row.diagnosis;
    case RemarkColumn:          return row.remark;
    default: return QString();
    }
}

void setEntryValue(SampleSummary *row, int column, const QString &value)
{
    if (!row) {
        return;
    }
    switch (column) {
    case DepartmentColumn:      row->department = value; break;
    case GenderColumn:          row->gender = value; break;
    case WardColumn:            row->ward = value; break;
    case SendingDoctorColumn:   row->sendingDoctor = value; break;
    case TestingDoctorColumn:   row->testingDoctor = value; break;
    case ReviewingDoctorColumn: row->reviewingDoctor = value; break;
    case DiagnosisColumn:       row->diagnosis = value; break;
    case RemarkColumn:          row->remark = value; break;
    default: break;
    }
}

QString statusColorName(const QString &status)
{
    if (status == QStringLiteral("测试中")) return QStringLiteral("#2F93E0");
    if (status == QStringLiteral("测试结束")) return QStringLiteral("#22A877");
    if (status == QStringLiteral("异常结果")) return QStringLiteral("#DF5A50");
    return QStringLiteral("#B7791F"); // 待测等
}

QIcon statusDotIcon(const QString &status)
{
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(statusColorName(status)));
    painter.drawEllipse(QPointF(6.0, 6.0), 3.5, 3.5);
    return QIcon(pixmap);
}

QString columnText(const SampleSummary &row, int column)
{
    switch (column) {
    case StatusColumn: return row.testStatus.isEmpty()
                ? QStringLiteral("待测") : row.testStatus;
    case SampleNoColumn: return row.sampleNumber.isEmpty()
                ? QStringLiteral("--") : row.sampleNumber;
    // 姓名可编辑：空值显示为空，双击后可直接输入，不用先删掉“--”。
    case NameColumn: return row.patientName;
    case TypeColumn: return row.sampleType.isEmpty()
                ? QStringLiteral("--") : row.sampleType;
    case ReagentCountColumn: return QString::number(row.testCount);
    // 带上日期：同一样本号可能在不同天重复使用，仅显示时分秒无法区分。
    case TimeColumn: return row.testTime.isValid()
                ? row.testTime.toString(QStringLiteral("MM-dd HH:mm"))
                : QStringLiteral("--");
    default: return entryValue(row, column);
    }
}

/**
 * 信息录入列代理：只有进入编辑（双击/按键）时才弹出下拉，
 * 单击仅选中整行；显示样式与下拉编辑器统一为医疗蓝白配色。
 */
class EntryComboDelegate : public QStyledItemDelegate
{
public:
    explicit EntryComboDelegate(const QString &categoryKey,
                                QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
        , m_categoryKey(categoryKey)
    {
    }

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        const QStringList options = informationEntryValues(m_categoryKey);
        if (options.isEmpty()) {
            // 信息录入里还没配置子项时退回文本编辑，避免完全无法输入。
            return QStyledItemDelegate::createEditor(parent, option, index);
        }

        auto *combo = new QComboBox(parent);
        combo->setObjectName(QStringLiteral("resultEntryCombo"));
        combo->setEditable(false);
        combo->setMaxVisibleItems(12);
        combo->addItems(options);
        const QString current =
                index.data(Qt::EditRole).toString().trimmed();
        if (!current.isEmpty() && combo->findText(current) < 0) {
            combo->addItem(current);
        }
        combo->setCurrentText(current);
        // 选中下拉项后立即提交并关闭编辑器，和普通表格代理行为一致。
        QObject::connect(
                    combo,
                    static_cast<void (QComboBox::*)(int)>(&QComboBox::activated),
                    combo, [this, combo](int) {
            EntryComboDelegate *self =
                    const_cast<EntryComboDelegate *>(this);
            emit self->commitData(combo);
            emit self->closeEditor(combo, QAbstractItemDelegate::NoHint);
        });
        // 进入编辑状态后才弹出下拉，单击单元格不会触发。
        QTimer::singleShot(0, combo, [combo]() {
            if (combo->isVisible()) {
                combo->setFocus(Qt::PopupFocusReason);
                combo->showPopup();
            }
        });
        return combo;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if (auto *combo = qobject_cast<QComboBox *>(editor)) {
            const QString current =
                    index.data(Qt::EditRole).toString().trimmed();
            int idx = combo->findText(current);
            if (idx < 0 && !current.isEmpty()) {
                combo->addItem(current);
                idx = combo->count() - 1;
            }
            combo->setCurrentIndex(idx);
            return;
        }
        QStyledItemDelegate::setEditorData(editor, index);
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        if (auto *combo = qobject_cast<QComboBox *>(editor)) {
            model->setData(index, combo->currentText(), Qt::EditRole);
            return;
        }
        QStyledItemDelegate::setModelData(editor, model, index);
    }

    void updateEditorGeometry(QWidget *editor,
                              const QStyleOptionViewItem &option,
                              const QModelIndex &index) const override
    {
        Q_UNUSED(index);
        editor->setGeometry(option.rect.adjusted(1, 1, -1, -1));
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        opt.text.clear();
        QStyle *style = opt.widget ? opt.widget->style()
                                   : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const bool selected = option.state & QStyle::State_Selected;
        const QRectF box = QRectF(option.rect).adjusted(2.0, 3.0, -6.0, -3.0);
        painter->setPen(QPen(selected ? QColor("#5FA8D8")
                                      : QColor("#A9C8DF"), 1.0));
        painter->setBrush(selected ? QColor("#E8F1FA") : QColor("#FFFFFF"));
        painter->drawRoundedRect(box, 4.0, 4.0);

        QFont font(QStringLiteral("Microsoft YaHei"));
        font.setPixelSize(12);
        painter->setFont(font);
        painter->setPen(QColor("#30475C"));
        const QFontMetrics metrics(font);
        const QString text = index.data(Qt::DisplayRole).toString();
        const QString elided = metrics.elidedText(
                    text, Qt::ElideRight,
                    qMax(10, qRound(box.width()) - 20));
        painter->drawText(box.adjusted(5.0, 0.0, -16.0, 0.0),
                          Qt::AlignLeft | Qt::AlignVCenter, elided);

        // 下拉箭头
        const qreal cx = box.right() - 9.0;
        const qreal cy = box.center().y();
        QPolygonF arrow;
        arrow << QPointF(cx - 3.5, cy - 1.8)
              << QPointF(cx + 3.5, cy - 1.8)
              << QPointF(cx, cy + 2.2);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor("#5D7386"));
        painter->drawPolygon(arrow);
        painter->restore();
    }

private:
    QString m_categoryKey;
};

} // namespace

ResultsTableWidget::ResultsTableWidget(QWidget *parent)
    : QTableWidget(parent)
{
    setObjectName(QStringLiteral("resultsTableWidget"));
    setColumnCount(ColumnCount);
    setHorizontalHeaderLabels(QStringList()
        << tr("状态") << tr("样本号") << tr("姓名") << tr("类型")
        << tr("试剂数") << tr("检测时间") << tr("科别") << tr("性别")
        << tr("病区") << tr("送检医生") << tr("检验医生")
        << tr("审核医生") << tr("临床诊断") << tr("备注"));

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // 单击只选中；双击或按键进入编辑后，下拉代理才会弹出列表。
    setEditTriggers(QAbstractItemView::DoubleClicked
                    | QAbstractItemView::EditKeyPressed
                    | QAbstractItemView::AnyKeyPressed);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setAlternatingRowColors(true);
    setShowGrid(true);
    setWordWrap(false);
    setTextElideMode(Qt::ElideRight);
    setSortingEnabled(false);
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFocusPolicy(Qt::StrongFocus);
    setIconSize(QSize(12, 12));

    verticalHeader()->setVisible(false);
    verticalHeader()->setDefaultSectionSize(34);
    verticalHeader()->setMinimumSectionSize(34);
    horizontalHeader()->setFixedHeight(38);
    horizontalHeader()->setHighlightSections(false);
    horizontalHeader()->setSectionsClickable(false);
    horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    horizontalHeader()->setStretchLastSection(false);
    for (int column = 0; column < ColumnCount; ++column) {
        horizontalHeader()->setSectionResizeMode(column, QHeaderView::Fixed);
    }

    for (int i = 0; i < entryColumnCount(); ++i) {
        const EntryColumnInfo &info = kEntryColumns[i];
        setItemDelegateForColumn(
                    info.column,
                    new EntryComboDelegate(
                        QString::fromLatin1(info.categoryKey), this));
    }

    m_emptyLabel = new QLabel(tr("暂无测试结果"), viewport());
    m_emptyLabel->setObjectName(QStringLiteral("resultsEmptyLabel"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_emptyLabel->setStyleSheet(QStringLiteral(
        "QLabel#resultsEmptyLabel {"
        "  color: #8A9CAB;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "  background: transparent;"
        "}"));
    m_emptyLabel->hide();

    setStyleSheet(QStringLiteral(
        "QTableWidget#resultsTableWidget {"
        "  background: #FFFFFF;"
        "  alternate-background-color: #F8FBFD;"
        "  border: 1px solid #DCE7F1;"
        "  border-radius: 10px;"
        "  gridline-color: #E6EEF5;"
        "  color: #30475C;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  selection-background-color: #E8F1FA;"
        "  selection-color: #1F4E79;"
        "}"
        "QTableWidget#resultsTableWidget::item {"
        "  padding-left: 6px;"
        "  padding-right: 4px;"
        "}"
        "QTableWidget#resultsTableWidget QLineEdit {"
        "  background: #FFFFFF;"
        "  border: 1px solid #5FA8D8;"
        "  border-radius: 4px;"
        "  padding: 2px 6px;"
        "  color: #1F2D3D;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  selection-background-color: #B9DDF3;"
        "}"
        "QComboBox#resultEntryCombo {"
        "  background: #FFFFFF;"
        "  border: 1px solid #5FA8D8;"
        "  border-radius: 4px;"
        "  padding: 2px 6px;"
        "  color: #1F2D3D;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QComboBox#resultEntryCombo::drop-down {"
        "  border: none;"
        "  width: 18px;"
        "}"
        "QComboBox#resultEntryCombo QAbstractItemView {"
        "  background: #FFFFFF;"
        "  border: 1px solid #A9C8DF;"
        "  selection-background-color: #E8F1FA;"
        "  selection-color: #1F4E79;"
        "  outline: 0;"
        "  padding: 2px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QHeaderView::section {"
        "  background: #EAF3FA;"
        "  color: #1F4E79;"
        "  border: none;"
        "  border-bottom: 1px solid #DCE7F1;"
        "  padding: 0px 6px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QTableCornerButton::section {"
        "  background: #EAF3FA;"
        "  border: none;"
        "}"));

    connect(this, &QTableWidget::itemChanged,
            this, &ResultsTableWidget::handleItemChanged);
    connect(this, &QTableWidget::cellClicked, this,
            [this](int row, int column) {
        if (row >= 0 && row < m_rows.size()
                && !isEditableColumn(column)) {
            emit rowActivated(m_rows.at(row).id);
        }
    });

    updateTableHeight();
}

void ResultsTableWidget::setRows(const QVector<SampleSummary> &rows)
{
    m_rows = rows;

    m_populating = true;
    clearContents();
    setRowCount(m_rows.size());
    for (int row = 0; row < m_rows.size(); ++row) {
        const SampleSummary &summary = m_rows.at(row);
        for (int column = 0; column < ColumnCount; ++column) {
            auto *item = new QTableWidgetItem(columnText(summary, column));
            item->setData(Qt::UserRole, summary.id);
            Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
            if (isEditableColumn(column)) {
                flags |= Qt::ItemIsEditable;
            }
            item->setFlags(flags);
            if (column == StatusColumn) {
                item->setIcon(statusDotIcon(summary.testStatus));
                item->setToolTip(summary.testStatus.isEmpty()
                                 ? tr("待测") : summary.testStatus);
            } else if (column == TimeColumn && summary.testTime.isValid()) {
                item->setToolTip(summary.testTime.toString(
                                     QStringLiteral("yyyy-MM-dd HH:mm:ss")));
            }
            setItem(row, column, item);
        }
    }
    m_populating = false;

    updateTableHeight();
    applyColumnWidths();
    updateEmptyLabel();
}

QSize ResultsTableWidget::sizeHint() const
{
    return QSize(1100, qMax(height(), 72));
}

void ResultsTableWidget::resizeEvent(QResizeEvent *event)
{
    QTableWidget::resizeEvent(event);
    applyColumnWidths();
    updateEmptyLabel();
}

void ResultsTableWidget::handleItemChanged(QTableWidgetItem *item)
{
    if (m_populating || !item) {
        return;
    }
    const int row = item->row();
    const int column = item->column();
    if (row < 0 || row >= m_rows.size()) {
        return;
    }

    const QString text = item->text().trimmed();
    const qint64 sampleId = m_rows.at(row).id;

    if (column == NameColumn) {
        if (m_rows.at(row).patientName == text) {
            return;
        }
        m_rows[row].patientName = text;
        emit entryValueChanged(sampleId, QStringLiteral("patient_name"), text);
        return;
    }

    const EntryColumnInfo *info = entryColumnAt(column);
    if (!info) {
        return;
    }
    if (entryValue(m_rows.at(row), column) == text) {
        return;
    }
    setEntryValue(&m_rows[row], column, text);
    emit entryValueChanged(sampleId, QString::fromLatin1(info->dbKey), text);
}

void ResultsTableWidget::applyColumnWidths()
{
    if (!viewport()) {
        return;
    }
    const int totalWidth = viewport()->width();
    if (totalWidth <= 0) {
        return;
    }

    double totalRatio = 0.0;
    for (double ratio : kColumnRatios) {
        totalRatio += ratio;
    }

    int used = 0;
    for (int column = 0; column < ColumnCount; ++column) {
        int width = qRound(totalWidth * kColumnRatios[column] / totalRatio);
        if (column == ColumnCount - 1) {
            width = totalWidth - used;
        }
        width = qMax(40, width);
        setColumnWidth(column, width);
        used += width;
    }
}

void ResultsTableWidget::updateTableHeight()
{
    const int headerHeight = horizontalHeader()->height() > 0
            ? horizontalHeader()->height() : 38;
    const int rowHeight = verticalHeader()->defaultSectionSize();
    const int rows = qMax(1, m_rows.size());
    setFixedHeight(headerHeight + rows * rowHeight + frameWidth() * 2);
    updateGeometry();
}

void ResultsTableWidget::updateEmptyLabel()
{
    if (!m_emptyLabel) {
        return;
    }
    m_emptyLabel->setVisible(m_rows.isEmpty());
    if (m_emptyLabel->isVisible()) {
        m_emptyLabel->setGeometry(viewport()->rect());
        m_emptyLabel->raise();
    }
}
