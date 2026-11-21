#ifndef FRAMELESSDIALOG_H
#define FRAMELESSDIALOG_H

#include <QByteArray>
#include <QDialog>
#include <QString>

class CustomTitleBar;
class QKeyEvent;
class QShowEvent;
class QVBoxLayout;
class QWidget;

/**
 * 带公用自定义标题栏的无边框对话框基类。
 * 后续新对话框继承本类，把内容加到 contentLayout() 或调用 setContentWidget()。
 */
class FramelessDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FramelessDialog(const QString &title, QWidget *parent = nullptr);

    /** 作为主界面标签页嵌入：去掉窗口/模态属性并隐藏自绘标题栏 */
    void embedAsPage();

protected:
    void setContentWidget(QWidget *widget);
    QVBoxLayout *contentLayout() const;

    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message,
                     long *result) override;

private:
    CustomTitleBar *m_titleBar = nullptr;
    QVBoxLayout *m_contentLayout = nullptr;
    bool m_embedded = false;
};

#endif // FRAMELESSDIALOG_H
