#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "curvedisplaywidget.h"

#include "aggregationcalculator.h"
#include "medicalwidgets.h"
#include "referenceinterval.h"

#include <algorithm>
#include <cmath>
#include <QCheckBox>
#include <QDebug>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

const char* const kCurveColors[] = {
    "#2F93E0",   // AA 蓝
    "#22A877",   // ADP 绿
    "#B85CC4",   // EPI 紫
    "#E08A2F",   // COL 橙
    "#DF5A50"    // RIS 红
};
constexpr int kMaxSeries = 5;

const char* const kTitleColor = "#1F4E79";
const char* const kMutedColor = "#7A8FA3";
const char* const kTextColor  = "#3A5065";
const char* const kCardBorder = "#DCE7F1";
const char* const kCardBg     = "#FFFFFF";
const char* const kBadColor   = "#DF5A50";

/** 缺失哨兵：远小于任何真实聚集率（真实可为负但不会低至此）。 */
constexpr double kSentinelMissing = -9999.0;

QColor curveColor(int index)
{
    return QColor(QString::fromLatin1(
                      kCurveColors[((index % kMaxSeries) + kMaxSeries)
                                   % kMaxSeries]));
}

/** 试剂字符串 -> 参考区间下标（AA0/ADP1/EPI2/COL3/RIS4），未知返回 -1 */
int reagentReferenceIndex(const QString &reagent)
{
    if (reagent == QStringLiteral("AA"))  return 0;
    if (reagent == QStringLiteral("ADP")) return 1;
    if (reagent == QStringLiteral("EPI")) return 2;
    if (reagent == QStringLiteral("COL")) return 3;
    if (reagent == QStringLiteral("RIS")) return 4;
    return -1;
}

QString fmtPercent(double value)
{
    // 哨兵/越界缺失显示 --；真实值（含负数）直接显示。
    if (value <= kSentinelMissing / 2.0) {
        return QStringLiteral("--");
    }
    return QString::number(value, 'f', 1) + QStringLiteral("%");
}

} // namespace

// =================== 叠绘曲线控件 ===================
/** 在一个面板上多条聚集率曲线叠加；x 为秒（1~300），y 为聚集率 %。 */
class CurveDisplayWidget::CurvePlotPanel : public QWidget
{
public:
    struct Series {
        QString name;
        QVector<QPointF> points;   // (second, aggregation %)
        QColor color;
        bool visible = true;
    };

