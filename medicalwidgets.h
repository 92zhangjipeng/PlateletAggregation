#ifndef MEDICALWIDGETS_H
#define MEDICALWIDGETS_H

#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFocusEvent>
#include <QFont>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QSpinBox>
#include <QWheelEvent>
#include <QtGlobal>

class QPainter;

/**
 * 全局医疗器械风格控件。
 *
 * 设计原则：
 * - 白底、浅灰蓝描边、6px 圆角，焦点用 2C7BBF 粗描边；
 * - 最小高度 32px，适合触屏/戴手套操作；
 * - 禁用状态灰底灰字，状态清晰；
 * - 下拉框/数字框都不响应滚轮，避免滚动页面时误改参数；
 * - 自绘边框与箭头，不依赖操作系统样式。
 */
namespace medicalui {

constexpr int kControlHeight = 32;
constexpr int kSpinButtonWidth = 22;

QColor controlTextColor(bool enabled);
void paintControlFrame(QPainter &painter, const QRect &rect,
                       bool focused, bool hovered, bool enabled);
void paintChevron(QPainter &painter, const QPoint &center, bool up,
                  const QColor &color);
QRect spinButtonColumn(const QRect &rect);
QRect spinUpButtonRect(const QRect &rect);
QRect spinDownButtonRect(const QRect &rect);
void paintSpinButtons(QPainter &painter, const QRect &rect,
                      bool enabled, int hoverPart, int pressedPart);

} // namespace medicalui

/** 医疗器械风格下拉框（自绘边框、分隔线和下拉箭头） */
class MedicalComboBox : public QComboBox
{
    Q_OBJECT

public:
    explicit MedicalComboBox(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    bool m_hovered = false;
};

/**
 * 医疗器械风格数字输入框基类模板。
 * 自绘边框和上下按钮，内部 QLineEdit 透明化，保留编辑/校验/键盘操作。
 * hoverPart / pressedPart：0=无，1=上，2=下。
 */
template <typename Base>
class MedicalSpinBoxT : public Base
{
public:
    explicit MedicalSpinBoxT(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateEditGeometry();
    int buttonPartAt(const QPoint &position) const;
    void setHoverPart(int part);

    int m_hoverPart = 0;
    int m_pressedPart = 0;
    bool m_hoverTextArea = false;
};

using MedicalSpinBox = MedicalSpinBoxT<QSpinBox>;
using MedicalDoubleSpinBox = MedicalSpinBoxT<QDoubleSpinBox>;

template <typename Base>
MedicalSpinBoxT<Base>::MedicalSpinBoxT(QWidget *parent)
    : Base(parent)
{
    this->setMinimumHeight(medicalui::kControlHeight);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    this->setMouseTracking(true);
    this->setFocusPolicy(Qt::StrongFocus);
    this->setButtonSymbols(QAbstractSpinBox::NoButtons);
    this->setAccelerated(true);

    QFont controlFont = this->font();
    controlFont.setFamily(QStringLiteral("Microsoft YaHei"));
    controlFont.setPixelSize(12);
    this->setFont(controlFont);

    if (QLineEdit *edit = this->lineEdit()) {
        edit->setFrame(false);
        edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        edit->setFont(controlFont);
        edit->setStyleSheet(QStringLiteral(
            "QLineEdit {"
            "  background: transparent;"
            "  border: none;"
            "  padding: 0px 2px;"
            "  color: #1F2D3D;"
            "  selection-background-color: #2C7BBF;"
            "  selection-color: #FFFFFF;"
            "}"
            "QLineEdit:disabled { color: #9AA9B5; }"));
        edit->installEventFilter(this);
    }

    updateEditGeometry();
}

template <typename Base>
void MedicalSpinBoxT<Base>::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    const QLineEdit *edit = this->lineEdit();
    const bool focused = this->hasFocus() || (edit && edit->hasFocus());
    const bool hovered = m_hoverTextArea || m_hoverPart != 0;

