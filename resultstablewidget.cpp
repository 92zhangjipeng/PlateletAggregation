#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "resultstablewidget.h"

#include "informationentry.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QHeaderView>
#include <QIcon>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QResizeEvent>
#include <QStyledItemDelegate>

#include <memory>

namespace {

// 列顺序
enum Column {
    StatusColumn = 0,      // 测试状态
    SampleNoColumn,        // 样本号
    NameColumn,            // 姓名（可编辑）
    AgeColumn,             // 年龄（可编辑）
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
    0.065, // 姓名
    0.04,  // 年龄
    0.05,  // 类型
    0.045, // 试剂数
    0.07,  // 时间
    0.065, // 科别
    0.04,  // 性别
    0.065, // 病区
    0.07,  // 送检医生
    0.07,  // 检验医生
    0.07,  // 审核医生
    0.08,  // 临床诊断
    0.07,  // 备注
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
    return column == NameColumn || column == AgeColumn
            || entryColumnAt(column) != nullptr;
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
    // 姓名/年龄可编辑：空值显示为空，双击后可直接输入。
    case NameColumn: return row.patientName;
    case AgeColumn: return row.age >= 0
                ? QString::number(row.age) : QString();
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
 * 下拉编辑器样式：浅蓝边框 + 深蓝下拉按钮 + 白色箭头，
 * 直接设置在编辑器上，避免继承表格样式导致箭头/边框对比度不足。
 */
QString entryComboStyleSheet()
{
    return QStringLiteral(
        "QComboBox {"
        "  background: #FFFFFF;"
        "  border: 1px solid #5FA8D8;"
        "  border-radius: 4px;"
        "  padding: 2px 6px;"
        "  color: #1F2D3D;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QComboBox::drop-down {"
        "  subcontrol-origin: padding;"
        "  subcontrol-position: center right;"
        "  width: 20px;"
        "  border: none;"
        "  border-left: 1px solid #A9C8DF;"
        "  background: #2C7BBF;"
        "  border-top-right-radius: 4px;"
        "  border-bottom-right-radius: 4px;"
        "}"
        "QComboBox::down-arrow {"
        "  image: url(:/img/arrow_down_light.png);"
        "  width: 14px;"
        "  height: 14px;"
        "}"
        "QComboBox::drop-down:hover {"
        "  background: #3A8DCE;"
        "}"
        "QComboBox QLineEdit {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "  margin: 0px;"
        "  color: #1F2D3D;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  selection-background-color: #B9DDF3;"
        "}"
        "QComboBox QAbstractItemView {"
        "  background: #FFFFFF;"
        "  border: 1px solid #A9C8DF;"
        "  selection-background-color: #E8F1FA;"
        "  selection-color: #1F4E79;"
        "  outline: 0;"
        "  padding: 2px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}");
}

/**
 * 信息录入列代理：单元格默认按普通文本绘制（空值就是空白），
 * 只有双击/按键进入编辑时才创建 QComboBox 编辑器。
 * 编辑器可下拉选择信息录入子项，也可直接输入自定义内容。
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
            // 信息录入里还没配置子项时退回文本编辑，仍可输入内容。
            return QStyledItemDelegate::createEditor(parent, option, index);
        }

        auto *combo = new QComboBox(parent);
        combo->setObjectName(QStringLiteral("resultEntryCombo"));
        combo->setStyleSheet(entryComboStyleSheet());
        combo->setEditable(true);   // 可下拉选择，也可直接输入
        combo->setInsertPolicy(QComboBox::NoInsert);
        combo->setMaxVisibleItems(12);
        combo->addItems(options);
        const QString current =
                index.data(Qt::EditRole).toString().trimmed();
        combo->setCurrentText(current);
        if (combo->lineEdit()) {
            combo->lineEdit()->setPlaceholderText(tr("可选择或输入"));
            // 进入编辑后直接输入即可替换原内容。
            combo->lineEdit()->selectAll();
        }

        auto committed = std::make_shared<bool>(false);
        auto commitAndClose = [this, combo, committed]() {
            if (*committed) {
                return;
            }
            *committed = true;
            EntryComboDelegate *self =
                    const_cast<EntryComboDelegate *>(this);
            emit self->commitData(combo);
            emit self->closeEditor(combo, QAbstractItemDelegate::NoHint);
        };
        // 下拉选择或输入回车后立即提交并关闭编辑器。
        QObject::connect(
                    combo,
                    static_cast<void (QComboBox::*)(int)>(&QComboBox::activated),
                    combo, [commitAndClose](int) { commitAndClose(); });
        if (combo->lineEdit()) {
            QObject::connect(combo->lineEdit(), &QLineEdit::returnPressed,
                             combo, commitAndClose);
        }
        return combo;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if (auto *combo = qobject_cast<QComboBox *>(editor)) {
            combo->setCurrentText(
                        index.data(Qt::EditRole).toString().trimmed());
            if (combo->lineEdit()) {
                combo->lineEdit()->selectAll();
            }
            return;
        }
        QStyledItemDelegate::setEditorData(editor, index);
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        if (auto *combo = qobject_cast<QComboBox *>(editor)) {
            model->setData(index, combo->currentText().trimmed(),
                           Qt::EditRole);
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

private:
    QString m_categoryKey;
};

/** 年龄列代理：只允许 0~150 的整数，清空表示未录入。 */
class AgeDelegate : public QStyledItemDelegate
{
public:
    explicit AgeDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        Q_UNUSED(option);
        Q_UNUSED(index);
        auto *edit = new QLineEdit(parent);
        edit->setValidator(new QIntValidator(0, 150, edit));
        edit->setPlaceholderText(tr("年龄"));
        return edit;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        QStyledItemDelegate::setEditorData(editor, index);
        if (auto *edit = qobject_cast<QLineEdit *>(editor)) {
            edit->selectAll();
        }
    }
};

} // namespace

ResultsTableWidget::ResultsTableWidget(QWidget *parent)
    : QTableWidget(parent)
{
    setObjectName(QStringLiteral("resultsTableWidget"));
    setColumnCount(ColumnCount);
    setHorizontalHeaderLabels(QStringList()
        << tr("状态") << tr("样本号") << tr("姓名") << tr("年龄")
        << tr("类型") << tr("试剂数") << tr("检测时间") << tr("科别")
        << tr("性别") << tr("病区") << tr("送检医生") << tr("检验医生")
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
    setItemDelegateForColumn(AgeColumn, new AgeDelegate(this));

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

    if (column == AgeColumn) {
        int age = -1;
        if (!text.isEmpty()) {
            bool ok = false;
            age = text.toInt(&ok);
            if (!ok || age < 0 || age > 150) {
                // 非法年龄还原原值，等待重新输入。
                m_populating = true;
                item->setText(m_rows.at(row).age >= 0
                              ? QString::number(m_rows.at(row).age)
                              : QString());
                m_populating = false;
                return;
            }
        }
        if (m_rows.at(row).age == age) {
            return;
        }
        m_rows[row].age = age;
        emit entryValueChanged(sampleId, QStringLiteral("age"),
                               age >= 0 ? QString::number(age) : QString());
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
