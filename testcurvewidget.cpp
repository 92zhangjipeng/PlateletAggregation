#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "testcurvewidget.h"

#include "medicalwidgets.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>

class CurvePlotWidget : public QWidget
{
public:
    explicit CurvePlotWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(260);
    }

    void setData(const QVector<QPointF> &testCurve,
                 const QVector<QPointF> &aggregationCurve,
                 bool aggregationMode)
    {
        m_testCurve = testCurve;
        m_aggregationCurve = aggregationCurve;
        m_aggregationMode = aggregationMode;
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
        p.setPen(QPen(QColor("#DCE7F1"), 1.0));
        p.setBrush(QColor("#FFFFFF"));
        p.drawRoundedRect(card, 12.0, 12.0);

        const QVector<QPointF> &data = m_aggregationMode
                ? m_aggregationCurve : m_testCurve;
        const QColor lineColor = m_aggregationMode
                ? QColor("#22A877") : QColor("#2F93E0");
        const QString modeTitle = m_aggregationMode
                ? tr("集聚曲线") : tr("测试曲线");
        const QString modeSubtitle = m_aggregationMode
                ? tr("聚集百分比") : tr("AD 原始值");

        QFont titleFont(QStringLiteral("Microsoft YaHei"));
        titleFont.setPixelSize(14);
        titleFont.setBold(true);
        p.setFont(titleFont);
        p.setPen(QColor("#1F4E79"));
        const QFontMetrics titleMetrics(titleFont);
        p.drawText(QRectF(card.left() + 20.0, card.top() + 10.0,
                          220.0, 24.0),
                   Qt::AlignLeft | Qt::AlignVCenter, modeTitle);

        QFont subtitleFont(QStringLiteral("Microsoft YaHei"));
        subtitleFont.setPixelSize(10);
        p.setFont(subtitleFont);
        p.setPen(QColor("#8AA0B1"));
        p.drawText(QRectF(card.left() + 24.0 + titleMetrics.width(modeTitle),
                          card.top() + 10.0, 180.0, 24.0),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("· ") + modeSubtitle);

        // 实时标记
        const QString liveText = tr("● 实时");
        QFont liveFont(QStringLiteral("Microsoft YaHei"));
        liveFont.setPixelSize(10);
        liveFont.setBold(true);
        const QFontMetrics liveMetrics(liveFont);
        const qreal liveWidth = liveMetrics.width(liveText) + 20.0;
        const QRectF livePill(card.right() - liveWidth - 18.0,
                              card.top() + 12.0, liveWidth, 20.0);
        QColor liveFill = lineColor;
        liveFill.setAlpha(28);
        QColor liveBorder = lineColor;
        liveBorder.setAlpha(90);
        p.setPen(QPen(liveBorder, 1.0));
        p.setBrush(liveFill);
        p.drawRoundedRect(livePill, 10.0, 10.0);
        p.setFont(liveFont);
        p.setPen(lineColor);
        p.drawText(livePill, Qt::AlignCenter, liveText);

        const QRectF plotArea = card.adjusted(64.0, 46.0, -24.0, -46.0);
        if (plotArea.width() < 80.0 || plotArea.height() < 80.0) {
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
        if (!m_aggregationMode) {
            if (data.isEmpty()) {
                yMin = 0.0;
                yMax = 4095.0;
            } else {
                double minValue = data.first().y();
                double maxValue = data.first().y();
                for (const QPointF &point : data) {
                    minValue = qMin(minValue, point.y());
                    maxValue = qMax(maxValue, point.y());
                }
                if (maxValue - minValue < 100.0) {
                    const double middle = (maxValue + minValue) * 0.5;
                    minValue = middle - 50.0;
                    maxValue = middle + 50.0;
                }
                const double padding = (maxValue - minValue) * 0.12;
                yMin = qMax(0.0, std::floor((minValue - padding) / 100.0) * 100.0);
                yMax = qMin(4095.0, std::ceil((maxValue + padding) / 100.0) * 100.0);
                if (yMax - yMin < 200.0) {
                    yMax = qMin(4095.0, yMin + 200.0);
                }
            }
        }
        if (yMax <= yMin) {
            yMax = yMin + 1.0;
        }

        QFont axisFont(QStringLiteral("Microsoft YaHei"));
        axisFont.setPixelSize(9);

        // 网格
        QPen gridPen(QColor("#E7F0F7"));
        gridPen.setStyle(Qt::DashLine);
        gridPen.setWidthF(1.0);
        p.setPen(gridPen);
        constexpr int kDivisions = 5;
        for (int i = 0; i <= kDivisions; ++i) {
            const qreal ratio = static_cast<qreal>(i) / kDivisions;
            const qreal x = plotArea.left() + plotArea.width() * ratio;
            const qreal y = plotArea.top() + plotArea.height() * ratio;
            p.drawLine(QPointF(x, plotArea.top()), QPointF(x, plotArea.bottom()));
            p.drawLine(QPointF(plotArea.left(), y), QPointF(plotArea.right(), y));
        }

        // 坐标轴
        p.setPen(QPen(QColor("#B8CDDC"), 1.2));
        p.drawLine(plotArea.bottomLeft(), plotArea.bottomRight());
        p.drawLine(plotArea.topLeft(), plotArea.bottomLeft());

        p.setFont(axisFont);
        p.setPen(QColor("#7A8FA3"));
        for (int i = 0; i <= kDivisions; ++i) {
            const qreal ratio = static_cast<qreal>(i) / kDivisions;
            const qreal y = plotArea.top() + plotArea.height() * ratio;
            const double value = yMax - (yMax - yMin) * ratio;
            const QString text = QString::number(qRound(value));
            p.drawText(QRectF(plotArea.left() - 56.0, y - 9.0, 48.0, 18.0),
                       Qt::AlignRight | Qt::AlignVCenter, text);
        }
        for (int i = 0; i <= kDivisions; ++i) {
            const qreal ratio = static_cast<qreal>(i) / kDivisions;
            const qreal x = plotArea.left() + plotArea.width() * ratio;
            const double value = xMax * ratio;
            const QString text = QString::number(qRound(value));
            p.drawText(QRectF(x - 28.0, plotArea.bottom() + 4.0, 56.0, 18.0),
                       Qt::AlignHCenter | Qt::AlignTop, text);
        }

        QFont axisTitleFont(QStringLiteral("Microsoft YaHei"));
        axisTitleFont.setPixelSize(10);
        p.setFont(axisTitleFont);
        p.setPen(QColor("#8AA0B1"));
        p.drawText(QRectF(plotArea.left(), plotArea.bottom() + 22.0,
                          plotArea.width(), 18.0),
                   Qt::AlignHCenter | Qt::AlignTop, tr("时间 (s)"));
        p.drawText(QRectF(plotArea.left() - 52.0, plotArea.top() - 24.0,
                          48.0, 18.0),
                   Qt::AlignRight | Qt::AlignVCenter,
                   m_aggregationMode ? QStringLiteral("%") : QStringLiteral("AD"));

        const auto mapPoint = [&plotArea, xMax, yMin, yMax](const QPointF &point) {
            const double xValue = qBound(0.0, point.x(), xMax);
            const double yValue = qBound(yMin, point.y(), yMax);
            const qreal x = plotArea.left()
                    + (xValue / xMax) * plotArea.width();
            const qreal y = plotArea.bottom()
                    - ((yValue - yMin) / (yMax - yMin)) * plotArea.height();
            return QPointF(x, y);
        };

        if (data.isEmpty()) {
            QFont emptyFont(QStringLiteral("Microsoft YaHei"));
            emptyFont.setPixelSize(14);
            p.setFont(emptyFont);
            p.setPen(QColor("#A5B6C4"));
            p.drawText(plotArea, Qt::AlignCenter, tr("等待测试数据…"));

            QFont hintFont(QStringLiteral("Microsoft YaHei"));
            hintFont.setPixelSize(10);
            p.setFont(hintFont);
            p.setPen(QColor("#C0CDD8"));
            p.drawText(plotArea.adjusted(0.0, 26.0, 0.0, 0.0),
                       Qt::AlignCenter,
                       tr("测试开始后每秒自动刷新"));
            return;
        }

        p.save();
        p.setClipRect(plotArea.adjusted(1.0, 1.0, -1.0, -1.0));

        QPainterPath linePath;
        linePath.moveTo(mapPoint(data.first()));
        for (int i = 1; i < data.size(); ++i) {
            linePath.lineTo(mapPoint(data.at(i)));
        }

        QPainterPath fillPath = linePath;
        const QPointF firstPoint = mapPoint(data.first());
        const QPointF lastPoint = mapPoint(data.last());
        fillPath.lineTo(lastPoint.x(), plotArea.bottom());
        fillPath.lineTo(firstPoint.x(), plotArea.bottom());
        fillPath.closeSubpath();

        QColor gradientTop = lineColor;
        gradientTop.setAlpha(80);
        QColor gradientBottom = lineColor;
        gradientBottom.setAlpha(5);
        QLinearGradient fillGradient(0.0, plotArea.top(),
                                     0.0, plotArea.bottom());
        fillGradient.setColorAt(0.0, gradientTop);
        fillGradient.setColorAt(1.0, gradientBottom);
        p.setPen(Qt::NoPen);
        p.setBrush(fillGradient);
        p.drawPath(fillPath);

        QPen linePen(lineColor);
        linePen.setWidthF(2.4);
        linePen.setCapStyle(Qt::RoundCap);
        linePen.setJoinStyle(Qt::RoundJoin);
        p.setPen(linePen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(linePath);

        p.setPen(QPen(QColor(255, 255, 255, 235), 2.0));
        p.setBrush(lineColor);
        p.drawEllipse(lastPoint, 4.8, 4.8);

        const QString valueText = m_aggregationMode
                ? QString::number(data.last().y(), 'f', 1) + QStringLiteral("%")
                : QString::number(qRound(data.last().y()));
        QFont valueFont(QStringLiteral("Microsoft YaHei"));
        valueFont.setPixelSize(9);
        valueFont.setBold(true);
        const QFontMetrics valueMetrics(valueFont);
        const qreal valueWidth = valueMetrics.width(valueText) + 12.0;
        const qreal valueHeight = valueMetrics.height() + 4.0;
        qreal valueX = lastPoint.x() + 8.0;
        qreal valueY = lastPoint.y() - valueHeight - 6.0;
        if (valueX + valueWidth > plotArea.right() - 2.0) {
            valueX = lastPoint.x() - valueWidth - 8.0;
        }
        if (valueY < plotArea.top() + 2.0) {
            valueY = lastPoint.y() + 8.0;
        }
        const QRectF valuePill(valueX, valueY, valueWidth, valueHeight);
        QColor valueFill = lineColor;
        valueFill.setAlpha(225);
        p.setPen(Qt::NoPen);
        p.setBrush(valueFill);
        p.drawRoundedRect(valuePill, valueHeight / 2.0, valueHeight / 2.0);
        p.setFont(valueFont);
        p.setPen(QColor("#FFFFFF"));
        p.drawText(valuePill, Qt::AlignCenter, valueText);

        p.restore();
    }

private:
    QVector<QPointF> m_testCurve;
    QVector<QPointF> m_aggregationCurve;
    bool m_aggregationMode = false;
};

TestCurveWidget::TestCurveWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("testCurveRoot"));
    setMinimumSize(720, 480);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(18, 16, 18, 14);
    rootLayout->setSpacing(12);

    auto *headerCard = new QFrame(this);
    headerCard->setObjectName(QStringLiteral("curveInfoCard"));
    auto *headerLayout = new QHBoxLayout(headerCard);
    headerLayout->setContentsMargins(18, 12, 18, 12);
    headerLayout->setSpacing(14);

    m_infoLabel = new QLabel(headerCard);
    m_infoLabel->setObjectName(QStringLiteral("curveInfoLabel"));
    m_infoLabel->setTextFormat(Qt::RichText);
    m_infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_channelLabel = new QLabel(headerCard);
    m_channelLabel->setObjectName(QStringLiteral("curveChannelLabel"));
    m_channelLabel->setAlignment(Qt::AlignCenter);

    headerLayout->addWidget(m_infoLabel, 1);
    headerLayout->addWidget(m_channelLabel, 0, Qt::AlignVCenter);
    rootLayout->addWidget(headerCard);

    m_plot = new CurvePlotWidget(this);
    m_plot->setObjectName(QStringLiteral("curvePlot"));
    rootLayout->addWidget(m_plot, 1);

    auto *bottomWidget = new QWidget(this);
    auto *bottomLayout = new QHBoxLayout(bottomWidget);
    bottomLayout->setContentsMargins(2, 0, 2, 0);
    bottomLayout->setSpacing(24);

    m_testCheck = new MedicalCheckBox(tr("测试曲线"), bottomWidget);
    m_aggregationCheck = new MedicalCheckBox(tr("集聚曲线"), bottomWidget);
    m_testCheck->setCursor(Qt::PointingHandCursor);
    m_aggregationCheck->setCursor(Qt::PointingHandCursor);

    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->setExclusive(true);
    m_modeGroup->addButton(m_testCheck, int(CurveMode::TestCurve));
    m_modeGroup->addButton(m_aggregationCheck, int(CurveMode::AggregationCurve));

    bottomLayout->addWidget(m_testCheck);
    bottomLayout->addWidget(m_aggregationCheck);
    bottomLayout->addStretch(1);

    m_dataStateLabel = new QLabel(bottomWidget);
    m_dataStateLabel->setObjectName(QStringLiteral("curveDataState"));
    bottomLayout->addWidget(m_dataStateLabel);
    rootLayout->addWidget(bottomWidget);

    m_testCheck->setChecked(true);

    connect(m_testCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            setCurveMode(CurveMode::TestCurve);
        }
    });
    connect(m_aggregationCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            setCurveMode(CurveMode::AggregationCurve);
        }
    });

    setStyleSheet(QStringLiteral(
        "QWidget#testCurveRoot { background: #F4F8FC; }"
        "QFrame#curveInfoCard {"
        "  background: #FFFFFF;"
        "  border: 1px solid #DCE7F1;"
        "  border-radius: 10px;"
        "}"
        "QLabel#curveInfoLabel {"
        "  color: #3A5065;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "}"
        "QLabel#curveChannelLabel {"
        "  color: #1F4E79;"
        "  background: #E8F3FB;"
        "  border: 1px solid #B9D5E8;"
        "  border-radius: 11px;"
        "  padding: 4px 12px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QLabel#curveDataState {"
        "  color: #8296A9;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QCheckBox {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "  spacing: 8px;"
        "}"));

    updateHeaderText();
    updateDataState();
}

