#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "mainboardstatusicons.h"

#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QToolTip>

namespace {

constexpr int kIconCellWidth = 20;
constexpr int kIconCellHeight = 22;
constexpr int kIconGap = 3;
constexpr int kIconTop = 1;

} // namespace

MainBoardStatusIconBar::MainBoardStatusIconBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("mainBoardStatusIcons"));
    setMouseTracking(true);
    setFixedSize(IconCount * kIconCellWidth + (IconCount - 1) * kIconGap,
                 kIconCellHeight + kIconTop * 2);

    m_icons.resize(IconCount);

    m_blinkTimer.setInterval(430);
    connect(&m_blinkTimer, &QTimer::timeout, this, [this]() {
        m_blinkOn = !m_blinkOn;
        update();
    });

    clearStatus();
}

void MainBoardStatusIconBar::setStatus(bool s2Normal, bool wasteNormal,
                                        const bool cupPresent[4],
                                        bool heightOk, int activeCupCount)
{
    m_activeCupCount = qBound(1, activeCupCount, 4);

    m_icons[IconS2].known = true;
    m_icons[IconS2].normal = s2Normal;
    m_icons[IconS2].active = true;
    m_icons[IconS2].tooltip = tr("外部清洗液S2：%1")
            .arg(s2Normal ? tr("正常") : tr("不足/断开"));

    m_icons[IconWaste].known = true;
    m_icons[IconWaste].normal = wasteNormal;
    m_icons[IconWaste].active = true;
    m_icons[IconWaste].tooltip = tr("废液：%1")
            .arg(wasteNormal ? tr("正常") : tr("满/断开"));

    for (int i = 0; i < 4; ++i) {
        IconState &cup = m_icons[IconCup1 + i];
        cup.known = true;
        cup.active = i < m_activeCupCount;
        cup.normal = cup.active ? cupPresent[i] : true;
        cup.tooltip = cup.active
                ? tr("托盘%1：%2")
                  .arg(i + 1)
                  .arg(cupPresent[i] ? tr("正常") : tr("缺少"))
                : tr("托盘%1：未使用").arg(i + 1);
    }

    m_icons[IconHeight].known = true;
    m_icons[IconHeight].normal = heightOk;
    m_icons[IconHeight].active = true;
    m_icons[IconHeight].tooltip = tr("测高检测：%1")
            .arg(heightOk ? tr("成功") : tr("失败"));

    updateBlinkTimer();
}

void MainBoardStatusIconBar::clearStatus(const QString &reason)
{
    const QString reasonText = reason.isEmpty()
            ? tr("等待主板状态") : reason;

    m_icons[IconS2].known = false;
    m_icons[IconS2].normal = true;
    m_icons[IconS2].active = true;
    m_icons[IconS2].tooltip =
            tr("外部清洗液S2：%1").arg(reasonText);

    m_icons[IconWaste].known = false;
    m_icons[IconWaste].normal = true;
    m_icons[IconWaste].active = true;
    m_icons[IconWaste].tooltip = tr("废液：%1").arg(reasonText);

    for (int i = 0; i < 4; ++i) {
        IconState &cup = m_icons[IconCup1 + i];
        cup.known = false;
        cup.normal = true;
        cup.active = i < m_activeCupCount;
        cup.tooltip = cup.active
                ? tr("托盘%1：%2").arg(i + 1).arg(reasonText)
                : tr("托盘%1：未使用").arg(i + 1);
    }

    m_icons[IconHeight].known = false;
    m_icons[IconHeight].normal = true;
    m_icons[IconHeight].active = true;
    m_icons[IconHeight].tooltip = tr("测高检测：%1").arg(reasonText);

    updateBlinkTimer();
}

QSize MainBoardStatusIconBar::sizeHint() const
{
    return QSize(IconCount * kIconCellWidth
                 + (IconCount - 1) * kIconGap,
                 kIconCellHeight + kIconTop * 2);
}

