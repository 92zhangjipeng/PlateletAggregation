#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "querywidget.h"
#include "testdatastore.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QShowEvent>
#include <QTreeWidget>
#include <QVariant>
#include <QVBoxLayout>

QueryWidget::QueryWidget(TestDataStore *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    setObjectName(QStringLiteral("queryPage"));
    setStyleSheet(QStringLiteral(
        "QWidget#queryPage { background: #F4F8FC; }"
        "QLabel#pageTitle {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 21px;"
        "  font-weight: 700;"
        "}"
        "QLabel#pageSubtitle {"
        "  color: #7A8FA3;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QLabel#queryCount {"
        "  color: #5D7386;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "}"
        "QLineEdit#queryEdit {"
        "  background: #FFFFFF;"
        "  border: 1px solid #C9DCEB;"
        "  border-radius: 6px;"
        "  padding: 7px 12px;"
        "  color: #1F2D3D;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "  selection-background-color: #B9DDF3;"
        "}"
        "QLineEdit#queryEdit:focus { border: 1px solid #5FA8D8; }"
        "QRadioButton {"
        "  color: #1F4E79; font-family: 'Microsoft YaHei'; font-size: 12px;"
        "  spacing: 6px;"
        "}"
        "QPushButton#queryButton {"
        "  background: #2C7BBF;"
        "  color: #FFFFFF;"
        "  border: 1px solid #24699F;"
        "  border-radius: 6px;"
        "  padding: 7px 20px;"
        "  font-family: 'Microsoft YaHei'; font-size: 13px;"
        "  font-weight: 600;"
        "}"
        "QPushButton#queryButton:hover { background: #3A8DCE; }"
        "QPushButton#queryButton:pressed { background: #24699F; }"
        "QPushButton#queryResetButton {"
        "  background: #FFFFFF;"
        "  color: #1F4E79;"
        "  border: 1px solid #A9C8DF;"
        "  border-radius: 6px;"
        "  padding: 7px 16px;"
        "  font-family: 'Microsoft YaHei'; font-size: 12px;"
        "}"
        "QPushButton#queryResetButton:hover { background: #E8F1FA; }"
        "QTreeWidget#queryTree {"
        "  background: #FFFFFF;"
        "  alternate-background-color: #F8FBFD;"
        "  border: 1px solid #DCE7F1;"
        "  border-radius: 10px;"
        "  color: #30475C;"
        "  font-family: 'Microsoft YaHei'; font-size: 12px;"
        "  outline: 0;"
        "}"
        "QTreeWidget#queryTree::item {"
        "  min-height: 32px; padding-left: 4px;"
        "}"
        "QTreeWidget#queryTree::item:hover { background: #EFF6FC; }"
        "QTreeWidget#queryTree::item:selected {"
        "  background: #E8F1FA; color: #1F4E79;"
        "}"
        "QHeaderView::section {"
        "  background: #EAF3FA; color: #1F4E79; border: none;"
        "  border-bottom: 1px solid #DCE7F1; padding: 0px 10px;"
        "  font-family: 'Microsoft YaHei'; font-size: 13px; font-weight: 600;"
        "}"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(12);

    auto *title = new QLabel(tr("查询"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    auto *subtitle = new QLabel(
                tr("按样本号/患者姓名 + 时间范围查询，结果以树形展示"), this);
    subtitle->setObjectName(QStringLiteral("pageSubtitle"));
    root->addWidget(title);
    root->addWidget(subtitle);

    // —— 关键字 ——
    auto *filterRow = new QHBoxLayout;
    filterRow->setSpacing(10);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("queryEdit"));
    m_searchEdit->setPlaceholderText(tr("输入样本号或患者姓名（可留空）"));
    m_searchEdit->setClearButtonEnabled(true);
    filterRow->addWidget(m_searchEdit, 1);
    root->addLayout(filterRow);

    // —— 时间范围 ——
    auto *rangeRow = new QHBoxLayout;
    rangeRow->setSpacing(12);
    m_radioToday = new QRadioButton(tr("当天"), this);
    m_radioThisWeek = new QRadioButton(tr("本周"), this);
    m_radioThisMonth = new QRadioButton(tr("本月"), this);
    m_radioThisYear = new QRadioButton(tr("本年"), this);
    m_radioCustom = new QRadioButton(tr("自定义"), this);
    for (QRadioButton *rb : { m_radioToday, m_radioThisWeek,
                              m_radioThisMonth, m_radioThisYear,
                              m_radioCustom }) {
        rb->setCursor(Qt::PointingHandCursor);
        rangeRow->addWidget(rb);
    }
    m_fromEdit = new QLineEdit(this);
    m_fromEdit->setObjectName(QStringLiteral("queryEdit"));
    m_fromEdit->setPlaceholderText(tr("开始 yyyy-MM-dd"));
    m_toEdit = new QLineEdit(this);
    m_toEdit->setObjectName(QStringLiteral("queryEdit"));
    m_toEdit->setPlaceholderText(tr("结束 yyyy-MM-dd"));
    m_fromEdit->setFixedWidth(150);
    m_toEdit->setFixedWidth(150);
    rangeRow->addWidget(m_fromEdit);
    rangeRow->addWidget(m_toEdit);
    m_fromEdit->setEnabled(false);
    m_toEdit->setEnabled(false);

    m_radioGroup = new QButtonGroup(this);
    m_radioGroup->setExclusive(true);
    m_radioGroup->addButton(m_radioToday);
    m_radioGroup->addButton(m_radioThisWeek);
    m_radioGroup->addButton(m_radioThisMonth);
    m_radioGroup->addButton(m_radioThisYear);
    m_radioGroup->addButton(m_radioCustom);
    m_radioToday->setChecked(true);
    rangeRow->addStretch(1);
    root->addLayout(rangeRow);

    for (QRadioButton *rb : { m_radioCustom }) {
        connect(rb, &QRadioButton::toggled, this,
                [this](bool on) {
            m_fromEdit->setEnabled(on);
            m_toEdit->setEnabled(on);
        });
    }

    // —— 操作行 ——
    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(10);
    auto *searchButton = new QPushButton(tr("查询"), this);
    searchButton->setObjectName(QStringLiteral("queryButton"));
    searchButton->setCursor(Qt::PointingHandCursor);
    auto *resetButton = new QPushButton(tr("清空条件"), this);
    resetButton->setObjectName(QStringLiteral("queryResetButton"));
    resetButton->setCursor(Qt::PointingHandCursor);
    m_countLabel = new QLabel(tr("共 0 条记录"), this);
    m_countLabel->setObjectName(QStringLiteral("queryCount"));
    actionRow->addWidget(searchButton);
    actionRow->addWidget(resetButton);
    actionRow->addStretch(1);
    actionRow->addWidget(m_countLabel, 0, Qt::AlignVCenter);
    root->addLayout(actionRow);

    // —— 树形结果 ——
    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("queryTree"));
    // 列：样本号/姓名/年龄/类型/科别/性别/病区/床号/送检医生/
    //     检验医生/审核医生/临床诊断/备注/曲线概要
    m_tree->setColumnCount(14);
    m_tree->setHeaderLabels(QStringList()
        << tr("样本号") << tr("姓名") << tr("年龄") << tr("类型")
        << tr("科别") << tr("性别") << tr("病区") << tr("床号")
        << tr("送检医生") << tr("检验医生") << tr("审核医生")
        << tr("临床诊断") << tr("备注") << tr("60S/180S/300S/MAX"));
    m_tree->setRootIsDecorated(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(false);
    m_tree->setExpandsOnDoubleClick(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setIndentation(22);
    // 表头加高，列宽加大（最后一列自动铺满剩余宽度）。
    QHeaderView *header = m_tree->header();
    header->setMinimumSectionSize(60);
    header->setStretchLastSection(true);
    header->setFixedHeight(46);
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    const int columnWidths[] = {
        150, 100, 80, 90, 110, 70, 110, 90,
        110, 110, 110, 160, 160
    };
    for (int c = 0; c < 13; ++c) {
        header->setSectionResizeMode(c, QHeaderView::Interactive);
        header->resizeSection(c, columnWidths[c]);
    }
    header->setSectionResizeMode(13, QHeaderView::Stretch);
    root->addWidget(m_tree, 1);

    connect(searchButton, &QPushButton::clicked,
            this, &QueryWidget::refresh);
    connect(m_searchEdit, &QLineEdit::returnPressed,
            this, &QueryWidget::refresh);
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        m_searchEdit->clear();
        m_radioToday->setChecked(true);
        m_fromEdit->clear();
        m_toEdit->clear();
        m_searchEdit->setFocus();
    });

    // 点击任意节点都弹出该样本详情
    connect(m_tree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *item, int column) {
        Q_UNUSED(column);
        if (!item) {
            return;
        }
        const QVariant id = item->data(0, Qt::UserRole);
        if (id.isValid() && id.toLongLong() > 0) {
            emit sampleSelected(id.toLongLong());
        }
    });

    // 默认只显示当天，保持树空到用户点「查询」
    m_tree->clear();
    if (m_countLabel) {
        m_countLabel->setText(tr("共 0 条记录"));
    }
}

