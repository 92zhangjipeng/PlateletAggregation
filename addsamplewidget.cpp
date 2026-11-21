#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "addsamplewidget.h"

#include "appconfig.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineF>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPushButton>
#include <QRadialGradient>
#include <QShowEvent>
#include <QStackedWidget>
#include <QToolTip>
#include <QVBoxLayout>
#include <QVector>
#include <QtMath>

namespace {

QPointF lerpPoint(const QPointF &from, const QPointF &to, qreal t)
{
    return from + (to - from) * t;
}

} // namespace

/**
 * 血样区：按机型绘制 2 行 × (组数 × 5 列) 的 3D 玻璃血样孔。
 * 每组 5 列 × 2 行 = 10 孔；KS600=6 组、KS800=8 组、KS1200=12 组。
 */
class BloodSampleAreaWidget : public QWidget
{
public:
    explicit BloodSampleAreaWidget(MachineModel model, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_model(model)
    {
        setMouseTracking(true);
        setMinimumSize(860, 250);
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

        // 桌面阴影
        const qreal plateWidth = geometry.topFace.at(2).x()
                - geometry.topFace.at(3).x();
        const QRectF shadowRect(geometry.topFace.at(3).x() + 10.0,
                                geometry.bottomY + geometry.thickness - 4.0,
                                plateWidth - 20.0, 18.0);
        QRadialGradient shadow(shadowRect.center(),
                               qMax<qreal>(10.0, shadowRect.width() / 2.0));
        shadow.setColorAt(0.0, QColor(31, 78, 121, 55));
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
        p.drawPolygon(geometry.frontFace);

        // 玻璃台面
        QLinearGradient topGradient(0.0, geometry.topY,
                                    0.0, geometry.bottomY);
        topGradient.setColorAt(0.0, QColor(242, 252, 255, 205));
        topGradient.setColorAt(0.5, QColor(220, 242, 252, 188));
        topGradient.setColorAt(1.0, QColor(193, 226, 243, 205));
        p.setPen(QPen(QColor(146, 190, 214, 240), 1.2));
        p.setBrush(topGradient);
        p.drawPolygon(geometry.topFace);

        p.setPen(QPen(QColor(255, 255, 255, 235), 1.4));
        p.drawLine(geometry.topFace.at(0), geometry.topFace.at(1));
        p.setPen(QPen(QColor(255, 255, 255, 150), 1.0));
        p.drawLine(geometry.topFace.at(0), geometry.topFace.at(3));
        p.setPen(QPen(QColor(255, 255, 255, 80), 1.0));
        p.drawLine(geometry.topFace.at(1), geometry.topFace.at(2));

        p.save();
        QPainterPath clip;
        clip.addPolygon(geometry.topFace);
        clip.closeSubpath();
        p.setClipPath(clip);
        p.setPen(QPen(QColor(255, 255, 255, 40), 9.0));
        p.drawLine(QPointF(geometry.topFace.at(3).x() + plateWidth * 0.08,
                           geometry.bottomY),
                   QPointF(geometry.topFace.at(0).x() + plateWidth * 0.42,
                           geometry.topY));
        p.setPen(QPen(QColor(255, 255, 255, 25), 4.0));
        p.drawLine(QPointF(geometry.topFace.at(3).x() + plateWidth * 0.55,
                           geometry.bottomY),
                   QPointF(geometry.topFace.at(0).x() + plateWidth * 0.82,
                           geometry.topY));
        p.restore();

        // 组间玻璃隔断
        p.setPen(QPen(QColor(120, 170, 200, 170), 1.2));
        for (const QLineF &line : geometry.groupDividers) {
            p.drawLine(line);
            p.setPen(QPen(QColor(255, 255, 255, 90), 0.7));
            p.drawLine(line.translated(1.0, 0.0));
            p.setPen(QPen(QColor(120, 170, 200, 170), 1.2));
        }

        // 行号
        QFont labelFont(QStringLiteral("Microsoft YaHei"));
        labelFont.setPixelSize(8);
        labelFont.setBold(true);
        p.setFont(labelFont);
        p.setPen(QColor("#5D7386"));
        const qreal leftX = qMin(geometry.topFace.at(0).x(),
                                 geometry.topFace.at(3).x()) - 12.0;
        for (int row = 0; row < geometry.rows; ++row) {
            const QPointF &point = geometry.centers.at(
                        row * geometry.columns);
            p.drawText(QRectF(leftX - 8.0, point.y() - 6.0, 14.0, 12.0),
                       Qt::AlignCenter, QString::number(row + 1));
        }

        // 组号
        for (int group = 0; group < geometry.groupLabelPoints.size();
             ++group) {
            const QPointF point = geometry.groupLabelPoints.at(group);
            p.drawText(QRectF(point.x() - 28.0, point.y() - 6.0,
                              56.0, 12.0),
                       Qt::AlignCenter,
                       tr("组 %1").arg(group + 1));
        }

        // 血样孔（后排先画）
        for (int row = 0; row < geometry.rows; ++row) {
            for (int column = 0; column < geometry.columns; ++column) {
                const int index = row * geometry.columns + column;
                paintWell(p,
                          geometry.centers.at(index),
                          geometry.radiusX.at(index),
                          geometry.radiusY.at(index),
                          index == m_hoverWell);
            }
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const int index = wellAt(event->pos());
        if (index != m_hoverWell) {
            m_hoverWell = index;
            update();
            if (index >= 0) {
                const int group = index / 10 + 1;
                const int local = index % 10;
                const int row = local / 5 + 1;
                const int column = local % 5 + 1;
                QToolTip::showText(
                            event->globalPos(),
                            tr("血样孔 %1\n组 %2 · 第 %3 行第 %4 列\n"
                               "等待插入血样管")
                            .arg(index + 1)
                            .arg(group)
                            .arg(row)
                            .arg(column),
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
    struct Geometry {
        QPolygonF topFace;
        QPolygonF frontFace;
        QVector<QPointF> centers;
        QVector<qreal> radiusX;
        QVector<qreal> radiusY;
        QVector<QPointF> groupLabelPoints;
        QVector<QLineF> groupDividers;
        int rows = 2;
        int columns = 0;
        qreal topY = 0.0;
        qreal bottomY = 0.0;
        qreal thickness = 0.0;

        bool valid() const
        {
            return columns > 0 && centers.size() == rows * columns;
        }
    };

    Geometry rackGeometry() const
    {
        Geometry geometry;
        const QRectF area = QRectF(rect()).adjusted(8.0, 8.0, -8.0, -8.0);
        const int groupCount = machineBloodSampleGroupCount(m_model);
        const int columns = groupCount * 5;
        const int rows = 2;
        if (area.width() < 200.0 || area.height() < 120.0) {
            return geometry;
        }

        geometry.rows = rows;
        geometry.columns = columns;
        const qreal headerSpace = 24.0;
        const qreal availableHeight = area.bottom() - area.top()
                - headerSpace - 10.0;
        const qreal plateHeight = qMin<qreal>(
                    qMax<qreal>(100.0, availableHeight), 170.0);
        geometry.topY = area.top() + headerSpace
                + (availableHeight - plateHeight) / 2.0;
        geometry.bottomY = geometry.topY + plateHeight;
        const qreal height = plateHeight;
        const qreal bottomWidth = qMax<qreal>(140.0, area.width() - 12.0);
        const qreal topWidth = bottomWidth * 0.985;
        const qreal centerX = area.center().x();

        geometry.topFace
                << QPointF(centerX - topWidth / 2.0, geometry.topY)
                << QPointF(centerX + topWidth / 2.0, geometry.topY)
                << QPointF(centerX + bottomWidth / 2.0, geometry.bottomY)
                << QPointF(centerX - bottomWidth / 2.0, geometry.bottomY);
        geometry.thickness = qBound<qreal>(8.0, height * 0.06, 14.0);
        const QPointF depth(0.0, geometry.thickness);
        geometry.frontFace
                << geometry.topFace.at(3) << geometry.topFace.at(2)
                << geometry.topFace.at(2) + depth
                << geometry.topFace.at(3) + depth;

        const QPointF topLeft = geometry.topFace.at(0);
        const QPointF topRight = geometry.topFace.at(1);
        const QPointF bottomRight = geometry.topFace.at(2);
        const QPointF bottomLeft = geometry.topFace.at(3);
        const qreal marginU = 0.008;
        const qreal rowFractions[2] = {0.30, 0.70};

        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                const qreal u = marginU
                        + (1.0 - 2.0 * marginU)
                          * (columns > 1
                             ? qreal(column) / (columns - 1) : 0.5);
                const QPointF topPoint = lerpPoint(topLeft, topRight, u);
                const QPointF bottomPoint = lerpPoint(bottomLeft,
                                                      bottomRight, u);
                geometry.centers.append(
                            lerpPoint(topPoint, bottomPoint,
                                      rowFractions[row]));
            }
        }

        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                const int index = row * columns + column;
                const QPointF &center = geometry.centers.at(index);
                qreal horizontal = 1.0e9;
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
                const int otherRow = row == 0 ? 1 : 0;
                const qreal vertical = QLineF(
                            center,
                            geometry.centers.at(
                                otherRow * columns + column)).length();
                const qreal radius = qMax<qreal>(
                            4.0, qMin(horizontal * 0.40,
                                      vertical * 0.48));
                geometry.radiusX.append(radius);
                geometry.radiusY.append(radius * 0.62);
            }
        }

        for (int group = 0; group < groupCount; ++group) {
            const int first = group * 5;
            const int last = first + 4;
            if (last >= columns) {
                break;
            }
            const qreal x = (geometry.centers.at(first).x()
                             + geometry.centers.at(last).x()) / 2.0;
            geometry.groupLabelPoints.append(
                        QPointF(x, geometry.topY - 8.0));
        }

        for (int group = 1; group < groupCount; ++group) {
            const qreal boundary = group * 5 - 0.5;
            const qreal u = marginU
                    + (1.0 - 2.0 * marginU)
                      * (boundary / (columns - 1));
            if (u <= 0.0 || u >= 1.0) {
                continue;
            }
            geometry.groupDividers.append(
                        QLineF(lerpPoint(topLeft, topRight, u),
                               lerpPoint(bottomLeft, bottomRight, u)));
        }

        return geometry;
    }

    void paintWell(QPainter &p, const QPointF &center,
                   qreal rx, qreal ry, bool hovered) const
    {
        if (hovered) {
            QRadialGradient glow(center, qMax<qreal>(10.0, rx * 2.4));
            glow.setColorAt(0.0, QColor(44, 123, 191, 100));
            glow.setColorAt(0.65, QColor(44, 123, 191, 35));
            glow.setColorAt(1.0, QColor(44, 123, 191, 0));
            p.setPen(Qt::NoPen);
            p.setBrush(glow);
            p.drawEllipse(center, rx * 1.8, ry * 2.0);
        }

        p.setPen(QPen(hovered ? QColor("#2C7BBF")
                              : QColor(150, 192, 216, 235),
                      hovered ? 1.8 : 1.0));
        p.setBrush(QColor(232, 246, 252, 165));
        p.drawEllipse(center, rx, ry);

        const QPointF inner = center + QPointF(0.0, ry * 0.14);
        QRadialGradient depth(inner, qMax<qreal>(6.0, rx));
        depth.setColorAt(0.0, QColor(42, 64, 78, 165));
        depth.setColorAt(0.60, QColor(94, 126, 144, 110));
        depth.setColorAt(1.0, QColor(203, 229, 243, 55));
        p.setPen(Qt::NoPen);
        p.setBrush(depth);
        p.drawEllipse(inner, rx * 0.80, ry * 0.78);

        // 孔内底部，提示等待插入血样管
        p.setPen(QPen(QColor(255, 255, 255, 90), 0.7));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(inner + QPointF(0.0, ry * 0.10),
                      rx * 0.50, ry * 0.42);

        p.setPen(QPen(QColor(255, 255, 255, 220), 0.9));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(center.x() - rx, center.y() - ry,
                         rx * 2.0, ry * 2.0),
                  35 * 16, 110 * 16);
    }

    int wellAt(const QPoint &position) const
    {
        const Geometry geometry = rackGeometry();
        if (!geometry.valid()) {
            return -1;
        }

        int best = -1;
        qreal bestScore = 1.0;
        for (int i = 0; i < geometry.centers.size(); ++i) {
            const qreal rx = geometry.radiusX.at(i);
            const qreal ry = geometry.radiusY.at(i);
            const qreal dx = (position.x() - geometry.centers.at(i).x())
                    / (rx * 1.6);
            const qreal dy = (position.y() - geometry.centers.at(i).y())
                    / (ry * 1.9);
            const qreal score = dx * dx + dy * dy;
            if (score <= 1.0 && score < bestScore) {
                bestScore = score;
                best = i;
            }
        }
        return best;
    }

    MachineModel m_model = defaultMachineModel();
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
        "QLabel#modeBadge {"
        "  border-radius: 11px;"
        "  padding: 4px 12px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QFrame#modeCard, QFrame#bloodCard {"
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
        "QLabel#readbackValue {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QLabel#cameraPlaceholder {"
        "  background: #1E2A35;"
        "  color: #A9C4D6;"
        "  border: 1px dashed #58788E;"
        "  border-radius: 8px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QLabel#plasmaPlaceholder {"
        "  background: #F8FBFD;"
        "  color: #7A8FA3;"
        "  border: 1px dashed #C9DCEB;"
        "  border-radius: 8px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QPushButton#modeActionButton {"
        "  background: #FFFFFF;"
        "  color: #1F4E79;"
        "  border: 1px solid #A9C8DF;"
        "  border-radius: 6px;"
        "  padding: 6px 12px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QPushButton#modeActionButton:disabled {"
        "  color: #AFC0CC;"
        "  border-color: #DCE7F1;"
        "  background: #F7FAFC;"
        "}"));

    buildUi();
    refreshMode();
}

