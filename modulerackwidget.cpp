#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "modulerackwidget.h"
#include "testsession.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRadialGradient>
#include <QSizePolicy>
#include <cmath>

namespace {

constexpr int kChannelsPerModule = 4;

bool statusLooksError(const QString &status)
{
    return status.contains(QStringLiteral("异常"))
            || status.contains(QStringLiteral("错误"))
            || status.contains(QStringLiteral("失败"));
}

} // namespace

ModuleRackWidget::ModuleRackWidget(int moduleNumber, QWidget *parent)
    : QWidget(parent)
    , m_moduleNumber(qBound(1, moduleNumber, 3))
{
    setObjectName(QStringLiteral("moduleRackWidget"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setMinimumSize(260, 320);
    setToolTip(tr("测试开始后，点击已插入试管的玻璃管可查看该通道实时曲线"));

    m_channels.resize(kChannelsPerModule);

    m_animationTimer.setInterval(30);
    connect(&m_animationTimer, &QTimer::timeout,
            this, &ModuleRackWidget::handleAnimationTick);
}

ModuleRackWidget::~ModuleRackWidget() = default;

int ModuleRackWidget::moduleNumber() const
{
    return m_moduleNumber;
}

void ModuleRackWidget::setModuleNumber(int moduleNumber)
{
    const int value = qBound(1, moduleNumber, 3);
    if (m_moduleNumber == value) {
        return;
    }
    m_moduleNumber = value;
    update();
}

int ModuleRackWidget::channelCount() const
{
    return kChannelsPerModule;
}

int ModuleRackWidget::globalChannelNumber(int localChannel) const
{
    return (m_moduleNumber - 1) * kChannelsPerModule + localChannel;
}

int ModuleRackWidget::channelIndex(int localChannel) const
{
    if (localChannel < 1 || localChannel > m_channels.size()) {
        return -1;
    }
    return localChannel - 1;
}

bool ModuleRackWidget::isChannelEnabled(int localChannel) const
{
    const int index = channelIndex(localChannel);
    return index >= 0 && m_channels.at(index).enabled;
}

ChannelRunState ModuleRackWidget::channelRunState(int localChannel) const
{
    const int index = channelIndex(localChannel);
    if (index < 0) {
        return ChannelRunState::Disabled;
    }
    const ChannelState &state = m_channels.at(index);
    return state.enabled ? state.runState : ChannelRunState::Disabled;
}

bool ModuleRackWidget::isTubeInserted(int localChannel) const
{
    const int index = channelIndex(localChannel);
    return index >= 0 && m_channels.at(index).tubeInserted;
}

bool ModuleRackWidget::isMotorRunning(int localChannel) const
{
    const int index = channelIndex(localChannel);
    return index >= 0 && m_channels.at(index).motorRunning;
}

double ModuleRackWidget::moduleTemperature() const
{
    return m_moduleTemperature;
}

int ModuleRackWidget::channelProgress(int localChannel) const
{
    const int index = channelIndex(localChannel);
    return index >= 0 ? m_channels.at(index).progress : 0;
}

QString ModuleRackWidget::channelStatus(int localChannel) const
{
    const int index = channelIndex(localChannel);
    return index >= 0 ? m_channels.at(index).status : QString();
}

ModuleRackWidget::TubeContent ModuleRackWidget::tubeContent(int localChannel) const
{
    const int index = channelIndex(localChannel);
    return index >= 0 ? m_channels.at(index).content : TubeContent::Prp;
}

void ModuleRackWidget::setTubeContent(int localChannel, TubeContent content)
{
    const int index = channelIndex(localChannel);
    if (index < 0 || m_channels.at(index).content == content) {
        return;
    }
    m_channels[index].content = content;
    update();
}

int ModuleRackWidget::runningChannelCount() const
{
    int count = 0;
    for (const ChannelState &state : m_channels) {
        if (state.enabled && state.motorRunning) {
            ++count;
        }
    }
    return count;
}

int ModuleRackWidget::insertedTubeCount() const
{
    int count = 0;
    for (const ChannelState &state : m_channels) {
        if (state.enabled && state.tubeInserted) {
            ++count;
        }
    }
    return count;
}

ModuleRackWidget::ChannelTestInfo ModuleRackWidget::channelTestInfo(int localChannel) const
{
    const int index = channelIndex(localChannel);
    return index >= 0 ? m_channels.at(index).info : ChannelTestInfo();
}

int ModuleRackWidget::channelAdValue(int localChannel) const
{
    const int index = channelIndex(localChannel);
    return index >= 0 ? m_channels.at(index).info.adValue : -1;
}

QVector<QPointF> ModuleRackWidget::testCurve(int localChannel) const
{
    const int index = channelIndex(localChannel);
    return index >= 0 ? m_channels.at(index).testCurve : QVector<QPointF>();
}

QVector<QPointF> ModuleRackWidget::aggregationCurve(int localChannel) const
{
    const int index = channelIndex(localChannel);
    return index >= 0 ? m_channels.at(index).aggregationCurve : QVector<QPointF>();
}

void ModuleRackWidget::setChannelTestInfo(int localChannel,
                                           const ChannelTestInfo &info)
{
    const int index = channelIndex(localChannel);
    if (index < 0) {
        return;
    }
    m_channels[index].info = info;
    update();
    emit channelTestInfoChanged(m_moduleNumber, localChannel);
}

void ModuleRackWidget::setChannelAdValue(int localChannel, int adValue)
{
    const int index = channelIndex(localChannel);
    if (index < 0) {
        return;
    }

    ChannelState &state = m_channels[index];
    if (!state.enabled) {
        if (state.info.adValue != -1) {
            state.info.adValue = -1;
            update();
            emit channelTestInfoChanged(m_moduleNumber, localChannel);
        }
        return;
    }
    state.info.adValue = adValue;

    bool curveChanged = false;
    const bool testing = state.runState == ChannelRunState::Testing
            || TestSession::instance().isTestingActive();
    const bool collectCurve = adValue >= 0
            && state.enabled
            && state.tubeInserted
            && testing;
    if (collectCurve) {
        // 试剂测试期间，曲线头部的 PRP 值跟随每秒 AD 实时刷新；
        // 第一个有效值作为 PRP0 保留，供测试结束保存。
        if (state.info.prp0Value < 0) {
            state.info.prp0Value = adValue;
        }
        state.info.prpValue = adValue;
        ++state.info.elapsedSeconds;
        const QPointF point(static_cast<double>(state.info.elapsedSeconds),
                            static_cast<double>(adValue));
        state.testCurve.append(point);
        constexpr int kMaxCurvePoints = 7200;
        if (state.testCurve.size() > kMaxCurvePoints) {
            state.testCurve.remove(0, state.testCurve.size() - kMaxCurvePoints);
        }
        curveChanged = true;
    }

    update();
    emit channelTestInfoChanged(m_moduleNumber, localChannel);
    if (curveChanged) {
        emit curveDataChanged(m_moduleNumber, localChannel);
    }
}

void ModuleRackWidget::appendChannelTestPoint(int localChannel, double seconds,
                                               double value)
{
    const int index = channelIndex(localChannel);
    if (index < 0) {
        return;
    }
    m_channels[index].testCurve.append(QPointF(seconds, value));
    emit curveDataChanged(m_moduleNumber, localChannel);
}

void ModuleRackWidget::appendChannelAggregationPoint(int localChannel,
                                                      double seconds,
                                                      double value)
{
    const int index = channelIndex(localChannel);
    if (index < 0) {
        return;
    }
    m_channels[index].aggregationCurve.append(QPointF(seconds, value));
    emit curveDataChanged(m_moduleNumber, localChannel);
}

void ModuleRackWidget::clearChannelCurve(int localChannel)
{
    const int index = channelIndex(localChannel);
    if (index < 0) {
        return;
    }
    ChannelState &state = m_channels[index];
    state.testCurve.clear();
    state.aggregationCurve.clear();
    emit curveDataChanged(m_moduleNumber, localChannel);
    update();
}

void ModuleRackWidget::setChannelEnabled(int localChannel, bool enabled)
{
    const int index = channelIndex(localChannel);
    if (index < 0) {
        return;
    }
    ChannelState &state = m_channels[index];
    if (state.enabled == enabled) {
        return;
    }

    state.enabled = enabled;
    if (!enabled) {
        state.tubeInserted = false;
        state.motorRunning = false;
        state.progress = 0;
        state.progressDisplay = 0.0;
        state.runState = ChannelRunState::Idle;
        state.status = tr("禁用");
        state.info.adValue = -1;
        state.info.prpValue = -1;
        state.info.prp0Value = -1;
        state.info.elapsedSeconds = 0;
        state.testCurve.clear();
        state.aggregationCurve.clear();
        emit curveDataChanged(m_moduleNumber, localChannel);
        emit channelTestInfoChanged(m_moduleNumber, localChannel);
    } else if (state.status == tr("禁用")) {
        state.status.clear();
    }
    ensureAnimationTimer();
    update();
    emit channelRunStateChanged(m_moduleNumber, localChannel,
                                channelRunState(localChannel));
}

void ModuleRackWidget::setChannelRunState(int localChannel,
                                          ChannelRunState state)
{
    const int index = channelIndex(localChannel);
    if (index < 0) {
        return;
    }

    if (state == ChannelRunState::Disabled) {
        if (m_channels.at(index).enabled) {
            setChannelEnabled(localChannel, false);
        }
        return;
    }

    if (!m_channels.at(index).enabled) {
        setChannelEnabled(localChannel, true);
    }

    ChannelState &channel = m_channels[index];
    if (channel.runState == state) {
        return;
    }
    channel.runState = state;
    update();
    emit channelRunStateChanged(m_moduleNumber, localChannel, state);
}

void ModuleRackWidget::setTubeInserted(int localChannel, bool inserted, bool animated)
{
    const int index = channelIndex(localChannel);
    if (index < 0) {
        return;
    }
    ChannelState &state = m_channels[index];
    if (!state.enabled) {
        return;
    }
    if (state.tubeInserted == inserted) {
        return;
    }
    state.tubeInserted = inserted;

    if (inserted) {
        // 新试管插入视为新一轮测试：清空上一轮曲线和当前 AD/计时
        state.testCurve.clear();
        state.aggregationCurve.clear();
        state.info.elapsedSeconds = 0;
        state.info.adValue = -1;
        state.info.prpValue = -1;
        state.info.prp0Value = -1;
        emit curveDataChanged(m_moduleNumber, localChannel);
        emit channelTestInfoChanged(m_moduleNumber, localChannel);
    }

    if (!animated) {
        state.tubePosition = inserted ? 1.0 : 0.0;
        update();
        return;
    }
    ensureAnimationTimer();
    update();
}

void ModuleRackWidget::setChannelProgress(int localChannel, int percent)
{
    const int index = channelIndex(localChannel);
    if (index < 0) {
        return;
    }
    ChannelState &state = m_channels[index];
    const int value = qBound(0, percent, 100);
    if (state.progress == value) {
        return;
    }
    state.progress = value;
    ensureAnimationTimer();
    update();
}

void ModuleRackWidget::setChannelStatus(int localChannel, const QString &status)
{
    const int index = channelIndex(localChannel);
    if (index < 0) {
        return;
    }
    ChannelState &state = m_channels[index];
    const QString value = status.trimmed();
    if (state.status == value) {
        return;
    }
    state.status = value;
    update();
}

void ModuleRackWidget::startMotor(int localChannel)
{
    setMotorRunning(localChannel, true);
}

void ModuleRackWidget::stopMotor(int localChannel)
{
    setMotorRunning(localChannel, false);
}

void ModuleRackWidget::setMotorRunning(int localChannel, bool running)
{
    const int index = channelIndex(localChannel);
    if (index < 0) {
        return;
    }
    ChannelState &state = m_channels[index];
    if (!state.enabled || state.motorRunning == running) {
        return;
    }
    state.motorRunning = running;
    ensureAnimationTimer();
    update();
}

void ModuleRackWidget::resetChannels()
{
    for (ChannelState &state : m_channels) {
        const bool enabled = state.enabled;
        state = ChannelState();
        state.enabled = enabled;
        if (!enabled) {
            state.status = tr("禁用");
        }
    }
    m_animationTimer.stop();
    update();

    // 如果曲线窗口还开着，让它同步清空
    for (int localChannel = 1; localChannel <= m_channels.size(); ++localChannel) {
        emit curveDataChanged(m_moduleNumber, localChannel);
        emit channelTestInfoChanged(m_moduleNumber, localChannel);
        emit channelRunStateChanged(m_moduleNumber, localChannel,
                                    channelRunState(localChannel));
    }
}

void ModuleRackWidget::setModuleTemperature(double celsius)
{
    const double value = celsius < 0.0 ? -1.0 : celsius;
    if (m_moduleTemperature < 0.0 && value < 0.0) {
        return;
    }
    if (m_moduleTemperature >= 0.0 && value >= 0.0
            && qAbs(m_moduleTemperature - value) < 0.005) {
        return;
    }
    m_moduleTemperature = value;
    update();
}

void ModuleRackWidget::ensureAnimationTimer()
{
    if (!m_animationTimer.isActive()) {
        m_animationClock.restart();
        m_animationTimer.start();
    }
}

void ModuleRackWidget::handleAnimationTick()
{
    const qreal dt = qBound<qreal>(0.0, m_animationClock.restart() / 1000.0, 0.1);
    bool active = false;

    for (ChannelState &state : m_channels) {
        const qreal tubeTarget = state.tubeInserted ? 1.0 : 0.0;
        if (qAbs(state.tubePosition - tubeTarget) > 0.001) {
            const qreal step = dt / 0.35;
            if (state.tubePosition < tubeTarget) {
                state.tubePosition = qMin(tubeTarget, state.tubePosition + step);
            } else {
                state.tubePosition = qMax(tubeTarget, state.tubePosition - step);
            }
            active = true;
        } else {
            state.tubePosition = tubeTarget;
        }

        if (qAbs(state.progressDisplay - state.progress) > 0.4) {
            const qreal step = dt * 100.0 / 0.45;
            if (state.progressDisplay < state.progress) {
                state.progressDisplay = qMin<qreal>(state.progress,
                                                   state.progressDisplay + step);
            } else {
                state.progressDisplay = qMax<qreal>(state.progress,
                                                   state.progressDisplay - step);
            }
            active = true;
        } else {
            state.progressDisplay = state.progress;
        }

        if (state.enabled && state.motorRunning) {
            state.motorAngle = std::fmod(state.motorAngle + dt * 480.0, 360.0);
            active = true;
        }
    }

    update();
    if (!active) {
        m_animationTimer.stop();
    }
}

bool ModuleRackWidget::canOpenCurve(int localChannel) const
{
    const int index = channelIndex(localChannel);
    if (index < 0) {
        return false;
    }
    const ChannelState &state = m_channels.at(index);
    const bool testing = state.runState == ChannelRunState::Testing
            || TestSession::instance().isTestingActive();
    return state.enabled
            && state.tubeInserted
            && testing;
}

int ModuleRackWidget::cylinderChannelAt(const QPoint &pos) const
{
    const LayoutMetrics m = metrics();
    for (int i = 0; i < kChannelsPerModule; ++i) {
        // 马达区域不响应曲线点击
        if (motorRect(m, i).adjusted(-3.0, -3.0, 3.0, 3.0).contains(pos)) {
            return 0;
        }
        const QRectF hitRect = cylinderRect(m, i).adjusted(
                    -m.cylinderWidth * 0.12, -6.0,
                    m.cylinderWidth * 0.12, 4.0);
        if (hitRect.contains(pos)) {
            return i + 1;
        }
    }
    return 0;
}

void ModuleRackWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const int localChannel = cylinderChannelAt(event->pos());
    if (localChannel > 0 && canOpenCurve(localChannel)) {
        emit curveRequested(m_moduleNumber, localChannel);
        event->accept();
        return;
    }

    // 点击马达或非测试状态下的玻璃管不做任何动作；
    // 试管插拔和马达启停只由测试流程接口控制。
    QWidget::mousePressEvent(event);
}

QSize ModuleRackWidget::sizeHint() const
{
    return QSize(380, 440);
}

QSize ModuleRackWidget::minimumSizeHint() const
{
    return QSize(260, 320);
}

ModuleRackWidget::LayoutMetrics ModuleRackWidget::metrics() const
{
    LayoutMetrics m;
    m.card = QRectF(rect()).adjusted(2.0, 2.0, -2.0, -2.0);

    const qreal headerHeight = qBound<qreal>(40.0, m.card.height() * 0.11, 56.0);
    const qreal footerHeight = qBound<qreal>(22.0, m.card.height() * 0.06, 30.0);

    m.header = QRectF(m.card.left() + 16.0, m.card.top() + 6.0,
                      m.card.width() - 32.0, headerHeight - 6.0);
    m.footer = QRectF(m.card.left() + 10.0, m.card.bottom() - footerHeight,
                      m.card.width() - 20.0, footerHeight);
    m.rack = QRectF(m.card.left() + 12.0, m.card.top() + headerHeight,
                    m.card.width() - 24.0,
                    m.card.height() - headerHeight - footerHeight - 6.0);

    m.slotWidth = m.rack.width() / kChannelsPerModule;

    const qreal motorBand = qBound<qreal>(28.0, m.rack.height() * 0.16, 68.0);
    const qreal platformHeight = qBound<qreal>(5.0, m.rack.height() * 0.035, 12.0);
    qreal cylinderHeight = m.rack.height() - motorBand - platformHeight - 18.0;
    cylinderHeight = qBound<qreal>(70.0, cylinderHeight, 330.0);

    m.cylinderHeight = cylinderHeight;
    m.cylinderTop = m.rack.top() + 8.0;
    m.cylinderBottom = m.cylinderTop + cylinderHeight;
    m.platformTop = m.cylinderBottom;
    m.platformHeight = platformHeight;
    m.motorTop = m.platformTop + platformHeight + 6.0;
    m.motorHeight = qMax<qreal>(20.0, m.rack.bottom() - m.motorTop);

    m.cylinderWidth = qMin(qMin(m.slotWidth * 0.54, m.cylinderHeight * 0.36), 64.0);
    m.tubeWidth = m.cylinderWidth * 0.62;
    m.tubeHeight = m.cylinderHeight * 0.78;
    m.motorWidth = qMin(m.slotWidth * 0.62, m.cylinderWidth * 1.32);
    return m;
}

QRectF ModuleRackWidget::slotRect(const LayoutMetrics &m, int channelIndex) const
{
    return QRectF(m.rack.left() + m.slotWidth * channelIndex, m.rack.top(),
                  m.slotWidth, m.rack.height());
}

QRectF ModuleRackWidget::cylinderRect(const LayoutMetrics &m, int channelIndex) const
{
    const qreal cx = channelCenterX(m, channelIndex);
    return QRectF(cx - m.cylinderWidth / 2.0, m.cylinderTop,
                  m.cylinderWidth, m.cylinderHeight);
}

QRectF ModuleRackWidget::motorRect(const LayoutMetrics &m, int channelIndex) const
{
    const qreal cx = channelCenterX(m, channelIndex);
    return QRectF(cx - m.motorWidth / 2.0, m.motorTop,
                  m.motorWidth, m.motorHeight);
}

qreal ModuleRackWidget::channelCenterX(const LayoutMetrics &m, int channelIndex) const
{
    return m.rack.left() + m.slotWidth * (channelIndex + 0.5);
}

qreal ModuleRackWidget::smoothStep(qreal value) const
{
    const qreal t = qBound<qreal>(0.0, value, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

QColor ModuleRackWidget::progressColor(const ChannelState &state) const
{
    if (!state.enabled) {
        return QColor("#B7C5D1");
    }
    if (state.runState == ChannelRunState::Abnormal
            || statusLooksError(state.status)) {
        return QColor("#DF5A50");
    }
    if (state.progressDisplay >= 99.5) {
        return QColor("#22A877");
    }
    if (state.motorRunning || state.progressDisplay > 0.5) {
        return QColor("#2F93E0");
    }
    return QColor("#9FB7C9");
}

QColor ModuleRackWidget::footerColor(const ChannelState &state) const
{
    if (!state.enabled) {
        return QColor("#A7B5C2");
    }
    if (state.runState == ChannelRunState::Abnormal
            || statusLooksError(state.status)) {
        return QColor("#CF4F45");
    }
    if (state.runState == ChannelRunState::Testing || state.motorRunning) {
        return QColor("#1E9B63");
    }
    if (state.tubeInserted) {
        return QColor("#2A7FA8");
    }
    return QColor("#6B8398");
}

void ModuleRackWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const LayoutMetrics m = metrics();
    paintCard(p, m);
    paintHeader(p, m);
    paintRackBack(p, m);
    paintPlatform(p, m);

    for (int i = 0; i < kChannelsPerModule; ++i) {
        paintCylinderBody(p, m, i);
    }
    for (int i = 0; i < kChannelsPerModule; ++i) {
        paintTube(p, m, i);
    }
    for (int i = 0; i < kChannelsPerModule; ++i) {
        paintCylinderFront(p, m, i);
    }
    for (int i = 0; i < kChannelsPerModule; ++i) {
        paintMotor(p, m, i);
    }
    paintFooter(p, m);

    for (int i = 0; i < kChannelsPerModule; ++i) {
        paintDisabledCross(p, m, i);
    }
}

void ModuleRackWidget::paintCard(QPainter &p, const LayoutMetrics &m)
{
    p.save();
    for (int i = 4; i >= 1; --i) {
        const QRectF shadow = m.card.adjusted(-i + 1.0, i * 0.6 + 2.0,
                                              i - 1.0, i + 2.0);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(26, 74, 110, 7));
        p.drawRoundedRect(shadow, 18.0, 18.0);
    }

    QLinearGradient background(m.card.topLeft(), m.card.bottomLeft());
    background.setColorAt(0.0, QColor("#FFFFFF"));
    background.setColorAt(0.55, QColor("#F8FCFF"));
    background.setColorAt(1.0, QColor("#E9F4FC"));
    p.setPen(QPen(QColor("#B9D5E8"), 1.2));
    p.setBrush(background);
    p.drawRoundedRect(m.card, 18.0, 18.0);

    QRadialGradient glow(QPointF(m.card.right() - 42.0, m.card.top() + 18.0), 150.0);
    glow.setColorAt(0.0, QColor(76, 170, 232, 30));
    glow.setColorAt(1.0, QColor(76, 170, 232, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawRoundedRect(m.card.adjusted(1.0, 1.0, -1.0, -1.0), 17.0, 17.0);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 170), 1.0));
    p.drawRoundedRect(m.card.adjusted(1.5, 1.5, -1.5, -1.5), 16.5, 16.5);
    p.restore();
}

