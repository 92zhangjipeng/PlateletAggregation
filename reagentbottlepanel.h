#ifndef REAGENTBOTTLEPANEL_H
#define REAGENTBOTTLEPANEL_H

#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>

/**
 * 主界面模组下方的试剂余量面板。
 *
 * 11 个瓶子：
 * 0=S1清洗液（按实拍白色方瓶：切角方瓶身、短粗螺纹圆口）
 * 1=AA左, 2=AA右
 * 3/4=ADP左/右, 5/6=COL左/右, 7/8=EPI左/右, 9/10=RIS左/右
 *    （按实拍透明玻璃小瓶：圆柱瓶身、圆肩、窄颈、顶部翻边瓶口）
 *
 * 每个瓶子支持 0~100% 液位；默认给一组演示液位，后续可调用
 * setLevel()/setLevels() 接入真实余量。
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
    void resetDemoLevels();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct BottleSpec {
        QString label;
        QColor liquidColor;
        bool squareBody = false;
    };

    void paintBottle(QPainter &p, const QRectF &slot, int bottleIndex);
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
};

#endif // REAGENTBOTTLEPANEL_H
