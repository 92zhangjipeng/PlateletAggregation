#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "sampledetailpanel.h"

#include "referenceinterval.h"

#include <cmath>
#include <QAbstractAnimation>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDebug>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// 面板配色（与主界面其余卡片统一）
const char* const kPanelBg    = "#F4F8FC";
const char* const kCardBg     = "#FFFFFF";
const char* const kCardBorder = "#DCE7F1";
const char* const kTitleColor = "#1F4E79";
const char* const kTextColor  = "#3A5065";
const char* const kMutedColor = "#7A8FA3";
const char* const kTestColor  = "#2F93E0";   // 测试曲线（AD 原始值）
const char* const kAggColor   = "#22A877";   // 集聚曲线（%）
const char* const kBadColor   = "#DF5A50";

// 面板上下留白：顶部留一点边距，底部避开操作条(96)+状态栏(34)，
// 避免详情面板盖住右下角的开始按钮和状态信息。
constexpr int kPanelTopInset = 12;
constexpr int kPanelBottomInset = 130;

/** 试剂字符串 -> 显示名（AA/ADP/EPI/COL/RIS）。 */
QString reagentDisplay(const QString &reagent)
{
    return reagent.trimmed().isEmpty()
            ? QStringLiteral("试剂") : reagent.trimmed().toUpper();
}

/** 试剂字符串 -> ReferenceInterval 下标（AA0/ADP1/EPI2/COL3/RIS4）。 */
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
    // 哨兵值（时间点越界/缺失）显示 --；其余真实值（含负数）直接显示。
    if (value <= kSentinelMissing / 2.0) {
        return QStringLiteral("--");
    }
    return QString::number(value, 'f', 1) + QStringLiteral("%");
}

// —— 状态徽标 ——
struct StatusSpec {
    QString text;
    QString color;
};
StatusSpec reagentStatusSpec(const QString &status)
{
    if (status == QStringLiteral("测试中")) {
        return { status, QStringLiteral("#2F93E0") };
    }
    if (status == QStringLiteral("测试结束")) {
        return { QStringLiteral("完成"), QStringLiteral("#22A877") };
    }
    if (status == QStringLiteral("完成")) {
        return { status, QStringLiteral("#22A877") };
    }
    if (status == QStringLiteral("异常结果")) {
        return { status, QStringLiteral("#DF5A50") };
    }
    // 无曲线 / 状态未知：待测
    return { QStringLiteral("待测"), QStringLiteral("#B7791F") };
}

QString makeStatusLabelStyle(const QString &color)
{
    return QStringLiteral(
        "background: %1; color: #FFFFFF; border: none; border-radius: 9px;"
        " padding: 2px 10px; font-family: 'Microsoft YaHei';"
        " font-size: 11px; font-weight: 600;").arg(color);
}

void applyStatusToLabel(QLabel *label, const QString &status)
{
    if (!label) {
        return;
    }
    const StatusSpec spec = reagentStatusSpec(status);
    label->setText(spec.text);
    label->setStyleSheet(makeStatusLabelStyle(spec.color));
}

/**
 * 紧凑只读曲线控件：一张卡片内绘制测试曲线（AD 原始值）或集聚曲线（%）。
 * 绘图逻辑精简自 testcurvewidget.cpp 的 CurvePlotWidget。
 */
class CompactCurveWidget : public QWidget
{
public:
    enum class Mode { Test = 0, Aggregation = 1 };

    explicit CompactCurveWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(150);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setTestCurve(const QVector<int> &prp)
    {
        m_testPoints.clear();
        m_testPoints.reserve(prp.size());
        for (int i = 0; i < prp.size(); ++i) {
            m_testPoints.append(QPointF(double(i + 1), double(prp.at(i))));
        }
    }

    void setAggregationCurve(const QVector<double> &agg)
    {
        m_aggPoints.clear();
        m_aggPoints.reserve(agg.size());
        for (int i = 0; i < agg.size(); ++i) {
            m_aggPoints.append(QPointF(double(i + 1), agg.at(i)));
        }
    }