    explicit CurvePlotPanel(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(300);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setSeries(const QVector<Series> &series)
    {
        m_series = series;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        const QRectF card = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        p.setPen(QPen(QColor(kCardBorder), 1.0));
        p.setBrush(QColor(kCardBg));
        p.drawRoundedRect(card, 10.0, 10.0);

        p.setPen(QColor(kTitleColor));
        QFont titleFont(QStringLiteral("Microsoft YaHei"));
        titleFont.setPixelSize(14);
        titleFont.setBold(true);
        p.setFont(titleFont);
        p.drawText(QRectF(card.left() + 14.0, card.top() + 8.0,
                          160.0, 22.0),
                   Qt::AlignLeft | Qt::AlignVCenter, tr("聚集曲线"));

        p.setPen(QColor(kMutedColor));
        QFont subFont(QStringLiteral("Microsoft YaHei"));
        subFont.setPixelSize(10);
        p.setFont(subFont);
        p.drawText(QRectF(card.left() + 150.0, card.top() + 8.0,
                          card.width() - 160.0, 22.0),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   tr("聚集率 % · 同时显示多条试剂曲线"));

        const QRectF plotArea = card.adjusted(54.0, 34.0, -18.0, -36.0);
        if (plotArea.width() < 60.0 || plotArea.height() < 60.0) {
            return;
        }

        // X：0~300 固定；Y：聚集率范围 [-20,100]，留负区间观察过冲。
        const double xMax = 300.0;
        const double yMin = -20.0;
        const double yMax = 100.0;

        // 网格
        p.setPen(QPen(QColor("#E7F0F7"), 1.0, Qt::DashLine));
        constexpr int kDiv = 4;
        for (int i = 0; i <= kDiv; ++i) {
            const qreal ratio = qreal(i) / kDiv;
            const qreal x = plotArea.left() + plotArea.width() * ratio;
            const qreal y = plotArea.top() + plotArea.height() * ratio;
            p.drawLine(QPointF(x, plotArea.top()), QPointF(x, plotArea.bottom()));
            p.drawLine(QPointF(plotArea.left(), y), QPointF(plotArea.right(), y));
        }

        // 坐标轴
        p.setPen(QPen(QColor("#B8CDDC"), 1.0));
        p.drawLine(plotArea.bottomLeft(), plotArea.bottomRight());
        p.drawLine(plotArea.topLeft(), plotArea.bottomLeft());

        QFont axisFont(QStringLiteral("Microsoft YaHei"));
        axisFont.setPixelSize(8);
        p.setFont(axisFont);
        p.setPen(QColor(kMutedColor));
        for (int i = 0; i <= kDiv; ++i) {
            const qreal ratio = qreal(i) / kDiv;
            const qreal y = plotArea.top() + plotArea.height() * ratio;
            p.drawText(QRectF(plotArea.left() - 46.0, y - 7.0, 40.0, 14.0),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(qRound(yMax - (yMax - yMin) * ratio)));
            const qreal x = plotArea.left() + plotArea.width() * ratio;
            p.drawText(QRectF(x - 20.0, plotArea.bottom() + 4.0, 40.0, 14.0),
                       Qt::AlignHCenter | Qt::AlignTop,
                       QString::number(qRound(xMax * ratio)));
        }

        const auto mapPoint = [&plotArea, xMax, yMin, yMax](const QPointF &pt) {
            const double x = qBound(0.0, pt.x(), xMax);
            const double y = qBound(yMin, pt.y(), yMax);
            return QPointF(plotArea.left() + (x / xMax) * plotArea.width(),
                           plotArea.bottom()
                           - ((y - yMin) / (yMax - yMin)) * plotArea.height());
        };

        bool anyVisible = false;
        for (const Series &s : m_series) {
            if (!s.visible || s.points.isEmpty()) {
                continue;
            }
            anyVisible = true;
            p.save();
            p.setPen(QPen(s.color, 2.0, Qt::SolidLine, Qt::RoundCap,
                          Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            QPainterPath path;
            bool first = true;
            for (const QPointF &pt : s.points) {
                const QPointF m = mapPoint(pt);
                if (first) {
                    path.moveTo(m);
                    first = false;
                } else {
                    path.lineTo(m);
                }
            }
            if (path.isEmpty()) {
                p.restore();
                continue;
            }
            p.drawPath(path);

            // 终点圆点（突出 Max 位置）
            const QPointF last = mapPoint(s.points.last());
            p.setPen(Qt::NoPen);
            p.setBrush(s.color);
            p.drawEllipse(last, 4.0, 4.0);
            p.restore();
        }

        if (!anyVisible) {
            QFont ef(QStringLiteral("Microsoft YaHei"));
            ef.setPixelSize(13);
            p.setFont(ef);
            p.setPen(QColor("#A5B6C4"));
            p.drawText(plotArea, Qt::AlignCenter, tr("该样本暂无曲线数据"));
        }
    }

private:
    QVector<Series> m_series;
};

// =================== 页面 ===================
CurveDisplayWidget::CurveDisplayWidget(TestDataStore *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    setObjectName(QStringLiteral("curvePage"));
    setStyleSheet(QStringLiteral(
        "QWidget#curvePage { background: #F4F8FC; }"
        "QLabel#curvePageTitle {"
        "  color: #1F4E79; font-family: 'Microsoft YaHei';"
        "  font-size: 21px; font-weight: 700;"
        "}"
        "QLabel#curveStatus {"
        "  color: #5D7386; font-family: 'Microsoft YaHei'; font-size: 12px;"
        "}"
        "QTreeWidget#curveTree {"
        "  background: #FFFFFF; alternate-background-color: #F8FBFD;"
        "  border: 1px solid #DCE7F1; border-radius: 10px;"
        "  color: #30475C; font-family: 'Microsoft YaHei'; font-size: 12px;"
        "  outline: 0;"
        "}"
        "QTreeWidget#curveTree::item { min-height: 30px; padding-left: 4px; }"
        "QTreeWidget#curveTree::item:hover { background: #EFF6FC; }"
        "QTreeWidget#curveTree::item:selected {"
        "  background: #E8F1FA; color: #1F4E79;"
        "}"
        "QHeaderView::section {"
        "  background: #EDF4FB; color: #33556F;"
        "  border: none; border-bottom: 1px solid #C9DDEB;"
        "  padding: 6px; font-family: 'Microsoft YaHei'; font-size: 12px;"
        "}"
        "QCheckBox { color: #30475C; font-family: 'Microsoft YaHei';"
        "  font-size: 12px; spacing: 6px; }"
        "QTableWidget {"
        "  background: #FFFFFF; alternate-background-color: #F8FBFD;"
        "  border: 1px solid #DCE7F1; border-radius: 8px;"
        "  color: #30475C; font-family: 'Microsoft YaHei'; font-size: 12px;"
        "  gridline-color: #E3EDF5;"
        "}"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);

    // 标题 + 刷新
    auto *header = new QHBoxLayout;
    auto *title = new QLabel(tr("曲线分析"), this);
    title->setObjectName(QStringLiteral("curvePageTitle"));
    auto *refreshBtn = new QPushButton(tr("刷新"), this);
    refreshBtn->setCursor(Qt::PointingHandCursor);
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("curveStatus"));
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(m_statusLabel);
    header->addWidget(refreshBtn);
    layout->addLayout(header);

    auto *body = new QHBoxLayout;
    body->setSpacing(12);

    // —— 左侧样本树（年-月-日-样本号）——
    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("curveTree"));
    m_tree->setColumnCount(1);
    m_tree->setHeaderLabels(QStringList() << tr("全部样本"));
    m_tree->setRootIsDecorated(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setExpandsOnDoubleClick(true);
    m_tree->setIndentation(20);
    m_tree->setFixedWidth(240);
    body->addWidget(m_tree, 0);

    connect(m_tree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *item, int) {
        const QVariant id = item->data(0, Qt::UserRole);
        if (id.isValid() && id.toLongLong() > 0) {
            loadSample(id.toLongLong());
        }
    });

    // —— 右侧：图例 + 曲线 + 表格 ——
    auto *right = new QVBoxLayout;
    right->setSpacing(8);

    m_plotArea = new QWidget(this);
    m_legendBox = new QHBoxLayout(m_plotArea);
    m_legendBox->setContentsMargins(0, 0, 0, 0);
    m_legendBox->addStretch(1);
    right->addWidget(m_plotArea);

    m_plot = new CurvePlotPanel(this);
    right->addWidget(m_plot, 1);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(12);
    m_table->setHorizontalHeaderLabels(QStringList()
        << tr("试剂") << tr("60S") << tr("180S") << tr("300S") << tr("Max")
        << tr("斜率") << tr("面积AUC") << tr("TMA时间") << tr("延时时间")
        << tr("粘附率") << tr("参考范围") << tr("状态"));
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setFixedHeight(210);
    m_table->horizontalHeader()->setStretchLastSection(true);
    for (int c = 0; c < 12; ++c) {
        m_table->horizontalHeader()->setSectionResizeMode(
                    c, c == 1 || c == 2 || c == 3 || c == 4
                       ? QHeaderView::ResizeToContents
                       : QHeaderView::Interactive);
    }
    right->addWidget(m_table);

    body->addLayout(right, 1);
    layout->addLayout(body, 1);

    connect(refreshBtn, &QPushButton::clicked, this, &CurveDisplayWidget::refresh);

    refresh();
}

/** 计算某试剂聚集率曲线（%）：优先用库中已算好的集聚百分比；
 *  为空时用 prpPoints + PPP/PRP0 现算。无数据返回空。 */
QVector<double> CurveDisplayWidget::aggregationCurveFor(
        const TestCurveData &test, double ppp) const
{
    if (!test.aggregationPoints.isEmpty()) {
        return test.aggregationPoints;
    }
    if (test.prpPoints.isEmpty()
            || test.prp0 <= 0 || ppp <= 0) {
        return QVector<double>();
    }
    QVector<double> agg;
    agg.reserve(test.prpPoints.size());
    for (int prpn : test.prpPoints) {
        agg.append(AggregationCalculator::aggregationPercent(
                       double(prpn), double(test.prp0), ppp));
    }
    return agg;
}

void CurveDisplayWidget::rebuildSampleTree()
{
    m_tree->clear();
    if (!m_store || !m_store->isOpen()) {
        m_statusLabel->setText(tr("测试数据库未打开。"));
        return;
    }
    // 查询全量样本（不带时间/关键字，取大数据量上限以覆盖全部历史）。
    QString error;
    const QVector<SampleSummary> samples =
            m_store->querySamples(QString(), QString(), QString(),
                                  5000, &error);
    if (!error.isEmpty()) {
        qWarning() << "曲线样本查询失败:" << error;
        m_statusLabel->setText(tr("查询失败：%1").arg(error));
        return;
    }

    // 按 年 -> 月 -> 日 三层建树，样本挂在日节点下。
    struct DayNode { QTreeWidgetItem *item; };
    QHash<QString, QTreeWidgetItem*> yearItems;
    QHash<QString, QTreeWidgetItem*> monthItems;
    QHash<QString, QTreeWidgetItem*> dayItems;

    for (const SampleSummary &sample : samples) {
        if (!sample.testTime.isValid()) {
            continue;
        }
        const QDate d = sample.testTime.date();
        const QString year = QString::number(d.year());
        const QString month = QStringLiteral("%1年%2月")
                .arg(d.year()).arg(d.month(), 2, 10, QLatin1Char('0'));
        const QString dayKey = d.toString(QStringLiteral("yyyy-MM-dd"));
        const QString dayLabel = QStringLiteral("%1日").arg(d.day(), 2, 10,
                                                           QLatin1Char('0'));

        QTreeWidgetItem *yearItem = yearItems.value(year);
        if (!yearItem) {
            yearItem = new QTreeWidgetItem(m_tree, QStringList() << year);
            yearItems.insert(year, yearItem);
        }
        QTreeWidgetItem *monthItem = monthItems.value(month);
        if (!monthItem) {
            monthItem = new QTreeWidgetItem(yearItem, QStringList() << month);
            monthItems.insert(month, monthItem);
        }
        QTreeWidgetItem *dayItem = dayItems.value(dayKey);
        if (!dayItem) {
            dayItem = new QTreeWidgetItem(monthItem, QStringList() << dayLabel);
            dayItems.insert(dayKey, dayItem);
            dayItem->setData(0, Qt::UserRole, dayKey);
        }
        auto *sampleItem = new QTreeWidgetItem(
                    dayItem, QStringList() << sample.sampleNumber);
        sampleItem->setData(0, Qt::UserRole, sample.id);
        sampleItem->setToolTip(0, tr("样本号：%1　测试时间：%2")
                               .arg(sample.sampleNumber,
                                    sample.testTime.toString(
                                        QStringLiteral("yyyy-MM-dd HH:mm"))));
    }

    // 全部展开年一层，方便浏览最近年份。
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        m_tree->topLevelItem(i)->setExpanded(true);
    }
    m_statusLabel->setText(tr("共 %1 个样本").arg(samples.size()));
}

void CurveDisplayWidget::loadSample(qint64 sampleId)
{
    if (!m_store || !m_store->isOpen()) {
        return;
    }
    SampleTestData loaded;
    QString error;
    if (!m_store->loadSample(sampleId, &loaded, &error)) {
        qWarning() << "曲线读取样本失败:" << sampleId << error;
        m_statusLabel->setText(tr("读取样本失败：%1").arg(error));
        return;
    }
    m_sample = loaded;
    m_currentSampleId = sampleId;
    updateSeriesVisibility();
    populateTable(m_sample);
}

/** 由图例勾选状态刷新曲线叠加显示（默认全部勾选）。 */
void CurveDisplayWidget::updateSeriesVisibility()
{
    // 重建图例
    while (QLayoutItem *child = m_legendBox->takeAt(0)) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }

    m_reagentVisible.clear();

    QVector<CurvePlotPanel::Series> allSeries;
    allSeries.reserve(m_sample.tests.size());
    for (int i = 0; i < m_sample.tests.size() && i < kMaxSeries; ++i) {
        const TestCurveData &test = m_sample.tests.at(i);
        const QVector<double> agg = aggregationCurveFor(test, double(m_sample.pppValue));
        QVector<QPointF> pts;
        pts.reserve(agg.size());
        for (int k = 0; k < agg.size(); ++k) {
            pts.append(QPointF(double(k + 1), agg.at(k)));
        }

        CurvePlotPanel::Series s;
        s.name = test.reagent.trimmed().isEmpty()
                ? QStringLiteral("试剂%1").arg(i + 1) : test.reagent;
        s.points = pts;
        s.color = curveColor(i);
        s.visible = true;
        allSeries.append(s);

        m_reagentVisible.append(true);
        auto *box = new MedicalCheckBox(s.name, m_plotArea);
        box->setProperty("curveIndex", i);
        box->setChecked(true);
        connect(box, &QCheckBox::toggled, this, [this](bool on) {
            auto *cb = qobject_cast<MedicalCheckBox*>(sender());
            if (!cb) {
                return;
            }
            const int idx = cb->property("curveIndex").toInt();
            if (idx >= 0 && idx < m_reagentVisible.size()) {
                m_reagentVisible[idx] = on;
            }
            applySeriesVisibility();
        });
        m_legendBox->addWidget(box);
    }
    m_legendBox->addStretch(1);
    m_plotArea->setVisible(!allSeries.isEmpty());

