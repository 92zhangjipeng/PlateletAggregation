#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "reagentbottlepanel.h"

#include <QFont>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRadialGradient>

namespace {

QColor darker(const QColor &color, int amount)
{
    return color.darker(amount);
}

QColor lighter(const QColor &color, int amount)
{
    return color.lighter(amount);
}

} // namespace

ReagentBottlePanel::ReagentBottlePanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("reagentBottlePanel"));
    setToolTip(tr("试剂余量：瓶内液位 0~100%"));

    m_specs = {
        { tr("S1清洗液"), QColor("#38BDF8"), true },
        { tr("AA左"), QColor("#F59E0B"), false },
        { tr("AA右"), QColor("#F59E0B"), false },
        { tr("ADP左"), QColor("#14B8A6"), false },
        { tr("ADP右"), QColor("#14B8A6"), false },
        { tr("COL左"), QColor("#8B5CF6"), false },
        { tr("COL右"), QColor("#8B5CF6"), false },
        { tr("EPI左"), QColor("#EF4444"), false },
        { tr("EPI右"), QColor("#EF4444"), false },
        { tr("RIS左"), QColor("#22C55E"), false },
        { tr("RIS右"), QColor("#22C55E"), false }
    };

    resetDemoLevels();
}

int ReagentBottlePanel::bottleCount() const
{
    return m_specs.size();
}

double ReagentBottlePanel::level(int bottleIndex) const
{
    if (bottleIndex < 0 || bottleIndex >= m_levels.size()) {
        return 0.0;
    }
    return m_levels.at(bottleIndex);
}

void ReagentBottlePanel::setLevel(int bottleIndex, double percent)
{
    if (bottleIndex < 0 || bottleIndex >= m_levels.size()) {
        return;
    }
    const double value = qBound(0.0, percent, 100.0);
    if (qFuzzyCompare(m_levels.at(bottleIndex) + 1.0, value + 1.0)) {
        return;
    }
    m_levels[bottleIndex] = value;
    update();
}

void ReagentBottlePanel::setLevels(const QVector<double> &levels)
{
    const int count = qMin(levels.size(), m_levels.size());
    for (int i = 0; i < count; ++i) {
        m_levels[i] = qBound(0.0, levels.at(i), 100.0);
    }
    update();
}

void ReagentBottlePanel::resetDemoLevels()
{
    m_levels = {
        100.0,  // S1清洗液
        85.0, 82.0,   // AA左 / AA右
        74.0, 68.0,   // ADP左 / ADP右
        61.0, 57.0,   // COL左 / COL右
        52.0, 47.0,   // EPI左 / EPI右
        41.0, 36.0    // RIS左 / RIS右
    };
    update();
}

QSize ReagentBottlePanel::sizeHint() const
{
    return QSize(1100, 160);
}

QSize ReagentBottlePanel::minimumSizeHint() const
{
    return QSize(720, 146);
}