void ModuleRackWidget::paintHeader(QPainter &p, const LayoutMetrics &m)
{
    p.save();

    const int titleSize = qBound(14, qRound(m.card.height() * 0.035), 19);
    QFont titleFont(QStringLiteral("Microsoft YaHei"));
    titleFont.setPixelSize(titleSize);
    titleFont.setBold(true);

    const QString title = tr("模组 %1").arg(m_moduleNumber);
    const QFontMetrics titleMetrics(titleFont);
    const int titleWidth = titleMetrics.width(title);

    QFont rangeFont(QStringLiteral("Microsoft YaHei"));
    rangeFont.setPixelSize(qBound(9, titleSize - 5, 12));
    const QFontMetrics rangeMetrics(rangeFont);
    const QString range = tr("CH%1–CH%2")
            .arg(globalChannelNumber(1))
            .arg(globalChannelNumber(kChannelsPerModule));
    const int rangeWidth = rangeMetrics.width(range);

    // 左上角显示模组号和通道范围，为顶部居中的温度让出空间
    const qreal leftBlockWidth = qMax(titleWidth, rangeWidth) + 2.0;
    const qreal leftBlockHeight = titleSize + rangeMetrics.height() + 1.0;
    const qreal leftTop = m.header.top()
            + qMax<qreal>(0.0, (m.header.height() - leftBlockHeight) / 2.0);

    p.setFont(titleFont);
    p.setPen(QColor("#1F4E79"));
    p.drawText(QRectF(m.header.left(), leftTop, leftBlockWidth,
                      titleSize + 3.0),
               Qt::AlignLeft | Qt::AlignVCenter, title);

    p.setFont(rangeFont);
    p.setPen(QColor("#7C92A5"));
    p.drawText(QRectF(m.header.left(), leftTop + titleSize,
                      leftBlockWidth, rangeMetrics.height() + 1.0),
               Qt::AlignLeft | Qt::AlignVCenter, range);

    const int running = runningChannelCount();
    const int tubes = insertedTubeCount();
    QString badgeText;
    QColor badgeColor;
    if (running > 0) {
        badgeText = tr("测试中 %1/%2").arg(running).arg(kChannelsPerModule);
        badgeColor = QColor("#2F93E0");
    } else if (tubes > 0) {
        badgeText = tr("就绪 %1/%2").arg(tubes).arg(kChannelsPerModule);
        badgeColor = QColor("#22A877");
    } else {
        badgeText = tr("空闲");
        badgeColor = QColor("#859BAE");
    }

    QFont badgeFont(QStringLiteral("Microsoft YaHei"));
    badgeFont.setPixelSize(qBound(9, titleSize - 5, 12));
    badgeFont.setBold(true);
    const QFontMetrics badgeMetrics(badgeFont);
    const qreal badgeHeight = 22.0;
    const qreal badgeWidth = badgeMetrics.width(badgeText) + 28.0;
    const QRectF badge(m.header.right() - badgeWidth,
                       m.header.center().y() - badgeHeight / 2.0,
                       badgeWidth, badgeHeight);

    // 模组温度：顶部居中，显示在标题与状态徽标之间
    const bool hasTemperature = m_moduleTemperature >= 0.0;
    const QString temperatureText = hasTemperature
            ? QStringLiteral("%1 ℃").arg(m_moduleTemperature, 0, 'f', 1)
            : tr("温度 --");

    QFont temperatureFont(QStringLiteral("Microsoft YaHei"));
    temperatureFont.setPixelSize(qBound(9, titleSize - 5, 11));
    temperatureFont.setBold(true);
    const QFontMetrics temperatureMetrics(temperatureFont);
    const qreal pillHeight = 22.0;
    const qreal pillWidth = temperatureMetrics.width(temperatureText) + 29.0;

    const qreal leftLimit = m.header.left() + leftBlockWidth + 7.0;
    const qreal rightLimit = badge.left() - 7.0;
    if (rightLimit - leftLimit >= pillWidth) {
        const qreal minCenter = leftLimit + pillWidth / 2.0;
        const qreal maxCenter = rightLimit - pillWidth / 2.0;
        const qreal centerX = qBound(minCenter, m.header.center().x(),
                                     maxCenter);
        const QRectF pill(centerX - pillWidth / 2.0,
                          m.header.center().y() - pillHeight / 2.0,
                          pillWidth, pillHeight);

        QColor pillFill("#EAF7FD");
        QColor pillBorder("#AFD7EB");
        QColor pillText("#1F6FA5");
        if (!hasTemperature) {
            pillFill = QColor("#F2F6F9");
            pillBorder = QColor("#D3DFE8");
            pillText = QColor("#8A99A8");
        } else if (m_moduleTemperature < 35.0 || m_moduleTemperature > 39.0) {
            pillFill = QColor("#FFF7E6");
            pillBorder = QColor("#E6CC94");
            pillText = QColor("#A56A1C");
        }

        p.setPen(QPen(pillBorder, 1.0));
        p.setBrush(pillFill);
        p.drawRoundedRect(pill, pillHeight / 2.0, pillHeight / 2.0);

        // 小温度计图标
        const QPointF thermometerCenter(pill.left() + 10.0,
                                        pill.center().y());
        p.setPen(QPen(pillText, 1.3, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(thermometerCenter.x(), thermometerCenter.y() - 5.5),
                   QPointF(thermometerCenter.x(), thermometerCenter.y() + 2.0));
        p.setPen(Qt::NoPen);
        p.setBrush(pillText);
        p.drawEllipse(thermometerCenter, 2.6, 2.6);

        p.setFont(temperatureFont);
        p.setPen(pillText);
        p.drawText(pill.adjusted(17.0, 0.0, -4.0, 0.0),
                   Qt::AlignCenter, temperatureText);
    }

    QColor badgeFill = badgeColor;
    badgeFill.setAlpha(28);
    QColor badgeBorder = badgeColor;
    badgeBorder.setAlpha(90);
    p.setPen(QPen(badgeBorder, 1.0));
    p.setBrush(badgeFill);
    p.drawRoundedRect(badge, badgeHeight / 2.0, badgeHeight / 2.0);

    p.setPen(Qt::NoPen);
    p.setBrush(badgeColor);
    p.drawEllipse(QPointF(badge.left() + 11.0, badge.center().y()), 3.0, 3.0);

    p.setFont(badgeFont);
    p.setPen(badgeColor);
    p.drawText(badge.adjusted(19.0, 0.0, -5.0, 0.0),
               Qt::AlignLeft | Qt::AlignVCenter, badgeText);

    p.restore();
}

void ModuleRackWidget::paintRackBack(QPainter &p, const LayoutMetrics &m)
{
    p.save();

    const QRectF panel(m.rack.left() + 5.0, m.rack.top() + 1.0,
                       m.rack.width() - 10.0,
                       m.platformTop - m.rack.top() + m.platformHeight);
    QLinearGradient panelGradient(panel.topLeft(), panel.bottomLeft());
    panelGradient.setColorAt(0.0, QColor(248, 252, 255, 135));
    panelGradient.setColorAt(1.0, QColor(224, 240, 250, 165));
    p.setPen(QPen(QColor(206, 227, 241, 180), 1.0));
    p.setBrush(panelGradient);
    p.drawRoundedRect(panel, 14.0, 14.0);

    QLinearGradient metal(0.0, panel.top(), 0.0, panel.bottom());
    metal.setColorAt(0.0, QColor("#E7EFF5"));
    metal.setColorAt(0.5, QColor("#B8CAD8"));
    metal.setColorAt(1.0, QColor("#8299AB"));

    const qreal postWidth = qMax<qreal>(4.0, m.cylinderWidth * 0.12);
    const QRectF leftPost(panel.left() + 5.0, panel.top() + 8.0,
                          postWidth, panel.height() - 8.0);
    const QRectF rightPost(panel.right() - 5.0 - postWidth, panel.top() + 8.0,
                           postWidth, panel.height() - 8.0);
    p.setPen(QPen(QColor("#71899C"), 0.8));
    p.setBrush(metal);
    p.drawRoundedRect(leftPost, postWidth / 2.0, postWidth / 2.0);
    p.drawRoundedRect(rightPost, postWidth / 2.0, postWidth / 2.0);

    const QRectF topBeam(panel.left() + 3.0, panel.top() + 4.0,
                         panel.width() - 6.0, qMax<qreal>(5.0, postWidth * 1.25));
    p.setPen(QPen(QColor("#71899C"), 0.8));
    p.setBrush(metal);
    p.drawRoundedRect(topBeam, topBeam.height() / 2.0, topBeam.height() / 2.0);

    p.setPen(QPen(QColor(255, 255, 255, 140), 1.0));
    p.drawLine(QPointF(topBeam.left() + 5.0, topBeam.top() + 1.0),
               QPointF(topBeam.right() - 5.0, topBeam.top() + 1.0));

    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#6E8597"));
    p.drawEllipse(QPointF(topBeam.left() + 5.0, topBeam.center().y()), 1.8, 1.8);
    p.drawEllipse(QPointF(topBeam.right() - 5.0, topBeam.center().y()), 1.8, 1.8);

    p.restore();
}

void ModuleRackWidget::paintPlatform(QPainter &p, const LayoutMetrics &m)
{
    p.save();

    const QRectF topFace(m.rack.left() + 3.0, m.platformTop,
                         m.rack.width() - 6.0, m.platformHeight * 0.92);
    const QRectF frontFace(m.rack.left(), m.platformTop + m.platformHeight * 0.45,
                           m.rack.width(), m.platformHeight * 0.85);

    QLinearGradient topGradient(topFace.topLeft(), topFace.bottomLeft());
    topGradient.setColorAt(0.0, QColor("#F7FBFE"));
    topGradient.setColorAt(0.6, QColor("#D9E9F5"));
    topGradient.setColorAt(1.0, QColor("#BFD6E6"));
    p.setPen(QPen(QColor("#9FBCCF"), 0.8));
    p.setBrush(topGradient);
    p.drawRoundedRect(topFace, 3.0, 3.0);

    QLinearGradient frontGradient(frontFace.topLeft(), frontFace.bottomLeft());
    frontGradient.setColorAt(0.0, QColor("#B7CDDD"));
    frontGradient.setColorAt(0.5, QColor("#91AEC3"));
    frontGradient.setColorAt(1.0, QColor("#728FA6"));
    p.setPen(QPen(QColor("#6D879B"), 0.8));
    p.setBrush(frontGradient);
    p.drawRoundedRect(frontFace, 4.0, 4.0);

    p.setPen(QPen(QColor(255, 255, 255, 150), 1.0));
    p.drawLine(QPointF(topFace.left() + 5.0, topFace.top() + 1.0),
               QPointF(topFace.right() - 5.0, topFace.top() + 1.0));

    p.setPen(QPen(QColor(67, 95, 115, 90), 1.0));
    for (int i = 1; i < kChannelsPerModule; ++i) {
        const qreal x = m.rack.left() + m.slotWidth * i;
        p.drawLine(QPointF(x, topFace.top() + 1.0),
                   QPointF(x, frontFace.bottom() - 1.0));
    }

    p.restore();
}

void ModuleRackWidget::paintCylinderBody(QPainter &p, const LayoutMetrics &m,
                                         int channelIndex)
{
    const ChannelState &state = m_channels.at(channelIndex);
    const QRectF rect = cylinderRect(m, channelIndex);
    const qreal cx = rect.center().x();
    const qreal cw = rect.width();
    const qreal ry = cw * 0.16;

    p.save();

    // 圆柱在平台上的投影
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(34, 75, 104, state.enabled ? 34 : 18));
    p.drawEllipse(QPointF(cx, m.platformTop + m.platformHeight * 0.34),
                  cw * 0.62, cw * 0.19);

    QPainterPath bodyPath;
    bodyPath.setFillRule(Qt::WindingFill);
    const QRectF middle(rect.left(), rect.top() + ry,
                        cw, rect.height() - ry * 2.0);
    bodyPath.addRect(middle);
    bodyPath.addEllipse(QRectF(rect.left(), rect.bottom() - ry * 2.0, cw, ry * 2.0));
    bodyPath.addEllipse(QRectF(rect.left(), rect.top(), cw, ry * 2.0));

    QLinearGradient bodyGradient(rect.left(), 0.0, rect.right(), 0.0);
    if (state.enabled) {
        bodyGradient.setColorAt(0.0, QColor(255, 255, 255, 225));
        bodyGradient.setColorAt(0.18, QColor(189, 224, 244, 215));
        bodyGradient.setColorAt(0.46, QColor(244, 251, 255, 175));
        bodyGradient.setColorAt(0.72, QColor(151, 201, 229, 200));
        bodyGradient.setColorAt(0.90, QColor(208, 237, 250, 220));
        bodyGradient.setColorAt(1.0, QColor(246, 252, 255, 230));
    } else {
        bodyGradient.setColorAt(0.0, QColor(245, 248, 251, 220));
        bodyGradient.setColorAt(0.5, QColor(219, 227, 234, 210));
        bodyGradient.setColorAt(1.0, QColor(239, 244, 248, 220));
    }
    p.setPen(QPen(state.enabled ? QColor(124, 173, 202, 220)
                                : QColor(171, 186, 198, 200), 1.1));
    p.setBrush(bodyGradient);
    p.drawPath(bodyPath);

    QLinearGradient verticalShade(rect.topLeft(), rect.bottomLeft());
    verticalShade.setColorAt(0.0, QColor(255, 255, 255, 85));
    verticalShade.setColorAt(0.55, QColor(255, 255, 255, 0));
    verticalShade.setColorAt(1.0, QColor(40, 84, 116, 42));
    p.setPen(Qt::NoPen);
    p.setBrush(verticalShade);
    p.drawPath(bodyPath);

    const QRectF shine(cx - cw * 0.33, rect.top() + ry + 7.0,
                       cw * 0.10, rect.height() * 0.60);
    QLinearGradient shineGradient(shine.topLeft(), shine.bottomLeft());
    shineGradient.setColorAt(0.0, QColor(255, 255, 255, 0));
    shineGradient.setColorAt(0.25, QColor(255, 255, 255, 165));
    shineGradient.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setBrush(shineGradient);
    p.drawRoundedRect(shine, shine.width() / 2.0, shine.width() / 2.0);

    p.restore();
}

