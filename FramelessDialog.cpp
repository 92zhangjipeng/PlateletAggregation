#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "FramelessDialog.h"
#include "AppIcon.h"
#include "CustomTitleBar.h"
#include "FramelessWindowSupport.h"

#include <QKeyEvent>
#include <QShowEvent>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

FramelessDialog::FramelessDialog(const QString &title, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("framelessDialog"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setWindowTitle(title);
    setWindowIcon(appui::appIcon());

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_titleBar = new CustomTitleBar(title, CustomTitleBar::CloseButton, this);
    rootLayout->addWidget(m_titleBar);

    auto *content = new QWidget(this);
    content->setObjectName(QStringLiteral("framelessDialogContent"));
    m_contentLayout = new QVBoxLayout(content);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);
    rootLayout->addWidget(content, 1);
}

void FramelessDialog::setContentWidget(QWidget *widget)
{
    if (widget && m_contentLayout) {
        m_contentLayout->addWidget(widget, 1);
    }
}

void FramelessDialog::embedAsPage()
{
    m_embedded = true;
    setModal(false);
    setWindowFlags(Qt::Widget);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (m_titleBar) {
        m_titleBar->hide();
    }
}

QVBoxLayout *FramelessDialog::contentLayout() const
{
    return m_contentLayout;
}

void FramelessDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (!m_embedded) {
        frameless::enableNativeResize(this);
    }
}

void FramelessDialog::keyPressEvent(QKeyEvent *event)
{
    if (m_embedded) {
        // 作为标签页时不响应 Esc 关闭/隐藏。
        QWidget::keyPressEvent(event);
        return;
    }
    QDialog::keyPressEvent(event);
}

bool FramelessDialog::nativeEvent(const QByteArray &eventType, void *message,
                                  long *result)
{
    if (frameless::handleNativeResizeEvent(this, eventType, message, result)) {
        return true;
    }
    return QDialog::nativeEvent(eventType, message, result);
}