void AddSampleWidget::setMachineModel(MachineModel model)
{
    m_model = model;
    if (m_bloodArea) {
        m_bloodArea->setMachineModel(model);
    }
    updateModeLabels();
}

void AddSampleWidget::refreshMode()
{
    updateModeLabels();
}

void AddSampleWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateModeLabels();
}

void AddSampleWidget::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 18);
    root->setSpacing(12);

    auto *header = new QHBoxLayout;
    auto *titleBox = new QVBoxLayout;
    titleBox->setSpacing(2);
    auto *title = new QLabel(tr("加样"), this);
    title->setObjectName(QStringLiteral("addSampleTitle"));
    m_subtitle = new QLabel(this);
    m_subtitle->setObjectName(QStringLiteral("addSampleSubtitle"));
    titleBox->addWidget(title);
    titleBox->addWidget(m_subtitle);
    header->addLayout(titleBox);
    header->addStretch(1);
    m_modeBadge = new QLabel(this);
    m_modeBadge->setObjectName(QStringLiteral("modeBadge"));
    header->addWidget(m_modeBadge, 0, Qt::AlignVCenter);
    root->addLayout(header);

    m_modeStack = new QStackedWidget(this);
    m_modeStack->addWidget(createWholeBloodCard());
    m_modeStack->addWidget(createPlasmaCard());
    m_modeStack->setMinimumHeight(225);
    root->addWidget(m_modeStack);

    root->addWidget(createBloodAreaCard(), 1);
}