void ModuleRackWidget::paintTube(QPainter &p, const LayoutMetrics &m,
                                 int channelIndex)
{
    const ChannelState &state = m_channels.at(channelIndex);
    if (!state.enabled) {
        return;
    }
    if (state.tubePosition <= 0.001 && !state.tubeInserted) {
        return;
    }

    const qreal cx = channelCenterX(m, channelIndex);
    const qreal eased = smoothStep(state.tubePosition);
    const qreal insertedBottom = m.cylinderBottom - 7.0;
    const qreal removedBottom = m.cylinderTop - m.tubeHeight - 2.0;
    const qreal tubeBottom = removedBottom + (insertedBottom - removedBottom) * eased;
    const QRectF tube(cx - m.tubeWidth / 2.0, tubeBottom - m.tubeHeight,
                      m.tubeWidth, m.tubeHeight);
    if (tube.bottom() < m.rack.top() || tube.top() > m.rack.bottom()) {
        return;
    }

    p.save();
    p.setClipRect(m.rack.adjusted(-2.0, 0.0, 2.0, 0.0));

    const qreal capHeight = qMax<qreal>(8.0, m.tubeWidth * 0.30);
    const QRectF bodyRect(tube.left() + 0.8, tube.top() + capHeight * 0.55,
                          tube.width() - 1.6, tube.height() - capHeight * 0.55);
    QPainterPath tubePath;
    tubePath.addRoundedRect(bodyRect, m.tubeWidth * 0.45, m.tubeWidth * 0.45);

    QLinearGradient glass(bodyRect.left(), 0.0, bodyRect.right(), 0.0);
    glass.setColorAt(0.0, QColor(255, 255, 255, 235));
    glass.setColorAt(0.22, QColor(232, 247, 254, 215));
    glass.setColorAt(0.55, QColor(254, 255, 255, 170));
    glass.setColorAt(0.82, QColor(200, 229, 244, 210));
    glass.setColorAt(1.0, QColor(252, 254, 255, 235));
    p.setPen(QPen(QColor(153, 197, 220, 230), 1.0));
    p.setBrush(glass);
    p.drawPath(tubePath);

    p.save();
    p.setClipPath(tubePath);
    const bool ppp = state.content == TubeContent::Ppp;
    const qreal liquidHeight = bodyRect.height() * 0.38;
    const QRectF liquid(bodyRect.left(), bodyRect.bottom() - liquidHeight - 2.0,
                        bodyRect.width(), liquidHeight);
    QLinearGradient liquidGradient(liquid.topLeft(), liquid.bottomLeft());
    if (ppp) {
        // PPP 血清：更浅的淡黄色
        liquidGradient.setColorAt(0.0, QColor("#FFF7DA"));
        liquidGradient.setColorAt(0.55, QColor("#F4E4B1"));
        liquidGradient.setColorAt(1.0, QColor("#DFC77D"));
    } else {
        // PRP：偏黄的血浆
        liquidGradient.setColorAt(0.0, QColor("#FFE28A"));
        liquidGradient.setColorAt(0.55, QColor("#F3C243"));
        liquidGradient.setColorAt(1.0, QColor("#D99B21"));
    }
    p.setPen(Qt::NoPen);
    p.setBrush(liquidGradient);
    p.drawRect(liquid);

    QColor meniscus = ppp ? QColor(255, 251, 229, 220)
                          : QColor(255, 238, 166, 220);
    p.setBrush(meniscus);
    p.drawEllipse(QRectF(liquid.left(), liquid.top() - 3.0,
                         liquid.width(), 6.0));

    // 试管最底部的小磁棒：与下方马达同步旋转
    const qreal barCenterY = liquid.bottom()
            - qMax<qreal>(5.0, m.tubeWidth * 0.16);
    if (state.motorRunning) {
        QColor ripple = ppp ? QColor(255, 252, 235, 120)
                            : QColor(255, 244, 190, 120);
        QPen ripplePen(ripple);
        ripplePen.setWidthF(1.0);
        p.setPen(ripplePen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(cx, barCenterY),
                      m.tubeWidth * 0.22, m.tubeWidth * 0.075);
        p.drawEllipse(QPointF(cx, barCenterY),
                      m.tubeWidth * 0.30, m.tubeWidth * 0.10);
    }

    const qreal barLength = qMax<qreal>(8.0, m.tubeWidth * 0.72);
    const qreal barThickness = qMax<qreal>(2.4, m.tubeWidth * 0.15);
    p.save();
    p.translate(cx, barCenterY);
    p.scale(1.0, 0.42);
    p.rotate(state.motorAngle);
    const QRectF bar(-barLength / 2.0, -barThickness / 2.0,
                     barLength, barThickness);
    QLinearGradient barGradient(bar.topLeft(), bar.bottomLeft());
    barGradient.setColorAt(0.0, QColor("#B6C4CE"));
    barGradient.setColorAt(0.42, QColor("#667784"));
    barGradient.setColorAt(1.0, QColor("#303C46"));
    p.setPen(QPen(QColor("#27323A"), 0.8));
    p.setBrush(barGradient);
    p.drawRoundedRect(bar, barThickness / 2.0, barThickness / 2.0);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#253039"));
    p.drawEllipse(QPointF(-barLength / 2.0 + barThickness * 0.34, 0.0),
                  barThickness * 0.34, barThickness * 0.34);
    p.drawEllipse(QPointF(barLength / 2.0 - barThickness * 0.34, 0.0),
                  barThickness * 0.34, barThickness * 0.34);
    p.restore();

    p.restore();

    p.setPen(QPen(QColor(255, 255, 255, 170), qMax<qreal>(1.0, m.tubeWidth * 0.06)));
    p.drawLine(QPointF(tube.left() + m.tubeWidth * 0.26, tube.top() + capHeight + 5.0),
               QPointF(tube.left() + m.tubeWidth * 0.26, tube.bottom() - 8.0));

    const QRectF cap(tube.left() - 2.0, tube.top() - 1.0,
                     tube.width() + 4.0, capHeight);
    QLinearGradient capGradient(cap.topLeft(), cap.bottomLeft());
    capGradient.setColorAt(0.0, QColor("#F8D889"));
    capGradient.setColorAt(0.55, QColor("#EFB64B"));
    capGradient.setColorAt(1.0, QColor("#D28B1C"));
    p.setPen(QPen(QColor("#B87816"), 1.0));
    p.setBrush(capGradient);
    p.drawRoundedRect(cap, capHeight * 0.34, capHeight * 0.34);

    const QRectF opening(cap.left() + cap.width() * 0.19, cap.top() - capHeight * 0.10,
                         cap.width() * 0.62, capHeight * 0.44);
    p.setPen(QPen(QColor("#C08A25"), 0.9));
    p.setBrush(QColor(255, 249, 231, 235));
    p.drawEllipse(opening);

    QFont capFont(QStringLiteral("Microsoft YaHei"));
    capFont.setPixelSize(qBound(7, qRound(m.tubeWidth * 0.36), 11));
    capFont.setBold(true);
    p.setFont(capFont);
    p.setPen(QColor("#6A450B"));
    p.drawText(cap.adjusted(0.0, capHeight * 0.08, 0.0, 0.0),
               Qt::AlignCenter, QString::number(globalChannelNumber(channelIndex + 1)));

    p.restore();
}