void ReagentBottlePanel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRectF card = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);

    // 卡片背景：浅色玻璃质感的白色面板
    QLinearGradient cardGradient(card.topLeft(), card.bottomLeft());
    cardGradient.setColorAt(0.0, QColor("#FFFFFF"));
    cardGradient.setColorAt(0.55, QColor("#FBFDFF"));
    cardGradient.setColorAt(1.0, QColor("#EEF8FE"));
    p.setPen(QPen(QColor("#C5DDEB"), 1.0));
    p.setBrush(cardGradient);
    p.drawRoundedRect(card, 14.0, 14.0);

    QRadialGradient glow(QPointF(card.left() + 120.0, card.top() + 20.0),
                         260.0);
    glow.setColorAt(0.0, QColor(105, 198, 244, 26));
    glow.setColorAt(1.0, QColor(105, 198, 244, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawRoundedRect(card.adjusted(1.0, 1.0, -1.0, -1.0), 13.0, 13.0);

    // 标题
    QFont titleFont(QStringLiteral("Microsoft YaHei"));
    titleFont.setPixelSize(14);
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.setPen(QColor("#1F4E79"));
    p.drawText(QRectF(card.left() + 16.0, card.top() + 8.0,
                      card.width() - 32.0, 20.0),
               Qt::AlignLeft | Qt::AlignVCenter, tr("试剂余量"));

    QFont hintFont(QStringLiteral("Microsoft YaHei"));
    hintFont.setPixelSize(9);
    p.setFont(hintFont);
    p.setPen(QColor("#8AA0B2"));
    p.drawText(QRectF(card.left() + 110.0, card.top() + 8.0,
                      card.width() - 126.0, 20.0),
               Qt::AlignRight | Qt::AlignVCenter,
               tr("S1 · AA · ADP · COL · EPI · RIS    液位 0~100%"));

    p.setPen(QPen(QColor(213, 230, 241, 190), 1.0));
    p.drawLine(QPointF(card.left() + 16.0, card.top() + 31.0),
               QPointF(card.right() - 16.0, card.top() + 31.0));

    // 11 个瓶位
    const qreal sideMargin = 10.0;
    const QRectF slotArea(card.left() + sideMargin, card.top() + 35.0,
                          card.width() - sideMargin * 2.0,
                          card.height() - 39.0);
    const qreal slotWidth = slotArea.width() / qMax(1, m_specs.size());

    for (int i = 0; i < m_specs.size(); ++i) {
        const QRectF slot(slotArea.left() + slotWidth * i, slotArea.top(),
                          slotWidth, slotArea.height());
        paintBottle(p, slot, i);
    }
}

void ReagentBottlePanel::paintBottle(QPainter &p, const QRectF &slot,
                                     int bottleIndex)
{
    if (bottleIndex < 0 || bottleIndex >= m_specs.size()) {
        return;
    }

    const BottleSpec &spec = m_specs.at(bottleIndex);
    const double percent = bottleIndex < m_levels.size()
            ? m_levels.at(bottleIndex) : 0.0;

    const qreal bodyWidth = qMin(spec.squareBody ? slot.width() * 0.62
                                                 : slot.width() * 0.46,
                                 spec.squareBody ? 58.0 : 42.0);
    const qreal bodyBottom = slot.bottom() - 31.0;
    const qreal bodyTop = slot.top() + (spec.squareBody ? 16.0 : 24.0);
    const QRectF body(slot.center().x() - bodyWidth / 2.0, bodyTop,
                      bodyWidth, qMax<qreal>(42.0, bodyBottom - bodyTop));

    if (spec.squareBody) {
        paintCleaningBottle(p, body, spec, percent);
    } else {
        paintReagentVial(p, body, spec, percent);
    }

    // 名称
    QFont nameFont(QStringLiteral("Microsoft YaHei"));
    nameFont.setPixelSize(10);
    nameFont.setBold(true);
    p.setFont(nameFont);
    p.setPen(QColor("#244A67"));
    p.drawText(QRectF(slot.left(), slot.bottom() - 27.0,
                      slot.width(), 13.0),
               Qt::AlignHCenter | Qt::AlignVCenter, spec.label);

    // 液位百分比
    QFont percentFont(QStringLiteral("Microsoft YaHei"));
    percentFont.setPixelSize(9);
    percentFont.setBold(true);
    p.setFont(percentFont);
    QColor percentColor = percent > 0.5
            ? darker(spec.liquidColor, 112) : QColor("#9AAAB8");
    p.setPen(percentColor);
    p.drawText(QRectF(slot.left(), slot.bottom() - 13.0,
                      slot.width(), 13.0),
               Qt::AlignHCenter | Qt::AlignVCenter,
               QStringLiteral("%1%").arg(qRound(percent)));
}

void ReagentBottlePanel::paintCleaningBottle(QPainter &p,
                                             const QRectF &body,
                                             const BottleSpec &spec,
                                             double percent)
{
    const qreal bw = body.width();
    const qreal bh = body.height();

    // 底部投影
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(31, 78, 121, 28));
    p.drawEllipse(QRectF(body.center().x() - bw * 0.58,
                         body.bottom() - 2.0, bw * 1.16, 8.0));

    const QRectF front(body.left(), body.top() + 3.0, bw, bh - 3.0);
    const qreal topChamfer = bw * 0.19;
    const qreal bottomChamfer = bw * 0.09;

    // 实拍白瓶：方形瓶身，顶部肩部大切角，底部小切角
    QPainterPath bottlePath;
    bottlePath.moveTo(front.left() + topChamfer, front.top());
    bottlePath.lineTo(front.right() - topChamfer, front.top());
    bottlePath.lineTo(front.right(), front.top() + topChamfer);
    bottlePath.lineTo(front.right(), front.bottom() - bottomChamfer);
    bottlePath.lineTo(front.right() - bottomChamfer, front.bottom());
    bottlePath.lineTo(front.left() + bottomChamfer, front.bottom());
    bottlePath.lineTo(front.left(), front.bottom() - bottomChamfer);
    bottlePath.lineTo(front.left(), front.top() + topChamfer);
    bottlePath.closeSubpath();

    // 白色瓶身
    QLinearGradient glass(front.left(), 0.0, front.right(), 0.0);
    glass.setColorAt(0.0, QColor(255, 255, 255, 248));
    glass.setColorAt(0.22, QColor(247, 252, 255, 240));
    glass.setColorAt(0.72, QColor(231, 243, 250, 232));
    glass.setColorAt(1.0, QColor(190, 215, 230, 242));
    p.setPen(Qt::NoPen);
    p.setBrush(glass);
    p.drawPath(bottlePath);

    // 液位（半透明显示在白瓶内部）
    const qreal innerBottom = front.bottom() - 3.0;
    const qreal liquidHeight = qMax<qreal>(1.0, front.height() - 8.0)
            * qBound(0.0, percent, 100.0) / 100.0;
    if (liquidHeight > 0.4) {
        const qreal liquidTop = innerBottom - liquidHeight;
        p.save();
        p.setClipPath(bottlePath);

        QLinearGradient liquid(0.0, liquidTop, 0.0, innerBottom);
        liquid.setColorAt(0.0, lighter(spec.liquidColor, 140));
        liquid.setColorAt(0.46, spec.liquidColor);
        liquid.setColorAt(1.0, darker(spec.liquidColor, 118));
        p.setBrush(liquid);
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(front.left() + 2.0, liquidTop,
                          bw - 4.0, innerBottom - liquidTop + 2.0));

        QColor surface = lighter(spec.liquidColor, 155);
        surface.setAlpha(210);
        p.setBrush(surface);
        p.drawEllipse(QRectF(front.left() + 3.5, liquidTop - 3.0,
                             bw - 7.0, 6.5));

        // 白色塑料瓶的半透质感
        p.setBrush(QColor(255, 255, 255, 72));
        p.drawRect(front);
        p.restore();
    }

    // 瓶身轮廓
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(148, 177, 198, 210), 1.1));
    p.drawPath(bottlePath);

    // 实拍瓶正面的凸起面板
    const QRectF panel(front.left() + bw * 0.17,
                       front.top() + bh * 0.13,
                       bw * 0.66, bh * 0.68);
    p.setPen(QPen(QColor(255, 255, 255, 175), 0.9));
    p.setBrush(QColor(255, 255, 255, 28));
    p.drawRoundedRect(panel, bw * 0.06, bw * 0.06);
    p.setPen(QPen(QColor(118, 152, 176, 45), 0.9));
    p.drawLine(panel.bottomLeft(), panel.bottomRight());
    p.drawLine(panel.topRight(), panel.bottomRight());

    // 左侧高光和顶部肩线
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 130));
    p.drawRoundedRect(QRectF(front.left() + bw * 0.08,
                             front.top() + bh * 0.12,
                             qMax<qreal>(2.0, bw * 0.07),
                             bh * 0.58),
                      bw * 0.035, bw * 0.035);
    p.setPen(QPen(QColor(255, 255, 255, 205), 1.0));
    p.drawLine(QPointF(front.left() + topChamfer, front.top() + 1.0),
               QPointF(front.right() - topChamfer, front.top() + 1.0));

    paintCleaningBottleNeck(p, body, spec);
}