QFrame *AddSampleWidget::createWholeBloodCard()
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("modeCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);

    auto *title = new QLabel(tr("全血模式"), card);
    title->setObjectName(QStringLiteral("cardTitle"));
    auto *note = new QLabel(
                tr("全血加样需要测高，并用摄像头拍照识别血样高度；"
                   "识别与测高流程后续设计。"), card);
    note->setObjectName(QStringLiteral("cardNote"));
    note->setWordWrap(true);
    layout->addWidget(title);
    layout->addWidget(note);

    auto *body = new QHBoxLayout;
    body->setSpacing(14);
    auto *camera = new QLabel(tr("摄像头画面\n（后续接入）"), card);
    camera->setObjectName(QStringLiteral("cameraPlaceholder"));
    camera->setAlignment(Qt::AlignCenter);
    camera->setMinimumHeight(120);
    camera->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    body->addWidget(camera, 2);

    auto *side = new QVBoxLayout;
    side->setSpacing(8);
    auto *result = new QLabel(tr("识别结果：--"), card);
    result->setObjectName(QStringLiteral("readbackValue"));
    auto *height = new QLabel(tr("血样高度：-- mm"), card);
    height->setObjectName(QStringLiteral("readbackValue"));
    auto *capture = new QPushButton(tr("拍照测高"), card);
    capture->setObjectName(QStringLiteral("modeActionButton"));
    capture->setEnabled(false);
    auto *retry = new QPushButton(tr("重新识别"), card);
    retry->setObjectName(QStringLiteral("modeActionButton"));
    retry->setEnabled(false);
    side->addWidget(result);
    side->addWidget(height);
    side->addWidget(capture);
    side->addWidget(retry);
    side->addStretch(1);
    body->addLayout(side, 1);
    layout->addLayout(body, 1);
    return card;
}

