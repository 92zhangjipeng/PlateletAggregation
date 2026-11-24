#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "MedicalMessageBox.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QVBoxLayout>

namespace {

const char *kMessageBoxStyle =
    "MedicalMessageBox {"
    "  background: #FFFFFF;"
    "}"
    "MedicalMessageBox QLabel {"
    "  color: #1F4E79;"
    "  font-family: 'Microsoft YaHei';"
    "  font-size: 14px;"
    "  background: transparent;"
    "}"
    "MedicalMessageBox QPushButton {"
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
    "      stop:0 #FFFFFF, stop:1 #D6EAF8);"
    "  color: #1F4E79;"
    "  border: 1px solid #A8CBE4;"
    "  border-radius: 6px;"
    "  min-width: 88px;"
    "  min-height: 34px;"
    "  padding: 0px 18px;"
    "  font-family: 'Microsoft YaHei';"
    "  font-size: 13px;"
    "  font-weight: 600;"
    "}"
    "MedicalMessageBox QPushButton:hover {"
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
    "      stop:0 #FFFFFF, stop:1 #C4E1F5);"
    "  border: 1px solid #6FA8D0;"
    "}"
    "MedicalMessageBox QPushButton:pressed {"
    "  background: #B3D6EE;"
    "  border: 1px solid #5A94BE;"
    "}"
    "MedicalMessageBox QPushButton:default {"
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
    "      stop:0 #4C9FDC, stop:1 #2C7BBF);"
    "  color: #FFFFFF;"
    "  border: 1px solid #2C7BBF;"
    "}"
    "MedicalMessageBox QPushButton:default:hover {"
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
    "      stop:0 #5FAEE6, stop:1 #3589CC);"
    "}";

const int kIconSize = 36;
const int kMinWidth = 380;
const int kMaxTextWidth = 520;   // 主文本单行最大宽度，超过则换行
const int kMaxWidth = 680;       // 对话框最大宽度

/// 按图标给出强调色
QColor accentFor(QMessageBox::Icon icon)
{
    switch (icon) {
    case QMessageBox::Warning:  return QColor(0xB7, 0x79, 0x1F);
    case QMessageBox::Critical: return QColor(0xC0, 0x39, 0x2B);
    case QMessageBox::Question: return QColor(0x2C, 0x7B, 0xBF);
    case QMessageBox::Information: return QColor(0x2E, 0x8B, 0x57);
    default:                    return QColor(0x2E, 0x8B, 0x57);
    }
}

QString iconGlyph(QMessageBox::Icon icon)
{
    switch (icon) {
    case QMessageBox::Warning:  return QStringLiteral("!");
    case QMessageBox::Critical: return QStringLiteral("✕");
    case QMessageBox::Question: return QStringLiteral("?");
    case QMessageBox::Information: return QStringLiteral("i");
    default:                    return QStringLiteral("i");
    }
}

} // namespace

MedicalMessageBox::MedicalMessageBox(QWidget *parent)
    : FramelessDialog(QString(), parent)
{
    setStyleSheet(QString::fromLatin1(kMessageBoxStyle));
    setWindowModality(Qt::ApplicationModal);
    setMinimumWidth(kMinWidth);

    m_contentLayout = contentLayout();
    m_contentLayout->setContentsMargins(24, 20, 24, 16);
    m_contentLayout->setSpacing(12);

    buildUi();
}

void MedicalMessageBox::buildUi()
{
    // 消息行：圆形语义图标 + 主文本
    auto *messageRow = new QHBoxLayout();
    messageRow->setSpacing(14);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(kIconSize, kIconSize);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    messageRow->addWidget(m_iconLabel, 0, Qt::AlignTop);

    m_textLabel = new QLabel(this);
    m_textLabel->setTextFormat(Qt::PlainText);
    m_textLabel->setWordWrap(true);
    m_textLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_textLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    messageRow->addWidget(m_textLabel, 1);

    m_contentLayout->addLayout(messageRow);

    // 详细信息（可折叠不需要，直接多行展示）
    m_informativeLabel = new QLabel(this);
    m_informativeLabel->setTextFormat(Qt::PlainText);
    m_informativeLabel->setWordWrap(true);
    m_informativeLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_informativeLabel->setStyleSheet(
        "color: #4A6B8A; font-size: 13px;");
    m_informativeLabel->hide();
    m_contentLayout->addWidget(m_informativeLabel);

    // 图标语义色强调条
    m_accentBar = new QLabel(this);
    m_accentBar->setFixedSize(46, 4);
    m_contentLayout->addWidget(m_accentBar, 0, Qt::AlignLeft);

    // 按钮行
    m_buttonArea = new QWidget(this);
    auto *buttonRow = new QHBoxLayout(m_buttonArea);
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(10);
    buttonRow->addStretch(1);
    m_contentLayout->addWidget(m_buttonArea);
}