void TestCurveWidget::setChannel(int moduleNumber, int globalChannel)
{
    m_moduleNumber = moduleNumber;
    m_globalChannel = globalChannel;
    if (m_channelLabel) {
        m_channelLabel->setText(
                    (m_moduleNumber > 0 && m_globalChannel > 0)
                    ? tr("模组%1 · CH%2").arg(m_moduleNumber).arg(m_globalChannel)
                    : QStringLiteral("--"));
    }
}

void TestCurveWidget::setHeaderInfo(const QString &sampleNumber,
                                    const QString &reagentName,
                                    int elapsedSeconds,
                                    int prpValue)
{
    m_sampleNumber = sampleNumber;
    m_reagentName = reagentName;
    m_elapsedSeconds = elapsedSeconds;
    m_prpValue = prpValue;
    updateHeaderText();
}

void TestCurveWidget::setCurves(const QVector<QPointF> &testCurve,
                                const QVector<QPointF> &aggregationCurve)
{
    m_testCurve = testCurve;
    m_aggregationCurve = aggregationCurve;
    refreshPlot();
    updateDataState();
}

void TestCurveWidget::appendPoint(double seconds, double testValue,
                                  double aggregationValue)
{
    if (testValue >= 0.0) {
        m_testCurve.append(QPointF(seconds, testValue));
    }
    if (aggregationValue >= 0.0) {
        m_aggregationCurve.append(QPointF(seconds, aggregationValue));
    }
    refreshPlot();
    updateDataState();
}

