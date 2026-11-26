#ifndef REAGENTBOTTLEPANEL_H
#define REAGENTBOTTLEPANEL_H

#include <QBasicTimer>
#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>

class QPaintEvent;
class QTimerEvent;

/**
 * 主界面模组左侧的试剂余量面板（竖排）。
 *
 * 11 个瓶子，按行排列：
 * 第1行 = S1清洗液（居中，按实拍白色方瓶：切角方瓶身、短粗螺纹圆口）
 * 之后每行成对：AA左/右、ADP左/右、COL左/右、EPI左/右、RIS左/右
 *    （按实拍透明玻璃小瓶：圆柱瓶身、圆肩、窄颈、顶部翻边瓶口）
 *
 * 每个瓶子支持 0~100% 液位；未连接主板时全部显示 0%，
 * 连接后由主窗口按主板耗材余量调用 setLevel() 更新真实比例。
 */
class ReagentBottlePanel : public QWidget
{
    Q_OBJECT

public:
    enum BottleIndex {
        S1Clean = 0,
        AA_Left,
        AA_Right,
        ADP_Left,
        ADP_Right,
        COL_Left,
        COL_Right,
        EPI_Left,
        EPI_Right,
        RIS_Left,
        RIS_Right,
        BottleCount
    };

    explicit ReagentBottlePanel(QWidget *parent = nullptr);

    int bottleCount() const;
    double level(int bottleIndex) const;
    void setLevel(int bottleIndex, double percent);
    void setLevels(const QVector<double> &levels);
    /** 清除全部液位并复位报警闪烁状态 */
    void clearLevels();

    /**
     * 设置单个瓶子的报警阈值（%）。
     * 未设置时默认 0：表示关闭该瓶的报警闪烁。
     */
    void setAlarmPercent(int bottleIndex, double percent);
    /**
     * 设置单个瓶子的报警阈值（%）。
     * 未设置时默认 0：表示关闭该瓶的报警闪烁。
     */
    void setAlarmPercents(const QVector<double> &percents);
    /** 获取单个瓶子当前报警阈值（默认 0 = 关闭）。 */
    double alarmPercent(int bottleIndex) const;
    /** 单个瓶子当前是否处于低余量报警状态（百分位低于对应阈值）。 */
    bool isAlarmActive(int bottleIndex) const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void timerEvent(QTimerEvent *event) override;

private:
    struct BottleSpec {
        QString label;
        QColor liquidColor;
        bool squareBody = false;
    };

    void updateAlarmTimer();
    void paintBottle(QPainter &p, const QRectF &slot, int bottleIndex);
    /** 在瓶身正中绘制液位百分比，含报警红闪。 */
    void paintBottlePercent(QPainter &p, const QRectF &body,
                            double percent, bool alarm);
    /** S1 白色方形清洗液瓶（切角瓶身 + 螺纹短颈） */
    void paintCleaningBottle(QPainter &p, const QRectF &body,
                             const BottleSpec &spec, double percent);
    void paintCleaningBottleNeck(QPainter &p, const QRectF &body,
                                 const BottleSpec &spec);
    /** 透明玻璃试剂小瓶（圆柱瓶身 + 圆肩 + 翻边瓶口） */
    void paintReagentVial(QPainter &p, const QRectF &body,
                          const BottleSpec &spec, double percent);
    void paintReagentVialNeck(QPainter &p, const QRectF &body,
                              const BottleSpec &spec);

    QVector<BottleSpec> m_specs;
    QVector<double> m_levels;
    QVector<double> m_alarmPercents;
    QBasicTimer m_blinkTimer;
    bool m_blinkOn = false;
};

#endif // REAGENTBOTTLEPANEL_H
