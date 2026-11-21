#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "medicalwidgets.h"

#include <QAbstractItemView>
#include <QPainterPath>
#include <QPolygonF>

namespace medicalui {

QColor controlTextColor(bool enabled)
{
    return enabled ? QColor(QStringLiteral("#1F2D3D"))
                   : QColor(QStringLiteral("#9AA9B5"));
}

void paintControlFrame(QPainter &painter, const QRect &rect,
                       bool focused, bool hovered, bool enabled)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal radius = 6.0;
    const QRectF outer = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);

    QColor background(QStringLiteral("#FFFFFF"));
    if (!enabled) {
        background = QColor(QStringLiteral("#F2F5F8"));
    } else if (hovered) {
        background = QColor(QStringLiteral("#F7FBFF"));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(outer, radius, radius);

    QColor border(QStringLiteral("#C5D4E4"));
    qreal borderWidth = 1.0;
    if (!enabled) {
        border = QColor(QStringLiteral("#DCE7F1"));
    } else if (focused) {
        border = QColor(QStringLiteral("#2C7BBF"));
        borderWidth = 2.0;
    } else if (hovered) {
        border = QColor(QStringLiteral("#8FB4D6"));
    }

    QPen pen(border, borderWidth);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    const QRectF borderRect = borderWidth > 1.0
            ? outer.adjusted(1.0, 1.0, -1.0, -1.0)
            : outer;
    painter.drawRoundedRect(borderRect, radius, radius);

    painter.restore();
}

void paintChevron(QPainter &painter, const QPoint &center, bool up,
                  const QColor &color)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPen pen(color, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const qreal halfWidth = 4.0;
    const qreal halfHeight = 2.0;
    QPolygonF points;
    if (up) {
        points << QPointF(center.x() - halfWidth, center.y() + halfHeight)
               << QPointF(center.x(), center.y() - halfHeight)
               << QPointF(center.x() + halfWidth, center.y() + halfHeight);
    } else {
        points << QPointF(center.x() - halfWidth, center.y() - halfHeight)
               << QPointF(center.x(), center.y() + halfHeight)
               << QPointF(center.x() + halfWidth, center.y() - halfHeight);
    }
    painter.drawPolyline(points);
    painter.restore();
}

QRect spinButtonColumn(const QRect &rect)
{
    const int margin = 3;
    return QRect(rect.left() + rect.width() - kSpinButtonWidth - margin,
                 rect.top() + margin,
                 kSpinButtonWidth,
                 qMax(2, rect.height() - margin * 2));
}

QRect spinUpButtonRect(const QRect &rect)
{
    const QRect column = spinButtonColumn(rect);
    return QRect(column.x(), column.y(),
                 column.width(), column.height() / 2);
}

QRect spinDownButtonRect(const QRect &rect)
{
    const QRect column = spinButtonColumn(rect);
    const int topHeight = column.height() / 2;
    return QRect(column.x(), column.y() + topHeight,
                 column.width(), column.height() - topHeight);
}

void paintSpinButtons(QPainter &painter, const QRect &rect,
                      bool enabled, int hoverPart, int pressedPart)
{
    const QRect column = spinButtonColumn(rect);
    const QRect up = spinUpButtonRect(rect);
    const QRect down = spinDownButtonRect(rect);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    if (enabled) {
        if (hoverPart == 1 || pressedPart == 1) {
            painter.setBrush(pressedPart == 1
                             ? QColor(QStringLiteral("#D3E6F5"))
                             : QColor(QStringLiteral("#E8F1FA")));
            painter.drawRoundedRect(QRectF(up.adjusted(1, 1, -1, -1)),
                                    4.0, 4.0);
        }
        if (hoverPart == 2 || pressedPart == 2) {
            painter.setBrush(pressedPart == 2
                             ? QColor(QStringLiteral("#D3E6F5"))
                             : QColor(QStringLiteral("#E8F1FA")));
            painter.drawRoundedRect(QRectF(down.adjusted(1, 1, -1, -1)),
                                    4.0, 4.0);
        }
    }

    const QColor lineColor = enabled
            ? QColor(QStringLiteral("#E2EBF2"))
            : QColor(QStringLiteral("#EDF2F7"));
    painter.setPen(QPen(lineColor, 1.0));
    painter.drawLine(column.left() - 2, column.top() + 4,
                     column.left() - 2, column.bottom() - 4);
    painter.drawLine(column.left() + 4, column.center().y() + 1,
                     column.right() - 4, column.center().y() + 1);

    const QColor chevronColor = enabled
            ? QColor(QStringLiteral("#5D7386"))
            : QColor(QStringLiteral("#B8C4CE"));
    paintChevron(painter, up.center(), true, chevronColor);
    paintChevron(painter, down.center(), false, chevronColor);
    painter.restore();
}

} // namespace medicalui