void ModuleRackWidget::paintCylinderFront(QPainter &p, const LayoutMetrics &m,
                                          int channelIndex)
{
    const ChannelState &state = m_channels.at(channelIndex);
    const QRectF rect = cylinderRect(m, channelIndex);
    const qreal cx = rect.center().x();
    const qreal cw = rect.width();
    const qreal ry = cw * 0.16;
    const QRectF topEllipse(rect.left(), rect.top(), cw, ry * 2.0);

    p.save();

    const QRectF bore(topEllipse.left() + cw * 0.15, topEllipse.top() + cw * 0.05,
                      topEllipse.width() - cw * 0.30,
                      topEllipse.height() - cw * 0.10);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(22, 58, 84, state.enabled ? 105 : 60));
    p.drawEllipse(bore);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 75), 1.0));
    p.drawEllipse(bore.adjusted(2.0, 1.0, -2.0, -1.0));

    if (state.tubePosition < 0.12) {
        QFont numberFont(QStringLiteral("Microsoft YaHei"));
        numberFont.setPixelSize(qBound(10, qRound(cw * 0.33), 18));
        numberFont.setBold(true);
        p.setFont(numberFont);
        p.setPen(QColor(186, 221, 241, state.enabled ? 220 : 130));
        p.drawText(topEllipse, Qt::AlignCenter,
                   QString::number(globalChannelNumber(channelIndex + 1)));
    }

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(116, 168, 199, 235), 1.4));
    p.drawEllipse(topEllipse);

    p.setPen(QPen(QColor(255, 255, 255, 190), 1.1));
    p.drawArc(topEllipse.adjusted(1.0, 1.0, -1.0, -1.0), 28 * 16, 112 * 16);

    const QRectF ringRect = topEllipse.adjusted(-4.0, -4.0, 4.0, 4.0);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(206, 220, 231, 230), 4.5,
                  Qt::SolidLine, Qt::RoundCap));
    p.drawEllipse(ringRect);

    const qreal value = qBound<qreal>(0.0, state.progressDisplay, 100.0);
    if (state.enabled && value > 0.05) {
        const QColor color = progressColor(state);
        QColor glowColor = color;
        glowColor.setAlpha(60);
        QPen glowPen(glowColor);
        glowPen.setWidthF(9.0);
        glowPen.setCapStyle(Qt::RoundCap);
        p.setPen(glowPen);
        p.drawArc(ringRect, 90 * 16, qRound(-360.0 * 16.0 * value / 100.0));

        QPen arcPen(color);
        arcPen.setWidthF(4.5);
        arcPen.setCapStyle(Qt::RoundCap);
        p.setPen(arcPen);
        p.drawArc(ringRect, 90 * 16, qRound(-360.0 * 16.0 * value / 100.0));
    }

    // 每秒 AD 值显示在玻璃管口（顶部圆环下方）
    const bool hasAd = state.info.adValue >= 0;
    const QString adText = hasAd
            ? tr("AD %1").arg(state.info.adValue)
            : tr("AD --");
    QFont adFont(QStringLiteral("Microsoft YaHei"));
    adFont.setPixelSize(qBound(7, qRound(cw * 0.21), 10));
    adFont.setBold(true);
    const QFontMetrics adMetrics(adFont);
    const qreal adWidth = adMetrics.width(adText) + 10.0;
    const qreal adHeight = adMetrics.height() + 4.0;
    const QRectF adPill(cx - adWidth / 2.0,
                        m.cylinderTop + cw * 0.42,
                        adWidth, adHeight);

    QColor adBackground(255, 255, 255, 210);
    QColor adBorder(188, 207, 220, 210);
    QColor adTextColor(128, 149, 165);
    if (state.enabled && hasAd) {
        adBackground = QColor(31, 78, 121, 225);
        adBorder = QColor(21, 58, 92, 235);
        adTextColor = QColor(234, 246, 255);
    }
    p.setPen(QPen(adBorder, 1.0));
    p.setBrush(adBackground);
    p.drawRoundedRect(adPill, adHeight / 2.0, adHeight / 2.0);
    p.setFont(adFont);
    p.setPen(adTextColor);
    p.drawText(adPill, Qt::AlignCenter, adText);

    // 进度百分比放在管身中下部，避开液体内的小磁棒
    const QColor color = progressColor(state);
    const QString percentText = QString::number(qRound(value)) + QStringLiteral("%");
    QFont percentFont(QStringLiteral("Microsoft YaHei"));
    percentFont.setPixelSize(qBound(8, qRound(cw * 0.23), 11));
    percentFont.setBold(true);
    const QFontMetrics percentMetrics(percentFont);
    const qreal pillWidth = percentMetrics.width(percentText) + 10.0;
    const qreal pillHeight = percentMetrics.height() + 4.0;
    qreal pillTop = m.cylinderTop + m.cylinderHeight * 0.55;
    pillTop = qMin(pillTop, m.cylinderBottom - pillHeight - 8.0);
    const QRectF pill(cx - pillWidth / 2.0, pillTop, pillWidth, pillHeight);
    p.setPen(QPen(QColor(color.red(), color.green(), color.blue(), 90), 1.0));
    p.setBrush(QColor(255, 255, 255, 218));
    p.drawRoundedRect(pill, pillHeight / 2.0, pillHeight / 2.0);
    p.setFont(percentFont);
    p.setPen(color);
    p.drawText(pill, Qt::AlignCenter, percentText);

    p.restore();
}

