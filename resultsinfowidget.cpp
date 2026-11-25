#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "resultsinfowidget.h"
#include "resultstablewidget.h"
#include "testdatastore.h"

#include <QDate>
#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QVBoxLayout>

ResultsInfoWidget::ResultsInfoWidget(TestDataStore *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    setObjectName(QStringLiteral("resultsInfoPage"));
    setStyleSheet(QStringLiteral(
        "QWidget#resultsInfoPage { background: #F4F8FC; }"
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
        "QLabel#resultCount {"
        "  color: #5D7386;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "}"
        "QPushButton#resultRefreshButton {"
        "  background: #FFFFFF;"
        "  color: #1F4E79;"
        "  border: 1px solid #A9C8DF;"
        "  border-radius: 6px;"
        "  padding: 6px 16px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QPushButton#resultRefreshButton:hover { background: #E8F1FA; }"
        "QPushButton#resultRefreshButton:pressed { background: #D3E6F5; }"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(12);

    auto *header = new QHBoxLayout;
    header->setSpacing(12);
    auto *titleBox = new QVBoxLayout;
    titleBox->setSpacing(2);
    auto *title = new QLabel(tr("结果信息"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    auto *subtitle = new QLabel(tr("最近测试结果 · 自绘表格"), this);
    subtitle->setObjectName(QStringLiteral("pageSubtitle"));
    titleBox->addWidget(title);
    titleBox->addWidget(subtitle);
    header->addLayout(titleBox);
    header->addStretch(1);

    m_countLabel = new QLabel(tr("共 0 条记录"), this);
    m_countLabel->setObjectName(QStringLiteral("resultCount"));
    header->addWidget(m_countLabel, 0, Qt::AlignVCenter);

    auto *refreshButton = new QPushButton(tr("刷新"), this);
    refreshButton->setObjectName(QStringLiteral("resultRefreshButton"));
    refreshButton->setCursor(Qt::PointingHandCursor);
    connect(refreshButton, &QPushButton::clicked,
            this, &ResultsInfoWidget::refresh);
    header->addWidget(refreshButton, 0, Qt::AlignVCenter);
    root->addLayout(header);

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("resultsTableScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea#resultsTableScroll { background: #F4F8FC; border: none; }"
        // 只设置滚动区视口背景；表格自身保持白色卡片样式。
        "QScrollArea#resultsTableScroll"
        " > QWidget#qt_scrollarea_viewport {"
        "  background: #F4F8FC;"
        "}"));

    m_table = new ResultsTableWidget(scroll);
    scroll->setWidget(m_table);
    connect(m_table, &ResultsTableWidget::entryValueChanged,
            this, [this](qint64 sampleId, const QString &entryKey,
                         const QString &value) {
        if (m_store && m_store->isOpen()) {
            QString error;
            if (!m_store->updateSampleEntryValue(sampleId, entryKey,
                                                 value, &error)) {
                qWarning() << "更新信息失败:" << error;
            }
        }
    });
    connect(m_table, &ResultsTableWidget::rowActivated, this,
            &ResultsInfoWidget::sampleSelected);
    root->addWidget(scroll, 1);

    refresh();
}

void ResultsInfoWidget::refresh()
{
    QVector<SampleSummary> rows;
    if (m_store && m_store->isOpen()) {
        // 结果信息表只显示当天样本。
        rows = m_store->samplesOnDay(
                    QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")),
                    500);
    }
    if (m_table) {
        m_table->setRows(rows);
    }
    if (m_countLabel) {
        m_countLabel->setText(tr("共 %1 条记录").arg(rows.size()));
    }
}

void ResultsInfoWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
}