void MedicalMessageBox::setIcon(Icon icon)
{
    m_icon = icon;
    const QColor accent = accentFor(icon);
    if (m_iconLabel) {
        m_iconLabel->setText(iconGlyph(icon));
        m_iconLabel->setStyleSheet(QStringLiteral(
            "background: %1; color: #FFFFFF; border-radius: %2px;"
            "font-size: 20px; font-weight: bold; border: none;")
            .arg(accent.name())
            .arg(kIconSize / 2));
    }
    if (m_accentBar) {
        m_accentBar->setStyleSheet(QStringLiteral(
            "background: %1; border-radius: 2px; border: none;")
            .arg(accent.name()));
    }
}

void MedicalMessageBox::setText(const QString &text)
{
    m_text = text;
    if (m_textLabel) {
        m_textLabel->setText(text);
    }
}

void MedicalMessageBox::setInformativeText(const QString &text)
{
    m_informative = text;
    if (m_informativeLabel) {
        m_informativeLabel->setText(text);
        m_informativeLabel->setVisible(!text.isEmpty());
    }
}

void MedicalMessageBox::setStandardButtons(StandardButtons buttons)
{
    m_standardButtons = buttons;
    rebuildButtons();
}

QPushButton *MedicalMessageBox::button(StandardButton which) const
{
    return m_standardMap.value(which, nullptr);
}

MedicalMessageBox::StandardButton
MedicalMessageBox::standardButton(const QAbstractButton *button) const
{
    if (!button) {
        return NoButton;
    }
    for (int i = 0; i < m_buttons.size(); ++i) {
        if (m_buttons.at(i) == button) {
            return m_buttonStandards.at(i);
        }
    }
    return NoButton;
}

void MedicalMessageBox::addButton(QPushButton *button)
{
    if (!button || !m_buttonArea) {
        return;
    }
    button->setParent(m_buttonArea);
    m_buttonArea->layout()->addWidget(button);
    connect(button, &QAbstractButton::clicked, this, [this, button]() {
        m_clickedButton = button;
        done(QDialog::Accepted);
    });
}

void MedicalMessageBox::setDefaultButton(StandardButton which)
{
    m_defaultStandard = which;
    if (QPushButton *btn = button(which)) {
        btn->setDefault(true);
    }
}

void MedicalMessageBox::setDefaultButton(QPushButton *button)
{
    if (button) {
        button->setDefault(true);
    }
}

QString MedicalMessageBox::standardText(StandardButton button)
{
    switch (button) {
    case QMessageBox::Ok:       return QStringLiteral("确定");
    case QMessageBox::Yes:      return QStringLiteral("是");
    case QMessageBox::No:       return QStringLiteral("否");
    case QMessageBox::Cancel:   return QStringLiteral("取消");
    case QMessageBox::Save:     return QStringLiteral("保存");
    case QMessageBox::SaveAll:  return QStringLiteral("全部保存");
    case QMessageBox::Open:     return QStringLiteral("打开");
    case QMessageBox::Close:    return QStringLiteral("关闭");
    case QMessageBox::Discard:  return QStringLiteral("丢弃");
    case QMessageBox::Apply:    return QStringLiteral("应用");
    case QMessageBox::Reset:    return QStringLiteral("重置");
    case QMessageBox::Help:     return QStringLiteral("帮助");
    case QMessageBox::Abort:    return QStringLiteral("中止");
    case QMessageBox::Retry:    return QStringLiteral("重试");
    case QMessageBox::Ignore:   return QStringLiteral("忽略");
    case QMessageBox::RestoreDefaults: return QStringLiteral("恢复默认");
    default:                    return QString();
    }
}

