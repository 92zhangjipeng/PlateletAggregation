#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "tubetraydialog.h"
#include "sampletraymodel.h"

#include <QAction>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QLineF>
#include <QLinearGradient>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QPushButton>
#include <QRadialGradient>
#include <QStyle>
#include <QToolTip>
#include <QVBoxLayout>

#include <functional>
#include <cmath>

namespace {

bool stateHasCup(SampleTrayModel::TubeState state)
{
    return state == SampleTrayModel::TubeState::Free
            || state == SampleTrayModel::TubeState::Allocated;
}

QColor cupMouthColor(SampleTrayModel::TubeState state)
{
    switch (state) {
    case SampleTrayModel::TubeState::Free: return QColor("#BFD9E8");
    case SampleTrayModel::TubeState::Allocated: return QColor("#E0A21A");
    case SampleTrayModel::TubeState::Sampled: return QColor("#1F6FA5");
    case SampleTrayModel::TubeState::Used: return QColor("#1E7B45");
    }
    return QColor("#BFD9E8");
}

QPointF lerpPoint(const QPointF &from, const QPointF &to, qreal t)
{
    return from + (to - from) * t;
}

} // namespace

class TrayGridWidget : public QWidget
{
public:
    using ClickHandler = std::function<void(int, const QPoint &)>;

