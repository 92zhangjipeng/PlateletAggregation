#ifndef CUSTOMTITLEBAR_H
#define CUSTOMTITLEBAR_H

#include <QPoint>
#include <QWidget>

class QLabel;
class QPushButton;

/**
 * 公用自定义标题栏（无边框窗口使用）
 *
 * - 左侧：程序图标 + 标题
 * - 右侧：最小化 / 最大化-还原 / 关闭（按构造参数决定）
 * - 按住空白处拖动窗口；双击最大化/还原；Windows 下由窗口类负责边缘缩放
 */
class CustomTitleBar : public QWidget
{
    Q_OBJECT

public:
    enum ButtonFlag {
        MinimizeButton = 0x01,
        MaximizeButton = 0x02,
        CloseButton    = 0x04
    };
    Q_DECLARE_FLAGS(Buttons, ButtonFlag)

    explicit CustomTitleBar(const QString &title,
                            Buttons buttons = CloseButton,
                            QWidget *parent = nullptr);

    void setTitle(const QString &title);
    QString title() const;
    void updateMaximizeButton();

signals:
    void minimizeRequested();
    void maximizeRestoreRequested();
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void toggleMaximize();

    QLabel *m_iconLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QPushButton *m_minButton = nullptr;
    QPushButton *m_maxButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    Buttons m_buttons;
    bool m_dragging = false;
    bool m_pressWasMaximized = false;
    QPoint m_dragOffset;
    QPoint m_pressPos;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(CustomTitleBar::Buttons)

#endif // CUSTOMTITLEBAR_H