void TestCurveWidget::clearCurves()
{
    m_testCurve.clear();
    m_aggregationCurve.clear();
    refreshPlot();
    updateDataState();
}

void TestCurveWidget::setCurveMode(CurveMode mode)
{
    if (m_mode == mode) {
        updateModeChecks();
        refreshPlot();
        return;
    }
    m_mode = mode;
    updateModeChecks();
    refreshPlot();
    emit curveModeChanged(m_mode);
}

TestCurveWidget::CurveMode TestCurveWidget::curveMode() const
{
    return m_mode;
}

void TestCurveWidget::updateHeaderText()
{
    if (!m_infoLabel) {
        return;
    }

    const QString sample = m_sampleNumber.trimmed().isEmpty()
            ? QStringLiteral("--")
            : m_sampleNumber.trimmed().toHtmlEscaped();
    const QString reagent = m_reagentName.trimmed().isEmpty()
            ? QStringLiteral("--")
            : m_reagentName.trimmed().toHtmlEscaped();
    const QString elapsed = QString::number(qMax(0, m_elapsedSeconds));
    const QString prpValue = m_prpValue >= 0
            ? QString::number(m_prpValue) : QStringLiteral("--");

    const QString text = QStringLiteral(
                "<span style='color:#7A8FA3;'>样本</span>"
                "<span style='color:#1F4E79;font-weight:700;font-size:16px;'>%1</span>"
                "&nbsp;&nbsp;"
                "<span style='color:#2F93E0;font-weight:700;'>%2</span>"
                "&nbsp;&nbsp;"
                "<span style='color:#7A8FA3;'>%3S</span>"
                "&nbsp;&nbsp;"
                "<span style='color:#1F4E79;font-weight:600;'>PRP=%4</span>")
            .arg(sample)
            .arg(reagent)
            .arg(elapsed)
            .arg(prpValue);
    m_infoLabel->setText(text);
}

void TestCurveWidget::updateModeChecks()
{
    if (!m_testCheck || !m_aggregationCheck) {
        return;
    }
    const QSignalBlocker testBlocker(m_testCheck);
    const QSignalBlocker aggregationBlocker(m_aggregationCheck);
    m_testCheck->setChecked(m_mode == CurveMode::TestCurve);
    m_aggregationCheck->setChecked(m_mode == CurveMode::AggregationCurve);
}

void TestCurveWidget::refreshPlot()
{
    if (m_plot) {
        m_plot->setData(m_testCurve, m_aggregationCurve,
                        m_mode == CurveMode::AggregationCurve);
    }
}

void TestCurveWidget::updateDataState()
{
    if (!m_dataStateLabel) {
        return;
    }
    if (m_testCurve.isEmpty() && m_aggregationCurve.isEmpty()) {
        m_dataStateLabel->setText(tr("等待测试数据"));
    } else {
        m_dataStateLabel->setText(
                    tr("测试 %1 点 · 集聚 %2 点 · 每秒刷新")
                    .arg(m_testCurve.size())
                    .arg(m_aggregationCurve.size()));
    }
}
