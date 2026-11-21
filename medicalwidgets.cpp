#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "medicalwidgets.h"

#include <QAbstractItemView>
#include <QFontMetrics>
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

MedicalCheckBox::MedicalCheckBox(QWidget *parent)
    : QCheckBox(parent)
{
    setMinimumHeight(medicalui::kCheckBoxHeight);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);

    QPalette boxPalette = palette();
    boxPalette.setColor(QPalette::WindowText, QColor(QStringLiteral("#3A5065")));
    setPalette(boxPalette);

    QFont boxFont = font();
    boxFont.setFamily(QStringLiteral("Microsoft YaHei"));
    boxFont.setPixelSize(12);
    setFont(boxFont);

    connect(this, &QCheckBox::stateChanged, this,
            [this](int) { update(); });
}

MedicalCheckBox::MedicalCheckBox(const QString &text, QWidget *parent)
    : MedicalCheckBox(parent)
{
    setText(text);
}

QSize MedicalCheckBox::sizeHint() const
{
    const QFontMetrics metrics(font());
    const int textWidth = text().isEmpty() ? 0 : metrics.width(text());
    const int gap = text().isEmpty() ? 0 : 8;
    const int width = 2 + medicalui::kCheckIndicatorSize + gap
            + textWidth + 4;
    const int height = qMax(medicalui::kCheckBoxHeight,
                            metrics.height() + 8);
    return QSize(width, height);
}

QSize MedicalCheckBox::minimumSizeHint() const
{
    return sizeHint();
}

void MedicalCheckBox::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const bool enabled = isEnabled();
    const Qt::CheckState state = checkState();
    const bool checked = state == Qt::Checked;
    const bool partial = state == Qt::PartiallyChecked;
    const bool focused = hasFocus();

    const int side = medicalui::kCheckIndicatorSize;
    const QRectF indicator(2.0, (height() - side) / 2.0, side, side);

    if (focused && enabled) {
        painter.setPen(QPen(QColor(44, 123, 191, 90), 1.4));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(indicator.adjusted(-2.5, -2.5, 2.5, 2.5),
                                6.0, 6.0);
    }

    QColor fill = enabled ? QColor(QStringLiteral("#FFFFFF"))
                          : QColor(QStringLiteral("#F2F5F8"));
    QColor border = enabled ? QColor(QStringLiteral("#A9C8DF"))
                            : QColor(QStringLiteral("#D5DEE6"));
    if (enabled && (checked || partial)) {
        fill = m_pressed ? QColor(QStringLiteral("#2468A3"))
                         : (m_hovered ? QColor(QStringLiteral("#3C8BCF"))
                                      : QColor(QStringLiteral("#2C7BBF")));
        border = fill;
    } else if (!enabled && (checked || partial)) {
        fill = QColor(QStringLiteral("#B9C7D3"));
        border = fill;
    } else if (enabled && m_hovered) {
        fill = QColor(QStringLiteral("#F7FBFF"));
        border = QColor(QStringLiteral("#7FAFCF"));
    }
    if (enabled && focused) {
        border = QColor(QStringLiteral("#2C7BBF"));
    }

    painter.setPen(QPen(border, focused ? 2.0 : 1.2));
    painter.setBrush(fill);
    painter.drawRoundedRect(indicator, 4.0, 4.0);

    if (checked || partial) {
        QPen checkPen(enabled ? QColor(QStringLiteral("#FFFFFF"))
                              : QColor(QStringLiteral("#F7FAFC")),
                      2.0);
        checkPen.setCapStyle(Qt::RoundCap);
        checkPen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(checkPen);
        painter.setBrush(Qt::NoBrush);

        if (partial) {
            painter.drawLine(
                        QPointF(indicator.left() + side * 0.26,
                                indicator.center().y()),
                        QPointF(indicator.right() - side * 0.26,
                                indicator.center().y()));
        } else {
            QPainterPath check;
            check.moveTo(indicator.left() + side * 0.24,
                         indicator.center().y());
            check.lineTo(indicator.left() + side * 0.44,
                         indicator.bottom() - side * 0.26);
            check.lineTo(indicator.right() - side * 0.20,
                         indicator.top() + side * 0.28);
            painter.drawPath(check);
        }
    }

    const int textLeft = int(indicator.right()) + 8;
    const QRect textRect(textLeft, 0, qMax(0, width() - textLeft - 2),
                         height());

    QColor textColor = enabled
            ? palette().color(QPalette::WindowText)
            : palette().color(QPalette::Disabled, QPalette::WindowText);
    if (property("state").toString() == QLatin1String("disabled")) {
        textColor = QColor(QStringLiteral("#C0392B"));
    } else if (!enabled
               && textColor == palette().color(QPalette::WindowText)) {
        textColor = QColor(QStringLiteral("#9AA9B5"));
    }

    painter.setPen(textColor);
    painter.setFont(font());
    const QFontMetrics metrics(font());
    const QString elided = metrics.elidedText(
                text(), Qt::ElideRight, textRect.width());
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elided);
}

void MedicalCheckBox::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isEnabled()) {
        m_pressed = true;
        update();
    }
    QCheckBox::mousePressEvent(event);
}

void MedicalCheckBox::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_pressed) {
        m_pressed = false;
        update();
    }
    QCheckBox::mouseReleaseEvent(event);
}

void MedicalCheckBox::enterEvent(QEvent *event)
{
    m_hovered = true;
    update();
    QCheckBox::enterEvent(event);
}

void MedicalCheckBox::leaveEvent(QEvent *event)
{
    m_hovered = false;
    m_pressed = false;
    update();
    QCheckBox::leaveEvent(event);
}

void MedicalCheckBox::focusInEvent(QFocusEvent *event)
{
    QCheckBox::focusInEvent(event);
    update();
}

void MedicalCheckBox::focusOutEvent(QFocusEvent *event)
{
    QCheckBox::focusOutEvent(event);
    update();
}

void MedicalCheckBox::changeEvent(QEvent *event)
{
    QCheckBox::changeEvent(event);
    if (event->type() == QEvent::EnabledChange
            || event->type() == QEvent::PaletteChange
            || event->type() == QEvent::FontChange
            || event->type() == QEvent::StyleChange) {
        update();
    }
}

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