    medicalui::paintControlFrame(painter, this->rect(), focused, hovered,
                                 this->isEnabled());
    medicalui::paintSpinButtons(painter, this->rect(), this->isEnabled(),
                                m_hoverPart, m_pressedPart);
}

template <typename Base>
void MedicalSpinBoxT<Base>::wheelEvent(QWheelEvent *event)
{
    // 不改变数值，交给父级滚动区域
    event->ignore();
}

template <typename Base>
void MedicalSpinBoxT<Base>::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const int part = buttonPartAt(event->pos());
        if (part != 0) {
            m_pressedPart = part;
            this->update();
            if (QLineEdit *edit = this->lineEdit()) {
                edit->setFocus(Qt::MouseFocusReason);
            }
            if (part == 1) {
                this->stepUp();
            } else {
                this->stepDown();
            }
            event->accept();
            return;
        }
    }
    Base::mousePressEvent(event);
}

template <typename Base>
void MedicalSpinBoxT<Base>::mouseMoveEvent(QMouseEvent *event)
{
    setHoverPart(buttonPartAt(event->pos()));
    Base::mouseMoveEvent(event);
}

template <typename Base>
void MedicalSpinBoxT<Base>::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_pressedPart != 0) {
        m_pressedPart = 0;
        this->update();
        event->accept();
        return;
    }
    Base::mouseReleaseEvent(event);
}

template <typename Base>
void MedicalSpinBoxT<Base>::leaveEvent(QEvent *event)
{
    m_hoverPart = 0;
    m_pressedPart = 0;
    m_hoverTextArea = false;
    this->update();
    Base::leaveEvent(event);
}

template <typename Base>
void MedicalSpinBoxT<Base>::resizeEvent(QResizeEvent *event)
{
    Base::resizeEvent(event);
    updateEditGeometry();
}

template <typename Base>
void MedicalSpinBoxT<Base>::changeEvent(QEvent *event)
{
    Base::changeEvent(event);
    if (event->type() == QEvent::FontChange
            || event->type() == QEvent::StyleChange) {
        updateEditGeometry();
    }
    if (event->type() == QEvent::EnabledChange
            || event->type() == QEvent::PaletteChange
            || event->type() == QEvent::FontChange) {
        this->update();
    }
}

template <typename Base>
bool MedicalSpinBoxT<Base>::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == this->lineEdit()) {
        switch (event->type()) {
        case QEvent::Enter:
            m_hoverTextArea = true;
            this->update();
            break;
        case QEvent::Leave:
            m_hoverTextArea = false;
            this->update();
            break;
        case QEvent::FocusIn:
        case QEvent::FocusOut:
            this->update();
            break;
        case QEvent::Wheel:
            event->ignore();
            break;
        default:
            break;
        }
    }
    return Base::eventFilter(watched, event);
}

template <typename Base>
void MedicalSpinBoxT<Base>::updateEditGeometry()
{
    QLineEdit *edit = this->lineEdit();
    if (!edit) {
        return;
    }
    const QRect column = medicalui::spinButtonColumn(this->rect());
    const int left = 10;
    const int right = column.left() - 6;
    edit->setGeometry(left, 3, qMax(12, right - left),
                      qMax(12, this->height() - 6));
}

template <typename Base>
int MedicalSpinBoxT<Base>::buttonPartAt(const QPoint &position) const
{
    if (medicalui::spinUpButtonRect(this->rect()).contains(position)) {
        return 1;
    }
    if (medicalui::spinDownButtonRect(this->rect()).contains(position)) {
        return 2;
    }
    return 0;
}

template <typename Base>
void MedicalSpinBoxT<Base>::setHoverPart(int part)
{
    if (m_hoverPart == part) {
        return;
    }
    m_hoverPart = part;
    this->update();
}

#endif // MEDICALWIDGETS_H