void ModuleRackWidget::paintMotor(QPainter &p, const LayoutMetrics &m,
                                  int channelIndex)
{
    const ChannelState &state = m_channels.at(channelIndex);
    const QRectF rect = motorRect(m, channelIndex);
    if (rect.width() <= 2.0 || rect.height() <= 2.0) {
        return;
    }
    const qreal cx = rect.center().x();
    const qreal motorWidth = rect.width();
    const qreal topEllipseHeight = motorWidth * 0.32;

    p.save();

    const QRectF shaft(cx - 2.0, m.platformTop + m.platformHeight * 0.35,
                       4.0, m.motorTop - m.platformTop - m.platformHeight * 0.35 + 4.0);
    QLinearGradient shaftGradient(shaft.left(), 0.0, shaft.right(), 0.0);
    shaftGradient.setColorAt(0.0, QColor("#D8E4EC"));
    shaftGradient.setColorAt(0.5, QColor("#8197A9"));
    shaftGradient.setColorAt(1.0, QColor("#D8E4EC"));
    p.setPen(Qt::NoPen);
    p.setBrush(shaftGradient);
    p.drawRoundedRect(shaft, 2.0, 2.0);

    QColor glowColor = state.motorRunning ? QColor(37, 194, 110, 60)
                                          : QColor(0, 0, 0, 0);
    if (state.motorRunning && state.enabled) {
        p.setPen(Qt::NoPen);
        p.setBrush(glowColor);
        p.drawEllipse(QPointF(cx, rect.top() + topEllipseHeight * 0.5),
                      motorWidth * 0.62, topEllipseHeight * 1.2);
    }

    const QRectF body(rect.left(), rect.top() + topEllipseHeight * 0.35,
                      rect.width(), rect.height() - topEllipseHeight * 0.35);
    QPainterPath motorPath;
    motorPath.addRoundedRect(body, motorWidth * 0.25, motorWidth * 0.25);

    QLinearGradient bodyGradient(body.topLeft(), body.bottomLeft());
    if (state.enabled) {
        bodyGradient.setColorAt(0.0, QColor("#9FB3C2"));
        bodyGradient.setColorAt(0.32, QColor("#708597"));
        bodyGradient.setColorAt(1.0, QColor("#3F5261"));
    } else {
        bodyGradient.setColorAt(0.0, QColor("#C6D0D8"));
        bodyGradient.setColorAt(1.0, QColor("#8C9AA5"));
    }
    p.setPen(QPen(QColor("#32424F"), 1.0));
    p.setBrush(bodyGradient);
    p.drawPath(motorPath);

    p.setPen(QPen(QColor(255, 255, 255, 90), 1.0));
    p.drawLine(QPointF(body.left() + motorWidth * 0.22, body.top() + 5.0),
               QPointF(body.left() + motorWidth * 0.22, body.bottom() - 5.0));

    const QRectF topEllipse(cx - motorWidth / 2.0, rect.top(),
                            motorWidth, topEllipseHeight);
    QLinearGradient topGradient(topEllipse.topLeft(), topEllipse.bottomLeft());
    topGradient.setColorAt(0.0, QColor("#C7D7E2"));
    topGradient.setColorAt(1.0, QColor("#71889A"));
    p.setPen(QPen(QColor("#4A5D6C"), 1.0));
    p.setBrush(topGradient);
    p.drawEllipse(topEllipse);

    p.save();
    p.translate(cx, topEllipse.center().y());
    p.scale(1.0, 0.38);
    p.rotate(state.motorAngle);
    p.setPen(QPen(QColor(52, 72, 88, 150), 0.7));
    for (int bladeIndex = 0; bladeIndex < 3; ++bladeIndex) {
        p.save();
        p.rotate(bladeIndex * 120.0);
        const QRectF blade(-motorWidth * 0.065, -motorWidth * 0.42,
                           motorWidth * 0.13, motorWidth * 0.36);
        QLinearGradient bladeGradient(blade.topLeft(), blade.bottomLeft());
        bladeGradient.setColorAt(0.0, QColor("#F4FAFD"));
        bladeGradient.setColorAt(0.55, QColor("#C0D3E0"));
        bladeGradient.setColorAt(1.0, QColor("#8AA0B1"));
        p.setBrush(bladeGradient);
        p.drawRoundedRect(blade, blade.width() / 2.0, blade.width() / 2.0);
        p.restore();
    }
    p.restore();

    p.setPen(QPen(QColor("#1E2B35"), 1.0));
    p.setBrush(QColor("#2D3D49"));
    p.drawEllipse(QPointF(cx, topEllipse.center().y()),
                  motorWidth * 0.07, motorWidth * 0.07 * 0.38);

    const QColor ledColor = (!state.enabled ? QColor("#A8B3BC")
                            : (state.motorRunning ? QColor("#25C26E")
                                                  : QColor("#8B9AA7")));
    const QPointF ledPosition(rect.right() - motorWidth * 0.18,
                              rect.top() + rect.height() * 0.55);
    if (state.enabled && state.motorRunning) {
        QColor ledGlow = ledColor;
        ledGlow.setAlpha(70);
        p.setPen(Qt::NoPen);
        p.setBrush(ledGlow);
        p.drawEllipse(ledPosition, 6.0, 6.0);
    }
    p.setPen(QPen(QColor(255, 255, 255, 165), 0.9));
    p.setBrush(ledColor);
    p.drawEllipse(ledPosition, 3.0, 3.0);

    p.restore();
}