    void setMode(Mode mode)
    {
        if (m_mode == mode) {
            return;
        }
        m_mode = mode;
        update();
    }

    Mode mode() const { return m_mode; }

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

        const QVector<QPointF> &data = m_mode == Mode::Aggregation
                ? m_aggPoints : m_testPoints;
        const QColor lineColor = m_mode == Mode::Aggregation
                ? QColor(kAggColor) : QColor(kTestColor);
        const QString modeTitle = m_mode == Mode::Aggregation
                ? tr("集聚曲线") : tr("测试曲线");
        const QString modeSubtitle = m_mode == Mode::Aggregation
                ? tr("聚集百分比") : tr("AD 原始值");

        QFont titleFont(QStringLiteral("Microsoft YaHei"));
        titleFont.setPixelSize(12);
        titleFont.setBold(true);
        const QFontMetrics titleMetrics(titleFont);
        p.setFont(titleFont);
        p.setPen(QColor(kTitleColor));
        p.drawText(QRectF(card.left() + 14.0, card.top() + 8.0, 120.0, 20.0),
                   Qt::AlignLeft | Qt::AlignVCenter, modeTitle);

        QFont subFont(QStringLiteral("Microsoft YaHei"));
        subFont.setPixelSize(9);
        p.setFont(subFont);
        p.setPen(QColor(kMutedColor));
        p.drawText(QRectF(card.left() + 14.0 + titleMetrics.width(modeTitle) + 4.0,
                          card.top() + 8.0, 140.0, 20.0),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("· ") + modeSubtitle);

        const QRectF plotArea = card.adjusted(52.0, 32.0, -18.0, -34.0);
        if (plotArea.width() < 60.0 || plotArea.height() < 60.0) {
            return;
        }

        double xMax = 300.0;
        if (!data.isEmpty()) {
            xMax = qMax(60.0, data.last().x());
            xMax = std::ceil(xMax / 60.0) * 60.0;
        }

        // 集聚曲线 Y 固定 [-20,100]；测试曲线按 AD 数据自适应。
        double yMin = -20.0;
        double yMax = 100.0;
        if (m_mode == Mode::Aggregation) {
            // 固定范围；聚集率理论上 0~100，留负区间便于观察过冲。
        } else {
            if (data.isEmpty()) {
                yMax = 4095.0;
            } else {
                double minV = data.first().y();
                double maxV = data.first().y();
                for (const QPointF &pt : data) {
                    minV = qMin(minV, pt.y());
                    maxV = qMax(maxV, pt.y());
                }
                if (maxV - minV < 100.0) {
                    const double mid = (maxV + minV) * 0.5;
                    minV = mid - 50.0;
                    maxV = mid + 50.0;
                }
                const double pad = (maxV - minV) * 0.12;
                yMin = qMax(0.0, std::floor((minV - pad) / 100.0) * 100.0);
                yMax = qMin(4095.0, std::ceil((maxV + pad) / 100.0) * 100.0);
                if (yMax - yMin < 200.0) {
                    yMax = qMin(4095.0, yMin + 200.0);
                }
            }
        }
        if (yMax <= yMin) {
            yMax = yMin + 1.0;
        }

        QFont axisFont(QStringLiteral("Microsoft YaHei"));
        axisFont.setPixelSize(8);

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

        p.setFont(axisFont);
        p.setPen(QColor("#7A8FA3"));
        for (int i = 0; i <= kDiv; ++i) {
            const qreal ratio = qreal(i) / kDiv;
            const qreal y = plotArea.top() + plotArea.height() * ratio;
            const double value = yMax - (yMax - yMin) * ratio;
            p.drawText(QRectF(plotArea.left() - 46.0, y - 7.0, 40.0, 14.0),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(qRound(value)));
        }
        for (int i = 0; i <= kDiv; ++i) {
            const qreal ratio = qreal(i) / kDiv;
            const qreal x = plotArea.left() + plotArea.width() * ratio;
            const double value = xMax * ratio;
            p.drawText(QRectF(x - 20.0, plotArea.bottom() + 4.0, 40.0, 14.0),
                       Qt::AlignHCenter | Qt::AlignTop,
                       QString::number(qRound(value)));
        }