    TrayGridWidget(SampleTrayModel *model, int trayIndex, QWidget *parent)
        : QWidget(parent)
        , m_model(model)
        , m_trayIndex(trayIndex)
    {
        setMouseTracking(true);
        setMinimumSize(250, 400);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setActive(bool active)
    {
        if (m_active == active) {
            return;
        }
        m_active = active;
        update();
    }

    bool isActive() const
    {
        return m_active;
    }

    void setClickHandler(const ClickHandler &handler)
    {
        m_clickHandler = handler;
    }

    int trayIndex() const
    {
        return m_trayIndex;
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

        QFont titleFont(QStringLiteral("Microsoft YaHei"));
        titleFont.setPixelSize(14);
        titleFont.setBold(true);
        p.setFont(titleFont);
        p.setPen(QColor("#1F4E79"));
        const int startId = m_trayIndex * SampleTrayModel::TubesPerTray;
        const int endId = startId + SampleTrayModel::TubesPerTray - 1;
        p.drawText(QRectF(card.left() + 16.0, card.top() + 8.0,
                          240.0, 22.0),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   tr("试管盘 %1").arg(m_trayIndex + 1));

        QFont subFont(QStringLiteral("Microsoft YaHei"));
        subFont.setPixelSize(10);
        p.setFont(subFont);
        p.setPen(QColor("#8A9CAB"));
        p.drawText(QRectF(card.left() + 104.0, card.top() + 8.0,
                          card.width() - 120.0, 22.0),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   tr("编号 %1–%2").arg(startId).arg(endId));

        if (!m_active) {
            p.setPen(QColor("#A9B9C6"));
            QFont inactiveFont(QStringLiteral("Microsoft YaHei"));
            inactiveFont.setPixelSize(18);
            inactiveFont.setBold(true);
            p.setFont(inactiveFont);
            p.drawText(card, Qt::AlignCenter, tr("当前机型未使用此试管盘"));
            return;
        }

        paintRack(p, card);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const int index = tubeAt(event->pos());
        if (index != m_hoverIndex) {
            m_hoverIndex = index;
            if (index >= 0 && m_active) {
                const SampleTrayModel::TubeInfo info =
                        m_model->tubeInfo(index);
                QString text = tr("试管 %1\n试管盘 %2 · 第 %3 行第 %4 列\n状态：%5")
                        .arg(index)
                        .arg(SampleTrayModel::trayOf(index) + 1)
                        .arg(SampleTrayModel::rowOf(index) + 1)
                        .arg(SampleTrayModel::columnOf(index) + 1)
                        .arg(SampleTrayModel::stateText(info.state));
                if (!info.sampleNumber.isEmpty()) {
                    text += tr("\n样本：%1").arg(info.sampleNumber);
                }
                if (!info.reagent.isEmpty()) {
                    text += tr("\n试剂：%1").arg(info.reagent);
                }
                if (info.updatedAt.isValid()) {
                    text += tr("\n更新：%1")
                            .arg(info.updatedAt.toString(
                                     QStringLiteral("yyyy-MM-dd HH:mm:ss")));
                }
                QToolTip::showText(event->globalPos(), text, this);
            } else {
                QToolTip::hideText();
            }
            update();
        }
        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hoverIndex = -1;
        QToolTip::hideText();
        update();
        QWidget::leaveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_active && event->button() == Qt::LeftButton) {
            const int index = tubeAt(event->pos());
            if (index >= 0 && m_clickHandler) {
                m_clickHandler(index, event->globalPos());
            }
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    struct RackGeometry {
        QPolygonF topFace;
        QPolygonF frontFace;
        QPolygonF leftFace;
        QPolygonF rightFace;
        QVector<QPointF> centers;
        QVector<qreal> radiusX;
        QVector<qreal> radiusY;
        qreal topY = 0.0;
        qreal bottomY = 0.0;
        qreal thickness = 0.0;
        qreal bottomWidth = 0.0;

        bool valid() const
        {
            return centers.size() == SampleTrayModel::TubesPerTray;
        }
    };

    RackGeometry rackGeometry() const
    {
        RackGeometry geometry;
        const QRectF card = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        const qreal titleBand = 40.0;
        const QRectF area(card.left() + 30.0,
                          card.top() + titleBand,
                          card.width() - 60.0,
                          card.height() - titleBand - 24.0);
        if (area.width() < 120.0 || area.height() < 180.0) {
            return geometry;
        }

        geometry.topY = area.top() + 12.0;
        geometry.bottomY = area.bottom() - 2.0;
        const qreal height = qMax<qreal>(160.0,
                                         geometry.bottomY - geometry.topY);
        geometry.bottomWidth = qMin(area.width() - 16.0, height * 0.90);
        const qreal topWidth = geometry.bottomWidth * 0.80;
        const qreal centerX = area.center().x();

        geometry.topFace
                << QPointF(centerX - topWidth / 2.0, geometry.topY)
                << QPointF(centerX + topWidth / 2.0, geometry.topY)
                << QPointF(centerX + geometry.bottomWidth / 2.0,
                           geometry.bottomY)
                << QPointF(centerX - geometry.bottomWidth / 2.0,
                           geometry.bottomY);

        geometry.thickness = qBound<qreal>(10.0, height * 0.045, 18.0);
        const QPointF depth(0.0, geometry.thickness);
        geometry.frontFace
                << geometry.topFace.at(3) << geometry.topFace.at(2)
                << geometry.topFace.at(2) + depth
                << geometry.topFace.at(3) + depth;
        geometry.leftFace
                << geometry.topFace.at(0) << geometry.topFace.at(3)
                << geometry.topFace.at(3) + depth
                << geometry.topFace.at(0) + depth;
        geometry.rightFace
                << geometry.topFace.at(1) << geometry.topFace.at(2)
                << geometry.topFace.at(2) + depth
                << geometry.topFace.at(1) + depth;

        const int rows = SampleTrayModel::RowsPerTray;
        const int columns = SampleTrayModel::ColumnsPerTray;
        const qreal marginU = 0.11;
        const qreal marginV = 0.09;
        const QPointF topLeft = geometry.topFace.at(0);
        const QPointF topRight = geometry.topFace.at(1);
        const QPointF bottomRight = geometry.topFace.at(2);
        const QPointF bottomLeft = geometry.topFace.at(3);

        for (int row = 0; row < rows; ++row) {
            const qreal v = marginV
                    + (1.0 - 2.0 * marginV)
                      * (rows > 1 ? qreal(row) / (rows - 1) : 0.5);
            for (int column = 0; column < columns; ++column) {
                const qreal u = marginU
                        + (1.0 - 2.0 * marginU)
                          * (columns > 1
                             ? qreal(column) / (columns - 1) : 0.5);
                const QPointF topPoint = lerpPoint(topLeft, topRight, u);
                const QPointF bottomPoint = lerpPoint(bottomLeft,
                                                      bottomRight, u);
                geometry.centers.append(
                            lerpPoint(topPoint, bottomPoint, v));
            }
        }

        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                const int index = row * columns + column;
                const QPointF &center = geometry.centers.at(index);
                qreal horizontal = 1.0e9;
                qreal vertical = 1.0e9;
                if (column > 0) {
                    horizontal = qMin(
                                horizontal,
                                QLineF(center,
                                       geometry.centers.at(index - 1))
                                .length());
                }
                if (column < columns - 1) {
                    horizontal = qMin(
                                horizontal,
                                QLineF(center,
                                       geometry.centers.at(index + 1))
                                .length());
                }
                if (row > 0) {
                    vertical = qMin(
                                vertical,
                                QLineF(center,
                                       geometry.centers.at(index - columns))
                                .length());
                }
                if (row < rows - 1) {
                    vertical = qMin(
                                vertical,
                                QLineF(center,
                                       geometry.centers.at(index + columns))
                                .length());
                }
                const qreal radius = qMax<qreal>(
                            7.0, qMin(horizontal * 0.36, vertical * 0.62));
                geometry.radiusX.append(radius);
                geometry.radiusY.append(radius * 0.60);
            }
        }
        return geometry;
    }

