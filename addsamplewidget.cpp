#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "addsamplewidget.h"

#include "appconfig.h"
#include "testdatastore.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDate>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QLinearGradient>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPushButton>
#include <QRadialGradient>
#include <QSet>
#include <QShowEvent>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolTip>
#include <QVBoxLayout>
#include <QVector>
#include <QtMath>

#include <functional>
#include <utility>

namespace {

enum SampleColumn {
    SampleNoColumn = 0,
    BarcodeColumn,
    TestItemColumn,
    HeightColumn,
    PositionColumn,
    SampleColumnCount
};

const int kSampleRowHeight = 28;

QPointF lerpPoint(const QPointF &from, const QPointF &to, qreal t)
{
    return from + (to - from) * t;
}

QString groupLetter(int index)
{
    return QString(QChar('A' + index));
}

QString normalizedSampleNumber(QString number)
{
    number = number.trimmed();
    if (number.startsWith(QLatin1Char('t'))) {
        number[0] = QLatin1Char('T');
    }
    return number;
}

QString editorStyleSheet()
{
    return QStringLiteral(
                "QComboBox, QDoubleSpinBox {"
                "  border: 1px solid #A9C8DF;"
                "  border-radius: 4px;"
                "  background: #FFFFFF;"
                "  color: #1F4E79;"
                "  padding: 1px 4px;"
                "  font-family: 'Microsoft YaHei';"
                "  font-size: 12px;"
                "}");
}

/** 测试项目：可直接选 AA/ADP/COL/EPI/RIS，也可输入自定义项目。 */
class TestItemDelegate : public QStyledItemDelegate
{
public:
    explicit TestItemDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *combo = new QComboBox(parent);
        combo->setEditable(true);
        combo->setInsertPolicy(QComboBox::NoInsert);
        combo->addItems(QStringList()
                        << QStringLiteral("AA")
                        << QStringLiteral("ADP")
                        << QStringLiteral("COL")
                        << QStringLiteral("EPI")
                        << QStringLiteral("RIS"));
        combo->setStyleSheet(editorStyleSheet());
        return combo;
    }

    void setEditorData(QWidget *editor,
                       const QModelIndex &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (!combo) {
            return;
        }
        combo->setCurrentText(
                    index.model()->data(index, Qt::EditRole).toString());
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (!combo) {
            return;
        }
        model->setData(index, combo->currentText().trimmed(),
                       Qt::EditRole);
    }
};

/** 高度：全血为识别高度，血浆为吸样高度；空值显示 “—”。 */
class HeightDelegate : public QStyledItemDelegate
{
public:
    explicit HeightDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *spin = new QDoubleSpinBox(parent);
        spin->setRange(0.0, 999.99);
        spin->setDecimals(2);
        spin->setSingleStep(0.1);
        spin->setSpecialValueText(QStringLiteral("—"));
        spin->setFrame(false);
        spin->setStyleSheet(editorStyleSheet());
        return spin;
    }

    void setEditorData(QWidget *editor,
                       const QModelIndex &index) const override
    {
        auto *spin = qobject_cast<QDoubleSpinBox *>(editor);
        if (!spin) {
            return;
        }
        bool ok = false;
        const double value = index.model()
                ->data(index, Qt::EditRole).toString().toDouble(&ok);
        spin->setValue(ok ? value : 0.0);
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        auto *spin = qobject_cast<QDoubleSpinBox *>(editor);
        if (!spin) {
            return;
        }
        if (spin->value() <= 0.0) {
            model->setData(index, QString(), Qt::EditRole);
        } else {
            model->setData(index,
                           QString::number(spin->value(), 'f', 2),
                           Qt::EditRole);
        }
    }
};

/** 样本位置：下拉选择当前机型下未被其他样本占用的位置。 */
class PositionDelegate : public QStyledItemDelegate
{
public:
    using PositionsProvider = std::function<QStringList(int)>;

    explicit PositionDelegate(PositionsProvider provider,
                              QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
        , m_provider(std::move(provider))
    {
    }

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &,
                          const QModelIndex &index) const override
    {
        auto *combo = new QComboBox(parent);
        combo->addItems(m_provider(index.row()));
        combo->setStyleSheet(editorStyleSheet());
        return combo;
    }

