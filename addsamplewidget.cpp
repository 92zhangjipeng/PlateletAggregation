#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "addsamplewidget.h"

#include "appconfig.h"

#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineF>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRadialGradient>
#include <QShowEvent>
#include <QToolTip>
#include <QVBoxLayout>
#include <QVector>
#include <QtMath>

namespace {

QPointF lerpPoint(const QPointF &from, const QPointF &to, qreal t)
{
    return from + (to - from) * t;
}

QString groupLetter(int index)
{
    return QString(QChar('A' + index));
}

} // namespace

/**
 * 血样区：按机型绘制可插采血管的 3D 采血管架。
 *
 * 每组 5 行 × 2 列 = 10 孔，组数按机型：
 * KS600=6 组（A~F）、KS800=8 组（A~H）、KS1200=12 组（A~L）。
 * 每个孔内画一支已插入的采血管：
 * - 全血模式：管底 60% 红色血液，上部 40% 淡黄色；
 * - 血浆模式：整管淡黄色。
 */
class BloodSampleAreaWidget : public QWidget
{
public:
    explicit BloodSampleAreaWidget(MachineModel model, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_model(model)
    {
        setMouseTracking(true);
        setMinimumSize(720, 360);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setMachineModel(MachineModel model)
    {
        if (m_model == model) {
            return;
        }
        m_model = model;
        m_hoverWell = -1;
        update();
    }

    void setPlasma(bool plasma)
    {
        if (m_plasma == plasma) {
            return;
        }
        m_plasma = plasma;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        const Geometry geometry = rackGeometry();
        if (!geometry.valid()) {
            return;
        }

        paintRack(p, geometry);
        paintHoles(p, geometry);
        paintRowLabels(p, geometry);

        // 后排先画，前排后画，形成遮挡关系。
        for (int row = 0; row < geometry.rows; ++row) {
            for (int column = 0; column < geometry.columns; ++column) {
                const int index = row * geometry.columns + column;
                paintTube(p, geometry.wells.at(index),
                          index == m_hoverWell);
            }
        }

        paintGroupLabels(p, geometry);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const int index = wellAt(event->pos());
        if (index != m_hoverWell) {
            m_hoverWell = index;
            update();
            if (index >= 0) {
                const Geometry geometry = rackGeometry();
                const Well &well = geometry.wells.at(index);
                const QString liquid = m_plasma
                        ? tr("血浆：整管淡黄色")
                        : tr("全血：下层红色 60% / 上层淡黄色 40%");
                QToolTip::showText(
                            event->globalPos(),
                            tr("采血管 %1 · 第 %2 行 · 第 %3 列\n%4")
                            .arg(groupLetter(well.group))
                            .arg(well.row + 1)
                            .arg(well.column + 1)
                            .arg(liquid),
                            this);
            } else {
                QToolTip::hideText();
            }
        }
        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hoverWell = -1;
        QToolTip::hideText();
        update();
        QWidget::leaveEvent(event);
    }

private:
    struct Well {
        QPointF center;      // 孔中心
        qreal holeRx = 0.0;
        qreal holeRy = 0.0;
        qreal bodyRadius = 0.0; // 采血管半径
        qreal tubeHeight = 0.0;
        int group = 0;
        int row = 0;
        int column = 0;      // 组内列号 0/1
    };

    struct Geometry {
        QPolygonF topFace;
        QPolygonF frontFace;
        QPolygonF leftSide;
        QPolygonF rightSide;
        QVector<Well> wells;
        QVector<QPointF> groupLabelPoints;
        QVector<QLineF> groupDividers;
        int rows = 5;
        int columns = 0;
        int groups = 0;
        qreal backY = 0.0;
        qreal frontY = 0.0;
        qreal wallHeight = 0.0;
        qreal bottomWidth = 0.0;
        qreal topWidth = 0.0;
        qreal frontLeft = 0.0;
        qreal pitch = 0.0;

        bool valid() const
        {
            return groups > 0 && columns == groups * 2
                    && wells.size() == rows * columns;
        }
    };

    Geometry rackGeometry() const
    {
        Geometry geometry;
        const QRectF area = QRectF(rect()).adjusted(10.0, 8.0, -10.0, -8.0);
        const int groups = machineBloodSampleGroupCount(m_model);
        const int columns = groups * 2;
        const int rows = 5;
        if (area.width() < 240.0 || area.height() < 220.0) {
            return geometry;
        }

        geometry.groups = groups;
        geometry.columns = columns;
        geometry.rows = rows;

        const qreal headerSpace = 56.0;  // 组名标签 + 后排采血管顶部空间
        const qreal shadowSpace = 16.0;
        const qreal wallHeight = qBound<qreal>(28.0, area.height() * 0.07, 38.0);
        const qreal availableFace = area.height() - headerSpace
                - wallHeight - shadowSpace;
        const qreal faceHeight = qBound<qreal>(200.0, availableFace, 420.0);

        const qreal blockHeight = headerSpace + faceHeight
                + wallHeight + shadowSpace;
        const qreal blockTop = area.top()
                + qMax<qreal>(0.0, (area.height() - blockHeight) / 2.0);
        geometry.backY = blockTop + headerSpace;
        geometry.frontY = geometry.backY + faceHeight;
        geometry.wallHeight = wallHeight;

        // 左右各留出 28px，左侧放行号，避免满宽时行号被裁掉。
        const qreal maxWidth = area.width() - 56.0;

        // 组间距尽量大；窗口较窄时自动收小间距，保证采血管仍有足够宽度。
        qreal groupGapFactor = 0.85;
        qreal pitch = maxWidth
                / (columns + (groups - 1) * groupGapFactor + 1.25);
        if (pitch < 28.0 && groups > 1) {
            groupGapFactor = qMax<qreal>(
                        0.35,
                        (maxWidth / 28.0 - columns - 1.25)
                        / (groups - 1));
            pitch = maxWidth
                    / (columns + (groups - 1) * groupGapFactor + 1.25);
        }
        pitch = qBound<qreal>(22.0, pitch, 48.0);
        const qreal groupGap = pitch * groupGapFactor;
        const qreal sideMargin = pitch * 0.62;

        geometry.pitch = pitch;
        geometry.bottomWidth = columns * pitch
                + (groups - 1) * groupGap + 2.0 * sideMargin;
        geometry.topWidth = geometry.bottomWidth * 0.975;
        const qreal centerX = area.center().x();
        geometry.frontLeft = centerX - geometry.bottomWidth / 2.0;
        const qreal backLeft = centerX - geometry.topWidth / 2.0;
        const qreal frontRight = geometry.frontLeft + geometry.bottomWidth;
        const qreal backRight = backLeft + geometry.topWidth;

        geometry.topFace
                << QPointF(backLeft, geometry.backY)
                << QPointF(backRight, geometry.backY)
                << QPointF(frontRight, geometry.frontY)
                << QPointF(geometry.frontLeft, geometry.frontY);

        const QPointF wallDepth(0.0, geometry.wallHeight);
        geometry.frontFace
                << geometry.topFace.at(3) << geometry.topFace.at(2)
                << geometry.topFace.at(2) + wallDepth
                << geometry.topFace.at(3) + wallDepth;

        const QPointF backWallDepth(0.0, geometry.wallHeight * 0.78);
        geometry.leftSide
                << geometry.topFace.at(0) << geometry.topFace.at(3)
                << geometry.topFace.at(3) + wallDepth
                << geometry.topFace.at(0) + backWallDepth;
        geometry.rightSide
                << geometry.topFace.at(1) << geometry.topFace.at(2)
                << geometry.topFace.at(2) + wallDepth
                << geometry.topFace.at(1) + backWallDepth;

        const qreal rowFractions[5] = {0.10, 0.30, 0.50, 0.70, 0.90};
        const qreal rowPitch = faceHeight / qreal(rows);

        // 按“行 → 组 → 组内列”存放，保证 wells 是行优先：
        // index = row * columns + column，绘制和行号都依赖这个顺序。
        for (int row = 0; row < rows; ++row) {
            const qreal fraction = rowFractions[row];
            const qreal scale = 0.84 + 0.04 * row;

            for (int group = 0; group < groups; ++group) {
                for (int localColumn = 0; localColumn < 2; ++localColumn) {
                    const int column = group * 2 + localColumn;
                    const qreal frontX = geometry.frontLeft + sideMargin
                            + column * pitch + group * groupGap
                            + pitch / 2.0;
                    const qreal u = (frontX - geometry.frontLeft)
                            / geometry.bottomWidth;
                    const qreal xTop = backLeft + u * geometry.topWidth;
                    const qreal xBottom = geometry.frontLeft
                            + u * geometry.bottomWidth;

                    Well well;
                    well.center = QPointF(
                                lerpPoint(QPointF(xTop, geometry.backY),
                                          QPointF(xBottom, geometry.frontY),
                                          fraction).x(),
                                geometry.backY + fraction * faceHeight);
                    well.holeRx = pitch * 0.36 * scale;
                    well.holeRy = well.holeRx * 0.42;
                    well.bodyRadius = well.holeRx * 0.72;
                    // 控制管高小于行距，避免相邻两排采血管首尾重叠成一条。
                    well.tubeHeight = qMin<qreal>(
                                rowPitch * 0.94,
                                well.bodyRadius * 2.0 * 4.6) * scale;
                    well.group = group;
                    well.row = row;
                    well.column = localColumn;
                    geometry.wells.append(well);
                }
            }
        }

        // 组名标在架子上方，位于该组两列的正中间。
        const qreal labelY = geometry.backY - 38.0;
        for (int group = 0; group < groups; ++group) {
            const qreal leftX = geometry.frontLeft + sideMargin
                    + group * 2 * pitch + group * groupGap + pitch / 2.0;
            const qreal rightX = leftX + pitch;
            const qreal uLeft = (leftX - geometry.frontLeft)
                    / geometry.bottomWidth;
            const qreal uRight = (rightX - geometry.frontLeft)
                    / geometry.bottomWidth;
            const qreal xTopLeft = backLeft + uLeft * geometry.topWidth;
            const qreal xTopRight = backLeft + uRight * geometry.topWidth;
            geometry.groupLabelPoints.append(
                        QPointF((xTopLeft + xTopRight) / 2.0, labelY));
        }

        // 组与组之间在台面上画一条分隔槽。
        for (int group = 1; group < groups; ++group) {
            const qreal offset = sideMargin + group * 2 * pitch
                    + (group - 1) * groupGap + groupGap / 2.0;
            const qreal u = offset / geometry.bottomWidth;
            geometry.groupDividers.append(
                        QLineF(QPointF(backLeft + u * geometry.topWidth,
                                       geometry.backY + 2.0),
                               QPointF(geometry.frontLeft
                                       + u * geometry.bottomWidth,
                                       geometry.frontY - 2.0)));
        }

        return geometry;
    }

    void paintRack(QPainter &p, const Geometry &geometry) const
    {
        // 桌面阴影
        const qreal bottomY = geometry.frontY + geometry.wallHeight;
        const QRectF shadowRect(geometry.frontLeft + 12.0, bottomY + 1.0,
                                geometry.bottomWidth - 24.0, 18.0);
        QRadialGradient shadow(shadowRect.center(),
                               qMax<qreal>(20.0, shadowRect.width() / 2.0));
        shadow.setColorAt(0.0, QColor(31, 78, 121, 55));
        shadow.setColorAt(1.0, QColor(31, 78, 121, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(shadow);
        p.drawEllipse(shadowRect);

        // 左右侧壁
        QLinearGradient sideGradient(0.0, geometry.backY,
                                     0.0, bottomY);
        sideGradient.setColorAt(0.0, QColor(151, 174, 193));
        sideGradient.setColorAt(1.0, QColor(116, 142, 163));
        p.setPen(QPen(QColor(104, 130, 151), 1.0));
        p.setBrush(sideGradient);
        p.drawPolygon(geometry.leftSide);
        p.drawPolygon(geometry.rightSide);

        // 前挡板
        QLinearGradient frontGradient(0.0, geometry.frontY,
                                      0.0, bottomY);
        frontGradient.setColorAt(0.0, QColor(201, 217, 230));
        frontGradient.setColorAt(1.0, QColor(158, 181, 200));
        p.setPen(QPen(QColor(133, 158, 178), 1.0));
        p.setBrush(frontGradient);
        p.drawPolygon(geometry.frontFace);

        // 前挡板中部压线
        const qreal seamY = geometry.frontY + geometry.wallHeight * 0.72;
        p.setPen(QPen(QColor(126, 151, 170, 80), 0.8));
        p.drawLine(QPointF(geometry.frontLeft + 12.0, seamY),
                   QPointF(geometry.frontLeft + geometry.bottomWidth - 12.0,
                           seamY));
        p.setPen(QPen(QColor(255, 255, 255, 90), 0.7));
        p.drawLine(QPointF(geometry.frontLeft + 12.0, seamY + 1.0),
                   QPointF(geometry.frontLeft + geometry.bottomWidth - 12.0,
                           seamY + 1.0));

        // 台面
        QLinearGradient topGradient(0.0, geometry.backY,
                                    0.0, geometry.frontY);
        topGradient.setColorAt(0.0, QColor(245, 249, 252));
        topGradient.setColorAt(0.45, QColor(226, 237, 244));
        topGradient.setColorAt(1.0, QColor(205, 220, 231));
        p.setPen(QPen(QColor(158, 182, 199), 1.1));
        p.setBrush(topGradient);
        p.drawPolygon(geometry.topFace);

        p.setPen(QPen(QColor(255, 255, 255, 235), 1.2));
        p.drawLine(geometry.topFace.at(0), geometry.topFace.at(1));
        p.setPen(QPen(QColor(255, 255, 255, 130), 0.9));
        p.drawLine(geometry.topFace.at(0), geometry.topFace.at(3));
        p.drawLine(geometry.topFace.at(1), geometry.topFace.at(2));
        p.setPen(QPen(QColor(112, 140, 162, 120), 0.8));
        p.drawLine(geometry.topFace.at(3), geometry.topFace.at(2));

        // 台面高光
        p.save();
        QPainterPath clip;
        clip.addPolygon(geometry.topFace);
        clip.closeSubpath();
        p.setClipPath(clip);
        p.setPen(QPen(QColor(255, 255, 255, 45), 12.0));
        p.drawLine(QPointF(geometry.frontLeft + 30.0,
                           geometry.frontY - 6.0),
                   QPointF(geometry.frontLeft + geometry.topWidth * 0.38,
                           geometry.backY + 6.0));
        p.restore();

        // 组间分隔槽
        for (const QLineF &line : geometry.groupDividers) {
            p.setPen(QPen(QColor(86, 113, 134, 45), 1.6));
            p.drawLine(line);
            p.setPen(QPen(QColor(255, 255, 255, 120), 0.7));
            p.drawLine(line.translated(1.0, 0.0));
        }

        // 前挡板四角螺钉
        const qreal screwY = geometry.frontY + geometry.wallHeight * 0.5;
        const qreal screwXs[2] = {geometry.frontLeft + 7.0,
                                  geometry.frontLeft
                                  + geometry.bottomWidth - 7.0};
        for (qreal x : screwXs) {
            p.setPen(QPen(QColor(125, 149, 168), 0.8));
            p.setBrush(QColor(190, 204, 216));
            p.drawEllipse(QPointF(x, screwY), 2.2, 2.2);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 170));
            p.drawEllipse(QPointF(x - 0.6, screwY - 0.7), 0.8, 0.8);
        }
    }

    void paintHoles(QPainter &p, const Geometry &geometry) const
    {
        for (int i = 0; i < geometry.wells.size(); ++i) {
            const Well &well = geometry.wells.at(i);

            // 孔外圈凸台
            p.setPen(QPen(QColor(255, 255, 255, 160), 0.8));
            p.setBrush(QColor(188, 207, 221, 95));
            p.drawEllipse(well.center, well.holeRx * 1.12,
                          well.holeRy * 1.12);

            // 孔内深色
            QRadialGradient hole(well.center
                                 + QPointF(-well.holeRx * 0.16,
                                           -well.holeRy * 0.18),
                                 qMax<qreal>(5.0, well.holeRx * 1.35));
            hole.setColorAt(0.0, QColor(39, 58, 72, 235));
            hole.setColorAt(0.62, QColor(79, 103, 120, 210));
            hole.setColorAt(1.0, QColor(151, 176, 194, 180));
            p.setPen(QPen(QColor(115, 141, 160, 190), 0.9));
            p.setBrush(hole);
            p.drawEllipse(well.center, well.holeRx, well.holeRy);

            // 孔口内沿阴影
            p.setPen(QPen(QColor(24, 40, 52, 110), 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawArc(QRectF(well.center.x() - well.holeRx * 0.92,
                             well.center.y() - well.holeRy * 0.92,
                             well.holeRx * 1.84, well.holeRy * 1.84),
                      20 * 16, 140 * 16);

            if (i == m_hoverWell) {
                p.setPen(QPen(QColor(44, 123, 191, 210), 1.6));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(well.center, well.holeRx * 1.28,
                              well.holeRy * 1.28);
            }
        }
    }

    void paintRowLabels(QPainter &p, const Geometry &geometry) const
    {
        QFont font(QStringLiteral("Microsoft YaHei"));
        font.setPixelSize(9);
        font.setBold(true);
        p.setFont(font);

        const qreal rackLeft = qMin(geometry.topFace.at(0).x(),
                                    geometry.topFace.at(3).x());
        for (int row = 0; row < geometry.rows; ++row) {
            const Well &well = geometry.wells.at(row * geometry.columns);
            const qreal y = well.center.y();
            const QRectF pill(rackLeft - 26.0, y - 8.0, 17.0, 16.0);
            p.setPen(QPen(QColor(174, 196, 212), 0.9));
            p.setBrush(QColor(255, 255, 255, 225));
            p.drawRoundedRect(pill, 3.0, 3.0);
            p.setPen(QColor("#5D7386"));
            p.drawText(pill, Qt::AlignCenter, QString::number(row + 1));

            p.setPen(QPen(QColor(128, 154, 173, 120), 0.8));
            p.drawLine(QPointF(pill.right() + 1.0, y),
                       QPointF(rackLeft - 3.0, y));
        }
    }

    void paintGroupLabels(QPainter &p, const Geometry &geometry) const
    {
        QFont font(QStringLiteral("Microsoft YaHei"));
        font.setPixelSize(11);
        font.setBold(true);
        p.setFont(font);

        for (int group = 0; group < geometry.groupLabelPoints.size();
             ++group) {
            const QPointF point = geometry.groupLabelPoints.at(group);
            const QRectF pill(point.x() - 11.0, point.y() - 8.0,
                              22.0, 16.0);
            p.setPen(QPen(QColor(169, 200, 223), 0.9));
            p.setBrush(QColor(232, 241, 250, 235));
            p.drawRoundedRect(pill, 4.0, 4.0);
            p.setPen(QColor("#1F4E79"));
            p.drawText(pill, Qt::AlignCenter, groupLetter(group));
        }
    }

    void paintTube(QPainter &p, const Well &well, bool hovered) const
    {
        const qreal bodyRadius = well.bodyRadius;
        const qreal capRadius = bodyRadius * 1.22;
        const qreal capHeight = qMax<qreal>(5.0, well.tubeHeight * 0.095);
        const qreal bodyTop = well.center.y() - well.tubeHeight
                + capHeight * 0.45;
        // 管身向孔内延伸，随后由孔口前侧壁遮住下段，形成插入深度。
        const qreal bodyBottom = well.center.y() + well.holeRy * 0.95;
        const qreal bodyHeight = bodyBottom - bodyTop;
        if (bodyHeight <= 2.0) {
            return;
        }

        if (hovered) {
            QRadialGradient glow(well.center
                                 + QPointF(0.0, -well.tubeHeight * 0.45),
                                 qMax<qreal>(14.0,
                                             well.tubeHeight * 0.62));
            glow.setColorAt(0.0, QColor(44, 123, 191, 95));
            glow.setColorAt(0.65, QColor(44, 123, 191, 30));
            glow.setColorAt(1.0, QColor(44, 123, 191, 0));
            p.setPen(Qt::NoPen);
            p.setBrush(glow);
            p.drawEllipse(well.center + QPointF(0.0,
                                                -well.tubeHeight * 0.45),
                          bodyRadius * 1.9, well.tubeHeight * 0.72);
        }

        // 采血管在台面上的投影
        QRadialGradient tubeShadow(
                    QPointF(well.center.x() + bodyRadius * 0.5,
                            well.center.y() + well.holeRy * 0.25),
                    bodyRadius * 1.7);
        tubeShadow.setColorAt(0.0, QColor(31, 52, 68, 70));
        tubeShadow.setColorAt(1.0, QColor(31, 52, 68, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(tubeShadow);
        p.drawEllipse(QPointF(well.center.x() + bodyRadius * 0.5,
                              well.center.y() + well.holeRy * 0.25),
                      bodyRadius * 1.5, well.holeRy * 1.3);

        const QRectF bodyRect(well.center.x() - bodyRadius, bodyTop,
                              bodyRadius * 2.0, bodyHeight);
        QPainterPath bodyPath;
        bodyPath.addRoundedRect(bodyRect, bodyRadius * 0.95,
                                bodyRadius * 0.95);

        // 管内液体
        p.save();
        p.setClipPath(bodyPath);
        const qreal liquidTop = bodyTop + bodyHeight * 0.10;
        const qreal liquidBottom = well.center.y() + well.holeRy * 0.15;
        const qreal liquidHeight = liquidBottom - liquidTop;
        const qreal boundary = liquidBottom - liquidHeight * 0.60;

        const QColor redTop(196, 58, 70);
        const QColor redBottom(158, 31, 43);
        const QColor yellowTop(247, 233, 181);
        const QColor yellowBottom(233, 212, 140);

        if (m_plasma) {
            QLinearGradient yellow(0.0, liquidTop, 0.0, liquidBottom);
            yellow.setColorAt(0.0, yellowTop);
            yellow.setColorAt(1.0, yellowBottom);
            p.fillRect(QRectF(well.center.x() - bodyRadius, liquidTop,
                              bodyRadius * 2.0, liquidHeight),
                       yellow);
        } else {
            QLinearGradient red(0.0, boundary, 0.0, liquidBottom);
            red.setColorAt(0.0, redTop);
            red.setColorAt(1.0, redBottom);
            p.fillRect(QRectF(well.center.x() - bodyRadius, boundary,
                              bodyRadius * 2.0, liquidBottom - boundary),
                       red);

            QLinearGradient yellow(0.0, liquidTop, 0.0, boundary);
            yellow.setColorAt(0.0, yellowTop);
            yellow.setColorAt(1.0, QColor(240, 218, 154));
            p.fillRect(QRectF(well.center.x() - bodyRadius, liquidTop,
                              bodyRadius * 2.0, boundary - liquidTop),
                       yellow);

            p.setPen(QPen(QColor(255, 238, 198, 150), 0.9));
            p.drawLine(QPointF(well.center.x() - bodyRadius, boundary),
                       QPointF(well.center.x() + bodyRadius, boundary));
        }

        // 液面
        p.setPen(QPen(QColor(255, 255, 255, 150), 0.7));
        p.setBrush(QColor(255, 255, 255, 38));
        p.drawEllipse(QPointF(well.center.x(), liquidTop),
                      bodyRadius * 0.90, bodyRadius * 0.20);
        p.restore();

        // 玻璃管高光/边缘
        p.save();
        p.setClipPath(bodyPath);
        QLinearGradient glass(well.center.x() - bodyRadius, 0.0,
                              well.center.x() + bodyRadius, 0.0);
        glass.setColorAt(0.0, QColor(255, 255, 255, 165));
        glass.setColorAt(0.20, QColor(255, 255, 255, 22));
        glass.setColorAt(0.50, QColor(255, 255, 255, 6));
        glass.setColorAt(0.78, QColor(255, 255, 255, 28));
        glass.setColorAt(1.0, QColor(255, 255, 255, 175));
        p.fillRect(bodyRect, glass);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 105));
        p.drawRoundedRect(QRectF(well.center.x() - bodyRadius * 0.55,
                                 bodyTop + bodyHeight * 0.05,
                                 bodyRadius * 0.20,
                                 bodyHeight * 0.88),
                          bodyRadius * 0.10, bodyRadius * 0.10);
        p.restore();

        p.setPen(QPen(QColor(112, 144, 166, 180), 0.9));
        p.setBrush(Qt::NoBrush);
        p.drawPath(bodyPath);
        p.setPen(QPen(QColor(70, 100, 120, 80), 0.7));
        p.drawLine(QPointF(well.center.x() + bodyRadius * 0.72, bodyTop + 3.0),
                   QPointF(well.center.x() + bodyRadius * 0.72,
                           bodyBottom - 3.0));

        // 管帽
        const QRectF capRect(well.center.x() - capRadius,
                             bodyTop - capHeight * 0.85,
                             capRadius * 2.0, capHeight);
        QLinearGradient capGradient(0.0, capRect.top(),
                                    0.0, capRect.bottom());
        capGradient.setColorAt(0.0, QColor(246, 250, 252));
        capGradient.setColorAt(0.55, QColor(219, 231, 239));
        capGradient.setColorAt(1.0, QColor(190, 207, 220));
        p.setPen(QPen(QColor(144, 168, 185), 0.9));
        p.setBrush(capGradient);
        p.drawRoundedRect(capRect, capHeight * 0.42, capHeight * 0.42);

        p.setPen(QPen(QColor(127, 153, 172, 130), 0.7));
        p.drawLine(QPointF(capRect.left() + 1.0,
                           capRect.bottom() - capHeight * 0.26),
                   QPointF(capRect.right() - 1.0,
                           capRect.bottom() - capHeight * 0.26));
        p.setPen(QPen(QColor(255, 255, 255, 190), 0.8));
        p.drawLine(QPointF(capRect.left() + capHeight * 0.30,
                           capRect.top() + 1.1),
                   QPointF(capRect.right() - capHeight * 0.30,
                           capRect.top() + 1.1));

        // 孔内前侧壁：盖住管身下段，形成采血管插进孔内的深度感。
        const QRectF holeRect(well.center.x() - well.holeRx,
                              well.center.y() - well.holeRy,
                              well.holeRx * 2.0, well.holeRy * 2.0);
        QPainterPath holePath;
        holePath.addEllipse(holeRect);
        p.save();
        p.setClipPath(holePath);
        QLinearGradient innerWall(0.0, well.center.y(),
                                  0.0, well.center.y()
                                  + well.holeRy * 1.2);
        innerWall.setColorAt(0.0, QColor(32, 49, 62, 245));
        innerWall.setColorAt(0.55, QColor(55, 76, 92, 245));
        innerWall.setColorAt(1.0, QColor(115, 141, 159, 245));
        p.setPen(Qt::NoPen);
        p.setBrush(innerWall);
        p.drawRect(QRectF(well.center.x() - well.holeRx,
                          well.center.y(),
                          well.holeRx * 2.0, well.holeRy * 1.2));
        p.restore();

        // 孔口前沿高光
        p.setPen(QPen(QColor(205, 225, 239, 230), 1.3));
        p.setBrush(Qt::NoBrush);
        p.drawArc(holeRect, 195 * 16, 150 * 16);
    }

    int wellAt(const QPoint &position) const
    {
        const Geometry geometry = rackGeometry();
        if (!geometry.valid()) {
            return -1;
        }

        // 从前排往后排命中，重叠时优先选前排。
        for (int i = geometry.wells.size() - 1; i >= 0; --i) {
            const Well &well = geometry.wells.at(i);
            const QRectF hitRect(well.center.x() - well.bodyRadius * 1.25,
                                 well.center.y() - well.tubeHeight - 3.0,
                                 well.bodyRadius * 2.5,
                                 well.tubeHeight + well.holeRy * 2.0 + 3.0);
            if (hitRect.contains(position)) {
                return i;
            }
        }
        return -1;
    }

    MachineModel m_model = defaultMachineModel();
    bool m_plasma = false;
    int m_hoverWell = -1;
};

AddSampleWidget::AddSampleWidget(MachineModel model, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
{
    setObjectName(QStringLiteral("addSamplePage"));
    setStyleSheet(QStringLiteral(
        "QWidget#addSamplePage { background: #F4F8FC; }"
        "QLabel#addSampleTitle {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 21px;"
        "  font-weight: 700;"
        "}"
        "QLabel#addSampleSubtitle {"
        "  color: #7A8FA3;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QFrame#bloodCard {"
        "  background: #FFFFFF;"
        "  border: 1px solid #DCE7F1;"
        "  border-radius: 10px;"
        "}"
        "QLabel#cardTitle {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 14px;"
        "  font-weight: 600;"
        "}"
        "QLabel#cardNote {"
        "  color: #7A8FA3;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 11px;"
        "}"));

    buildUi();
    updateSampleQuality();
}

void AddSampleWidget::setMachineModel(MachineModel model)
{
    m_model = model;
    if (m_bloodArea) {
        m_bloodArea->setMachineModel(model);
    }
    updateSampleQuality();
}

void AddSampleWidget::refreshMode()
{
    updateSampleQuality();
}

void AddSampleWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateSampleQuality();
}

void AddSampleWidget::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 18);
    root->setSpacing(12);

    auto *header = new QVBoxLayout;
    header->setSpacing(2);
    auto *title = new QLabel(tr("加样"), this);
    title->setObjectName(QStringLiteral("addSampleTitle"));
    m_subtitle = new QLabel(this);
    m_subtitle->setObjectName(QStringLiteral("addSampleSubtitle"));
    header->addWidget(title);
    header->addWidget(m_subtitle);
    root->addLayout(header);

    root->addWidget(createBloodAreaCard(), 1);
}

QFrame *AddSampleWidget::createBloodAreaCard()
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("bloodCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 12, 16, 14);
    layout->setSpacing(6);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel(tr("血样区"), card);
    title->setObjectName(QStringLiteral("cardTitle"));
    m_bloodCaption = new QLabel(card);
    m_bloodCaption->setObjectName(QStringLiteral("cardNote"));
    m_bloodCaption->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(m_bloodCaption);
    layout->addLayout(header);

    m_bloodArea = new BloodSampleAreaWidget(m_model, card);
    layout->addWidget(m_bloodArea, 1);
    return card;
}

void AddSampleWidget::updateSampleQuality()
{
    if (!m_subtitle || !m_bloodCaption || !m_bloodArea) {
        return;
    }

    const bool plasma = sampleQualityIsPlasma();
    m_bloodArea->setMachineModel(m_model);
    m_bloodArea->setPlasma(plasma);
    m_bloodArea->update();

    const int groups = machineBloodSampleGroupCount(m_model);
    const int wells = machineBloodSampleWellCount(m_model);
    const QString groupRange = QStringLiteral("%1~%2")
            .arg(groupLetter(0))
            .arg(groupLetter(groups - 1));

    m_subtitle->setText(
                tr("%1 · 样本质量：%2 · 血样区 %3 组（%4）× 10 孔 = %5 孔")
                .arg(QString::fromLatin1(machineModelName(m_model)))
                .arg(plasma ? tr("血浆") : tr("全血"))
                .arg(groups)
                .arg(groupRange)
                .arg(wells));

    m_bloodCaption->setText(
                plasma
                ? tr("每组 5 行 × 2 列 = 10 孔 · 采血管内全部淡黄色血浆")
                : tr("每组 5 行 × 2 列 = 10 孔 · 采血管下层红色 60% / "
                     "上层淡黄色 40%"));
}