QFrame *AddSampleWidget::createPlasmaCard()
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("modeCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);

    auto *title = new QLabel(tr("血降模式"), card);
    title->setObjectName(QStringLiteral("cardTitle"));
    auto *note = new QLabel(
                tr("血降（血浆）模式使用另一套加样界面；样本队列、"
                   "加样操作与流程后续设计。"), card);
    note->setObjectName(QStringLiteral("cardNote"));
    note->setWordWrap(true);
    layout->addWidget(title);
    layout->addWidget(note);

    auto *body = new QHBoxLayout;
    body->setSpacing(14);
    auto *placeholder = new QLabel(
                tr("血降样本队列 / 加样操作\n（后续设计）"), card);
    placeholder->setObjectName(QStringLiteral("plasmaPlaceholder"));
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setMinimumHeight(120);
    placeholder->setSizePolicy(QSizePolicy::Expanding,
                               QSizePolicy::Expanding);
    body->addWidget(placeholder, 2);

    auto *side = new QVBoxLayout;
    side->setSpacing(8);
    auto *current = new QLabel(tr("当前样本：--"), card);
    current->setObjectName(QStringLiteral("readbackValue"));
    auto *pending = new QLabel(tr("待加样：--"), card);
    pending->setObjectName(QStringLiteral("readbackValue"));
    auto *start = new QPushButton(tr("开始加样"), card);
    start->setObjectName(QStringLiteral("modeActionButton"));
    start->setEnabled(false);
    auto *refresh = new QPushButton(tr("刷新队列"), card);
    refresh->setObjectName(QStringLiteral("modeActionButton"));
    refresh->setEnabled(false);
    side->addWidget(current);
    side->addWidget(pending);
    side->addWidget(start);
    side->addWidget(refresh);
    side->addStretch(1);
    body->addLayout(side, 1);
    layout->addLayout(body, 1);
    return card;
}