void MainBoardStatusIconBar::updateBlinkTimer()
{
    bool hasAlert = false;
    for (const IconState &state : m_icons) {
        if (state.known && state.active && !state.normal) {
            hasAlert = true;
            break;
        }
    }

    if (hasAlert) {
        if (!m_blinkTimer.isActive()) {
            m_blinkOn = true;
            m_blinkTimer.start();
        }
    } else {
        m_blinkTimer.stop();
        m_blinkOn = true;
    }
    update();
}

QRectF MainBoardStatusIconBar::iconRect(int index) const
{
    return QRectF(index * (kIconCellWidth + kIconGap), kIconTop,
                  kIconCellWidth, kIconCellHeight);
}

int MainBoardStatusIconBar::iconAt(const QPoint &pos) const
{
    for (int i = 0; i < m_icons.size(); ++i) {
        if (iconRect(i).contains(QPointF(pos))) {
            return i;
        }
    }
    return -1;
}

void MainBoardStatusIconBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    for (int i = 0; i < m_icons.size(); ++i) {
        paintIcon(p, i);
    }
}

void MainBoardStatusIconBar::paintIcon(QPainter &p, int index)
{
    const QRectF rect = iconRect(index);
    const IconState &state = m_icons.at(index);

    const bool alert = state.known && state.active && !state.normal;
    const bool inactive = state.known && !state.active;
    const bool unknown = !state.known;
    const bool hovered = index == m_hoverIndex;

    p.save();
    if (alert && !m_blinkOn) {
        p.setOpacity(0.30);
    }

    QColor fill("#F6FBFE");
    QColor border("#D7E7F2");
    QColor accent("#2C7BBF");
    if (alert) {
        fill = QColor("#FDEDEC");
        border = QColor("#EBA8A3");
        accent = QColor("#D64545");
    } else if (inactive || unknown) {
        fill = QColor("#F4F7FA");
        border = QColor("#E1EAF1");
        accent = QColor("#AFC0CC");
    }

    if (hovered) {
        border = accent;
    }

    p.setPen(QPen(border, 1.0));
    p.setBrush(fill);
    p.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 6.0, 6.0);

    const qreal cx = rect.center().x();
    const qreal glyphTop = rect.top() + 3.0;
    p.setPen(QPen(accent, 1.25, Qt::SolidLine, Qt::RoundCap,
                  Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    switch (index) {
    case IconS2: {
        // 外部清洗液：小水滴
        QPainterPath drop;
        drop.moveTo(cx, glyphTop + 0.5);
        drop.cubicTo(cx + 4.6, glyphTop + 5.0,
                     cx + 5.0, glyphTop + 8.8,
                     cx + 2.8, glyphTop + 11.5);
        drop.cubicTo(cx + 1.0, glyphTop + 13.8,
                     cx - 2.2, glyphTop + 13.2,
                     cx - 3.5, glyphTop + 10.5);
        drop.cubicTo(cx - 4.8, glyphTop + 7.3,
                     cx - 3.0, glyphTop + 3.5,
                     cx, glyphTop + 0.5);
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 30));
        p.drawPath(drop);
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(cx - 2.5, glyphTop + 8.0, 4.6, 4.2),
                  120 * 16, 110 * 16);
        break;
    }
    case IconWaste: {
        // 废液：带盖的小桶
        p.drawLine(QPointF(cx - 5.5, glyphTop + 2.0),
                   QPointF(cx + 5.5, glyphTop + 2.0));
        p.drawLine(QPointF(cx - 3.8, glyphTop + 0.8),
                   QPointF(cx + 3.8, glyphTop + 0.8));
        QPainterPath can;
        can.moveTo(cx - 4.7, glyphTop + 3.2);
        can.lineTo(cx - 3.8, glyphTop + 13.0);
        can.quadTo(cx, glyphTop + 14.2, cx + 3.8, glyphTop + 13.0);
        can.lineTo(cx + 4.7, glyphTop + 3.2);
        p.drawPath(can);
        p.drawLine(QPointF(cx - 1.6, glyphTop + 5.0),
                   QPointF(cx - 1.3, glyphTop + 11.4));
        p.drawLine(QPointF(cx + 1.6, glyphTop + 5.0),
                   QPointF(cx + 1.3, glyphTop + 11.4));
        break;
    }
    case IconCup1:
    case IconCup2:
    case IconCup3:
    case IconCup4: {
        // 托盘：小圆盘内显示托盘编号
        const int cupNumber = index - IconCup1 + 1;
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 24));
        p.drawEllipse(QPointF(cx, glyphTop + 6.3), 6.2, 6.2);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(cx - 5.0, glyphTop + 13.6),
                   QPointF(cx + 5.0, glyphTop + 13.6));
        QFont numberFont(QStringLiteral("Microsoft YaHei"));
        numberFont.setPixelSize(8);
        numberFont.setBold(true);
        p.setFont(numberFont);
        QColor numberColor = accent;
        numberColor.setAlpha(235);
        p.setPen(numberColor);
        p.drawText(QRectF(cx - 5.5, glyphTop + 1.5, 11.0, 10.5),
                   Qt::AlignCenter, QString::number(cupNumber));
        break;
    }
    case IconHeight: {
        // 测高：上下箭头 + 基准线
        p.drawLine(QPointF(cx, glyphTop + 2.0),
                   QPointF(cx, glyphTop + 12.8));
        QPainterPath topArrow;
        topArrow.moveTo(cx, glyphTop + 0.4);
        topArrow.lineTo(cx - 2.0, glyphTop + 3.2);
        topArrow.lineTo(cx + 2.0, glyphTop + 3.2);
        topArrow.closeSubpath();
        p.setBrush(accent);
        p.setPen(Qt::NoPen);
        p.drawPath(topArrow);
        QPainterPath bottomArrow;
        bottomArrow.moveTo(cx, glyphTop + 14.8);
        bottomArrow.lineTo(cx - 2.0, glyphTop + 12.0);
        bottomArrow.lineTo(cx + 2.0, glyphTop + 12.0);
        bottomArrow.closeSubpath();
        p.drawPath(bottomArrow);
        p.setPen(QPen(accent, 1.25, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(cx - 5.0, glyphTop + 15.5),
                   QPointF(cx + 5.0, glyphTop + 15.5));
        break;
    }
    default:
        break;
    }

    // 状态圆点：正常绿色，异常红色并闪烁，未使用/未知灰色
    const QPointF dot(rect.right() - 4.4, rect.top() + 4.3);
    p.setPen(QPen(QColor(255, 255, 255, 230), 1.4));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(dot, 3.3, 3.3);

    QColor dotColor("#B5C3CF");
    if (state.known && state.active) {
        dotColor = state.normal ? QColor("#22A877") : QColor("#D64545");
    } else if (inactive) {
        dotColor = QColor("#C5D2DC");
    }
    p.setPen(Qt::NoPen);
    p.setBrush(dotColor);
    p.drawEllipse(dot, 2.3, 2.3);

    p.restore();
}

void MainBoardStatusIconBar::mouseMoveEvent(QMouseEvent *event)
{
    const int index = iconAt(event->pos());
    if (index != m_hoverIndex) {
        m_hoverIndex = index;
        if (index >= 0 && !m_icons.at(index).tooltip.isEmpty()) {
            QToolTip::showText(event->globalPos(),
                               m_icons.at(index).tooltip,
                               this, iconRect(index).toRect());
        } else {
            QToolTip::hideText();
        }
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void MainBoardStatusIconBar::leaveEvent(QEvent *event)
{
    m_hoverIndex = -1;
    QToolTip::hideText();
    update();
    QWidget::leaveEvent(event);
}
