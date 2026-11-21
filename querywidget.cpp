#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "querywidget.h"
#include "resultstablewidget.h"
#include "testdatastore.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
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
        "QPushButton#queryButton {"
        "  background: #2C7BBF;"
        "  color: #FFFFFF;"
        "  border: 1px solid #24699F;"
        "  border-radius: 6px;"
        "  padding: 7px 20px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
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
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QPushButton#queryResetButton:hover { background: #E8F1FA; }"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(12);

    auto *title = new QLabel(tr("查询"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    auto *subtitle = new QLabel(
                tr("按样本号或患者姓名查询测试结果"), this);
    subtitle->setObjectName(QStringLiteral("pageSubtitle"));
    root->addWidget(title);
    root->addWidget(subtitle);

    auto *filterRow = new QHBoxLayout;
    filterRow->setSpacing(10);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("queryEdit"));
    m_searchEdit->setPlaceholderText(tr("输入样本号或患者姓名"));
    m_searchEdit->setClearButtonEnabled(true);
    filterRow->addWidget(m_searchEdit, 1);

    auto *searchButton = new QPushButton(tr("查询"), this);
    searchButton->setObjectName(QStringLiteral("queryButton"));
    searchButton->setCursor(Qt::PointingHandCursor);
    filterRow->addWidget(searchButton);

    auto *resetButton = new QPushButton(tr("清空条件"), this);
    resetButton->setObjectName(QStringLiteral("queryResetButton"));
    resetButton->setCursor(Qt::PointingHandCursor);
    filterRow->addWidget(resetButton);

    m_countLabel = new QLabel(tr("共 0 条记录"), this);
    m_countLabel->setObjectName(QStringLiteral("queryCount"));
    filterRow->addWidget(m_countLabel, 0, Qt::AlignVCenter);
    root->addLayout(filterRow);

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("queryTableScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea#queryTableScroll { background: #F4F8FC; border: none; }"
        "QScrollArea#queryTableScroll > QWidget > QWidget {"
        "  background: #F4F8FC;"
        "}"));
    m_table = new ResultsTableWidget(scroll);
    scroll->setWidget(m_table);
    root->addWidget(scroll, 1);

    connect(searchButton, &QPushButton::clicked,
            this, &QueryWidget::refresh);
    connect(m_searchEdit, &QLineEdit::returnPressed,
            this, &QueryWidget::refresh);
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        m_searchEdit->clear();
        m_searchEdit->setFocus();
        refresh();
    });

    refresh();
}

void QueryWidget::refresh()
{
    QVector<SampleSummary> rows;
    if (m_store && m_store->isOpen()) {
        rows = m_store->recentSamples(500);
    }

    const QString keyword = m_searchEdit
            ? m_searchEdit->text().trimmed() : QString();
    if (!keyword.isEmpty()) {
        QVector<SampleSummary> filtered;
        filtered.reserve(rows.size());
        for (const SampleSummary &row : rows) {
            if (row.sampleNumber.contains(keyword, Qt::CaseInsensitive)
                    || row.patientName.contains(keyword,
                                                Qt::CaseInsensitive)) {
                filtered.append(row);
            }
        }
        rows = filtered;
    }

    if (m_table) {
        m_table->setRows(rows);
    }
    if (m_countLabel) {
        m_countLabel->setText(tr("共 %1 条记录").arg(rows.size()));
    }
}

void QueryWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
}