QFrame *AddSampleWidget::createBloodAreaCard()
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("bloodCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 12, 16, 14);
    layout->setSpacing(8);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel(tr("血样区（全血 / 血降共用）"), card);
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

void AddSampleWidget::updateModeLabels()
{
    if (!m_modeStack || !m_modeBadge || !m_subtitle || !m_bloodCaption
            || !m_bloodArea) {
        return;
    }

    const bool plasma = sampleQualityIsPlasma();
    m_modeStack->setCurrentIndex(plasma ? 1 : 0);

    if (plasma) {
        m_modeBadge->setText(tr("血降模式"));
        m_modeBadge->setStyleSheet(QStringLiteral(
            "QLabel#modeBadge {"
            "  background: #EAF8F1;"
            "  color: #1E7B45;"
            "  border: 1px solid #A9D8BC;"
            "}"));
    } else {
        m_modeBadge->setText(tr("全血模式"));
        m_modeBadge->setStyleSheet(QStringLiteral(
            "QLabel#modeBadge {"
            "  background: #E8F1FA;"
            "  color: #1F4E79;"
            "  border: 1px solid #A9C8DF;"
            "}"));
    }

    const int groups = machineBloodSampleGroupCount(m_model);
    const int wells = groups * 10;
    m_subtitle->setText(
                tr("%1 · 当前样本质量：%2 · 血样区 %3 组 × 10 孔 = %4 孔")
                .arg(QString::fromLatin1(machineModelName(m_model)))
                .arg(plasma ? tr("血浆（血降）") : tr("全血"))
                .arg(groups)
                .arg(wells));
    m_bloodCaption->setText(
                tr("每组 5 列 × 2 行 = 10 孔，共 %1 孔 · 等待插入血样管")
                .arg(wells));

    m_bloodArea->setMachineModel(m_model);
    m_bloodArea->update();
}