    void paintRack(QPainter &p, const QRectF &card) const
    {
        Q_UNUSED(card);
        if (!m_model) {
            return;
        }
        const RackGeometry geometry = rackGeometry();
        if (!geometry.valid()) {
            return;
        }

        // 桌面阴影
        const qreal shadowWidth = geometry.bottomWidth * 0.94;
        const QRectF shadowRect(
                    geometry.topFace.at(3).x()
                    + geometry.bottomWidth * 0.03,
                    geometry.bottomY + geometry.thickness - 4.0,
                    shadowWidth, 20.0);
        QRadialGradient shadow(shadowRect.center(),
                               qMax<qreal>(8.0, shadowWidth / 2.0));
        shadow.setColorAt(0.0, QColor(31, 78, 121, 60));
        shadow.setColorAt(1.0, QColor(31, 78, 121, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(shadow);
        p.drawEllipse(shadowRect);

        // 玻璃板厚度
        QLinearGradient sideGradient(0.0, geometry.bottomY,
                                     0.0, geometry.bottomY
                                     + geometry.thickness);
        sideGradient.setColorAt(0.0, QColor(176, 213, 233, 230));
        sideGradient.setColorAt(1.0, QColor(138, 184, 211, 245));
        p.setPen(QPen(QColor(132, 178, 205, 225), 1.0));
        p.setBrush(sideGradient);
        p.drawPolygon(geometry.leftFace);
        p.drawPolygon(geometry.rightFace);
        p.drawPolygon(geometry.frontFace);

        // 透明玻璃台面
        QLinearGradient topGradient(0.0, geometry.topY,
                                    0.0, geometry.bottomY);
        topGradient.setColorAt(0.0, QColor(242, 252, 255, 205));
        topGradient.setColorAt(0.45, QColor(220, 242, 252, 188));
        topGradient.setColorAt(1.0, QColor(193, 226, 243, 205));
        p.setPen(QPen(QColor(146, 190, 214, 240), 1.2));
        p.setBrush(topGradient);
        p.drawPolygon(geometry.topFace);

        p.setPen(QPen(QColor(255, 255, 255, 235), 1.4));
        p.drawLine(geometry.topFace.at(0), geometry.topFace.at(1));
        p.setPen(QPen(QColor(255, 255, 255, 165), 1.0));
        p.drawLine(geometry.topFace.at(0), geometry.topFace.at(3));
        p.setPen(QPen(QColor(255, 255, 255, 85), 1.0));
        p.drawLine(geometry.topFace.at(1), geometry.topFace.at(2));

        p.save();
        QPainterPath clip;
        clip.addPolygon(geometry.topFace);
        clip.closeSubpath();
        p.setClipPath(clip);
        p.setPen(QPen(QColor(255, 255, 255, 45), 10.0));
        p.drawLine(
                    QPointF(geometry.topFace.at(3).x()
                            + geometry.bottomWidth * 0.10,
                            geometry.bottomY),
                    QPointF(geometry.topFace.at(0).x()
                            + geometry.bottomWidth * 0.45,
                            geometry.topY));
        p.setPen(QPen(QColor(255, 255, 255, 30), 4.0));
        p.drawLine(
                    QPointF(geometry.topFace.at(3).x()
                            + geometry.bottomWidth * 0.42,
                            geometry.bottomY),
                    QPointF(geometry.topFace.at(0).x()
                            + geometry.bottomWidth * 0.70,
                            geometry.topY));
        p.restore();

        const int rows = SampleTrayModel::RowsPerTray;
        const int columns = SampleTrayModel::ColumnsPerTray;

        // 行号/列号
        QFont headerFont(QStringLiteral("Microsoft YaHei"));
        headerFont.setPixelSize(8);
        p.setFont(headerFont);
        p.setPen(QColor("#7A8FA3"));
        const qreal marginU = 0.11;
        const qreal marginV = 0.09;
        for (int column = 0; column < columns; ++column) {
            const qreal u = marginU
                    + (1.0 - 2.0 * marginU)
                      * (columns > 1
                         ? qreal(column) / (columns - 1) : 0.5);
            const QPointF topPoint = lerpPoint(
                        geometry.topFace.at(0),
                        geometry.topFace.at(1), u);
            p.drawText(QRectF(topPoint.x() - 10.0,
                              geometry.topY - 14.0, 20.0, 12.0),
                       Qt::AlignCenter, QString::number(column + 1));
        }
        for (int row = 0; row < rows; ++row) {
            const qreal v = marginV
                    + (1.0 - 2.0 * marginV)
                      * (rows > 1 ? qreal(row) / (rows - 1) : 0.5);
            const QPointF leftPoint = lerpPoint(
                        geometry.topFace.at(0),
                        geometry.topFace.at(3), v);
            p.drawText(QRectF(leftPoint.x() - 22.0,
                              leftPoint.y() - 6.0, 18.0, 12.0),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(row + 1));
        }

        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                const int localIndex = row * columns + column;
                const QPointF center = geometry.centers.at(localIndex);
                const qreal rx = geometry.radiusX.at(localIndex);
                const qreal ry = geometry.radiusY.at(localIndex);
                const int tubeIndex = SampleTrayModel::tubeIndex(
                            m_trayIndex, row, column);
                if (tubeIndex < 0) {
                    continue;
                }

                const SampleTrayModel::TubeInfo info =
                        m_model->tubeInfo(tubeIndex);
                const bool hasCup = stateHasCup(info.state);
                const bool hovered = tubeIndex == m_hoverIndex;

                paintHoleBase(p, center, rx, ry, info.state,
                              hovered, hasCup);
                if (hasCup) {
                    paintCup(p, center, rx, ry, info.state, hovered);
                }

                paintTubeNumber(
                            p,
                            QRectF(center.x() - rx, center.y() - ry,
                                   rx * 2.0, ry * 2.0),
                            QString::number(tubeIndex), hasCup,
                            info.state);
            }
        }
    }