        const auto mapPoint = [&plotArea, xMax, yMin, yMax](const QPointF &pt) {
            const double x = qBound(0.0, pt.x(), xMax);
            const double y = qBound(yMin, pt.y(), yMax);
            return QPointF(plotArea.left() + (x / xMax) * plotArea.width(),
                           plotArea.bottom()
                           - ((y - yMin) / (yMax - yMin)) * plotArea.height());
        };

        if (data.isEmpty()) {
            QFont ef(QStringLiteral("Microsoft YaHei"));
            ef.setPixelSize(12);
            p.setFont(ef);
            p.setPen(QColor("#A5B6C4"));
            p.drawText(plotArea, Qt::AlignCenter, tr("暂无曲线数据"));
            return;
        }

        p.save();
        p.setClipRect(plotArea.adjusted(1.0, 1.0, -1.0, -1.0));

        QPainterPath line;
        line.moveTo(mapPoint(data.first()));
        for (int i = 1; i < data.size(); ++i) {
            line.lineTo(mapPoint(data.at(i)));
        }

        QPainterPath fill = line;
        const QPointF first = mapPoint(data.first());
        const QPointF last = mapPoint(data.last());
        fill.lineTo(last.x(), plotArea.bottom());
        fill.lineTo(first.x(), plotArea.bottom());
        fill.closeSubpath();

        QColor top = lineColor;
        top.setAlpha(70);
        QColor bottom = lineColor;
        bottom.setAlpha(4);
        QLinearGradient grad(0.0, plotArea.top(), 0.0, plotArea.bottom());
        grad.setColorAt(0.0, top);
        grad.setColorAt(1.0, bottom);
        p.setPen(Qt::NoPen);
        p.setBrush(grad);
        p.drawPath(fill);

        QPen linePen(lineColor);
        linePen.setWidthF(2.0);
        linePen.setCapStyle(Qt::RoundCap);
        linePen.setJoinStyle(Qt::RoundJoin);
        p.setPen(linePen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(line);

        p.setPen(QPen(QColor(255, 255, 255, 235), 1.6));
        p.setBrush(lineColor);
        p.drawEllipse(last, 3.6, 3.6);

        p.restore();
    }

private:
    QVector<QPointF> m_testPoints;
    QVector<QPointF> m_aggPoints;
    Mode m_mode = Mode::Aggregation;
};

} // namespace