    void setEditorData(QWidget *editor,
                       const QModelIndex &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (!combo) {
            return;
        }
        combo->setCurrentText(
                    index.model()->data(index, Qt::EditRole).toString());
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (!combo) {
            return;
        }
        model->setData(index, combo->currentText(), Qt::EditRole);
    }

private:
    PositionsProvider m_provider;
};

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

AddSampleWidget::AddSampleWidget(MachineModel model,
                                 TestDataStore *dataStore,
                                 QWidget *parent)
    : QWidget(parent)
    , m_model(model)
    , m_dataStore(dataStore)
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
        "QFrame#sampleCard, QFrame#bloodCard {"
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
        "}"
        "QLabel#tableHint {"
        "  color: #8A9CAB;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 11px;"
        "}"
        "QPushButton#sampleAction {"
        "  background: #FFFFFF;"
        "  color: #1F4E79;"
        "  border: 1px solid #A9C8DF;"
        "  border-radius: 6px;"
        "  padding: 5px 12px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QPushButton#sampleAction:hover { background: #EAF4FC; }"
        "QPushButton#sampleAction:pressed { background: #DCEBFA; }"
        "QPushButton#sampleAction:disabled {"
        "  color: #AFC0CC;"
        "  border-color: #DCE7F1;"
        "  background: #F7FAFC;"
        "}"
        "QTableWidget#sampleTable {"
        "  background: #FFFFFF;"
        "  alternate-background-color: #F7FAFD;"
        "  border: 1px solid #D8E5EF;"
        "  border-radius: 6px;"
        "  gridline-color: #E6EEF5;"
        "  selection-background-color: #DCEBFA;"
        "  selection-color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QTableWidget#sampleTable::item { padding: 2px 5px; }"
        "QTableWidget#sampleTable QHeaderView::section {"
        "  background: #EAF2F9;"
        "  color: #1F4E79;"
        "  border: none;"
        "  border-right: 1px solid #D8E5EF;"
        "  border-bottom: 1px solid #D8E5EF;"
        "  padding: 4px 6px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QTableCornerButton::section {"
        "  background: #EAF2F9;"
        "  border: none;"
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

    root->addWidget(createSampleTableCard());
    root->addWidget(createBloodAreaCard(), 1);
}

