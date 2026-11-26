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
#include <QFocusEvent>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineF>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
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
        setMouseTracking(true);
    }

    void setSeries(const QVector<Series> &series)
    {
        m_series = series;
        m_hoverSeries = -1;
        m_hoverIndex = -1;
        update();
    }

protected:
    static constexpr double kXMax = 300.0;
    static constexpr double kYMin = -20.0;
    static constexpr double kYMax = 100.0;

    QRectF plotAreaRect() const
    {
        const QRectF card = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        return card.adjusted(54.0, 34.0, -18.0, -36.0);
    }

    QPointF mapPoint(const QRectF &plotArea, const QPointF &pt) const
    {
        const double x = qBound(0.0, pt.x(), kXMax);
        const double y = qBound(kYMin, pt.y(), kYMax);
        return QPointF(plotArea.left() + (x / kXMax) * plotArea.width(),
                       plotArea.bottom()
                       - ((y - kYMin) / (kYMax - kYMin)) * plotArea.height());
    }

    /** AUC：0~60s 梯形积分（%·s），点数不足返回 -1。 */
    double areaUnderCurve(const QVector<QPointF> &pts) const
    {
        if (pts.size() < 61) {
            return -1.0;
        }
        double area = 0.0;
        for (int k = 1; k <= 60; ++k) {
            area += (pts.at(k - 1).y() + pts.at(k).y()) * 0.5;
        }
        return area;
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const QRectF plotArea = plotAreaRect();
        int bestSeries = -1;
        int bestIndex = -1;
        qreal bestDist = 12.0;   // 命中半径
        for (int i = 0; i < m_series.size(); ++i) {
            const Series &s = m_series.at(i);
            if (!s.visible) {
                continue;
            }
            for (int k = 0; k < s.points.size(); ++k) {
                const QPointF m = mapPoint(plotArea, s.points.at(k));
                const qreal d = QLineF(event->localPos(), m).length();
                if (d < bestDist) {
                    bestDist = d;
                    bestSeries = i;
                    bestIndex = k;
                }
            }
        }
        if (bestSeries != m_hoverSeries || bestIndex != m_hoverIndex) {
            m_hoverSeries = bestSeries;
            m_hoverIndex = bestIndex;
            update();
        }
        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        if (m_hoverSeries >= 0 || m_hoverIndex >= 0) {
            m_hoverSeries = -1;
            m_hoverIndex = -1;
            update();
        }
        QWidget::leaveEvent(event);
    }

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
                   tr("聚集率 % · 同时显示多条试剂曲线 · 悬停查看数值"));

        const QRectF plotArea = plotAreaRect();
        if (plotArea.width() < 60.0 || plotArea.height() < 60.0) {
            return;
        }

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
                       QString::number(qRound(kYMax - (kYMax - kYMin) * ratio)));
            const qreal x = plotArea.left() + plotArea.width() * ratio;
            p.drawText(QRectF(x - 20.0, plotArea.bottom() + 4.0, 40.0, 14.0),
                       Qt::AlignHCenter | Qt::AlignTop,
                       QString::number(qRound(kXMax * ratio)));
        }

        // 特殊点刻度
        QFont specialFont(QStringLiteral("Microsoft YaHei"));
        specialFont.setPixelSize(9);
        p.setFont(specialFont);
        p.setPen(QColor(kMutedColor));
        for (int sec : { 60, 180, 300 }) {
            const qreal x = plotArea.left()
                    + (qreal(sec) / kXMax) * plotArea.width();
            p.drawLine(QPointF(x, plotArea.bottom() - 3.0),
                       QPointF(x, plotArea.bottom() + 1.0));
            p.drawText(QRectF(x - 20.0, plotArea.bottom() + 4.0, 40.0, 14.0),
                       Qt::AlignHCenter | Qt::AlignTop,
                       QStringLiteral("%1S").arg(sec));
        }

        bool anyVisible = false;
        for (const Series &s : m_series) {
            if (!s.visible || s.points.isEmpty()) {
                continue;
            }
            anyVisible = true;

            QPainterPath path;
            bool first = true;
            int firstMapped = -1;
            for (const QPointF &pt : s.points) {
                const QPointF m = mapPoint(plotArea, pt);
                if (firstMapped < 0) {
                    firstMapped = (pt.x() >= 1.0) ? qRound(pt.x() - 1.0) : 0;
                }
                if (first) {
                    path.moveTo(m);
                    first = false;
                } else {
                    path.lineTo(m);
                }
            }
            if (path.isEmpty()) {
                continue;
            }

            // —— AUC 面积填充（0~60s 梯形区域）——
            const double auc = areaUnderCurve(s.points);
            if (auc > 0.0) {
                QPolygonF fill;
                const int upto = qMin(60, s.points.size() - 1);
                for (int k = 0; k <= upto; ++k) {
                    fill << mapPoint(plotArea, s.points.at(k));
                }
                const qreal x0 = mapPoint(plotArea, s.points.first()).x();
                const qreal x1 = mapPoint(plotArea, s.points.at(upto)).x();
                fill << QPointF(x1, plotArea.bottom());
                fill << QPointF(x0, plotArea.bottom());
                p.save();
                p.setPen(Qt::NoPen);
                QColor fillColor = s.color;
                fillColor.setAlpha(34);
                p.setBrush(fillColor);
                p.drawPolygon(fill);
                // AUC 标注
                QFont aucFont(QStringLiteral("Microsoft YaHei"));
                aucFont.setPixelSize(10);
                aucFont.setBold(true);
                p.setFont(aucFont);
                p.setPen(s.color);
                const QPointF labelPos = mapPoint(
                            plotArea, s.points.at(qMin(30, s.points.size() - 1)));
                p.drawText(QRectF(labelPos.x() - 70.0, labelPos.y() - 34.0,
                                  140.0, 16.0),
                           Qt::AlignHCenter | Qt::AlignVCenter,
                           tr("AUC %1").arg(QString::number(auc, 'f', 1)));
                p.restore();
            }

            // —— 曲线 ——
            p.save();
            p.setPen(QPen(s.color, 2.0, Qt::SolidLine, Qt::RoundCap,
                          Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);

            // —— 最大斜率：相邻点最大上升段，两端延伸成切线 ——
            double maxSlope = 0.0;
            int slopeAt = -1;
            for (int k = 0; k + 1 < s.points.size(); ++k) {
                const double dx = s.points.at(k + 1).x() - s.points.at(k).x();
                const double dy = s.points.at(k + 1).y() - s.points.at(k).y();
                if (dx <= 0.0) {
                    continue;
                }
                const double sl = dy / dx;
                if (sl > maxSlope) {
                    maxSlope = sl;
                    slopeAt = k;
                }
            }
            if (slopeAt >= 0) {
                const QPointF a = mapPoint(plotArea, s.points.at(slopeAt));
                const QPointF b = mapPoint(plotArea, s.points.at(slopeAt + 1));
                QLineF seg(a, b);
                if (seg.length() > 0.01) {
                    const QPointF dir = (b - a) / seg.length();
                    const qreal ext = 26.0;
                    const QPointF p0 = a - dir * ext;
                    const QPointF p1 = b + dir * ext;
                    QPen slopePen(s.color, 1.6, Qt::SolidLine,
                                  Qt::RoundCap, Qt::RoundJoin);
                    slopePen.setColor(s.color);
                    p.setPen(slopePen);
                    p.drawLine(p0, p1);
                    // 切线两端小圆
                    p.setBrush(s.color);
                    p.setPen(Qt::NoPen);
                    p.drawEllipse(p0, 2.2, 2.2);
                    QFont slopeFont(QStringLiteral("Microsoft YaHei"));
                    slopeFont.setPixelSize(10);
                    slopeFont.setBold(true);
                    p.setFont(slopeFont);
                    p.setPen(s.color);
                    const QPointF mid = QPointF((a.x() + b.x()) * 0.5,
                                                (a.y() + b.y()) * 0.5);
                    p.drawText(QRectF(mid.x() + 8.0, mid.y() - 40.0, 150.0, 16.0),
                               Qt::AlignLeft | Qt::AlignVCenter,
                               tr("最大斜率 %1%/s")
                               .arg(QString::number(maxSlope, 'f', 2)));
                }
            }
            p.setPen(QPen(s.color, 2.0, Qt::SolidLine, Qt::RoundCap,
                          Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);

            // —— 特殊点 60/180/300/Max 原点标记 ——
            for (int sec : { 60, 180, 300 }) {
                const int idx = sec - 1;
                if (idx >= 0 && idx < s.points.size()) {
                    const QPointF m = mapPoint(plotArea, s.points.at(idx));
                    p.setPen(QPen(s.color, 1.6));
                    p.setBrush(Qt::white);
                    p.drawEllipse(m, 4.5, 4.5);
                    p.setBrush(s.color);
                    p.setPen(Qt::NoPen);
                    p.drawEllipse(m, 1.8, 1.8);
                }
            }
            // Max = 最后一点
            const QPointF mLast = mapPoint(plotArea, s.points.last());
            p.setPen(QPen(s.color, 1.6));
            p.setBrush(Qt::white);
            p.drawEllipse(mLast, 4.5, 4.5);
            p.setBrush(s.color);
            p.setPen(Qt::NoPen);
            p.drawEllipse(mLast, 1.8, 1.8);
            p.restore();
        }

        if (!anyVisible) {
            QFont ef(QStringLiteral("Microsoft YaHei"));
            ef.setPixelSize(13);
            p.setFont(ef);
            p.setPen(QColor("#A5B6C4"));
            p.drawText(plotArea, Qt::AlignCenter, tr("该样本暂无曲线数据"));
        }

        // —— 悬停数值工具提示 ——
        if (m_hoverSeries >= 0 && m_hoverSeries < m_series.size()
                && m_hoverIndex >= 0) {
            const Series &s = m_series.at(m_hoverSeries);
            if (s.visible && m_hoverIndex < s.points.size()) {
                const QPointF pt = s.points.at(m_hoverIndex);
                const QPointF m = mapPoint(plotArea, pt);
                const QString text = tr("%1\n%2 s: %3%")
                        .arg(s.name)
                        .arg(QString::number(pt.x(), 'f', 0))
                        .arg(QString::number(pt.y(), 'f', 1));
                QFont tipFont(QStringLiteral("Microsoft YaHei"));
                tipFont.setPixelSize(11);
                const QFontMetrics fm(tipFont);
                const QStringList lines = text.split(QLatin1Char('\n'));
                int w = 0;
                for (const QString &line : lines) {
                    w = qMax(w, fm.width(line));
                }
                const int lh = 16;
                const int h = lines.size() * lh;
                const int padX = 8;
                const int padY = 5;
                const QRectF box(m.x() + 12.0, m.y() - h - 22.0,
                                 w + padX * 2, h + padY * 2);
                QRectF clamped = box;
                clamped.moveLeft(qBound(plotArea.left(), clamped.left(),
                                        plotArea.right() - clamped.width()));
                clamped.moveTop(qBound(plotArea.top(), clamped.top(),
                                       plotArea.bottom() - clamped.height()));
                p.save();
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(24, 36, 48, 225));
                p.drawRoundedRect(clamped, 6.0, 6.0);
                p.setFont(tipFont);
                p.setPen(Qt::white);
                for (int i = 0; i < lines.size(); ++i) {
                    p.drawText(QRectF(clamped.left() + padX,
                                      clamped.top() + padY + i * lh,
                                      w, lh),
                               Qt::AlignLeft | Qt::AlignVCenter,
                               lines.at(i));
                }
                p.restore();
            }
        }
    }