void ReagentBottlePanel::paintCleaningBottleNeck(QPainter &p,
                                                 const QRectF &body,
                                                 const BottleSpec &spec)
{
    Q_UNUSED(spec);
    const qreal bw = body.width();
    const qreal neckWidth = bw * 0.50;
    const qreal neckHeight = qMax<qreal>(11.0, body.height() * 0.21);
    const qreal cx = body.center().x();
    const qreal topY = body.top() - neckHeight + 2.0;
    const qreal bottomY = body.top() + 5.0;
    const qreal halfTop = neckWidth * 0.45;
    const qreal halfBottom = neckWidth * 0.55;

    // 短粗圆颈
    QPainterPath neckPath;
    neckPath.moveTo(cx - halfBottom, bottomY);
    neckPath.lineTo(cx - halfTop, topY);
    neckPath.lineTo(cx + halfTop, topY);
    neckPath.lineTo(cx + halfBottom, bottomY);
    neckPath.closeSubpath();

    QLinearGradient neckGradient(cx - halfBottom, 0.0,
                                 cx + halfBottom, 0.0);
    neckGradient.setColorAt(0.0, QColor(255, 255, 255, 250));
    neckGradient.setColorAt(0.45, QColor(247, 252, 255, 244));
    neckGradient.setColorAt(1.0, QColor(206, 226, 238, 242));
    p.setPen(QPen(QColor(148, 177, 198, 210), 1.0));
    p.setBrush(neckGradient);
    p.drawPath(neckPath);

    // 螺纹
    p.setBrush(Qt::NoBrush);
    for (int i = 0; i < 3; ++i) {
        const qreal y = topY + 3.0 + i * 3.6;
        p.setPen(QPen(QColor(158, 185, 203, 190), 0.9));
        p.drawArc(QRectF(cx - halfTop * 0.98, y,
                         halfTop * 1.96, 3.4), 0, 180 * 16);
    }

    // 顶部翻边
    const QRectF lip(cx - neckWidth * 0.58, topY - 2.5,
                     neckWidth * 1.16, 5.0);
    p.setPen(QPen(QColor(140, 172, 193, 210), 0.9));
    p.setBrush(QColor(252, 254, 255, 245));
    p.drawRoundedRect(lip, 2.5, 2.5);

    // 开放瓶口
    const QRectF mouth(cx - halfTop * 0.78, topY - 2.0,
                       halfTop * 1.56, 4.2);
    p.setPen(QPen(QColor(120, 155, 177, 220), 0.8));
    p.setBrush(QColor(104, 132, 150, 225));
    p.drawEllipse(mouth);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(58, 84, 102, 190));
    p.drawEllipse(mouth.adjusted(1.0, 1.0, -1.0, -1.0));

    // 瓶口玻璃唇沿
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 235), 1.0));
    p.drawEllipse(mouth.adjusted(-0.9, -0.7, 0.9, 0.7));

    // 瓶颈高光
    p.setBrush(QColor(255, 255, 255, 150));
    p.drawRoundedRect(QRectF(cx - halfTop * 0.62, topY + 2.0,
                             qMax<qreal>(1.0, neckWidth * 0.08),
                             neckHeight * 0.55),
                      1.0, 1.0);
}

