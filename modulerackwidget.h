#ifndef MODULERACKWIDGET_H
#define MODULERACKWIDGET_H

#include "channelrunstate.h"

#include <QColor>
#include <QElapsedTimer>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QWidget>

class QMouseEvent;
class QPainter;
class QPaintEvent;

/**
 * 一个检测模组的伪 3D 视图。
 *
 * - 一个模组固定 4 路通道（CH1~CH4，全局通道号由模组号换算）；
 * - 4 个竖直的玻璃圆柱表示通道，试管可以插入 / 拔出（带滑动动画）；
 * - 每个圆柱顶部有一个进度环，显示该通道的测试进度；
 * - 每个圆柱底部显示该通道每秒的 AD 值；
 * - 圆柱下方的小马达由测试流程通过 startMotor()/stopMotor() 控制；
 * - 鼠标点击玻璃管不会改变插管/马达状态：只有在全局测试状态为 true、
 *   且该通道已插入试管时，点击玻璃管才发出 curveRequested()，
 *   由上层弹出该通道的实时曲线窗口。
 *
 * 控件只负责显示和保存本通道的曲线数据，不依赖串口/协议。
 */
class ModuleRackWidget : public QWidget
{
    Q_OBJECT

public:
    /** 通道测试信息，用于曲线窗口顶部的“样本/试剂/时间/PRP/AD”显示 */
    struct ChannelTestInfo {
        QString sampleNumber;       // 例如 "0001"
        QString reagentName;        // 例如 "AA"
        int elapsedSeconds = 0;     // 测试已进行秒数，例如 300
        int prpValue = -1;          // PRP 实时值，测试中随每秒 AD 刷新
        int prp0Value = -1;         // 试剂测试起始 PRP0，用于保存结果
        int adValue = -1;           // 最近一次每秒 AD 值
    };

    /** 试管内液体类型：PRP 偏黄，PPP 血清更浅 */
    enum class TubeContent {
        Prp = 0,
        Ppp = 1
    };

    explicit ModuleRackWidget(int moduleNumber = 1, QWidget *parent = nullptr);
    ~ModuleRackWidget() override;

    int moduleNumber() const;
    void setModuleNumber(int moduleNumber);

    /** 每个模组固定 4 路通道 */
    int channelCount() const;
    /** localChannel 为 1~4，返回全局通道号（模组1=1~4，模组2=5~8...） */
    int globalChannelNumber(int localChannel) const;

    bool isChannelEnabled(int localChannel) const;
    bool isTubeInserted(int localChannel) const;
    bool isMotorRunning(int localChannel) const;
    /** 通道运行状态：空闲 / 测试中 / 异常 / 禁用 */
    ChannelRunState channelRunState(int localChannel) const;
    /** 模组温度（摄氏度）；负数表示暂无有效温度 */
    double moduleTemperature() const;
    int channelProgress(int localChannel) const;
    QString channelStatus(int localChannel) const;
    TubeContent tubeContent(int localChannel) const;
    int runningChannelCount() const;
    int insertedTubeCount() const;

    /** 通道测试信息（样本号、试剂、PRP、AD 等） */
    ChannelTestInfo channelTestInfo(int localChannel) const;
    int channelAdValue(int localChannel) const;
    /** 曲线窗口用的数据副本，x 为秒，y 为 AD 或集聚百分比 */
    QVector<QPointF> testCurve(int localChannel) const;
    QVector<QPointF> aggregationCurve(int localChannel) const;

public slots:
    /** 启用/禁用一路通道；禁用时自动停止马达并拔出试管 */
    void setChannelEnabled(int localChannel, bool enabled);
    /**
     * 设置通道运行状态。
     * - Disabled：等价于 setChannelEnabled(false)，会停止马达、拔出试管并清空曲线；
     * - 其它状态：通道未启用时先自动启用，再写入状态。
     */
    void setChannelRunState(int localChannel, ChannelRunState state);
    /** 插入/拔出试管，animated=true 时带滑动动画；重新插管会清空上一轮曲线 */
    void setTubeInserted(int localChannel, bool inserted, bool animated = true);
    /** 设置试管内液体类型（PRP 偏黄 / PPP 血清浅黄） */
    void setTubeContent(int localChannel, TubeContent content);
    /** 设置进度环百分比（0~100） */
    void setChannelProgress(int localChannel, int percent);
    /** 通道底部的小字状态，例如“等待”“完成”“样本:123[AA]” */
    void setChannelStatus(int localChannel, const QString &status);
    /** 马达接口：启动 / 停止 / 直接设置状态；鼠标点击不会触发 */
    void startMotor(int localChannel);
    void stopMotor(int localChannel);
    void setMotorRunning(int localChannel, bool running);
    /** 清空所有通道状态（试管拔出、马达停止、进度归零、曲线清空） */
    void resetChannels();