QFrame *AddSampleWidget::createSampleTableCard()
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("sampleCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);

    auto *titleRow = new QHBoxLayout;
    auto *title = new QLabel(tr("样本列表"), card);
    title->setObjectName(QStringLiteral("cardTitle"));
    m_countLabel = new QLabel(card);
    m_countLabel->setObjectName(QStringLiteral("cardNote"));
    m_countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    titleRow->addWidget(title);
    titleRow->addStretch(1);
    titleRow->addWidget(m_countLabel);
    layout->addLayout(titleRow);

    auto *toolRow = new QHBoxLayout;
    toolRow->setSpacing(8);
    m_addSampleButton = new QPushButton(tr("＋ 添加样本"), card);
    m_removeSampleButton = new QPushButton(tr("删除选中"), card);
    m_modifySampleButton = new QPushButton(tr("修改样本号"), card);
    for (QPushButton *button : {m_addSampleButton, m_removeSampleButton,
                                m_modifySampleButton}) {
        button->setObjectName(QStringLiteral("sampleAction"));
        button->setCursor(Qt::PointingHandCursor);
        toolRow->addWidget(button);
    }
    toolRow->addStretch(1);
    auto *hint = new QLabel(
                tr("样本号默认 T+日期+4位序号；双击单元格编辑，"
                   "双击样本号可修改"), card);
    hint->setObjectName(QStringLiteral("tableHint"));
    toolRow->addWidget(hint);
    layout->addLayout(toolRow);

    m_sampleTable = new QTableWidget(card);
    m_sampleTable->setObjectName(QStringLiteral("sampleTable"));
    m_sampleTable->setColumnCount(SampleColumnCount);
    m_sampleTable->setHorizontalHeaderLabels(QStringList()
        << tr("样本号") << tr("条码") << tr("测试项目")
        << tr("识别高度") << tr("样本位置"));
    m_sampleTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_sampleTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_sampleTable->setEditTriggers(QAbstractItemView::DoubleClicked
                                   | QAbstractItemView::EditKeyPressed);
    m_sampleTable->setAlternatingRowColors(true);
    m_sampleTable->setShowGrid(true);
    m_sampleTable->setWordWrap(false);
    m_sampleTable->setFrameShape(QFrame::NoFrame);
    m_sampleTable->setFocusPolicy(Qt::StrongFocus);
    m_sampleTable->setCornerButtonEnabled(false);
    m_sampleTable->verticalHeader()->setVisible(false);
    m_sampleTable->verticalHeader()->setDefaultSectionSize(kSampleRowHeight);
    m_sampleTable->horizontalHeader()->setHighlightSections(false);
    m_sampleTable->horizontalHeader()->setSectionsClickable(false);
    m_sampleTable->horizontalHeader()->setFixedHeight(30);
    m_sampleTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_sampleTable->horizontalHeader()->setSectionResizeMode(
                BarcodeColumn, QHeaderView::Stretch);
    m_sampleTable->setColumnWidth(SampleNoColumn, 160);
    m_sampleTable->setColumnWidth(TestItemColumn, 120);
    m_sampleTable->setColumnWidth(HeightColumn, 130);
    m_sampleTable->setColumnWidth(PositionColumn, 120);
    m_sampleTable->setMinimumHeight(150);
    m_sampleTable->setMaximumHeight(200);

    m_sampleTable->setItemDelegateForColumn(
                TestItemColumn, new TestItemDelegate(m_sampleTable));
    m_sampleTable->setItemDelegateForColumn(
                HeightColumn, new HeightDelegate(m_sampleTable));
    m_sampleTable->setItemDelegateForColumn(
                PositionColumn,
                new PositionDelegate(
                    [this](int row) { return positionsForRow(row); },
                    m_sampleTable));

    auto *sampleNoHeader = m_sampleTable->horizontalHeaderItem(
                SampleNoColumn);
    if (sampleNoHeader) {
        sampleNoHeader->setToolTip(
                    tr("T + 日期(yyyyMMdd) + 4位序号；双击可修改"));
    }
    auto *barcodeHeader = m_sampleTable->horizontalHeaderItem(
                BarcodeColumn);
    if (barcodeHeader) {
        barcodeHeader->setToolTip(tr("扫码或手动输入条码"));
    }
    auto *testItemHeader = m_sampleTable->horizontalHeaderItem(
                TestItemColumn);
    if (testItemHeader) {
        testItemHeader->setToolTip(tr("选择 AA/ADP/COL/EPI/RIS，或输入自定义项目"));
    }
    auto *heightHeader = m_sampleTable->horizontalHeaderItem(HeightColumn);
    if (heightHeader) {
        heightHeader->setToolTip(tr("全血为识别高度，血浆为吸样高度"));
    }
    auto *positionHeader = m_sampleTable->horizontalHeaderItem(
                PositionColumn);
    if (positionHeader) {
        positionHeader->setToolTip(tr("格式：组-行-列，例如 A-1-2"));
    }

    connect(m_addSampleButton, &QPushButton::clicked,
            this, &AddSampleWidget::addSample);
    connect(m_removeSampleButton, &QPushButton::clicked,
            this, &AddSampleWidget::removeSelectedSamples);
    connect(m_modifySampleButton, &QPushButton::clicked,
            this, &AddSampleWidget::modifySampleNumber);
    connect(m_sampleTable, &QTableWidget::itemChanged,
            this, &AddSampleWidget::handleTableItemChanged);
    connect(m_sampleTable, &QTableWidget::cellDoubleClicked,
            this, [this](int row, int column) {
        if (column == SampleNoColumn) {
            m_sampleTable->selectRow(row);
            m_sampleTable->setCurrentCell(row, SampleNoColumn);
            modifySampleNumber();
        }
    });

    layout->addWidget(m_sampleTable, 1);
    return card;
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
    updateHeightHeader();
    updateSampleCount();

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

void AddSampleWidget::updateHeightHeader()
{
    if (!m_sampleTable) {
        return;
    }
    auto *item = m_sampleTable->horizontalHeaderItem(HeightColumn);
    if (!item) {
        item = new QTableWidgetItem;
        m_sampleTable->setHorizontalHeaderItem(HeightColumn, item);
    }
    const bool plasma = sampleQualityIsPlasma();
    item->setText(plasma ? tr("吸样高度") : tr("识别高度"));
    item->setToolTip(plasma ? tr("血浆模式：吸样高度")
                            : tr("全血模式：识别高度"));
}

void AddSampleWidget::updateSampleCount()
{
    if (!m_countLabel || !m_sampleTable) {
        return;
    }
    m_countLabel->setText(
                tr("共 %1 个样本 · 血样区 %2 个位置")
                .arg(m_sampleTable->rowCount())
                .arg(machineBloodSampleWellCount(m_model)));
}

void AddSampleWidget::addSample()
{
    if (!m_sampleTable) {
        return;
    }

    const QString position = nextFreePosition();
    if (position.isEmpty()) {
        QMessageBox::information(
                    this, tr("无法添加样本"),
                    tr("血样区已满：当前机型最多 %1 个样本。")
                    .arg(machineBloodSampleWellCount(m_model)));
        return;
    }

    QString error;
    const QString sampleNumber = generateNextSampleNumber(&error);
    if (sampleNumber.isEmpty()) {
        QMessageBox::warning(
                    this, tr("无法添加样本"),
                    error.isEmpty()
                    ? tr("无法生成当天样本号，请检查测试数据库。")
                    : error);
        return;
    }

    appendSampleRow(sampleNumber, QString(), QString(), QString(),
                    position);
    qInfo() << "Add sample:" << sampleNumber << "position" << position;

    const int row = m_sampleTable->rowCount() - 1;
    m_sampleTable->selectRow(row);
    m_sampleTable->setCurrentCell(row, BarcodeColumn);
    m_sampleTable->scrollToBottom();
}

void AddSampleWidget::removeSelectedSamples()
{
    if (!m_sampleTable) {
        return;
    }
    const QModelIndexList selected =
            m_sampleTable->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        QMessageBox::information(this, tr("删除样本"),
                                 tr("请先选择要删除的样本行。"));
        return;
    }

    if (QMessageBox::question(
                this, tr("删除样本"),
                tr("确定删除选中的 %1 个样本？").arg(selected.size()),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    QSet<int> rows;
    for (const QModelIndex &index : selected) {
        rows.insert(index.row());
    }
    m_updatingTable = true;
    for (int row = m_sampleTable->rowCount() - 1; row >= 0; --row) {
        if (rows.contains(row)) {
            m_sampleTable->removeRow(row);
        }
    }
    m_updatingTable = false;
    qInfo() << "Removed sample rows:" << rows.size();
    updateSampleCount();
}

void AddSampleWidget::modifySampleNumber()
{
    if (!m_sampleTable) {
        return;
    }

    int row = m_sampleTable->currentRow();
    if (row < 0) {
        const QModelIndexList selected =
                m_sampleTable->selectionModel()->selectedRows();
        if (!selected.isEmpty()) {
            row = selected.first().row();
        }
    }
    if (row < 0 || row >= m_sampleTable->rowCount()) {
        QMessageBox::information(this, tr("修改样本号"),
                                 tr("请先选择一行样本。"));
        return;
    }

    QTableWidgetItem *item = m_sampleTable->item(row, SampleNoColumn);
    if (!item) {
        return;
    }

    QString value = item->text();
    while (true) {
        bool ok = false;
        const QString entered = QInputDialog::getText(
                    this, tr("修改样本号"),
                    tr("样本号（T + 日期 + 4位序号）："),
                    QLineEdit::Normal, value, &ok);
        if (!ok) {
            return;
        }

        value = normalizedSampleNumber(entered);
        if (value == item->text()) {
            return;
        }

        QString reason;
        if (validateSampleNumber(value, row, &reason)) {
            const QString oldNumber = item->text();
            m_updatingTable = true;
            item->setText(value);
            item->setData(Qt::UserRole, value);
            m_updatingTable = false;
            qInfo() << "Sample number changed:" << oldNumber << "->"
                    << value;
            m_sampleTable->setCurrentCell(row, SampleNoColumn);
            return;
        }

        QMessageBox::warning(this, tr("样本号不可用"), reason);
    }
}

void AddSampleWidget::handleTableItemChanged(QTableWidgetItem *item)
{
    if (!item || m_updatingTable) {
        return;
    }

    const int column = item->column();
    if (column == HeightColumn) {
        const QString text = item->text().trimmed();
        if (!text.isEmpty()) {
            bool ok = false;
            const double value = text.toDouble(&ok);
            if (!ok || value < 0.0 || value > 999.99) {
                QMessageBox::warning(
                            this, tr("高度无效"),
                            tr("请输入 0~999.99 之间的数字，或留空。"));
                restoreTableItem(item);
                return;
            }
        }
    } else if (column == PositionColumn) {
        const QString text = item->text().trimmed();
        if (!allPositions().contains(text)
                || positionInTable(text, item->row())) {
            QMessageBox::warning(
                        this, tr("样本位置无效"),
                        tr("该位置不存在或已被其他样本占用。\n"
                           "格式：组-行-列，例如 A-1-2。"));
            restoreTableItem(item);
            return;
        }
        if (item->text() != text) {
            m_updatingTable = true;
            item->setText(text);
            m_updatingTable = false;
        }
    }

    m_updatingTable = true;
    item->setData(Qt::UserRole, item->text());
    m_updatingTable = false;
}

QStringList AddSampleWidget::allPositions() const
{
    QStringList result;
    const int groups = machineBloodSampleGroupCount(m_model);
    for (int group = 0; group < groups; ++group) {
        for (int row = 0; row < 5; ++row) {
            for (int column = 0; column < 2; ++column) {
                result << QStringLiteral("%1-%2-%3")
                          .arg(groupLetter(group))
                          .arg(row + 1)
                          .arg(column + 1);
            }
        }
    }
    return result;
}

QStringList AddSampleWidget::positionsForRow(int row) const
{
    QString current;
    if (m_sampleTable && row >= 0
            && row < m_sampleTable->rowCount()) {
        if (QTableWidgetItem *item =
                m_sampleTable->item(row, PositionColumn)) {
            current = item->text().trimmed();
        }
    }

    const QStringList all = allPositions();
    QStringList result;
    for (const QString &position : all) {
        if (position == current || !positionInTable(position, row)) {
            result << position;
        }
    }
    if (!current.isEmpty() && !all.contains(current)
            && !result.contains(current)) {
        result.prepend(current);
    }
    return result;
}

QString AddSampleWidget::nextFreePosition() const
{
    const QStringList positions = allPositions();
    for (const QString &position : positions) {
        if (!positionInTable(position)) {
            return position;
        }
    }
    return QString();
}

QString AddSampleWidget::generateNextSampleNumber(QString *error) const
{
    if (error) {
        error->clear();
    }

    const QDate today = QDate::currentDate();
    const QString prefix = QStringLiteral("T%1")
            .arg(today.toString(QStringLiteral("yyyyMMdd")));

    QString databaseNext;
    if (m_dataStore) {
        if (!m_dataStore->isOpen()) {
            if (error) {
                *error = tr("测试数据库未打开，无法生成当天样本号。");
            }
            return QString();
        }
        QString databaseError;
        databaseNext = m_dataStore->nextSampleNumber(today, &databaseError);
        if (databaseNext.isEmpty()) {
            if (error) {
                *error = databaseError.isEmpty()
                        ? tr("无法读取测试数据库中的当天样本号。")
                        : databaseError;
            }
            return QString();
        }
    }

    int sequence = 1;
    if (!databaseNext.isEmpty()) {
        bool ok = false;
        const int parsed = databaseNext.mid(9).toInt(&ok);
        if (ok && parsed > 0) {
            sequence = parsed;
        }
    }

    for (int number = sequence; number <= 9999; ++number) {
        const QString candidate = prefix + QStringLiteral("%1")
                .arg(number, 4, 10, QLatin1Char('0'));
        if (!sampleNumberInTable(candidate)) {
            // databaseNext 已是数据库当天最大号 + 1，比它大的号不会重复。
            return candidate;
        }
    }

    if (error) {
        *error = tr("当天样本号已用完（0001~9999）。");
    }
    return QString();
}

bool AddSampleWidget::sampleNumberInTable(const QString &number,
                                          int exceptRow) const
{
    if (!m_sampleTable) {
        return false;
    }
    const QString trimmed = number.trimmed();
    for (int row = 0; row < m_sampleTable->rowCount(); ++row) {
        if (row == exceptRow) {
            continue;
        }
        if (QTableWidgetItem *item =
                m_sampleTable->item(row, SampleNoColumn)) {
            if (item->text().trimmed() == trimmed) {
                return true;
            }
        }
    }
    return false;
}

bool AddSampleWidget::positionInTable(const QString &position,
                                      int exceptRow) const
{
    if (!m_sampleTable) {
        return false;
    }
    const QString trimmed = position.trimmed();
    for (int row = 0; row < m_sampleTable->rowCount(); ++row) {
        if (row == exceptRow) {
            continue;
        }
        if (QTableWidgetItem *item =
                m_sampleTable->item(row, PositionColumn)) {
            if (item->text().trimmed() == trimmed) {
                return true;
            }
        }
    }
    return false;
}

bool AddSampleWidget::validateSampleNumber(const QString &number,
                                           int exceptRow,
                                           QString *reason) const
{
    if (reason) {
        reason->clear();
    }
    const QString trimmed = number.trimmed();
    if (trimmed.isEmpty()) {
        if (reason) {
            *reason = tr("样本号不能为空。");
        }
        return false;
    }
    if (sampleNumberInTable(trimmed, exceptRow)) {
        if (reason) {
            *reason = tr("表格中已存在该样本号，请更换。");
        }
        return false;
    }
    if (m_dataStore) {
        if (!m_dataStore->isOpen()) {
            if (reason) {
                *reason = tr("测试数据库未打开，无法校验样本号是否重复。");
            }
            return false;
        }
        if (m_dataStore->sampleNumberExists(trimmed)) {
            if (reason) {
                *reason = tr("样本数据库中已存在该样本号，请更换。");
            }
            return false;
        }
    }
    return true;
}

void AddSampleWidget::appendSampleRow(const QString &sampleNumber,
                                      const QString &barcode,
                                      const QString &testItem,
                                      const QString &height,
                                      const QString &position)
{
    if (!m_sampleTable) {
        return;
    }

    m_updatingTable = true;
    const int row = m_sampleTable->rowCount();
    m_sampleTable->insertRow(row);

    const auto makeItem = [](const QString &text, bool editable) {
        auto *item = new QTableWidgetItem(text);
        item->setTextAlignment(Qt::AlignCenter);
        Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
        if (editable) {
            flags |= Qt::ItemIsEditable;
        }
        item->setFlags(flags);
        item->setData(Qt::UserRole, text);
        return item;
    };

    m_sampleTable->setItem(row, SampleNoColumn,
                           makeItem(sampleNumber, false));
    m_sampleTable->setItem(row, BarcodeColumn, makeItem(barcode, true));
    m_sampleTable->setItem(row, TestItemColumn,
                           makeItem(testItem, true));
    m_sampleTable->setItem(row, HeightColumn, makeItem(height, true));
    m_sampleTable->setItem(row, PositionColumn,
                           makeItem(position, true));

    if (QTableWidgetItem *item = m_sampleTable->item(row, SampleNoColumn)) {
        item->setToolTip(tr("双击可修改样本号"));
    }
    if (QTableWidgetItem *item = m_sampleTable->item(row, HeightColumn)) {
        item->setToolTip(tr("全血为识别高度，血浆为吸样高度"));
    }
    if (QTableWidgetItem *item = m_sampleTable->item(row, PositionColumn)) {
        item->setToolTip(tr("组-行-列，例如 A-1-2"));
    }

    m_sampleTable->setRowHeight(row, kSampleRowHeight);
    m_updatingTable = false;
    updateSampleCount();
}

void AddSampleWidget::restoreTableItem(QTableWidgetItem *item)
{
    if (!item) {
        return;
    }
    m_updatingTable = true;
    item->setText(item->data(Qt::UserRole).toString());
    m_updatingTable = false;
}
