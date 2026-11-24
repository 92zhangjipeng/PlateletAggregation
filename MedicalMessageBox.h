#ifndef MEDICALMESSAGEBOX_H
#define MEDICALMESSAGEBOX_H

#include <QDialog>
#include <QMap>
#include <QMessageBox>
#include <QString>

#include "FramelessDialog.h"

class QLabel;
class QPushButton;
class QVBoxLayout;

/**
 * 全局公用提示框：完全自绘的医疗器械风格消息框（不再继承 QMessageBox）。
 *
 * - 提供与 QMessageBox 静态接口兼容的 information / warning / critical /
 *   question，以及常用枚举别名，现有调用基本只需改头文件/类名；
 * - 实例式用法：构造后 setIcon / setWindowTitle / setText /
 *   setInformativeText / setStandardButtons / setDefaultButton /
 *   button() / standardButton() / clickedButton() / exec()；
 * - 垂直结构：[CustomTitleBar / 图标+主文本行 / 详细信息 / 强调色条 / 按钮行]，
 *   全部控件由本类自己创建，绝无内部布局冲突；
 * - 按钮默认文案与 QMessageBox 标准按钮中文一致，可用 button() 取到后改文案；
 * - exec() 返回值即所点标准按钮的枚举值，语义与 QMessageBox 一致。
 */
class MedicalMessageBox : public FramelessDialog
{
    Q_OBJECT

public:
    // 与 QMessageBox 一致的枚举别名（QMessageBox 标准按钮位值）
    using StandardButton = QMessageBox::StandardButton;
    using StandardButtons = QMessageBox::StandardButtons;
    static const StandardButton NoButton    = QMessageBox::NoButton;
    static const StandardButton Ok          = QMessageBox::Ok;
    static const StandardButton Save        = QMessageBox::Save;
    static const StandardButton SaveAll     = QMessageBox::SaveAll;
    static const StandardButton Open        = QMessageBox::Open;
    static const StandardButton Yes         = QMessageBox::Yes;
    static const StandardButton No          = QMessageBox::No;
    static const StandardButton Abort       = QMessageBox::Abort;
    static const StandardButton Retry       = QMessageBox::Retry;
    static const StandardButton Ignore      = QMessageBox::Ignore;
    static const StandardButton CloseButton = QMessageBox::Close;
    static const StandardButton Cancel      = QMessageBox::Cancel;
    static const StandardButton Discard     = QMessageBox::Discard;
    static const StandardButton Help        = QMessageBox::Help;
    static const StandardButton Apply       = QMessageBox::Apply;
    static const StandardButton Reset       = QMessageBox::Reset;
    static const StandardButton RestoreDefaults
                                            = QMessageBox::RestoreDefaults;
    using Icon = QMessageBox::Icon;
    static const Icon NoIcon       = QMessageBox::NoIcon;
    static const Icon Information  = QMessageBox::Information;
    static const Icon Warning      = QMessageBox::Warning;
    static const Icon Critical     = QMessageBox::Critical;
    static const Icon Question     = QMessageBox::Question;

    explicit MedicalMessageBox(QWidget *parent = nullptr);

    /** 兼容旧用法：实例配置完成后调用一次（现在其实可以直接 exec） */
    void applyMedicalStyle();

    void setIcon(Icon icon);
    Icon icon() const { return m_icon; }

    void setText(const QString &text);
    QString text() const { return m_text; }

    void setInformativeText(const QString &text);
    QString informativeText() const { return m_informative; }

    void setStandardButtons(StandardButtons buttons);
    StandardButtons standardButtons() const { return m_standardButtons; }

    void addButton(QPushButton *button);
    /** 兼容 QMessageBox::button(standard)：返回对应标准按钮控件 */
    QPushButton *button(StandardButton which) const;
    /** 兼容 QMessageBox::standardButton(btn) */
    StandardButton standardButton(const QAbstractButton *button) const;
    QAbstractButton *clickedButton() const { return m_clickedButton; }

    void setDefaultButton(StandardButton which);
    void setDefaultButton(QPushButton *button);

    void setVisible(bool visible) override;

    // 与 QMessageBox 同名静态接口
    static void information(QWidget *parent, const QString &title,
                            const QString &text);
    static void warning(QWidget *parent, const QString &title,
                        const QString &text);
    static void critical(QWidget *parent, const QString &title,
                         const QString &text);
    static StandardButton question(QWidget *parent, const QString &title,
                                   const QString &text,
                                   StandardButtons buttons = Yes | No,
                                   StandardButton defaultButton = NoButton);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void buildUi();
    void rebuildButtons();
    void syncTexts();
    void applyDialogGeometry();
    static QString standardText(StandardButton button);

    Icon m_icon = NoIcon;
    QString m_text;
    QString m_informative;
    StandardButtons m_standardButtons = NoButton;
    StandardButton m_defaultStandard = NoButton;

    QVBoxLayout *m_contentLayout = nullptr;
    QLabel *m_iconLabel = nullptr;
    QLabel *m_textLabel = nullptr;
    QLabel *m_informativeLabel = nullptr;
    QLabel *m_accentBar = nullptr;
    QWidget *m_buttonArea = nullptr;

    QList<QPushButton*> m_buttons;          // 按钮控件
    QList<StandardButton> m_buttonStandards;// 与 m_buttons 对应的标准值
    QMap<StandardButton, QPushButton*> m_standardMap;
    QAbstractButton *m_clickedButton = nullptr;
};

#endif // MEDICALMESSAGEBOX_H