QDateTime QueryWidget::dayStart(const QDate &d) const
{
    return QDateTime(d, QTime(0, 0, 0));
}

QDateTime QueryWidget::dayEnd(const QDate &d) const
{
    return QDateTime(d, QTime(23, 59, 59));
}

QueryWidget::TimeRange QueryWidget::currentTimeRange() const
{
    TimeRange range;
    range.valid = true;
    const QDate today = QDate::currentDate();

    if (m_radioCustom && m_radioCustom->isChecked()) {
        const QDate from = QDate::fromString(
                    m_fromEdit ? m_fromEdit->text().trimmed() : QString(),
                    QStringLiteral("yyyy-MM-dd"));
        const QDate to = QDate::fromString(
                    m_toEdit ? m_toEdit->text().trimmed() : QString(),
                    QStringLiteral("yyyy-MM-dd"));
        if (!from.isValid() && !to.isValid()) {
            range.valid = false;
            return range;
        }
        if (from.isValid()) {
            range.fromIso = dayStart(from).toString(Qt::ISODate);
        }
        if (to.isValid()) {
            range.toIso = dayEnd(to).toString(Qt::ISODate);
        }
        return range;
    }

    if (m_radioToday && m_radioToday->isChecked()) {
        range.fromIso = dayStart(today).toString(Qt::ISODate);
        range.toIso = dayEnd(today).toString(Qt::ISODate);
    } else if (m_radioThisWeek && m_radioThisWeek->isChecked()) {
        const QDate weekStart = today.addDays(-(today.dayOfWeek() - 1));
        range.fromIso = dayStart(weekStart).toString(Qt::ISODate);
        range.toIso = dayEnd(today).toString(Qt::ISODate);
    } else if (m_radioThisMonth && m_radioThisMonth->isChecked()) {
        const QDate monthStart(today.year(), today.month(), 1);
        range.fromIso = dayStart(monthStart).toString(Qt::ISODate);
        range.toIso = dayEnd(today).toString(Qt::ISODate);
    } else if (m_radioThisYear && m_radioThisYear->isChecked()) {
        const QDate yearStart(today.year(), 1, 1);
        range.fromIso = dayStart(yearStart).toString(Qt::ISODate);
        range.toIso = dayEnd(today).toString(Qt::ISODate);
    } else {
        range.valid = false;
    }
    return range;
}