void ModuleRackWidget::paintDisabledCross(QPainter &p, const LayoutMetrics &m,
                                          int channelIndex)
{
    if (m_channels.at(channelIndex).enabled) {
        return;
    }

    const QRectF rect = cylinderRect(m, channelIndex)
            .adjusted(-3.0, -3.0, 3.0, 3.0);
    const QPointF topLeft(rect.left() + 4.0, rect.top() + 7.0);
    const QPointF bottomRight(rect.right() - 4.0, rect.bottom() - 7.0);
    const QPointF topRight(rect.right() - 4.0, rect.top() + 7.0);
    const QPointF bottomLeft(rect.left() + 4.0, rect.bottom() - 7.0);

    QPen halo(QColor(255, 255, 255, 205));
    halo.setWidthF(8.5);
    halo.setCapStyle(Qt::RoundCap);
    p.setPen(halo);
    p.drawLine(topLeft, bottomRight);
    p.drawLine(topRight, bottomLeft);

    QPen cross(QColor(214, 48, 49, 235));
    cross.setWidthF(4.2);
    cross.setCapStyle(Qt::RoundCap);
    p.setPen(cross);
    p.drawLine(topLeft, bottomRight);
    p.drawLine(topRight, bottomLeft);
}

void ModuleRackWidget::paintFooter(QPainter &p, const LayoutMetrics &m)
{
    p.save();

    p.setPen(QPen(QColor(210, 226, 237, 220), 1.0));
    p.drawLine(QPointF(m.footer.left(), m.footer.top()),
               QPointF(m.footer.right(), m.footer.top()));

    QFont footerFont(QStringLiteral("Microsoft YaHei"));
    footerFont.setPixelSize(9);
    p.setFont(footerFont);
    const QFontMetrics footerMetrics(footerFont);

    for (int i = 0; i < kChannelsPerModule; ++i) {
        const ChannelState &state = m_channels.at(i);
        const ChannelRunState runState = channelRunState(i + 1);
        QString status = state.status.trimmed();
        if (runState == ChannelRunState::Disabled) {
            status = tr("禁用");
        } else if (runState == ChannelRunState::Abnormal) {
            if (status.isEmpty()) {
                status = tr("异常");
            }
        } else if (runState == ChannelRunState::Testing) {
            if (status.isEmpty()) {
                status = tr("测试中");
            }
        } else if (status.isEmpty()) {
            if (state.motorRunning) {
                status = tr("旋转中");
            } else if (state.tubeInserted) {
                status = tr("就绪");
            } else {
                status = tr("空闲");
            }
        }

        const QString text = QStringLiteral("CH%1  %2")
                .arg(globalChannelNumber(i + 1))
                .arg(status);
        const qreal centerX = channelCenterX(m, i);
        const int availableWidth = qMax(30, qRound(m.slotWidth - 12.0));
        const QString elided = footerMetrics.elidedText(text, Qt::ElideRight,
                                                        availableWidth);
        const QRectF textRect(centerX - availableWidth / 2.0, m.footer.top() + 4.0,
                              availableWidth, m.footer.height() - 5.0);

        const QColor color = footerColor(state);
        p.setPen(color);
        p.drawText(textRect, Qt::AlignCenter, elided);

        const int textWidth = footerMetrics.width(elided);
        const qreal dotX = centerX - textWidth / 2.0 - 7.0;
        if (dotX > textRect.left()) {
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawEllipse(QPointF(dotX, textRect.center().y() + 0.5), 2.4, 2.4);
        }
    }

    p.restore();
}