    /** 设置模组温度并显示在模组顶部居中位置；传负数表示暂无有效温度 */
    void setModuleTemperature(double celsius);

    /** 设置该通道的样本/PRP 等曲线头部信息 */
    void setChannelTestInfo(int localChannel, const ChannelTestInfo &info);
    /**
     * 每秒 AD 值。
     * - 始终更新玻璃管底部的 AD 显示；
     * - 全局测试状态为 true、通道已启用且试管已插入时，还会把该值追加到
     *   “测试曲线”，并按每秒一次递增 elapsedSeconds。
     */
    void setChannelAdValue(int localChannel, int adValue);
    /** 显式追加测试曲线点（如果不想使用 AD 自动采集） */
    void appendChannelTestPoint(int localChannel, double seconds, double value);
    /** 追加集聚曲线点，seconds 为秒，value 为集聚百分比 */
    void appendChannelAggregationPoint(int localChannel, double seconds,
                                       double value);
    /** 清空该通道两条曲线 */
    void clearChannelCurve(int localChannel);

signals:
    /** 测试状态为 true 且玻璃管内已插管时，点击玻璃管发出 */
    void curveRequested(int moduleNumber, int localChannel);
    /** 测试曲线/集聚曲线数据变化（供曲线窗口实时刷新） */
    void curveDataChanged(int moduleNumber, int localChannel);
    /** 样本号、PRP、AD 等头部信息变化 */
    void channelTestInfoChanged(int moduleNumber, int localChannel);
    /** 通道运行状态变化（空闲/测试中/异常/禁用） */
    void channelRunStateChanged(int moduleNumber, int localChannel,
                                ChannelRunState state);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    struct ChannelState {
        bool enabled = true;
        bool tubeInserted = false;
        qreal tubePosition = 0.0;      // 0=完全拔出，1=完全插入
        int progress = 0;
        qreal progressDisplay = 0.0;   // 进度环平滑动画值
        ChannelRunState runState = ChannelRunState::Idle;
        bool motorRunning = false;
        qreal motorAngle = 0.0;
        QString status;
        TubeContent content = TubeContent::Prp;
        ChannelTestInfo info;
        QVector<QPointF> testCurve;
        QVector<QPointF> aggregationCurve;
    };

    struct LayoutMetrics {
        QRectF card;
        QRectF header;
        QRectF rack;
        QRectF footer;
        qreal slotWidth = 0.0;
        qreal cylinderTop = 0.0;
        qreal cylinderBottom = 0.0;
        qreal cylinderHeight = 0.0;
        qreal cylinderWidth = 0.0;
        qreal tubeWidth = 0.0;
        qreal tubeHeight = 0.0;
        qreal platformTop = 0.0;
        qreal platformHeight = 0.0;
        qreal motorTop = 0.0;
        qreal motorHeight = 0.0;
        qreal motorWidth = 0.0;
    };

    void ensureAnimationTimer();
    void handleAnimationTick();
    int cylinderChannelAt(const QPoint &pos) const;
    int channelIndex(int localChannel) const;
    bool canOpenCurve(int localChannel) const;

    LayoutMetrics metrics() const;
    QRectF slotRect(const LayoutMetrics &m, int channelIndex) const;
    QRectF cylinderRect(const LayoutMetrics &m, int channelIndex) const;
    QRectF motorRect(const LayoutMetrics &m, int channelIndex) const;
    qreal channelCenterX(const LayoutMetrics &m, int channelIndex) const;
    qreal smoothStep(qreal value) const;
    QColor progressColor(const ChannelState &state) const;
    QColor footerColor(const ChannelState &state) const;

    void paintCard(QPainter &p, const LayoutMetrics &m);
    void paintHeader(QPainter &p, const LayoutMetrics &m);
    void paintRackBack(QPainter &p, const LayoutMetrics &m);
    void paintPlatform(QPainter &p, const LayoutMetrics &m);
    void paintCylinderBody(QPainter &p, const LayoutMetrics &m, int channelIndex);
    void paintTube(QPainter &p, const LayoutMetrics &m, int channelIndex);
    void paintCylinderFront(QPainter &p, const LayoutMetrics &m, int channelIndex);
    void paintMotor(QPainter &p, const LayoutMetrics &m, int channelIndex);
    void paintDisabledCross(QPainter &p, const LayoutMetrics &m, int channelIndex);
    void paintFooter(QPainter &p, const LayoutMetrics &m);

    QVector<ChannelState> m_channels;
    int m_moduleNumber = 1;
    double m_moduleTemperature = -1.0;
    QTimer m_animationTimer;
    QElapsedTimer m_animationClock;
};

#endif // MODULERACKWIDGET_H