void MedicalMessageBox::rebuildButtons()
{
    if (!m_buttonArea) {
        return;
    }
    // 清掉旧按钮
    for (QPushButton *btn : m_buttons) {
        btn->deleteLater();
    }
    m_buttons.clear();
    m_buttonStandards.clear();
    m_standardMap.clear();
    m_clickedButton = nullptr;

    auto *buttonRow = static_cast<QHBoxLayout *>(m_buttonArea->layout());
    // stretch 在最前，新按钮插到末尾
    const quint32 bits = static_cast<quint32>(m_standardButtons);
    for (int bit = 0; bit < 32; ++bit) {
        const StandardButton stdBtn = static_cast<StandardButton>(1u << bit);
        if (!(bits & (1u << bit))) {
            continue;
        }
        QPushButton *btn = new QPushButton(standardText(stdBtn), m_buttonArea);
        btn->setDefault(stdBtn == m_defaultStandard);
        connect(btn, &QAbstractButton::clicked, this, [this, btn, stdBtn]() {
            m_clickedButton = btn;
            done(static_cast<int>(stdBtn));
        });
        buttonRow->addWidget(btn, 0, Qt::AlignBottom);
        m_buttons.append(btn);
        m_buttonStandards.append(stdBtn);
        m_standardMap.insert(stdBtn, btn);
    }
}

void MedicalMessageBox::syncTexts()
{
    // 标题栏标题在 FramelessDialog 里创建，这里重新设置
    setWindowTitle(windowTitle());
    setText(m_text);
    setInformativeText(m_informative);
    setIcon(m_icon);
}

void MedicalMessageBox::applyDialogGeometry()
{
    // 主文本单行放下时的宽度，超过则换行
    const QFontMetrics fm(m_textLabel->font());
    int textWidth = fm.width(m_text);
    if (!m_informative.isEmpty()) {
        const QFontMetrics ifm(m_informativeLabel->font());
        textWidth = qMax(textWidth,
                         ifm.width(m_informative.split('\n').first()));
    }
    int w = textWidth + kIconSize + 14 /*spacing*/ + 48 /*margins*/;
    w = qBound(kMinWidth, w, kMaxWidth);
    resize(w, sizeHint().height());

    // 居中到父窗口（顶层）或所在屏幕
    QRect anchor;
    if (parentWidget()) {
        anchor = parentWidget()->window()->geometry();
    } else if (QScreen *screen = QApplication::primaryScreen()) {
        anchor = screen->availableGeometry();
    }
    if (!anchor.isNull()) {
        QPoint center = anchor.center()
                        - QRect(QPoint(0, 0), size()).center();
        move(center);
    }
}

void MedicalMessageBox::setVisible(bool visible)
{
    if (visible) {
        syncTexts();
        applyDialogGeometry();
    }
    QDialog::setVisible(visible);
}

void MedicalMessageBox::applyMedicalStyle()
{
    // 兼容旧调用：实例配置完后调用一次。现在所有配置都是即时生效的，
    // 这里仅确保按钮已按标准按钮集合重建。
    rebuildButtons();
}

void MedicalMessageBox::keyPressEvent(QKeyEvent *event)
{
    // Esc 触发取消类按钮（若存在），否则忽略
    if (event->matches(QKeySequence::Cancel)) {
        if (QPushButton *cancel = button(Cancel)) {
            cancel->click();
            return;
        }
        if (QPushButton *no = button(No)) {
            no->click();
            return;
        }
    }
    FramelessDialog::keyPressEvent(event);
}

void MedicalMessageBox::information(QWidget *parent, const QString &title,
                                    const QString &text)
{
    MedicalMessageBox box(parent);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(title);
    box.setText(text);
    box.setStandardButtons(QMessageBox::Ok);
    box.setDefaultButton(QMessageBox::Ok);
    box.exec();
}

void MedicalMessageBox::warning(QWidget *parent, const QString &title,
                                const QString &text)
{
    MedicalMessageBox box(parent);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(title);
    box.setText(text);
    box.setStandardButtons(QMessageBox::Ok);
    box.setDefaultButton(QMessageBox::Ok);
    box.exec();
}

void MedicalMessageBox::critical(QWidget *parent, const QString &title,
                                 const QString &text)
{
    MedicalMessageBox box(parent);
    box.setIcon(QMessageBox::Critical);
    box.setWindowTitle(title);
    box.setText(text);
    box.setStandardButtons(QMessageBox::Ok);
    box.setDefaultButton(QMessageBox::Ok);
    box.exec();
}

MedicalMessageBox::StandardButton MedicalMessageBox::question(
        QWidget *parent, const QString &title, const QString &text,
        StandardButtons buttons, StandardButton defaultButton)
{
    MedicalMessageBox box(parent);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(title);
    box.setText(text);
    box.setStandardButtons(buttons);
    if (buttons & defaultButton) {
        box.setDefaultButton(defaultButton);
    } else {
        box.setDefaultButton(QMessageBox::Yes);
    }
    return static_cast<StandardButton>(box.exec());
}