SampleDetailPanel::SampleDetailPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("sampleDetailPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "QWidget#sampleDetailPanel { background: %1; }"
        "QFrame#detailTitleBar {"
        "  background: %1; border: none; border-bottom: 1px solid %5;"
        "}"
        "QLabel#detailTitle {"
        "  color: %2; font-family: 'Microsoft YaHei';"
        "  font-size: 17px; font-weight: 700;"
        "}"
        "QLabel#detailEntry {"
        "  color: %3; font-family: 'Microsoft YaHei'; font-size: 13px;"
        "  line-height: 1.5;"
        "}"
        "QPushButton#detailClose {"
        "  background: %2; color: #FFFFFF; border: none; border-radius: 6px;"
        "  padding: 7px 22px; font-family: 'Microsoft YaHei'; font-size: 13px;"
        "}"
        "QPushButton#detailClose:hover { background: #2A6DA8; }"
        "QScrollArea#detailScroll { background: transparent; border: none; }"
        "QScrollArea#detailScroll > QWidget > QWidget { background: transparent; }"
        "QFrame#detailReagentCard {"
        "  background: %4; border: 1px solid %5; border-radius: 10px;"
        "}"
        "QFrame#reagentHeaderRow {"
        "  background: %4; border: none; border-bottom: 1px solid #EDF3F9;"
        "  border-radius: 0px;"
        "}"
        "QToolButton#reagentToggle {"
        "  border: none; background: transparent;"
        "  color: %2; font-family: 'Microsoft YaHei'; font-size: 14px;"
        "  font-weight: 600; text-align: left; padding: 5px 4px;"
        "}"
        "QToolButton#reagentToggle:hover { color: #2A6DA8; }"
        "QLabel#reagentName {"
        "  color: %2; font-family: 'Microsoft YaHei';"
        "  font-size: 14px; font-weight: 700;"
        "}"
        "QLabel#reagentSub {"
        "  color: %6; font-family: 'Microsoft YaHei'; font-size: 11px;"
        "}"
        "QLabel#reagentSummary {"
        "  color: %3; font-family: 'Microsoft YaHei'; font-size: 12px;"
        "}"
        "QLabel#paramsCaption {"
        "  color: %6; font-family: 'Microsoft YaHei'; font-size: 11px;"
        "}"
        "QLabel#paramsValue {"
        "  color: %3; font-family: 'Microsoft YaHei'; font-size: 15px;"
        "  font-weight: 600;"
        "}"
        "QLabel#paramsRange {"
        "  color: %6; font-family: 'Microsoft YaHei'; font-size: 9px;"
        "}"
        "QCheckBox {"
        "  color: %2; font-family: 'Microsoft YaHei';"
        "  font-size: 12px; spacing: 6px;"
        "}")
        .arg(QLatin1String(kPanelBg), QLatin1String(kTitleColor),
             QLatin1String(kTextColor), QLatin1String(kCardBg),
             QLatin1String(kCardBorder), QLatin1String(kMutedColor)));

    // 面板固定宽，height 随父级；默认定位在父级右侧外并隐藏。
    setFixedWidth(520);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 顶部标题条
    auto *titleBar = new QFrame(this);
    titleBar->setObjectName(QStringLiteral("detailTitleBar"));
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(20, 14, 20, 14);
    auto *title = new QLabel(tr("样本详情"), titleBar);
    title->setObjectName(QStringLiteral("detailTitle"));
    auto *closeButton = new QPushButton(tr("关闭"), titleBar);
    closeButton->setObjectName(QStringLiteral("detailClose"));
    closeButton->setCursor(Qt::PointingHandCursor);
    connect(closeButton, &QPushButton::clicked, this,
            &SampleDetailPanel::popOut);
    titleLayout->addWidget(title);
    titleLayout->addStretch(1);
    titleLayout->addWidget(closeButton);
    root->addWidget(titleBar);

    // 可滚动内容
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("detailScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    root->addWidget(m_scroll, 1);

    auto *content = new QWidget(m_scroll);
    m_cardLayout = new QVBoxLayout(content);
    m_cardLayout->setContentsMargins(16, 14, 16, 18);
    m_cardLayout->setSpacing(10);
    m_cardLayout->addStretch(1);
    m_scroll->setWidget(content);

    m_slideAnimation = new QPropertyAnimation(this, "geometry", this);
    m_slideAnimation->setEasingCurve(QEasingCurve::OutCubic);

    hide();
    if (parent) {
        // 初始：定位在父级右侧外、顶部留出标题高度（不遮挡主页顶部按钮区）。
        setGeometry(QRect(parent->rect().right() + 1, kPanelTopInset,
                          width(), qMax(0, parent->height() - kPanelTopInset - kPanelBottomInset)));
    }
}

SampleDetailPanel::~SampleDetailPanel() = default;

void SampleDetailPanel::setStore(TestDataStore *store)
{
    m_store = store;
}

bool SampleDetailPanel::isPanelVisible() const
{
    return isVisible();
}

QWidget *SampleDetailPanel::currentParentWidget() const
{
    return parentWidget();
}

void SampleDetailPanel::popOut()
{
    QWidget *parent = currentParentWidget();
    if (!parent) {
        hide();
        return;
    }
    disconnect(m_slideAnimation, nullptr, this, nullptr);
    connect(m_slideAnimation, &QPropertyAnimation::finished, this, [this]() {
        m_animating = false;
        hide();
        if (parentWidget()) {
            raise();
        }
    });
    m_animating = true;
    m_slideAnimation->setDuration(200);
    m_slideAnimation->setStartValue(geometry());
    m_slideAnimation->setEndValue(QRect(parent->width(), kPanelTopInset,
                                        width(),
                                        qMax(0, parent->height() - kPanelTopInset - kPanelBottomInset)));
    m_slideAnimation->start();
}