MedicalComboBox::MedicalComboBox(QWidget *parent)
    : QComboBox(parent)
{
    setMinimumHeight(medicalui::kControlHeight);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setMaxVisibleItems(10);

    QFont controlFont = font();
    controlFont.setFamily(QStringLiteral("Microsoft YaHei"));
    controlFont.setPixelSize(12);
    setFont(controlFont);

    if (QAbstractItemView *popup = view()) {
        popup->setFont(controlFont);
        popup->setMouseTracking(true);
        popup->setStyleSheet(QStringLiteral(
            "QAbstractItemView {"
            "  background: #FFFFFF;"
            "  border: 1px solid #C5D4E4;"
            "  border-radius: 6px;"
            "  padding: 4px;"
            "  outline: 0;"
            "}"
            "QAbstractItemView::item {"
            "  min-height: 28px;"
            "  padding: 2px 10px;"
            "  color: #1F2D3D;"
            "}"
            "QAbstractItemView::item:hover {"
            "  background: #E8F1FA;"
            "}"
            "QAbstractItemView::item:selected {"
            "  background: #DCEFFA;"
            "  color: #1F4E79;"
            "}"));
    }
}

void MedicalComboBox::paintEvent(QPaintEvent *event)
{
    if (isEditable()) {
        QComboBox::paintEvent(event);
        return;
    }

    QPainter painter(this);
    medicalui::paintControlFrame(painter, rect(), hasFocus(), m_hovered,
                                 isEnabled());

    const int arrowArea = 30;
    const QRect textRect(10, 0, qMax(0, width() - 40), height());
    painter.setFont(font());
    painter.setPen(medicalui::controlTextColor(isEnabled()));
    painter.drawText(
                textRect, Qt::AlignLeft | Qt::AlignVCenter,
                fontMetrics().elidedText(currentText(), Qt::ElideRight,
                                         textRect.width()));

    const int separatorX = width() - arrowArea;
    painter.setPen(QPen(QColor(QStringLiteral("#E2EBF2")), 1.0));
    painter.drawLine(separatorX, 6, separatorX, height() - 7);

    medicalui::paintChevron(
                painter,
                QPoint(width() - arrowArea / 2, height() / 2),
                false,
                isEnabled() ? QColor(QStringLiteral("#5D7386"))
                            : QColor(QStringLiteral("#B8C4CE")));
}

void MedicalComboBox::wheelEvent(QWheelEvent *event)
{
    event->ignore();
}

void MedicalComboBox::focusInEvent(QFocusEvent *event)
{
    QComboBox::focusInEvent(event);
    update();
}

void MedicalComboBox::focusOutEvent(QFocusEvent *event)
{
    QComboBox::focusOutEvent(event);
    update();
}

void MedicalComboBox::enterEvent(QEvent *event)
{
    QComboBox::enterEvent(event);
    m_hovered = true;
    update();
}

void MedicalComboBox::leaveEvent(QEvent *event)
{
    QComboBox::leaveEvent(event);
    m_hovered = false;
    update();
}