private:
    QVector<Series> m_series;
    int m_hoverSeries = -1;
    int m_hoverIndex = -1;
};

constexpr double CurveDisplayWidget::CurvePlotPanel::kXMax;
constexpr double CurveDisplayWidget::CurvePlotPanel::kYMin;
constexpr double CurveDisplayWidget::CurvePlotPanel::kYMax;

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
        "  padding: 8px 6px; min-height: 20px;"
        "  font-family: 'Microsoft YaHei'; font-size: 13px; font-weight: 600;"
        "}"
        "QPushButton#curveRefreshButton {"
        "  background: #FFFFFF; color: #1F4E79;"
        "  border: 1px solid #A9C8DF; border-radius: 6px;"
        "  padding: 6px 16px;"
        "  font-family: 'Microsoft YaHei'; font-size: 12px;"
        "}"
        "QPushButton#curveRefreshButton:hover { background: #E8F1FA; }"
        "QPushButton#curveRefreshButton:pressed { background: #D3E6F5; }"
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
    refreshBtn->setObjectName(QStringLiteral("curveRefreshButton"));
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
    auto *hheader = m_table->horizontalHeader();
    hheader->setStretchLastSection(true);
    hheader->setMinimumHeight(34);
    hheader->setDefaultSectionSize(96);
    for (int c = 0; c < 12; ++c) {
        if (c >= 1 && c <= 4) {
            // 60S/180S/300S/Max 数值列加宽，确保聚集率读数舒展。
            hheader->setSectionResizeMode(c, QHeaderView::Fixed);
            m_table->setColumnWidth(c, 110);
        } else {
            hheader->setSectionResizeMode(c, QHeaderView::Interactive);
            m_table->setColumnWidth(c, 84);
        }
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