void ReagentBottlePanel::paintReagentVial(QPainter &p, const QRectF &body,
                                          const BottleSpec &spec,
                                          double percent)
{
    const qreal bw = body.width();
    const qreal bh = body.height();
    const qreal cx = body.center().x();
    const qreal left = body.left();
    const qreal right = body.right();
    const qreal bottom = body.bottom() - 2.0;
    const qreal corner = bw * 0.12;
    const qreal neckHalf = bw * 0.19;
    const qreal shoulderHeight = bh * 0.20;
    const qreal shoulderEndY = body.top() + shoulderHeight;

    // 底部投影
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(31, 78, 121, 24));
    p.drawEllipse(QRectF(cx - bw * 0.55, bottom - 1.0, bw * 1.1, 7.0));

    // 圆柱瓶身 + 圆肩
    QPainterPath bodyPath;
    bodyPath.moveTo(left, bottom - corner);
    bodyPath.quadTo(left, bottom, left + corner, bottom);
    bodyPath.lineTo(right - corner, bottom);
    bodyPath.quadTo(right, bottom, right, bottom - corner);
    bodyPath.lineTo(right, shoulderEndY);
    bodyPath.cubicTo(right, body.top() + shoulderHeight * 0.35,
                     cx + neckHalf * 1.55,
                     body.top() + shoulderHeight * 0.12,
                     cx + neckHalf, body.top());
    bodyPath.lineTo(cx - neckHalf, body.top());
    bodyPath.cubicTo(cx - neckHalf * 1.55,
                     body.top() + shoulderHeight * 0.12,
                     left, body.top() + shoulderHeight * 0.35,
                     left, shoulderEndY);
    bodyPath.closeSubpath();

    // 透明玻璃瓶身
    QLinearGradient glass(left, 0.0, right, 0.0);
    glass.setColorAt(0.0, QColor(255, 255, 255, 120));
    glass.setColorAt(0.16, QColor(245, 252, 255, 70));
    glass.setColorAt(0.62, QColor(224, 242, 250, 45));
    glass.setColorAt(0.88, QColor(204, 229, 242, 60));
    glass.setColorAt(1.0, QColor(178, 207, 224, 100));
    p.setPen(Qt::NoPen);
    p.setBrush(glass);
    p.drawPath(bodyPath);

    // 瓶内液体
    const qreal innerBottom = bottom - 4.0;
    const qreal liquidHeight = qMax<qreal>(1.0, bh - 8.0)
            * qBound(0.0, percent, 100.0) / 100.0;
    if (liquidHeight > 0.4) {
        const qreal liquidTop = innerBottom - liquidHeight;
        p.save();
        p.setClipPath(bodyPath);

        QLinearGradient liquid(0.0, liquidTop, 0.0, innerBottom);
        liquid.setColorAt(0.0, lighter(spec.liquidColor, 135));
        liquid.setColorAt(0.42, spec.liquidColor);
        liquid.setColorAt(1.0, darker(spec.liquidColor, 115));
        p.setBrush(liquid);
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(left + 1.5, liquidTop,
                          bw - 3.0, innerBottom - liquidTop + 2.0));

        QColor surface = lighter(spec.liquidColor, 160);
        surface.setAlpha(230);
        p.setBrush(surface);
        p.drawEllipse(QRectF(left + bw * 0.10, liquidTop - 2.0,
                             bw * 0.80, 4.5));

        // 玻璃反光
        QLinearGradient sheen(left, 0.0, right, 0.0);
        sheen.setColorAt(0.0, QColor(255, 255, 255, 80));
        sheen.setColorAt(0.25, QColor(255, 255, 255, 22));
        sheen.setColorAt(0.70, QColor(255, 255, 255, 0));
        sheen.setColorAt(1.0, QColor(40, 90, 120, 30));
        p.setBrush(sheen);
        p.drawRect(QRectF(left, body.top(), bw, bh));
        p.restore();
    }

    // 瓶身轮廓
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(140, 172, 193, 195), 1.1));
    p.drawPath(bodyPath);

    // 内侧玻璃壁
    p.setPen(QPen(QColor(255, 255, 255, 150), 0.9));
    p.drawLine(QPointF(left + 2.5, shoulderEndY),
               QPointF(left + 2.5, bottom - corner));
    p.drawLine(QPointF(right - 2.5, shoulderEndY),
               QPointF(right - 2.5, bottom - corner));

    // 瓶底玻璃
    p.setPen(QPen(QColor(160, 190, 208, 180), 0.9));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(left + bw * 0.14, bottom - 7.0,
                     bw * 0.72, 8.0), 200 * 16, 140 * 16);

    // 左侧高光
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 150));
    p.drawRoundedRect(QRectF(left + bw * 0.14, shoulderEndY + 4.0,
                             qMax<qreal>(1.6, bw * 0.06), bh * 0.55),
                      bw * 0.03, bw * 0.03);

    // 肩部高光
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 170), 1.0));
    QPainterPath shoulderGlow;
    shoulderGlow.moveTo(left + bw * 0.16, shoulderEndY - 1.0);
    shoulderGlow.cubicTo(left + bw * 0.22,
                         body.top() + shoulderHeight * 0.35,
                         cx - neckHalf * 1.2,
                         body.top() + shoulderHeight * 0.18,
                         cx - neckHalf * 1.05, body.top() + 1.0);
    p.drawPath(shoulderGlow);

    paintReagentVialNeck(p, body, spec);
}