void SampleDetailPanel::showSample(qint64 sampleId)
{
    if (!m_store || !m_store->isOpen()) {
        return;
    }
    SampleTestData sample;
    QString error;
    if (!m_store->loadSample(sampleId, &sample, &error)) {
        qWarning() << "读取样本详情失败:" << sampleId << error;
        return;
    }
    m_currentSampleId = sampleId;

    clearTabs();
    m_cardLayout->insertWidget(0, buildHeaderCard(sample));
    for (const TestCurveData &test : sample.tests) {
        // 全部默认折叠：每个试剂只占一行，5 个试剂一屏即可看全，
        // 点击试剂行展开该试剂曲线与完整参数。
        m_cardLayout->insertWidget(m_cardLayout->count() - 1,
                                   buildReagentCard(test, sample));
    }

    QWidget *parent = currentParentWidget();
    if (!parent) {
        return;
    }
    if (isVisible()) {
        setGeometry(QRect(parent->width() - this->width(), kPanelTopInset,
                          this->width(),
                          qMax(0, parent->height() - kPanelTopInset - kPanelBottomInset)));
        raise();
        update();
        return;
    }
    startPopIn();
}

void SampleDetailPanel::startPopIn()
{
    QWidget *parent = currentParentWidget();
    if (!parent) {
        show();
        return;
    }
    const int h = qMax(0, parent->height() - kPanelTopInset - kPanelBottomInset);
    const QRect start(parent->width(), kPanelTopInset, width(), h);
    const QRect end(parent->width() - width(), kPanelTopInset, width(), h);

    disconnect(m_slideAnimation, nullptr, this, nullptr);
    connect(m_slideAnimation, &QPropertyAnimation::finished, this, [this]() {
        m_animating = false;
        raise();
    });
    m_animating = true;
    setGeometry(start);
    show();
    raise();
    m_slideAnimation->setDuration(220);
    m_slideAnimation->setStartValue(start);
    m_slideAnimation->setEndValue(end);
    m_slideAnimation->start();
}

void SampleDetailPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    QWidget *parent = currentParentWidget();
    if (!parent || m_animating || !isVisible()) {
        return;
    }
    const QRect target(parent->width() - this->width(), kPanelTopInset,
                       this->width(),
                       qMax(0, parent->height() - kPanelTopInset - kPanelBottomInset));
    if (geometry() != target) {
        setGeometry(target);
    }
}

void SampleDetailPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    QWidget *parent = currentParentWidget();
    if (parent && m_slideAnimation->state() != QAbstractAnimation::Running) {
        raise();
    }
}

