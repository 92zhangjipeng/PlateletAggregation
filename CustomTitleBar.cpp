#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "CustomTitleBar.h"
#include "AppIcon.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSizePolicy>

CustomTitleBar::CustomTitleBar(const QString &title, Buttons buttons, QWidget *parent)
    : QWidget(parent)
    , m_buttons(buttons)
{
    setObjectName(QStringLiteral("customTitleBar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(44);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    buildUi();
    setTitle(title);

    if (QWidget *top = window()) {
        top->installEventFilter(this);
    }

    setStyleSheet(QStringLiteral(
        "QWidget#customTitleBar {"
        "  background: #FFFFFF;"
        "  border-bottom: 1px solid #DCE7F1;"
        "}"
        "QLabel#titleBarTitle {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 14px;"
        "  font-weight: 600;"
        "}"
        "QPushButton#titleBarMinButton, QPushButton#titleBarMaxButton {"
        "  background: transparent;"
        "  border: none;"
        "  color: #5A7086;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 16px;"
        "  padding: 0px;"
        "}"
        "QPushButton#titleBarMinButton:hover, QPushButton#titleBarMaxButton:hover {"
        "  background: #E8F1FA;"
        "}"
        "QPushButton#titleBarMinButton:pressed, QPushButton#titleBarMaxButton:pressed {"
        "  background: #D3E6F5;"
        "}"
        "QPushButton#titleBarCloseButton {"
        "  background: transparent;"
        "  border: none;"
        "  color: #5A7086;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 14px;"
        "  padding: 0px;"
        "}"
        "QPushButton#titleBarCloseButton:hover {"
        "  background: #E81123;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#titleBarCloseButton:pressed {"
        "  background: #C50F1F;"
        "  color: #FFFFFF;"
        "}"));
}

void CustomTitleBar::buildUi()
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 6, 0);
    layout->setSpacing(6);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(24, 24);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    const QPixmap icon = appui::appIconPixmap(22);
    if (icon.isNull()) {
        m_iconLabel->hide();
    } else {
        m_iconLabel->setPixmap(icon);
    }
    m_iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(m_iconLabel);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName(QStringLiteral("titleBarTitle"));
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    layout->addWidget(m_titleLabel, 1);

    auto makeButton = [this](const QString &text, const char *objectName,
                             const QString &toolTip, int width) {
        auto *button = new QPushButton(text, this);
        button->setObjectName(QString::fromLatin1(objectName));
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedSize(width, 30);
        button->setToolTip(toolTip);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };

    if (m_buttons.testFlag(MinimizeButton)) {
        m_minButton = makeButton(QStringLiteral("—"), "titleBarMinButton",
                                 tr("最小化"), 40);
        connect(m_minButton, &QPushButton::clicked, this, [this]() {
            emit minimizeRequested();
            if (QWidget *top = window()) {
                top->showMinimized();
            }
        });
        layout->addWidget(m_minButton);
    }

    if (m_buttons.testFlag(MaximizeButton)) {
        m_maxButton = makeButton(QStringLiteral("□"), "titleBarMaxButton",
                                 tr("最大化"), 40);
        connect(m_maxButton, &QPushButton::clicked,
                this, &CustomTitleBar::toggleMaximize);
        layout->addWidget(m_maxButton);
    }

    if (m_buttons.testFlag(CloseButton)) {
        m_closeButton = makeButton(QStringLiteral("✕"), "titleBarCloseButton",
                                   tr("关闭"), 44);
        connect(m_closeButton, &QPushButton::clicked, this, [this]() {
            emit closeRequested();
            if (QWidget *top = window()) {
                top->close();
            }
        });
        layout->addWidget(m_closeButton);
    }

    updateMaximizeButton();
}

void CustomTitleBar::setTitle(const QString &title)
{
    if (m_titleLabel) {
        m_titleLabel->setText(title);
        m_titleLabel->setToolTip(title);
    }
}

QString CustomTitleBar::title() const
{
    return m_titleLabel ? m_titleLabel->text() : QString();
}

void CustomTitleBar::updateMaximizeButton()
{
    if (!m_maxButton) {
        return;
    }
    QWidget *top = window();
    const bool maximized = top && top->isMaximized();
    m_maxButton->setText(maximized ? QStringLiteral("❐") : QStringLiteral("□"));
    m_maxButton->setToolTip(maximized ? tr("还原") : tr("最大化"));
}

void CustomTitleBar::toggleMaximize()
{
    if (!m_buttons.testFlag(MaximizeButton)) {
        return;
    }
    QWidget *top = window();
    if (!top) {
        return;
    }
    if (top->isMaximized()) {
        top->showNormal();
    } else {
        top->showMaximized();
    }
    updateMaximizeButton();
    emit maximizeRestoreRequested();
}

void CustomTitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (QWidget *top = window()) {
            m_dragging = true;
            m_pressPos = event->globalPos();
            m_pressWasMaximized = top->isMaximized();
            m_dragOffset = m_pressPos - top->pos();
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void CustomTitleBar::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging || !(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    QWidget *top = window();
    if (!top) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint globalPos = event->globalPos();

    // 最大化状态下拖动：先还原，再按抓取位置继续拖
    if (m_pressWasMaximized && top->isMaximized()) {
        if ((globalPos - m_pressPos).manhattanLength() < 6) {
            event->accept();
            return;
        }
        const qreal ratio = qreal(m_pressPos.x() - top->geometry().x())
                / qMax(1, top->width());
        top->showNormal();
        const QPoint newPos(int(globalPos.x() - top->width() * ratio),
                            globalPos.y() - height() / 2);
        top->move(newPos);
        m_dragOffset = globalPos - top->pos();
        m_pressWasMaximized = false;
        updateMaximizeButton();
    }

    top->move(globalPos - m_dragOffset);
    event->accept();
}

void CustomTitleBar::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = false;
    QWidget::mouseReleaseEvent(event);
}

void CustomTitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton
            && m_buttons.testFlag(MaximizeButton)) {
        toggleMaximize();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

bool CustomTitleBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == window()) {
        if (event->type() == QEvent::WindowStateChange) {
            updateMaximizeButton();
        } else if (event->type() == QEvent::WindowTitleChange) {
            if (QWidget *top = window()) {
                setTitle(top->windowTitle());
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