    void paintHoleBase(QPainter &p, const QPointF &center,
                       qreal rx, qreal ry,
                       SampleTrayModel::TubeState state,
                       bool hovered, bool hasCup) const
    {
        if (hovered) {
            QRadialGradient glow(center, qMax<qreal>(12.0, rx * 2.2));
            glow.setColorAt(0.0, QColor(44, 123, 191, 95));
            glow.setColorAt(0.65, QColor(44, 123, 191, 35));
            glow.setColorAt(1.0, QColor(44, 123, 191, 0));
            p.setPen(Qt::NoPen);
            p.setBrush(glow);
            p.drawEllipse(center, rx * 1.75, ry * 1.95);
        }

        p.setPen(QPen(hovered ? QColor("#2C7BBF")
                              : QColor(150, 192, 216, 235),
                      hovered ? 1.8 : 1.1));
        p.setBrush(QColor(232, 246, 252, 170));
        p.drawEllipse(center, rx, ry);

        const QPointF inner = center + QPointF(0.0, ry * 0.12);
        QRadialGradient depth(inner, qMax<qreal>(8.0, rx));
        depth.setColorAt(0.0, QColor(46, 70, 84, 150));
        depth.setColorAt(0.60, QColor(100, 132, 149, 95));
        depth.setColorAt(1.0, QColor(205, 230, 243, 55));
        p.setPen(Qt::NoPen);
        p.setBrush(depth);
        p.drawEllipse(inner, rx * 0.80, ry * 0.78);

        p.setPen(QPen(QColor(255, 255, 255, 220), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(center.x() - rx, center.y() - ry,
                         rx * 2.0, ry * 2.0),
                  35 * 16, 110 * 16);

        if (!hasCup) {
            QColor tint = cupMouthColor(state);
            tint.setAlpha(195);
            p.setPen(QPen(tint, 1.8));
            p.drawEllipse(center, rx * 0.86, ry * 0.84);

            // 只加很淡的状态色，让孔内保持“空孔”的深色透明感
            QColor tintFill = cupMouthColor(state);
            tintFill.setAlpha(35);
            p.setPen(Qt::NoPen);
            p.setBrush(tintFill);
            p.drawEllipse(inner, rx * 0.72, ry * 0.70);
        }
    }

    void paintCup(QPainter &p, const QPointF &center,
                  qreal rx, qreal ry,
                  SampleTrayModel::TubeState state, bool hovered) const
    {
        Q_UNUSED(hovered);
        const qreal halfWidth = rx * 0.64;
        const qreal mouthRadiusY = ry * 0.78;
        const qreal bodyBottom = center.y() + ry * 1.45;
        const qreal bottomRadiusY = mouthRadiusY * 0.85;

        QPainterPath body;
        body.moveTo(center.x() - halfWidth, center.y());
        body.lineTo(center.x() - halfWidth, bodyBottom);
        body.arcTo(QRectF(center.x() - halfWidth,
                          bodyBottom - bottomRadiusY,
                          halfWidth * 2.0, bottomRadiusY * 2.0),
                   180.0, -180.0);
        body.lineTo(center.x() + halfWidth, center.y());
        body.closeSubpath();

        QLinearGradient glass(center.x() - halfWidth, 0.0,
                              center.x() + halfWidth, 0.0);
        glass.setColorAt(0.0, QColor(255, 255, 255, 225));
        glass.setColorAt(0.18, QColor(234, 248, 254, 185));
        glass.setColorAt(0.55, QColor(252, 255, 255, 120));
        glass.setColorAt(0.82, QColor(205, 234, 247, 150));
        glass.setColorAt(1.0, QColor(176, 214, 235, 200));
        p.setPen(QPen(QColor(151, 199, 224, 220), 1.0));
        p.setBrush(glass);
        p.drawPath(body);

        const QPointF bottomCenter(center.x(), bodyBottom);
        QRadialGradient bottomGradient(
                    bottomCenter + QPointF(0.0, bottomRadiusY * 0.3),
                    qMax<qreal>(6.0, halfWidth));
        bottomGradient.setColorAt(0.0, QColor(235, 249, 255, 210));
        bottomGradient.setColorAt(0.70, QColor(205, 233, 247, 160));
        bottomGradient.setColorAt(1.0, QColor(170, 209, 231, 110));
        p.setPen(QPen(QColor(150, 198, 224, 170), 0.8));
        p.setBrush(bottomGradient);
        p.drawEllipse(bottomCenter, halfWidth * 0.88,
                      bottomRadiusY * 0.82);

        paintStirBar(p, bottomCenter, halfWidth * 1.05,
                     qMax<qreal>(2.2, halfWidth * 0.16));

        p.setPen(QPen(QColor(255, 255, 255, 180), 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(center.x() - halfWidth * 0.62,
                           center.y() + mouthRadiusY * 0.4),
                   QPointF(center.x() - halfWidth * 0.62,
                           bodyBottom - bottomRadiusY * 0.8));

        const QRectF mouth(center.x() - halfWidth,
                           center.y() - mouthRadiusY,
                           halfWidth * 2.0, mouthRadiusY * 2.0);
        const QColor marker = cupMouthColor(state);
        QColor mouthFill(255, 255, 255, 45);
        if (state == SampleTrayModel::TubeState::Allocated) {
            mouthFill = QColor(255, 215, 110, 85);
        }
        p.setPen(QPen(marker,
                      state == SampleTrayModel::TubeState::Allocated
                      ? 2.3 : 1.3));
        p.setBrush(mouthFill);
        p.drawEllipse(mouth);

        p.setPen(QPen(QColor(255, 255, 255, 210), 0.8));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(mouth.adjusted(1.6, 1.0, -1.6, -1.0));

        p.setPen(QPen(QColor(255, 255, 255, 235), 1.2));
        p.drawArc(mouth.adjusted(2.0, 1.5, -2.0, -1.5),
                  38 * 16, 105 * 16);

        if (state == SampleTrayModel::TubeState::Allocated) {
            QColor glow = marker;
            glow.setAlpha(60);
            p.setPen(QPen(glow, 5.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(mouth.adjusted(-2.0, -1.5, 2.0, 1.5));
        }
    }

    void paintTubeNumber(QPainter &p, const QRectF &holeRect,
                         const QString &text, bool hasCup,
                         SampleTrayModel::TubeState state) const
    {
        QFont font(QStringLiteral("Microsoft YaHei"));
        font.setPixelSize(8);
        font.setBold(true);
        p.setFont(font);

        if (hasCup) {
            const QFontMetricsF metrics(font);
            const qreal pillWidth = qMin(holeRect.width() * 0.96,
                                         metrics.width(text) + 7.0);
            const qreal pillHeight = qMin(holeRect.height() * 1.15, 11.0);
            const QRectF pill(holeRect.center().x() - pillWidth / 2.0,
                              holeRect.center().y() - pillHeight / 2.0,
                              pillWidth, pillHeight);
            p.setPen(QPen(QColor(140, 180, 205, 170), 0.6));
            p.setBrush(QColor(255, 255, 255, 220));
            p.drawRoundedRect(pill, 3.0, 3.0);
            p.setPen(QColor("#1F4E79"));
            p.drawText(pill, Qt::AlignCenter, text);
            return;
        }

        // 空孔内部偏深：浅色字 + 深色落影，保证编号可读
        p.setPen(QColor(20, 45, 62, 170));
        p.drawText(holeRect.translated(0.6, 0.6), Qt::AlignCenter, text);
        QColor textColor = cupMouthColor(state).lighter(155);
        textColor.setAlpha(245);
        p.setPen(textColor);
        p.drawText(holeRect, Qt::AlignCenter, text);
    }

    void paintStirBar(QPainter &p, const QPointF &center,
                      qreal length, qreal thickness) const
    {
        p.save();
        p.translate(center);
        p.scale(1.0, 0.45);
        p.rotate(-14.0);

        const QRectF bar(-length / 2.0, -thickness / 2.0,
                         length, thickness);
        QLinearGradient gradient(bar.topLeft(), bar.bottomLeft());
        gradient.setColorAt(0.0, QColor("#C9D5DD"));
        gradient.setColorAt(0.45, QColor("#6F8290"));
        gradient.setColorAt(1.0, QColor("#33414C"));
        p.setPen(QPen(QColor("#27323A"), 0.7));
        p.setBrush(gradient);
        p.drawRoundedRect(bar, thickness / 2.0, thickness / 2.0);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#253039"));
        p.drawEllipse(QPointF(-length / 2.0 + thickness * 0.32, 0.0),
                      thickness * 0.32, thickness * 0.32);
        p.drawEllipse(QPointF(length / 2.0 - thickness * 0.32, 0.0),
                      thickness * 0.32, thickness * 0.32);
        p.restore();
    }

    int tubeAt(const QPoint &pos) const
    {
        if (!m_active) {
            return -1;
        }
        const RackGeometry geometry = rackGeometry();
        if (!geometry.valid()) {
            return -1;
        }

        int best = -1;
        qreal bestScore = 1.0;
        for (int i = 0; i < geometry.centers.size(); ++i) {
            const qreal rx = geometry.radiusX.at(i);
            const qreal ry = geometry.radiusY.at(i);
            const qreal dx = (pos.x() - geometry.centers.at(i).x())
                    / (rx * 1.5);
            const qreal dy = (pos.y() - (geometry.centers.at(i).y()
                                         + ry * 0.5))
                    / (ry * 1.9);
            const qreal score = dx * dx + dy * dy;
            if (score <= 1.0 && score < bestScore) {
                bestScore = score;
                best = i;
            }
        }
        if (best < 0) {
            return -1;
        }
        const int row = best / SampleTrayModel::ColumnsPerTray;
        const int column = best % SampleTrayModel::ColumnsPerTray;
        return SampleTrayModel::tubeIndex(m_trayIndex, row, column);
    }

    SampleTrayModel *m_model = nullptr;
    int m_trayIndex = 0;
    bool m_active = true;
    int m_hoverIndex = -1;
    ClickHandler m_clickHandler;
};

TubeTrayDialog::TubeTrayDialog(SampleTrayModel *model, QWidget *parent)
    : FramelessDialog(tr("试管盘状态"), parent)
    , m_model(model)
{
    setModal(false);
    const int trayCount = m_model
            ? m_model->activeTrayCount() : SampleTrayModel::MaxTrays;
    resize(qMax(940, 40 + trayCount * 290 + (trayCount - 1) * 12), 700);
    setMinimumSize(720, 560);

    setStyleSheet(QStringLiteral(
        "QDialog#framelessDialog {"
        "  background: #F4F8FC;"
        "  border: 1px solid #C5D4E4;"
        "}"
        "QWidget#tubeTrayContent { background: #F4F8FC; }"
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
        "QLabel#traySummary {"
        "  color: #5D7386;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "}"
        "QLabel#trayLegend {"
        "  color: #61788B;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 11px;"
        "}"
        "QPushButton#trayActionButton {"
        "  background: #FFFFFF;"
        "  color: #1F4E79;"
        "  border: 1px solid #A9C8DF;"
        "  border-radius: 6px;"
        "  padding: 5px 14px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QPushButton#trayActionButton:hover { background: #E8F1FA; }"
        "QPushButton#trayActionButton:pressed { background: #D3E6F5; }"
        "QLabel#trayHint {"
        "  color: #8A9CAB;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 11px;"
        "}"));

    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("tubeTrayContent"));
    auto *root = new QVBoxLayout(content);
    root->setContentsMargins(20, 16, 20, 14);
    root->setSpacing(10);

    auto *header = new QHBoxLayout;
    auto *titleBox = new QVBoxLayout;
    titleBox->setSpacing(2);
    auto *title = new QLabel(tr("试管盘状态"), content);
    title->setObjectName(QStringLiteral("pageTitle"));
    m_subtitleLabel = new QLabel(content);
    m_subtitleLabel->setObjectName(QStringLiteral("pageSubtitle"));
    titleBox->addWidget(title);
    titleBox->addWidget(m_subtitleLabel);
    header->addLayout(titleBox);
    header->addStretch(1);

    m_summaryLabel = new QLabel(content);
    m_summaryLabel->setObjectName(QStringLiteral("traySummary"));
    m_summaryLabel->setTextFormat(Qt::RichText);
    m_summaryLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header->addWidget(m_summaryLabel, 0, Qt::AlignVCenter);
    root->addLayout(header);

    m_trayRow = new QWidget(content);
    m_trayRow->setObjectName(QStringLiteral("trayRow"));
    m_trayLayout = new QHBoxLayout(m_trayRow);
    m_trayLayout->setContentsMargins(0, 0, 0, 0);
    m_trayLayout->setSpacing(12);
    root->addWidget(m_trayRow, 1);
    rebuildTrayWidgets();

    auto *legend = new QLabel(
                tr("<span style='color:#6B8194;'>○</span> 空闲（透明杯）"
                   "&nbsp;&nbsp;<span style='color:#E0A21A;'>●</span> "
                   "分配（杯口标记）"
                   "&nbsp;&nbsp;<span style='color:#1F6FA5;'>○</span> "
                   "已加样（孔内无杯）"
                   "&nbsp;&nbsp;<span style='color:#1E7B45;'>○</span> "
                   "已使用（孔内无杯）"),
                content);
    legend->setObjectName(QStringLiteral("trayLegend"));
    legend->setTextFormat(Qt::RichText);
    root->addWidget(legend);

    auto *footer = new QHBoxLayout;
    m_hintLabel = new QLabel(
                tr("维护提示：点击试管可打开状态维护菜单；每盘下方「更换」会将"
                   "该盘全部试管恢复为空闲。"),
                content);
    m_hintLabel->setObjectName(QStringLiteral("trayHint"));
    footer->addWidget(m_hintLabel, 1);
    root->addLayout(footer);
    setContentWidget(content);

    if (m_model) {
        connect(m_model, &SampleTrayModel::modelChanged,
                this, &TubeTrayDialog::refresh);
    }
    refresh();
}

void TubeTrayDialog::refresh()
{
    if (!m_model) {
        return;
    }

    const int activeTrays = m_model->activeTrayCount();
    const int activeTubes = activeTrays * SampleTrayModel::TubesPerTray;
    m_subtitleLabel->setText(
                tr("%1 · %2 个试管盘 · 每盘 %3 行 × %4 列 = %5 支 · 共 %6 支")
                .arg(QString::fromLatin1(
                         machineModelName(m_model->machineModel())))
                .arg(activeTrays)
                .arg(SampleTrayModel::RowsPerTray)
                .arg(SampleTrayModel::ColumnsPerTray)
                .arg(SampleTrayModel::TubesPerTray)
                .arg(activeTubes));

    m_summaryLabel->setText(
                tr("<span style='color:#6B8194;'>空闲 %1</span>"
                   "&nbsp;&nbsp;<span style='color:#966A16;'>分配 %2</span>"
                   "&nbsp;&nbsp;<span style='color:#1F6FA5;'>已加样 %3</span>"
                   "&nbsp;&nbsp;<span style='color:#1E7B45;'>已使用 %4</span>")
                .arg(m_model->countState(SampleTrayModel::TubeState::Free))
                .arg(m_model->countState(SampleTrayModel::TubeState::Allocated))
                .arg(m_model->countState(SampleTrayModel::TubeState::Sampled))
                .arg(m_model->countState(SampleTrayModel::TubeState::Used)));

    if (m_grids.size() != activeTrays) {
        rebuildTrayWidgets();
    }
    for (TrayGridWidget *grid : m_grids) {
        grid->setActive(true);
        grid->update();
    }
}

void TubeTrayDialog::rebuildTrayWidgets()
{
    if (!m_model || !m_trayLayout || !m_trayRow) {
        return;
    }

    const int trayCount = m_model->activeTrayCount();
    if (m_grids.size() == trayCount && m_trayLayout->count() == trayCount) {
        return;
    }

    while (QLayoutItem *item = m_trayLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    m_grids.clear();

    for (int tray = 0; tray < trayCount; ++tray) {
        auto *wrapper = new QWidget(m_trayRow);
        auto *wrapperLayout = new QVBoxLayout(wrapper);
        wrapperLayout->setContentsMargins(0, 0, 0, 0);
        wrapperLayout->setSpacing(8);

        auto *grid = new TrayGridWidget(m_model, tray, wrapper);
        grid->setClickHandler([this](int index, const QPoint &globalPos) {
            handleTubeClicked(index, globalPos);
        });
        wrapperLayout->addWidget(grid, 1);

        auto *replaceRow = new QHBoxLayout;
        replaceRow->addStretch(1);
        auto *replaceButton = new QPushButton(tr("更换"), wrapper);
        replaceButton->setObjectName(QStringLiteral("trayActionButton"));
        replaceButton->setCursor(Qt::PointingHandCursor);
        replaceButton->setToolTip(
                    tr("将该盘全部试管恢复为空闲，用于更换新盘"));
        connect(replaceButton, &QPushButton::clicked,
                this, [this, tray]() {
            emit trayReplaceRequested(tray);
        });
        replaceRow->addWidget(replaceButton);
        wrapperLayout->addLayout(replaceRow);

        m_trayLayout->addWidget(wrapper, 1);
        m_grids.append(grid);
    }
}

void TubeTrayDialog::handleTubeClicked(int tubeIndex,
                                       const QPoint &globalPos)
{
    if (!m_model || !SampleTrayModel::isValidTubeIndex(tubeIndex)) {
        return;
    }
    const SampleTrayModel::TubeInfo info = m_model->tubeInfo(tubeIndex);
    const SampleTrayModel::TubeState state = info.state;

    QMenu menu(this);
    QAction *header = menu.addAction(
                tr("试管 %1 · 第 %2 行第 %3 列 · %4")
                .arg(tubeIndex)
                .arg(SampleTrayModel::rowOf(tubeIndex) + 1)
                .arg(SampleTrayModel::columnOf(tubeIndex) + 1)
                .arg(SampleTrayModel::stateText(state)));
    header->setEnabled(false);
    if (!info.sampleNumber.isEmpty()) {
        QAction *sample = menu.addAction(
                    tr("样本：%1").arg(info.sampleNumber));
        sample->setEnabled(false);
    }
    menu.addSeparator();

    QAction *allocateAction = nullptr;
    QAction *sampleAction = nullptr;
    QAction *usedAction = nullptr;
    QAction *clearAction = nullptr;

    switch (state) {
    case SampleTrayModel::TubeState::Free:
        allocateAction = menu.addAction(tr("分配样本…"));
        break;
    case SampleTrayModel::TubeState::Allocated:
        sampleAction = menu.addAction(tr("标记为已加样"));
        clearAction = menu.addAction(tr("释放为空闲"));
        break;
    case SampleTrayModel::TubeState::Sampled:
        usedAction = menu.addAction(tr("标记为已使用"));
        clearAction = menu.addAction(tr("释放为空闲"));
        break;
    case SampleTrayModel::TubeState::Used:
        clearAction = menu.addAction(tr("回收为空闲"));
        break;
    }

    QAction *selected = menu.exec(globalPos);
    if (!selected) {
        return;
    }

    QString error;
    bool ok = false;
    if (selected == allocateAction) {
        bool accepted = false;
        const QString sampleNumber = QInputDialog::getText(
                    this, tr("分配试管"),
                    tr("试管 %1 的样本号：").arg(tubeIndex),
                    QLineEdit::Normal, QString(), &accepted).trimmed();
        if (!accepted) {
            return;
        }
        if (sampleNumber.isEmpty()) {
            QMessageBox::warning(this, tr("分配试管"),
                                 tr("样本号不能为空。"));
            return;
        }
        ok = m_model->allocateTube(tubeIndex, sampleNumber, &error);
    } else if (selected == sampleAction) {
        ok = m_model->markSampled(tubeIndex, QString(), &error);
    } else if (selected == usedAction) {
        ok = m_model->markUsed(tubeIndex, &error);
    } else if (selected == clearAction) {
        ok = m_model->clearTube(tubeIndex, &error);
    }

    if (!ok && !error.isEmpty()) {
        QMessageBox::warning(this, tr("试管状态维护"), error);
    }
}