void SampleDetailPanel::clearTabs()
{
    while (m_cardLayout->count() > 1) {
        QLayoutItem *item = m_cardLayout->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
}

QWidget *SampleDetailPanel::buildHeaderCard(const SampleTestData &sample)
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("detailReagentCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(6);

    const QString number = sample.sampleNumber.isEmpty()
            ? QStringLiteral("--") : sample.sampleNumber;
    auto *title = new QLabel(tr("样本 %1").arg(number), card);
    title->setObjectName(QStringLiteral("reagentName"));

    const QString name = sample.patientName.isEmpty()
            ? QStringLiteral("--") : sample.patientName;
    const QString age = sample.age >= 0
            ? QString::number(sample.age) : QStringLiteral("--");
    const QString gender = sample.gender.isEmpty()
            ? QStringLiteral("--") : sample.gender;
    const QString type = sample.sampleType.isEmpty()
            ? QStringLiteral("--") : sample.sampleType;
    const QString time = sample.testTime.isValid()
            ? sample.testTime.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
            : QStringLiteral("--");
    const QString ppp = sample.pppValue >= 0
            ? QString::number(sample.pppValue) : QStringLiteral("--");

    auto *info = new QLabel(
                tr("姓名 %1   年龄 %2   性别 %3   类型 %4\n"
                   "检测时间 %5   PPP基准 %6")
                .arg(name, age, gender, type, time, ppp), card);
    info->setObjectName(QStringLiteral("detailEntry"));

    layout->addWidget(title);
    layout->addWidget(info);
    return card;
}

SampleDetailPanel::ReagentParams
SampleDetailPanel::computeParams(const TestCurveData &test)
{
    ReagentParams params;
    params.reagentIndex = reagentReferenceIndex(test.reagent);
    params.hasCurve = !test.prpPoints.isEmpty();
    params.hasAggregation = !test.aggregationPoints.isEmpty();
    params.status = test.status;

    const QVector<double> &agg = test.aggregationPoints;
    if (agg.isEmpty()) {
        return params;
    }
    const auto valueAt = [&agg](int second) {
        const int idx = second - 1;
        // 越界（<300 点）返回哨兵值表示缺失，不作为真实数值显示。
        return (idx >= 0 && idx < agg.size())
                ? agg.at(idx) : kSentinelMissing;
    };
    params.s60 = valueAt(60);
    params.s180 = valueAt(180);
    params.s300 = valueAt(300);

    double maxV = kSentinelMissing;
    int maxSec = -1;
    for (int i = 0; i < agg.size(); ++i) {
        if (agg.at(i) > maxV) {
            maxV = agg.at(i);
            maxSec = i + 1;
        }
    }
    params.maxPercent = maxV;
    params.maxSecond = maxSec;
    return params;
}

QWidget *SampleDetailPanel::buildReagentCard(const TestCurveData &test,
                                             const SampleTestData &sample)
{
    const ReagentParams params = computeParams(test);

    // —— 可折叠卡片 ——
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("detailReagentCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    // 折叠头：三角 + 试剂名 + 状态徽标 + ‖ 概要
    auto *header = new QFrame(card);
    header->setObjectName(QStringLiteral("reagentHeaderRow"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 8, 12, 8);
    headerLayout->setSpacing(8);

    auto *toggle = new QToolButton(header);
    toggle->setObjectName(QStringLiteral("reagentToggle"));
    toggle->setCheckable(true);          // checked = 展开
    toggle->setCursor(Qt::PointingHandCursor);
    toggle->setText(tr(" %1").arg(reagentDisplay(test.reagent)));
    toggle->setArrowType(Qt::RightArrow);

    auto *statusLabel = new QLabel(header);
    statusLabel->setObjectName(QStringLiteral("reagentStatus"));
    applyStatusToLabel(statusLabel, params.status);

    auto *summary = new QLabel(header);
    summary->setObjectName(QStringLiteral("reagentSummary"));
    if (params.hasAggregation) {
        summary->setText(tr("60S %1   180S %2   300S %3   MAX %4")
                         .arg(fmtPercent(params.s60),
                              fmtPercent(params.s180),
                              fmtPercent(params.s300),
                              fmtPercent(params.maxPercent)));
    } else if (params.status == QStringLiteral("测试中")) {
        summary->setText(tr("测试进行中…"));
    } else if (params.hasCurve) {
        summary->setText(tr("已完成"));
    } else {
        summary->setText(tr("无曲线数据"));
    }

    headerLayout->addWidget(toggle, 0, Qt::AlignVCenter);
    headerLayout->addWidget(statusLabel, 0, Qt::AlignVCenter);
    headerLayout->addWidget(summary, 1, Qt::AlignVCenter);
    cardLayout->addWidget(header);

    // 展开体：曲线 + 参数网格
    auto *body = new QWidget(card);
    body->setObjectName(QStringLiteral("reagentBody"));
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(14, 10, 14, 14);
    bodyLayout->setSpacing(10);

    // 通道信息
    if (test.moduleNumber > 0 && test.globalChannel > 0) {
        auto *sub = new QLabel(tr("通道：模组%1 · CH%2")
                               .arg(test.moduleNumber).arg(test.globalChannel), body);
        sub->setObjectName(QStringLiteral("reagentSub"));
        bodyLayout->addWidget(sub);
    }

    // 曲线
    auto *curve = new CompactCurveWidget(body);
    curve->setTestCurve(test.prpPoints);
    curve->setAggregationCurve(test.aggregationPoints);

    auto *curveRow = new QHBoxLayout;
    curveRow->setSpacing(18);
    auto *testCheck = new QCheckBox(tr("测试"), body);
    auto *aggCheck = new QCheckBox(tr("集聚"), body);
    testCheck->setCursor(Qt::PointingHandCursor);
    aggCheck->setCursor(Qt::PointingHandCursor);
    // 互斥：同一时刻只显示一条曲线（测试 / 集聚）。
    auto *modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addButton(testCheck, int(CompactCurveWidget::Mode::Test));
    modeGroup->addButton(aggCheck, int(CompactCurveWidget::Mode::Aggregation));
    aggCheck->setChecked(true);
    Q_UNUSED(modeGroup);
    connect(testCheck, &QCheckBox::toggled, this, [curve](bool on) {
        if (on) {
            curve->setMode(CompactCurveWidget::Mode::Test);
        }
    });
    connect(aggCheck, &QCheckBox::toggled, this, [curve](bool on) {
        if (on) {
            curve->setMode(CompactCurveWidget::Mode::Aggregation);
        }
    });
    curveRow->addWidget(testCheck);
    curveRow->addWidget(aggCheck);
    curveRow->addStretch(1);
    bodyLayout->addWidget(curve, 1);
    bodyLayout->addLayout(curveRow);

    // 参数网格：60S / 180S / 300S / MAX
    struct CellInfo {
        const char *name;
        double value;
    };
    const CellInfo cells[4] = {
        { "60S",   params.s60 },
        { "180S",  params.s180 },
        { "300S",  params.s300 },
        { "MAX",   params.maxPercent },
    };

    auto *paramGrid = new QGridLayout;
    paramGrid->setHorizontalSpacing(8);
    paramGrid->setVerticalSpacing(3);

    for (int col = 0; col < 4; ++col) {
        const int index = params.reagentIndex;
        QString rangeText = QStringLiteral("--");
        QColor valueColor(kTextColor);
        // 判定是否缺失：时间点越界才视为无效（哨兵值），其余含负值均有效。
        const bool valueValid = cells[col].value > kSentinelMissing / 2.0;
        if (params.reagentIndex >= 0 && valueValid) {
            const int tp = col == 0 ? int(ReferenceInterval::Timepoint::S60)
                    : col == 1 ? int(ReferenceInterval::Timepoint::S180)
                    : col == 2 ? int(ReferenceInterval::Timepoint::S300)
                    : int(ReferenceInterval::Timepoint::Max);
            const bool inRange = ReferenceInterval::contains(
                        index, ReferenceInterval::Timepoint(tp),
                        cells[col].value);
            if (!inRange) {
                valueColor = QColor(kBadColor);
            }
            if (tp == int(ReferenceInterval::Timepoint::Max)) {
                rangeText = tr("%1~%2%%")
                        .arg(ReferenceInterval::low(
                                 index, ReferenceInterval::Timepoint::Max))
                        .arg(ReferenceInterval::high(
                                 index, ReferenceInterval::Timepoint::Max));
            }
        }

        auto *cell = new QWidget(body);
        auto *cellLayout = new QVBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(1);

        auto *caption = new QLabel(QLatin1String(cells[col].name), cell);
        caption->setObjectName(QStringLiteral("paramsCaption"));
        auto *value = new QLabel(fmtPercent(cells[col].value), cell);
        value->setObjectName(QStringLiteral("paramsValue"));
        value->setStyleSheet(
                    QStringLiteral("color: %1;").arg(valueColor.name()));
        auto *range = new QLabel(rangeText, cell);
        range->setObjectName(QStringLiteral("paramsRange"));

        cellLayout->addWidget(caption);
        cellLayout->addWidget(value);
        cellLayout->addWidget(range);
        paramGrid->addWidget(cell, 0, col);
    }
    bodyLayout->addLayout(paramGrid);

    // 默认折叠
    body->setVisible(false);

    connect(toggle, &QToolButton::toggled, this,
            [toggle, body](bool expanded) {
        toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        body->setVisible(expanded);
    });

    cardLayout->addWidget(body);
    return card;
}