void ReagentBottlePanel::paintReagentVialNeck(QPainter &p,
                                              const QRectF &body,
                                              const BottleSpec &spec)
{
    Q_UNUSED(spec);
    const qreal bw = body.width();
    const qreal bh = body.height();
    const qreal cx = body.center().x();
    const qreal neckWidth = bw * 0.38;
    const qreal halfNeck = neckWidth / 2.0;
    const qreal neckHeight = qMax<qreal>(9.0, bh * 0.17);
    const qreal rimHeight = qMax<qreal>(4.0, bh * 0.075);
    const qreal rimWidth = bw * 0.58;
    const qreal neckBottomY = body.top() + 2.0;
    const qreal neckTopY = neckBottomY - neckHeight;
    const qreal rimTopY = neckTopY - rimHeight;

    // 瓶颈
    const QRectF neckRect(cx - halfNeck, neckTopY, neckWidth,
                          neckBottomY - neckTopY);
    QPainterPath neckPath;
    neckPath.addRoundedRect(neckRect, neckWidth * 0.18, neckWidth * 0.18);
    QLinearGradient neckGradient(cx - halfNeck, 0.0,
                                 cx + halfNeck, 0.0);
    neckGradient.setColorAt(0.0, QColor(255, 255, 255, 150));
    neckGradient.setColorAt(0.35, QColor(245, 252, 255, 80));
    neckGradient.setColorAt(0.75, QColor(220, 238, 248, 65));
    neckGradient.setColorAt(1.0, QColor(180, 208, 225, 110));
    p.setPen(QPen(QColor(145, 175, 195, 190), 1.0));
    p.setBrush(neckGradient);
    p.drawPath(neckPath);

    // 顶部翻边（透明玻璃）
    const QRectF rim(cx - rimWidth / 2.0, rimTopY, rimWidth, rimHeight);
    p.setBrush(QColor(250, 254, 255, 150));
    p.setPen(QPen(QColor(135, 168, 190, 200), 1.0));
    p.drawRoundedRect(rim, rimHeight * 0.45, rimHeight * 0.45);
    p.setPen(QPen(QColor(255, 255, 255, 225), 0.9));
    p.drawLine(QPointF(rim.left() + rimHeight * 0.5, rim.top() + 0.8),
               QPointF(rim.right() - rimHeight * 0.5, rim.top() + 0.8));

    // 翻边下方的凹槽
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(140, 172, 193, 160), 0.9));
    p.drawLine(QPointF(cx - halfNeck * 0.9, neckTopY + 1.8),
               QPointF(cx + halfNeck * 0.9, neckTopY + 1.8));

    // 开放瓶口
    const QRectF mouth(cx - neckWidth * 0.34, rimTopY + 0.6,
                       neckWidth * 0.68, rimHeight * 0.9);
    p.setPen(QPen(QColor(110, 142, 162, 220), 0.8));
    p.setBrush(QColor(92, 120, 138, 225));
    p.drawEllipse(mouth);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(54, 78, 95, 190));
    p.drawEllipse(mouth.adjusted(0.8, 0.8, -0.8, -0.8));

    // 瓶口玻璃唇沿
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 240), 1.1));
    p.drawEllipse(mouth.adjusted(-1.0, -0.8, 1.0, 0.8));

    // 瓶颈高光
    p.setBrush(QColor(255, 255, 255, 160));
    p.drawRoundedRect(QRectF(cx - halfNeck * 0.6, neckTopY + 1.5,
                             qMax<qreal>(1.0, neckWidth * 0.08),
                             neckHeight * 0.7),
                      1.0, 1.0);
}