    m_plot->setSeries(allSeries);
}

/** 依据 m_reagentVisible 把数据重发到曲线控件（勾选改变时调用）。 */
void CurveDisplayWidget::applySeriesVisibility()
{
    if (!m_plot) {
        return;
    }
    const double ppp = double(m_sample.pppValue);
    QVector<CurvePlotPanel::Series> allSeries;
    allSeries.reserve(m_sample.tests.size());
    for (int i = 0; i < m_sample.tests.size() && i < kMaxSeries; ++i) {
        const TestCurveData &test = m_sample.tests.at(i);
        const QVector<double> agg = aggregationCurveFor(test, ppp);
        QVector<QPointF> pts;
        pts.reserve(agg.size());
        for (int k = 0; k < agg.size(); ++k) {
            pts.append(QPointF(double(k + 1), agg.at(k)));
        }
        CurvePlotPanel::Series s;
        s.name = test.reagent.trimmed().isEmpty()
                ? QStringLiteral("试剂%1").arg(i + 1) : test.reagent;
        s.points = pts;
        s.color = curveColor(i);
        s.visible = (i < m_reagentVisible.size()) && m_reagentVisible.at(i);
        allSeries.append(s);
    }
    m_plot->setSeries(allSeries);
}

/** 填充下方数据分析表。 */
void CurveDisplayWidget::populateTable(const SampleTestData &sample)
{
    m_table->setRowCount(0);
    const double ppp = double(sample.pppValue);

    int row = 0;
    for (int i = 0; i < sample.tests.size(); ++i) {
        const TestCurveData &test = sample.tests.at(i);
        const QVector<double> agg = aggregationCurveFor(test, ppp);
        const int index = reagentReferenceIndex(test.reagent);

        auto valueAt = [&agg](int second) {
            const int i = second - 1;
            return (i >= 0 && i < agg.size())
                    ? agg.at(i) : kSentinelMissing;
        };
        const double s60 = valueAt(60);
        const double s180 = valueAt(180);
        const double s300 = valueAt(300);

        double maxV = kSentinelMissing;
        for (double v : agg) {
            maxV = qMax(maxV, v);
        }

        // —— 派生指标 ——
        // 斜率：0~60s 聚集率的平均上升速度（%/s）
        QString slope = QStringLiteral("--");
        if (agg.size() >= 60 && s60 > kSentinelMissing / 2.0) {
            const double rise = qMax(0.0, s60);
            slope = QString::number(rise / 60.0, 'f', 2) + QStringLiteral("%/s");
        }
        // 面积 AUC：60 个点（0~60s）梯形积分近似
        QString auc = QStringLiteral("--");
        if (agg.size() >= 61) {
            double area = 0.0;
            for (int k = 1; k <= 60; ++k) {
                // agg[k] 对应第 k+1 秒，梯形：-(k)~-(k+1) 积分
                const double lb = agg.at(k - 1);
                const double ub = agg.at(k);
                if (lb > kSentinelMissing / 2.0 && ub > kSentinelMissing / 2.0) {
                    area += (lb + ub) * 0.5;
                }
            }
            auc = QString::number(area, 'f', 1);
        }
        // TMA：达到 90% Max 的最早时间
        QString tmaStr = QStringLiteral("--");
        if (maxV > kSentinelMissing / 2.0 && maxV > 0.0) {
            const double threshold = maxV * 0.9;
            for (int k = 0; k < agg.size(); ++k) {
                if (agg.at(k) >= threshold) {
                    tmaStr = QString::number(k + 1) + QStringLiteral(" s");
                    break;
                }
            }
        }
        // 延时时间：聚集率首次抬升（超过最大值的 2%）的最早时间
        QString lag = QStringLiteral("--");
        if (maxV > kSentinelMissing / 2.0 && maxV > 0.0) {
            const double threshold = maxV * 0.02;
            for (int k = 0; k < agg.size(); ++k) {
                if (agg.at(k) >= threshold) {
                    lag = QString::number(k + 1) + QStringLiteral(" s");
                    break;
                }
            }
        }
        // 粘附率：采用 Max 聚集率（缺失则 --）
        const QString adhesion = fmtPercent(maxV);

        // 参考范围（取 MAX 区间，含未设置）
        QString rangeText = QStringLiteral("--");
        if (index >= 0) {
            const int lo = ReferenceInterval::low(index, ReferenceInterval::Timepoint::Max);
            const int hi = ReferenceInterval::high(index, ReferenceInterval::Timepoint::Max);
            if (hi > 0) {
                rangeText = QStringLiteral("%1~%2%%").arg(lo).arg(hi);
            } else {
                rangeText = QStringLiteral("未设置");
            }
        }

        // 状态
        const QString status = test.status.trimmed().isEmpty()
                ? QStringLiteral("完成") : test.status;
        const bool abnormal = (status == QStringLiteral("异常结果"));

        m_table->insertRow(row);
        const auto setText = [this, row](int col, const QString &text) {
            m_table->setItem(row, col, new QTableWidgetItem(text));
        };
        setText(0, test.reagent);
        setText(1, fmtPercent(s60));
        setText(2, fmtPercent(s180));
        setText(3, fmtPercent(s300));
        setText(4, fmtPercent(maxV));
        setText(5, slope);
        setText(6, auc);
        setText(7, tmaStr);
        setText(8, lag);
        setText(9, adhesion);
        setText(10, rangeText);
        setText(11, status);

        // 状态列上色
        if (QTableWidgetItem *it = m_table->item(row, 11)) {
            it->setForeground(QColor(abnormal ? kBadColor : "#33556F"));
        }
        // 各时间点离群标红（在参考区间外）
        if (index >= 0) {
            const double tpVals[3] = { s60, s180, s300 };
            const int tpCols[3] = { 1, 2, 3 };
            for (int c = 0; c < 3; ++c) {
                if (tpVals[c] > kSentinelMissing / 2.0) {
                    const ReferenceInterval::Timepoint tp =
                            c == 0 ? ReferenceInterval::Timepoint::S60
                            : c == 1 ? ReferenceInterval::Timepoint::S180
                            : ReferenceInterval::Timepoint::S300;
                    if (!ReferenceInterval::contains(index, tp, tpVals[c])
                            && ReferenceInterval::isSet(index, tp)) {
                        if (QTableWidgetItem *it = m_table->item(row, tpCols[c])) {
                            it->setForeground(QColor(kBadColor));
                        }
                    }
                }
            }
            if (maxV > kSentinelMissing / 2.0
                    && ReferenceInterval::isSet(index,
                                                ReferenceInterval::Timepoint::Max)
                    && !ReferenceInterval::contains(index, maxV)) {
                if (QTableWidgetItem *it = m_table->item(row, 4)) {
                    it->setForeground(QColor(kBadColor));
                }
            }
        }
        ++row;
    }
}

void CurveDisplayWidget::refresh()
{
    rebuildSampleTree();
    if (m_currentSampleId > 0) {
        loadSample(m_currentSampleId);
    } else {
        m_table->setRowCount(0);
    }
}