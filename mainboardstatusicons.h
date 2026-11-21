#ifndef MAINBOARDSTATUSICONS_H
#define MAINBOARDSTATUSICONS_H

#include <QPoint>
#include <QRectF>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QWidget>

class QMouseEvent;
class QPainter;
class QPaintEvent;

/**
 * 状态栏里的主板状态小图标：
 * - 外部清洗液 S2
 * - 废液
 * - 4 个托盘
 * - 测高检测
 *
 * 正常时显示绿色状态点；异常时红色图标和状态点按约 450ms 闪烁。
 * 鼠标悬停可查看每个图标的具体状态。
 */
class MainBoardStatusIconBar : public QWidget
{
    Q_OBJECT

public:
    explicit MainBoardStatusIconBar(QWidget *parent = nullptr);

    void setStatus(bool s2Normal, bool wasteNormal,
                   const bool cupPresent[4], bool heightOk, int activeCupCount);
    void clearStatus(const QString &reason = QString());

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    enum IconId {
        IconS2 = 0,
        IconWaste,
        IconCup1,
        IconCup2,
        IconCup3,
        IconCup4,
        IconHeight,
        IconCount
    };

    struct IconState {
        QString tooltip;
        bool known = false;
        bool normal = true;
        bool active = true;
    };

    QRectF iconRect(int index) const;
    int iconAt(const QPoint &pos) const;
    void paintIcon(QPainter &p, int index);
    void updateBlinkTimer();

    QVector<IconState> m_icons;
    QTimer m_blinkTimer;
    bool m_blinkOn = true;
    int m_activeCupCount = 4;
    int m_hoverIndex = -1;
};

#endif // MAINBOARDSTATUSICONS_H