void QueryWidget::refresh()
{
    const QString keyword = m_searchEdit
            ? m_searchEdit->text().trimmed() : QString();
    const TimeRange range = currentTimeRange();

    m_tree->clear();
    QVector<SampleSummary> samples;
    if (range.valid && m_store && m_store->isOpen()) {
        QString error;
        samples = m_store->querySamples(keyword, range.fromIso,
                                        range.toIso, 500, &error);
        if (!error.isEmpty()) {
            qWarning() << "查询失败:" << error;
        }
    }

    for (const SampleSummary &sample : samples) {
        const QString time = sample.testTime.isValid()
                ? sample.testTime.toString(QStringLiteral("MM-dd HH:mm"))
                : QStringLiteral("--");
        // 样本行：与结果表列一致，含科别/性别/病区/床号/送检医生/
        // 检验医生/审核医生/临床诊断/备注，方便直接比对。
        const auto textOrDash = [](const QString &text) {
            return text.isEmpty() ? QStringLiteral("--") : text;
        };
        auto *sampleItem = new QTreeWidgetItem(
                    m_tree, QStringList()
                    << sample.sampleNumber << sample.patientName
                    << (sample.age >= 0 ? QString::number(sample.age)
                        : QStringLiteral("--"))
                    << textOrDash(sample.sampleType)
                    << textOrDash(sample.department)
                    << textOrDash(sample.gender)
                    << textOrDash(sample.ward)
                    << textOrDash(sample.bed)
                    << textOrDash(sample.sendingDoctor)
                    << textOrDash(sample.testingDoctor)
                    << textOrDash(sample.reviewingDoctor)
                    << textOrDash(sample.diagnosis)
                    << textOrDash(sample.remark)
                    << time);
        sampleItem->setData(0, Qt::UserRole, sample.id);
        sampleItem->setData(0, Qt::UserRole + 1, true);   // 样本节点标记

        // 每个试剂子节点：需要 loadSample 取该样本各试剂曲线与状态
        SampleTestData loaded;
        QString error;
        if (m_store->loadSample(sample.id, &loaded, &error)) {
            for (const TestCurveData &test : loaded.tests) {
                const QString status = test.status.isEmpty()
                        ? tr("待测") : test.status;
                QString summary;
                if (!test.aggregationPoints.isEmpty()) {
                    const auto sec = [&test](int second) {
                        const int i = second - 1;
                        return (i >= 0 && i < test.aggregationPoints.size())
                                ? QString::number(test.aggregationPoints.at(i), 'f', 1)
                                : QStringLiteral("--");
                    };
                    double maxV = -9999.0;
                    for (double v : test.aggregationPoints) {
                        maxV = qMax(maxV, v);
                    }
                    const QString maxS = maxV <= -9999.0 / 2.0
                            ? QStringLiteral("--")
                            : QString::number(maxV, 'f', 1);
                    summary = QStringLiteral("%1 / %2 / %3 / %4")
                            .arg(sec(60), sec(180), sec(300), maxS);
                } else if (test.status == QStringLiteral("测试中")) {
                    // “测试中”占位行：还没有曲线，但标记正在测试。
                    summary = tr("测试进行中…");
                } else {
                    summary = tr("无曲线");
                }
                // 子节点：试剂名放样本号列下（缩进）、状态放类型列、曲线放末列。
                auto *reagentItem = new QTreeWidgetItem(
                            sampleItem, QStringList()
                            << test.reagent << QString() << QString()
                            << status
                            << QString() << QString() << QString()
                            << QString() << QString() << QString()
                            << QString() << QString() << QString()
                            << summary);
                reagentItem->setData(0, Qt::UserRole, sample.id);  // 点击也弹样本详情
            }
        }
        sampleItem->setExpanded(true);
    }

    if (m_countLabel) {
        m_countLabel->setText(tr("共 %1 条记录").arg(samples.size()));
    }
    if (!range.valid) {
        m_tree->clear();
        if (m_countLabel) {
            m_countLabel->setText(tr("已选择自定义但未填有效日期"));
        }
    }
}

void QueryWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 进入查询页时保持空树，不自动加载，等待用户点查询。
}