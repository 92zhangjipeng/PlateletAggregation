#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "mainwindow.h"
#include "addsamplewidget.h"
#include "consumablesdialog.h"
#include "settingsdialog.h"
#include "CommunicationWorker.h"
#include "cleaningcontroller.h"
#include "resetcontroller.h"
#include "coordinatefileio.h"
#include "coordinateverificationcontroller.h"
#include "instrumentparameters.h"
#include "motioncontroller.h"
#include "perfusioncontroller.h"
#include "pumppositiontracker.h"
#include "testscheduler.h"
#include "testactioncontroller.h"
#include "AppIcon.h"
#include "CustomTitleBar.h"
#include "FramelessWindowSupport.h"
#include "moduleoverviewwidget.h"
#include "reagentbottlepanel.h"
#include "mainboardstatusicons.h"
#include "mainboardpoller.h"
#include "querywidget.h"
#include "resultsinfowidget.h"
#include "sampledetailpanel.h"
#include "sampletraymodel.h"
#include "testdatastore.h"
#include "testsession.h"
#include "tubetraydialog.h"
#include "appconfig.h"
#include "applogger.h"

#include <QAbstractButton>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include "MedicalMessageBox.h"
#include <QScopedValueRollback>
#include <QSettings>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStringList>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QTextCursor>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <QtMath>
#include <cmath>

namespace {

constexpr int kStartupConnectTimeoutMs = 30000;   // 启动后 30 秒内必须连上仪器

// 联机启动校准参数
// 模组目标温度按模组从 QSettings 读取，默认 35℃，可在模组控制页设置
// 升温超时改为基础设置项（默认 60s），每次升温开始时读入 m_heatingTimeoutMs
constexpr int kDimmingTargetAd = 3300;
constexpr int kDimmingResidualThreshold = 100;    // 超 ±100 视为疑似残留试杯
constexpr int kLedTuningTolerance = 3;            // AD 允许 ±3
constexpr int kMaxCupRetries = 1;   // 丢杯后只重新调光一次
constexpr int kMaxInvalidAdSamples = 8;
constexpr int kDiscardSimulatedMs = 2000;

/** 主板耗材位置 -> 主界面试剂瓶面板索引；无对应瓶返回 -1。 */
int reagentBottleForConsumablePosition(int position)
{
    using namespace MainBoardConsumable;
    switch (position) {
    case AA:            return ReagentBottlePanel::AA_Left;
    case AA_Second:     return ReagentBottlePanel::AA_Right;
    case ADP:           return ReagentBottlePanel::ADP_Left;
    case ADP_Second:    return ReagentBottlePanel::ADP_Right;
    case COL:           return ReagentBottlePanel::COL_Left;
    case COL_Second:    return ReagentBottlePanel::COL_Right;
    case EPI:           return ReagentBottlePanel::EPI_Left;
    case EPI_Second:    return ReagentBottlePanel::EPI_Right;
    case RIS:           return ReagentBottlePanel::RIS_Left;
    case RIS_Second:    return ReagentBottlePanel::RIS_Right;
    case InternalCleanS1: return ReagentBottlePanel::S1Clean;
    default:            return -1;
    }
}

// 调光后 AD 会慢慢爬升，必须等稳定后再判断。
constexpr int kDimmingSettleMinMs = 4000;
constexpr int kDimmingSettleMaxMs = 20000;
constexpr int kDimmingStableDelta = 3;       // 相邻两次 AD 变化不超过 3
constexpr int kDimmingStableSamples = 3;     // 连续 3 次稳定才算调完
constexpr int kLedCurrentSettleMs = 4000;    // 每次电流调整后等待 AD 稳定

enum class ToolbarIconType {
    Home,
    Results,
    Consumables,
    Search,
    Quality,
    Curve,
    Play,
    Pause,
    Reset,
    Clean,
    AddSample,
    Tray,
    Settings,
    Log
};

QPixmap renderToolbarIcon(ToolbarIconType type, const QColor &color)
{
    QPixmap pixmap(40, 40);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(40.0 / 32.0, 40.0 / 32.0);

    QPen pen(color, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (type) {
    case ToolbarIconType::Home: {
        QPainterPath house;
        house.moveTo(16.0, 4.5);
        house.lineTo(27.0, 14.0);
        house.lineTo(24.5, 14.0);
        house.lineTo(24.5, 27.0);
        house.lineTo(18.5, 27.0);
        house.lineTo(18.5, 19.0);
        house.lineTo(13.5, 19.0);
        house.lineTo(13.5, 27.0);
        house.lineTo(7.5, 27.0);
        house.lineTo(7.5, 14.0);
        house.lineTo(5.0, 14.0);
        house.closeSubpath();
        p.drawPath(house);
        break;
    }
    case ToolbarIconType::Results: {
        p.drawRoundedRect(QRectF(8.0, 5.0, 16.0, 22.0), 2.5, 2.5);
        p.drawLine(QPointF(11.5, 11.0), QPointF(20.5, 11.0));
        p.drawLine(QPointF(11.5, 15.5), QPointF(20.5, 15.5));
        p.drawLine(QPointF(11.5, 20.0), QPointF(18.0, 20.0));
        break;
    }
    case ToolbarIconType::Consumables: {
        p.drawRoundedRect(QRectF(13.0, 5.0, 6.0, 6.0), 1.6, 1.6);
        p.drawRoundedRect(QRectF(9.0, 10.0, 14.0, 17.0), 3.0, 3.0);
        p.drawLine(QPointF(10.5, 18.0), QPointF(21.5, 18.0));
        break;
    }
    case ToolbarIconType::Search: {
        p.drawEllipse(QPointF(14.0, 13.5), 6.2, 6.2);
        p.drawLine(QPointF(18.6, 18.1), QPointF(25.0, 24.5));
        break;
    }
    case ToolbarIconType::Quality: {
        QPainterPath shield;
        shield.moveTo(16.0, 4.0);
        shield.lineTo(25.0, 8.0);
        shield.lineTo(24.0, 17.5);
        shield.cubicTo(23.2, 22.5, 19.5, 26.0, 16.0, 28.0);
        shield.cubicTo(12.5, 26.0, 8.8, 22.5, 8.0, 17.5);
        shield.lineTo(7.0, 8.0);
        shield.closeSubpath();
        p.drawPath(shield);
        p.drawLine(QPointF(11.3, 16.0), QPointF(14.8, 19.5));
        p.drawLine(QPointF(14.8, 19.5), QPointF(21.5, 12.0));
        break;
    }
    case ToolbarIconType::Curve: {
        // 简单折线图：表示曲线/聚合测试
        QPainterPath curve;
        curve.moveTo(8.0, 20.0);
        curve.lineTo(13.0, 14.0);
        curve.lineTo(18.0, 17.0);
        curve.lineTo(24.0, 8.0);
        p.drawPath(curve);
        p.drawLine(QPointF(7.0, 24.0), QPointF(25.0, 24.0));
        break;
    }
    case ToolbarIconType::Play: {
        QPainterPath play;
        play.moveTo(11.0, 6.5);
        play.lineTo(25.0, 16.0);
        play.lineTo(11.0, 25.5);
        play.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPath(play);
        break;
    }
    case ToolbarIconType::Pause: {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(10.0, 7.0, 4.2, 18.0), 2.1, 2.1);
        p.drawRoundedRect(QRectF(17.8, 7.0, 4.2, 18.0), 2.1, 2.1);
        break;
    }
    case ToolbarIconType::Reset: {
        p.drawArc(QRectF(7.0, 7.0, 18.0, 18.0), 35 * 16, 285 * 16);
        QPainterPath arrow;
        arrow.moveTo(24.5, 5.3);
        arrow.lineTo(25.4, 12.6);
        arrow.lineTo(18.2, 11.8);
        arrow.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPath(arrow);
        break;
    }
    case ToolbarIconType::Clean: {
        QPainterPath drop;
        drop.moveTo(16.0, 4.0);
        drop.cubicTo(21.5, 10.5, 24.0, 16.0, 22.3, 20.5);
        drop.cubicTo(20.7, 25.0, 13.0, 27.2, 9.8, 22.7);
        drop.cubicTo(7.0, 18.7, 10.0, 11.5, 16.0, 4.0);
        p.drawPath(drop);
        p.drawArc(QRectF(12.0, 14.5, 6.0, 7.0), 110 * 16, 90 * 16);
        break;
    }
    case ToolbarIconType::AddSample: {
        p.drawRoundedRect(QRectF(9.0, 5.5, 10.0, 21.0), 5.0, 5.0);
        p.drawLine(QPointF(9.5, 11.5), QPointF(18.5, 11.5));
        p.drawLine(QPointF(23.5, 18.0), QPointF(23.5, 26.0));
        p.drawLine(QPointF(19.5, 22.0), QPointF(27.5, 22.0));
        break;
    }
    case ToolbarIconType::Tray: {
        p.drawRoundedRect(QRectF(5.5, 7.0, 21.0, 18.0), 3.0, 3.0);
        for (int row = 0; row < 2; ++row) {
            for (int column = 0; column < 3; ++column) {
                p.drawRoundedRect(
                            QRectF(9.0 + column * 5.0,
                                   10.0 + row * 6.0, 3.2, 4.4),
                            1.0, 1.0);
            }
        }
        break;
    }
    case ToolbarIconType::Log: {
        // 时钟 + 日志横线：表示运行日志/历史记录
        p.drawEllipse(QPointF(16.0, 16.0), 9.0, 9.0);
        p.drawLine(QPointF(16.0, 16.0), QPointF(16.0, 10.0));
        p.drawLine(QPointF(16.0, 16.0), QPointF(20.6, 18.2));
        p.drawLine(QPointF(10.0, 5.5), QPointF(14.0, 5.5));
        p.drawLine(QPointF(18.0, 5.5), QPointF(22.0, 5.5));
        break;
    }
    case ToolbarIconType::Settings: {
        const int toothCount = 8;
        QPainterPath gear;
        for (int i = 0; i < toothCount * 2; ++i) {
            const qreal angle = qDegreesToRadians(
                        -90.0 + i * 180.0 / toothCount);
            const qreal radius = (i % 2 == 0) ? 11.4 : 8.5;
            const QPointF point(16.0 + std::cos(angle) * radius,
                                16.0 + std::sin(angle) * radius);
            if (i == 0) {
                gear.moveTo(point);
            } else {
                gear.lineTo(point);
            }
        }
        gear.closeSubpath();
        p.drawPath(gear);
        p.drawEllipse(QPointF(16.0, 16.0), 2.7, 2.7);
        break;
    }
    }

    return pixmap;
}

QIcon makeToolbarIcon(ToolbarIconType type, const QColor &color,
                      const QColor &disabledColor = QColor())
{
    QIcon icon;
    icon.addPixmap(renderToolbarIcon(type, color), QIcon::Normal);
    if (disabledColor.isValid()) {
        icon.addPixmap(renderToolbarIcon(type, disabledColor),
                       QIcon::Disabled);
    }
    return icon;
}

/** 左侧竖向导航项：图标在上、文字在下，选中态由 QSS 左侧高亮条标识。 */
QToolButton *createNavButton(const QString &text, const QIcon &icon,
                             QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("navButton"));
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedHeight(68);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIconSize(QSize(26, 26));
    button->setText(text);
    button->setIcon(icon);
    return button;
}

/** 底部操作条按钮：图标在左、文字在右的胶囊形按钮。 */
QToolButton *createBottomActionButton(const QString &text, const QIcon &icon,
                                      QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("bottomActionButton"));
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedHeight(60);
    button->setMinimumWidth(104);
    button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setIconSize(QSize(22, 22));
    button->setText(text);
    button->setIcon(icon);
    return button;
}

QScrollArea *makePageScroll(QWidget *page, QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(page);
    return scroll;
}

QString onOffText(bool on)
{
    return on ? QObject::tr("开") : QObject::tr("关");
}

QString mainBoardCmdStatusText(quint8 status)
{
    switch (status) {
    case 0: return QObject::tr("等待");
    case 1: return QObject::tr("运行中");
    case 2: return QObject::tr("执行完成");
    case 3: return QObject::tr("命令错误");
    default: return QObject::tr("未知(%1)").arg(status);
    }
}

QString mainBoardMfcStatusText(quint8 status)
{
    switch (status) {
    case 0: return QObject::tr("正常");
    case 1: return QObject::tr("更新耗材总量");
    case 2: return QObject::tr("读卡成功");
    case 3: return QObject::tr("读卡失败");
    case 4: return QObject::tr("刷卡异常");
    case 5: return QObject::tr("充值成功");
    case 6: return QObject::tr("充值失败");
    case 7: return QObject::tr("全部写入 Flash");
    default: return QObject::tr("未知(%1)").arg(status);
    }
}

QString mainBoardRegTypeText(quint8 type)
{
    switch (type) {
    case 0: return QStringLiteral("AA");
    case 1: return QStringLiteral("EPI");
    case 2: return QStringLiteral("ADP");
    case 3: return QStringLiteral("COL");
    case 4: return QStringLiteral("RIS");
    case 5: return QObject::tr("内部清洗剂 S1");
    case 6: return QObject::tr("试管");
    case 7: return QObject::tr("外部清洗液 S2");
    default: return QObject::tr("未知(%1)").arg(type);
    }
}

QString mainBoardConsTypeText(quint8 type)
{
    switch (type) {
    case 0: return QStringLiteral("AA");
    case 1: return QStringLiteral("EPI");
    case 2: return QStringLiteral("ADP");
    case 3: return QStringLiteral("COL");
    case 4: return QStringLiteral("RIS");
    case 5: return QObject::tr("内部清洗剂");
    case 6: return QObject::tr("试管");
    case 7: return QStringLiteral("AA1");
    case 8: return QStringLiteral("EPI1");
    case 9: return QStringLiteral("ADP1");
    case 10: return QStringLiteral("COL1");
    case 11: return QStringLiteral("RIS1");
    case 12: return QObject::tr("无");
    default: return QObject::tr("未知(%1)").arg(type);
    }
}

QString mainBoardConsStatusText(quint8 status)
{
    switch (status) {
    case 0: return QObject::tr("默认");
    case 1: return QObject::tr("更新耗材余量");
    default: return QObject::tr("未知(%1)").arg(status);
    }
}

int activeCupCount(MachineModel model)
{
    switch (model) {
    case MachineModel::KS600: return 2;
    case MachineModel::KS800: return 3;
    case MachineModel::KS1200: return 4;
    }
    return 4;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("血小板聚集分析仪"));
    setWindowIcon(appui::appIcon());
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    resize(1200, 800);
    setMinimumSize(960, 640);
    m_machineModel = machineModelFromSettings();
    m_instrumentCoordinates.setMachineModel(m_machineModel);
    for (int i = 0; i < MainBoardConsumable::kPositionCount; ++i) {
        m_lastWrittenConsumableRatio[i] = -1;
        m_lastSeenConsumableRatio[i] = -1;
    }

    // 先设置样式，再创建子控件：Qt 5.10 下反过来可能不会给首个可见页面 polish
    setStyleSheet(QStringLiteral(
        "QMainWindow { background: #F4F8FC; }"
        "QStackedWidget { background: #F4F8FC; }"
        "QWidget#loginPage { background: #F4F8FC; }"
        "QFrame#loginCard {"
        "  background: #FFFFFF;"
        "  border: 1px solid #DCE7F1;"
        "  border-radius: 12px;"
        "}"
        "QLabel#loginTitle {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 22px;"
        "  font-weight: 700;"
        "}"
        "QLabel#loginSubtitle {"
        "  color: #7A8FA3;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "}"
        "QLabel#loginCaption {"
        "  color: #5A7086;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QLineEdit#loginInput {"
        "  background: #FFFFFF;"
        "  color: #1F2D3D;"
        "  border: 1px solid #C5D4E4;"
        "  border-radius: 6px;"
        "  padding: 4px 10px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 14px;"
        "  selection-background-color: #2C7BBF;"
        "  selection-color: #FFFFFF;"
        "}"
        "QLineEdit#loginInput:focus {"
        "  border: 1px solid #2C7BBF;"
        "  background: #F7FBFF;"
        "}"
        "QComboBox#loginInput {"
        "  background: #FFFFFF;"
        "  color: #1F2D3D;"
        "  border: 1px solid #C5D4E4;"
        "  border-radius: 6px;"
        "  padding: 4px 8px 4px 10px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 14px;"
        "}"
        "QComboBox#loginInput:focus, QComboBox#loginInput:on {"
        "  border: 1px solid #2C7BBF;"
        "  background: #F7FBFF;"
        "}"
        "QComboBox#loginInput::drop-down {"
        "  subcontrol-origin: padding;"
        "  subcontrol-position: center right;"
        "  width: 24px;"
        "  border: none;"
        "  background: transparent;"
        "}"
        "QComboBox#loginInput::down-arrow {"
        "  image: url(:/img/arrow_down.png);"
        "  width: 12px;"
        "  height: 12px;"
        "}"
        "QComboBox#loginInput::drop-down:hover {"
        "  background: #E8F1FA;"
        "  border-radius: 6px;"
        "}"
        "QComboBox#loginInput QLineEdit {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 14px;"
        "  selection-background-color: #2C7BBF;"
        "  selection-color: #FFFFFF;"
        "}"
        "QComboBox#loginInput QAbstractItemView {"
        "  background: #FFFFFF;"
        "  color: #1F2D3D;"
        "  border: 1px solid #C5D4E4;"
        "  outline: none;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 14px;"
        "  selection-background-color: #2C7BBF;"
        "  selection-color: #FFFFFF;"
        "}"
        "QPushButton#loginButton {"
        "  background: #1F4E79;"
        "  color: #FFFFFF;"
        "  border: none;"
        "  border-radius: 6px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 14px;"
        "  font-weight: 600;"
        "}"
        "QPushButton#loginButton:hover { background: #2C6398; }"
        "QPushButton#loginButton:pressed { background: #163B5C; }"
        "QPushButton#cancelButton {"
        "  background: #FFFFFF;"
        "  color: #1F4E79;"
        "  border: 1px solid #8FB4D6;"
        "  border-radius: 6px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 14px;"
        "}"
        "QPushButton#cancelButton:hover { background: #E8F1FA; }"
        "QPushButton#cancelButton:pressed { background: #D3E6F5; }"
        "QLabel#loginHint {"
        "  color: #A0AAB5;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 11px;"
        "}"
        "QWidget#mainPage { background: #F4F8FC; }"
        "QWidget#mainNavBar {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "      stop:0 #FBFDFF, stop:1 #EEF6FC);"
        "  border-right: 1px solid #D8E5EF;"
        "}"
        "QScrollArea#mainNavScroll { background: transparent; border: none; }"
        "QScrollArea#mainNavScroll > QWidget#qt_scrollarea_viewport {"
        "  background: transparent;"
        "}"
        "QWidget#mainNavContent { background: transparent; }"
        "QScrollArea#mainNavScroll QScrollBar:vertical {"
        "  background: transparent; width: 6px; margin: 0px;"
        "}"
        "QScrollArea#mainNavScroll QScrollBar::handle:vertical {"
        "  background: rgba(31,78,121,80); border-radius: 3px;"
        "  min-height: 30px;"
        "}"
        "QScrollArea#mainNavScroll QScrollBar::add-line:vertical,"
        "QScrollArea#mainNavScroll QScrollBar::sub-line:vertical { height: 0px; }"
        "QScrollArea#mainNavScroll QScrollBar::add-page:vertical,"
        "QScrollArea#mainNavScroll QScrollBar::sub-page:vertical {"
        "  background: transparent;"
        "}"
        "QWidget#bottomBar {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "      stop:0 #FFFFFF, stop:1 #F0F9FE);"
        "  border-top: 1px solid #D6E6F2;"
        "}"
        "QWidget#runControlRow {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "      stop:0 #FFFFFF, stop:1 #EAF5FC);"
        "  border: 1px solid #C9DDEB;"
        "  border-radius: 12px;"
        "}"
        "QLabel#footerHint {"
        "  color: #A0AAB5;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 11px;"
        "}"
        "QToolButton#navButton {"
        "  background: transparent;"
        "  color: #33556F;"
        "  border: none;"
        "  border-left: 4px solid transparent;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "  padding: 0px;"
        "}"
        "QToolButton#navButton:hover {"
        "  background: #EBF5FC;"
        "  color: #12466B;"
        "}"
        // 选中态：当前页面的导航项常亮，左侧高亮条 + 浅蓝底。
        "QToolButton#navButton:checked {"
        "  background: #E1EFFA;"
        "  color: #12466B;"
        "  border-left: 4px solid #2C7BBF;"
        "  font-weight: 700;"
        "}"
        "QToolButton#navButton:disabled { color: #A7B7C5; }"
        "QToolButton#bottomActionButton {"
        "  background: #FFFFFF;"
        "  color: #174A6D;"
        "  border: 1px solid #B9D5E8;"
        "  border-radius: 10px;"
        "  padding: 0px 14px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 14px;"
        "  font-weight: 600;"
        "}"
        "QToolButton#bottomActionButton:hover {"
        "  background: #EDF6FD;"
        "  border-color: #7FB6D8;"
        "}"
        "QToolButton#bottomActionButton:pressed { background: #DCEBF7; }"
        "QToolButton#bottomActionButton:disabled {"
        "  background: #F4F8FC;"
        "  color: #9FB3C4;"
        "  border-color: #DCE7F1;"
        "}"
        // E1 柔光胶囊：连体「开始+暂停」运行控制模块。
        "QToolButton#runStart, QToolButton#runPause {"
        "  border: none; border-radius: 22px; background: transparent;"
        "  color: #7E9BB2; font-family: 'Microsoft YaHei';"
        "  font-size: 14px; font-weight: 700; letter-spacing: 1px; padding: 0 4px;"
        "}"
        "QToolButton#runStart:enabled {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "      stop:0 #6EB6E8, stop:0.55 #3A8ECB, stop:1 #2E7BB8);"
        "  color: #FFFFFF;"
        "}"
        "QToolButton#runStart:hover:enabled {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "      stop:0 #7EC0EC, stop:0.55 #4E9BD6, stop:1 #3986C4);"
        "}"
        "QToolButton#runStart:disabled {"
        "  background: #E4EFF9; color: #8FA8BC;"
        "}"
        "QToolButton#runPause:disabled { color: #A9BFCF; }"
        "QToolButton#runPause:enabled { color: #7E9BB2; }"
        "QToolButton#runPause[state=\"hot\"] {"
        "  background: #FFF5E0; color: #B96E00;"
        "  border: 1.5px solid #F4C26B;"
        "}"
        "QToolButton#runPause[state=\"go\"] {"
        "  background: #3A8ECB; color: #FFFFFF;"
        "}"
        "QToolButton#startButton {"
        "  background: qradialgradient(cx:0.34, cy:0.24, radius:1.0,"
        "      fx:0.26, fy:0.16,"
        "      stop:0 rgba(255,255,255,252),"
        "      stop:0.16 rgba(158,224,255,248),"
        "      stop:0.48 rgba(56,143,200,250),"
        "      stop:1 rgba(17,62,98,252));"
        "  color: #FFFFFF;"
        "  border: 3px solid rgba(255,255,255,240);"
        "  border-radius: 40px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 16px;"
        "  font-weight: 700;"
        "}"
        "QToolButton#startButton:hover {"
        "  background: qradialgradient(cx:0.34, cy:0.24, radius:1.0,"
        "      fx:0.26, fy:0.16,"
        "      stop:0 rgba(255,255,255,255),"
        "      stop:0.18 rgba(188,236,255,250),"
        "      stop:0.50 rgba(76,163,219,252),"
        "      stop:1 rgba(22,77,119,252));"
        "}"
        "QToolButton#startButton:pressed {"
        "  background: qradialgradient(cx:0.34, cy:0.24, radius:1.0,"
        "      fx:0.26, fy:0.16,"
        "      stop:0 rgba(228,248,255,250),"
        "      stop:0.20 rgba(125,198,239,245),"
        "      stop:0.52 rgba(38,118,171,250),"
        "      stop:1 rgba(12,49,80,252));"
        "}"
        "QToolButton#startButton:disabled {"
        "  background: qradialgradient(cx:0.34, cy:0.24, radius:1.0,"
        "      fx:0.26, fy:0.16,"
        "      stop:0 rgba(250,253,255,245),"
        "      stop:0.22 rgba(212,230,241,240),"
        "      stop:0.65 rgba(167,190,207,240),"
        "      stop:1 rgba(124,151,171,245));"
        "  color: #5F7A8F;"
        "  border: 3px solid rgba(255,255,255,220);"
        "}"
        "QPushButton#switchUserLink {"
        "  background: transparent;"
        "  color: #1F4E79;"
        "  border: none;"
        "  padding: 0px 4px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QPushButton#switchUserLink:hover { color: #2C7BBF; }"
        "QPushButton#switchUserLink:pressed { color: #163B5C; }"
        "QLabel#actionHint {"
        "  color: #B7791F;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QPushButton#switchUserButton {"
        "  background: #FFFFFF;"
        "  color: #1F4E79;"
        "  border: 1px solid #8FB4D6;"
        "  border-radius: 6px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 14px;"
        "}"
        "QPushButton#switchUserButton:hover { background: #E8F1FA; }"
        "QPushButton#switchUserButton:pressed { background: #D3E6F5; }"
        "QPushButton#settingsButton {"
        "  background: #1F4E79;"
        "  color: #FFFFFF;"
        "  border: none;"
        "  border-radius: 6px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 14px;"
        "  font-weight: 600;"
        "}"
        "QPushButton#settingsButton:hover { background: #2C6398; }"
        "QPushButton#settingsButton:pressed { background: #163B5C; }"
        "QWidget#contentArea { background: #F4F8FC; }"
        "QScrollArea#mainContentScroll {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QScrollArea#mainContentScroll > QWidget > QWidget {"
        "  background: transparent;"
        "}"
        "QScrollBar:vertical {"
        "  background: #EEF5FA;"
        "  width: 10px;"
        "  margin: 2px;"
        "  border-radius: 5px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #B8D2E4;"
        "  min-height: 28px;"
        "  border-radius: 5px;"
        "}"
        "QScrollBar::handle:vertical:hover { background: #8FB8D3; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0px;"
        "}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "  background: transparent;"
        "}"
        "QLabel#mainPlaceholder {"
        "  color: #C3CFDB;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 22px;"
        "}"
        "QLabel#connectionStatus {"
        "  color: #8A99A8;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "}"
        "QLabel#connectionStatus[state=\"searching\"] { color: #B7791F; }"
        "QLabel#connectionStatus[state=\"online\"] {"
        "  color: #1E7B45;"
        "  font-weight: 600;"
        "}"
        "QLabel#connectionStatus[state=\"offline\"] { color: #C0392B; }"
        "QFrame#statusSeparator { color: #DCE7F1; }"
        "QLabel#mainboardStatus {"
        "  color: #8A99A8;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "}"
        "QLabel#mainboardStatus[state=\"ok\"] {"
        "  color: #1E7B45;"
        "  font-weight: 600;"
        "}"
        "QLabel#mainboardStatus[state=\"warning\"] { color: #B7791F; }"
        "QLabel#mainboardStatus[state=\"error\"] {"
        "  color: #C0392B;"
        "  font-weight: 600;"
        "}"
        "QLabel#mainboardStatus[state=\"offline\"] { color: #8A99A8; }"));

    m_connectDeadlineTimer = new QTimer(this);
    m_connectDeadlineTimer->setSingleShot(true);
    m_connectDeadlineTimer->setInterval(kStartupConnectTimeoutMs);
    connect(m_connectDeadlineTimer, &QTimer::timeout,
            this, &MainWindow::handleStartupTimeout);

    // 刷卡充值写卡窗口 watchdog：主板未回报结果时恢复轮询并提示。
    m_cardRechargeTimer = new QTimer(this);
    m_cardRechargeTimer->setSingleShot(true);
    m_cardRechargeTimer->setInterval(6000);
    connect(m_cardRechargeTimer, &QTimer::timeout, this, [this]() {
        if (m_cardRechargeState != CardRechargeState::Writing) {
            return;
        }
        finishCardRecharge(
                    false,
                    tr("主板在 6 秒内没有回报充值结果，请检查耗材余量后重新刷卡。"));
    });

    connect(&TestSession::instance(), &TestSession::testingActiveChanged,
            this, [this](bool) {
        updateConsumablesOperationState();
        updateStartButtonAppearance();
    });

    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    m_titleBar = new CustomTitleBar(
                tr("血小板聚集分析仪"),
                CustomTitleBar::MinimizeButton | CustomTitleBar::MaximizeButton
                | CustomTitleBar::CloseButton,
                central);
    centralLayout->addWidget(m_titleBar);

    for (int i = 0; i < 3; ++i) {
        m_moduleProtocols[i].setSlaveAddress(quint8(i + 1));
        m_moduleProtocols[i].setTargetTemperature(
                    platelet::DetectionModule::defaultTargetTemperature());
        // 与 ModuleAdPoller 的命令号错开，便于区分回读应答。
        m_moduleProtocols[i].resetCommandNumber(0x80);
    }

    m_stack = new QStackedWidget(central);
    // 确保内置测试账号 test/空密码 存在
    SettingsDialog::ensureTestUser();
    m_loginPage = createLoginPage();
    m_mainPage = createMainPage();
    m_stack->addWidget(m_loginPage);
    m_stack->addWidget(m_mainPage);
    centralLayout->addWidget(m_stack, 1);

    setCentralWidget(central);
    m_stack->setCurrentWidget(m_loginPage);
    createStartupMask();

    // 测试数据 SQLite 存储；打开失败不阻塞主流程，详细错误写入日志。
    m_testDataStore = new TestDataStore(QString(), this);
    QString databaseError;
    if (!m_testDataStore->open(&databaseError)) {
        qWarning() << "TestDataStore open failed:" << databaseError;
    }
    // 单动作运动控制接口；通讯线程创建后绑定 CommunicationWorker。
    m_motionController = new platelet::MotionController(this);

    // 坐标维护「触发移动」：先复位三个 Z 轴，收到全部完成回执后才允许 XY。
    m_verificationController = new CoordinateVerificationController(this);
    m_verificationController->setProtocol(&m_motionController->protocol());
    connect(m_motionController, &platelet::MotionController::reportReceived,
            m_verificationController,
            &CoordinateVerificationController::handleReport);
    connect(m_verificationController,
            &CoordinateVerificationController::sendRequested,
            m_motionController, &platelet::MotionController::sendCommand);
    connect(m_verificationController,
            &CoordinateVerificationController::statusChanged,
            this, [this](const QString &status) {
        showActionHint(status);
    });
    connect(m_verificationController,
            &CoordinateVerificationController::finished,
            this, [this](bool ok, const QString &message) {
        // 定位校验属于手动动作，期间暂停了周期采集，结束后恢复。
        if (m_commWorker) {
            QMetaObject::invokeMethod(m_commWorker, "resumePeriodicPolling",
                                      Qt::QueuedConnection);
        }
        updateConsumablesOperationState();
        showActionHint(message);
        if (!ok) {
            MedicalMessageBox::warning(this, tr("定位校验"), message);
        }
    });

    // 开机灌注：移植老软件 QUIUtils::perfusionAction 主流程。
    m_perfusionController = new PerfusionController(&m_instrumentCoordinates,
                                                    this);
    m_perfusionController->setProtocol(&m_motionController->protocol());
    connect(m_perfusionController, &PerfusionController::sendRequested,
            m_motionController, &platelet::MotionController::sendCommand);
    connect(m_motionController, &platelet::MotionController::reportReceived,
            m_perfusionController, &PerfusionController::handleReport);
    connect(m_perfusionController, &PerfusionController::progressChanged,
            this, [this](int completed, int total, const QString &status) {
        if (m_startupState != StartupCalibrationState::Perfusion) {
            return;
        }
        const int percent = total > 0
                ? qBound(0, completed * 100 / total, 100) : 0;
        setStartupMaskStatus(tr("开机灌注中"), status, true, percent);
    });
    connect(m_perfusionController, &PerfusionController::finished,
            this, [this](bool ok, const QString &message) {
        // 灌注期间暂停了主板/模组轮询，无论成功、失败还是取消都要恢复，
        // 否则后续升温、调光和耗材状态都不会再刷新。
        if (m_commWorker) {
            QMetaObject::invokeMethod(m_commWorker, "resumePeriodicPolling",
                                      Qt::QueuedConnection);
        }
        // test 模式的“两泵排空”复用同一个控制器，但不进入升温/调光。
        if (m_perfusionController->lastRunWasPumpEmpty()) {
            if (!ok) {
                m_testModePumpEmptyStarted = false;
            }
            updateConsumablesOperationState();
            showActionHint(message);
            if (!ok) {
                MedicalMessageBox::warning(this, tr("泵排空"), message);
            }
            return;
        }
        if (m_startupState != StartupCalibrationState::Perfusion) {
            return;
        }
        if (ok) {
            beginStartupHeating();
        } else {
            abortStartupCalibration(message, true);
        }
    });

    // 主页「清洗」：清洗双针，完成后按实际吸走的洗针量扣内部清洗液 S1。
    m_cleaningController = new CleaningController(&m_instrumentCoordinates,
                                                  this);
    m_cleaningController->setProtocol(&m_motionController->protocol());
    connect(m_cleaningController, &CleaningController::sendRequested,
            m_motionController, &platelet::MotionController::sendCommand);
    connect(m_motionController, &platelet::MotionController::reportReceived,
            m_cleaningController, &CleaningController::handleReport);
    connect(m_cleaningController, &CleaningController::statusChanged,
            this, [this](const QString &status) {
        showActionHint(status);
    });
    connect(m_cleaningController, &CleaningController::finished,
            this, [this](bool ok, const QString &message) {
        // 清洗是手动动作，老软件同样在动作期间暂停主板/模组采集。
        if (m_commWorker) {
            QMetaObject::invokeMethod(m_commWorker, "resumePeriodicPolling",
                                      Qt::QueuedConnection);
        }
        updateConsumablesOperationState();
        if (!ok) {
            showActionHint(message);
            MedicalMessageBox::warning(this, tr("清洗失败"), message);
            return;
        }
        const double usedUl = m_cleaningController->sampleWashVolumeUl()
                + m_cleaningController->reagentWashVolumeUl();
        QString error;
        if (!consumeConsumableUl(MainBoardConsumable::InternalCleanS1,
                                 usedUl, &error)) {
            showActionHint(tr("清洗完成，但扣清洗液失败：%1").arg(error));
        } else {
            showActionHint(tr("清洗完成，已扣内部清洗液 %1 µL")
                           .arg(usedUl, 0, 'f', 1));
        }

        // 清洗结束后再执行一次复位：三 Z 复位 → XY 负速度复位 → XY 到原点。
        if (m_resetController && m_serialConnected
                && m_startupState == StartupCalibrationState::Idle
                && !m_resetController->isBusy()) {
            if (m_commWorker) {
                QMetaObject::invokeMethod(m_commWorker, "pausePeriodicPolling",
                                          Qt::QueuedConnection);
            }
            m_resetController->start();
        }
    });

    // 主界面「复位」：三 Z 复位 → XY 负速度复位 → XY 到清洗位。
    m_resetController = new ResetController(&m_instrumentCoordinates, this);
    m_resetController->setProtocol(&m_motionController->protocol());
    connect(m_resetController, &ResetController::sendRequested,
            m_motionController, &platelet::MotionController::sendCommand);
    connect(m_motionController, &platelet::MotionController::reportReceived,
            m_resetController, &ResetController::handleReport);
    connect(m_resetController, &ResetController::statusChanged,
            this, [this](const QString &status) {
        showActionHint(status);
    });
    connect(m_resetController, &ResetController::finished,
            this, [this](bool ok, const QString &message) {
        if (m_commWorker) {
            QMetaObject::invokeMethod(m_commWorker, "resumePeriodicPolling",
                                      Qt::QueuedConnection);
        }
        updateConsumablesOperationState();
        showActionHint(message);
        if (!ok) {
            MedicalMessageBox::warning(this, tr("复位"), message);
        }
    });

    createResultTabs();
    createEmptyPlaceholderTabs();

    // 试管盘状态：机型决定 2/3/4 个盘，最多 240 个试管位。
    m_trayModel = new SampleTrayModel(this);
    m_trayModel->setMachineModel(m_machineModel);
    QString trayError;
    if (!m_trayModel->loadFromSettings(&trayError) && !trayError.isEmpty()) {
        qWarning() << "Sample tray state load failed:" << trayError;
    }
    createTubeTrayTab();

    // 坐标：启动时从默认坐标文件恢复到内存，坐标维护页与测试流程共用。
    loadInstrumentCoordinates();

    m_usernameEdit->setFocus();

    // 串口连接从程序启动就开始，登录页显示期间也在后台自动连接
    startInstrumentConnection();
    createSettingsTab();
}

MainWindow::~MainWindow()
{
    shutdownCommunicationThread();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    const bool testing = testingActive();
    const QString message = testing
            ? tr("当前有测试正在进行，关闭软件会中断测试。\n\n确定要关闭吗？")
            : tr("确定要关闭血小板聚集分析仪吗？");
    MedicalMessageBox box(this);
    box.applyMedicalStyle();
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("退出确认"));
    box.setText(message);
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    if (QAbstractButton *yesButton = box.button(QMessageBox::Yes)) {
        yesButton->setText(tr("确定"));
    }
    if (QAbstractButton *noButton = box.button(QMessageBox::No)) {
        noButton->setText(tr("取消"));
    }
    box.exec();
    if (box.standardButton(box.clickedButton()) != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    qInfo() << "用户确认退出程序";
    event->accept();
}

QWidget *MainWindow::createLoginPage()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("loginPage"));

    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 24, 24, 24);
    outer->addStretch(1);

    auto *row = new QHBoxLayout;
    row->addStretch(1);

    auto *card = new QFrame(page);
    card->setObjectName(QStringLiteral("loginCard"));
    card->setFixedWidth(440);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(40, 36, 40, 36);
    cardLayout->setSpacing(12);

    auto *title = new QLabel(tr("血小板聚集分析仪"), card);
    title->setObjectName(QStringLiteral("loginTitle"));
    title->setAlignment(Qt::AlignCenter);

    auto *subtitle = new QLabel(tr("用户登录"), card);
    subtitle->setObjectName(QStringLiteral("loginSubtitle"));
    subtitle->setAlignment(Qt::AlignCenter);

    auto *userCaption = new QLabel(tr("用户名"), card);
    userCaption->setObjectName(QStringLiteral("loginCaption"));

    m_usernameEdit = new QComboBox(card);
    m_usernameEdit->setObjectName(QStringLiteral("loginInput"));
    m_usernameEdit->setEditable(true);
    m_usernameEdit->setInsertPolicy(QComboBox::NoInsert);
    m_usernameEdit->setMinimumHeight(36);
    if (QLineEdit *userLine = m_usernameEdit->lineEdit()) {
        userLine->setPlaceholderText(tr("请输入或选择用户名"));
        userLine->setFrame(false);
    }
    refreshLoginUserNames();

    auto *passwordCaption = new QLabel(tr("密码"), card);
    passwordCaption->setObjectName(QStringLiteral("loginCaption"));

    m_passwordEdit = new QLineEdit(card);
    m_passwordEdit->setObjectName(QStringLiteral("loginInput"));
    m_passwordEdit->setPlaceholderText(tr("请输入密码"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setMinimumHeight(36);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(12);

    auto *loginButton = new QPushButton(tr("登入"), card);
    loginButton->setObjectName(QStringLiteral("loginButton"));
    loginButton->setCursor(Qt::PointingHandCursor);
    loginButton->setMinimumHeight(38);

    auto *cancelButton = new QPushButton(tr("取消"), card);
    cancelButton->setObjectName(QStringLiteral("cancelButton"));
    cancelButton->setCursor(Qt::PointingHandCursor);
    cancelButton->setMinimumHeight(38);

    buttonRow->addWidget(loginButton, 1);
    buttonRow->addWidget(cancelButton, 1);

    auto *hint = new QLabel(
                tr(""), card);
    hint->setObjectName(QStringLiteral("loginHint"));
    hint->setAlignment(Qt::AlignCenter);

    cardLayout->addWidget(title);
    cardLayout->addWidget(subtitle);
    cardLayout->addSpacing(10);
    cardLayout->addWidget(userCaption);
    cardLayout->addWidget(m_usernameEdit);
    cardLayout->addWidget(passwordCaption);
    cardLayout->addWidget(m_passwordEdit);
    cardLayout->addSpacing(8);
    cardLayout->addLayout(buttonRow);
    cardLayout->addWidget(hint);

    row->addWidget(card);
    row->addStretch(1);

    outer->addLayout(row);
    outer->addStretch(1);

    connect(loginButton, &QPushButton::clicked, this, &MainWindow::handleLogin);
    connect(cancelButton, &QPushButton::clicked, this, [this]() {
        if (!m_stack || !m_mainPage) {
            close();
            return;
        }
        if (m_hasLoggedIn) {
            // 从主界面切换用户后再点取消：返回主界面，不退出软件
            if (m_passwordEdit) {
                m_passwordEdit->clear();
            }
            m_stack->setCurrentWidget(m_mainPage);
        } else {
            close();
        }
    });
    if (QLineEdit *userLine = m_usernameEdit->lineEdit()) {
        connect(userLine, &QLineEdit::returnPressed, this, &MainWindow::handleLogin);
    }
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &MainWindow::handleLogin);

    return page;
}

void MainWindow::refreshLoginUserNames()
{
    if (!m_usernameEdit) {
        return;
    }
    const QString current = m_usernameEdit->currentText();
    const QSignalBlocker blocker(m_usernameEdit);
    m_usernameEdit->clear();
    m_usernameEdit->addItems(SettingsDialog::existingUserNames());
    m_usernameEdit->setCurrentText(current);
}

QWidget *MainWindow::createMainPage()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("mainPage"));

    auto *rootLayout = new QVBoxLayout(page);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // 左侧竖向导航：页面切换常驻左侧，顶部空间全部留给内容区。
    auto *body = new QWidget(page);
    body->setObjectName(QStringLiteral("mainBody"));
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    auto *navBar = new QWidget(body);
    navBar->setObjectName(QStringLiteral("mainNavBar"));
    navBar->setFixedWidth(108);
    auto *navBarLayout = new QVBoxLayout(navBar);
    navBarLayout->setContentsMargins(0, 0, 0, 0);
    navBarLayout->setSpacing(0);

    // 窗口较矮时导航可滚动，避免导航项被裁掉。
    auto *navScroll = new QScrollArea(navBar);
    navScroll->setObjectName(QStringLiteral("mainNavScroll"));
    navScroll->setWidgetResizable(true);
    navScroll->setFrameShape(QFrame::NoFrame);
    navScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    navScroll->viewport()->setAutoFillBackground(false);

    auto *navContent = new QWidget(navScroll);
    navContent->setObjectName(QStringLiteral("mainNavContent"));
    navContent->setAutoFillBackground(false);
    auto *navLayout = new QVBoxLayout(navContent);
    navLayout->setContentsMargins(0, 10, 0, 10);
    navLayout->setSpacing(6);

    const QColor topIconColor(QStringLiteral("#1F4E79"));

    struct NavSpec {
        QString text;
        ToolbarIconType icon;
    };
    const NavSpec navSpecs[] = {
        { tr("主页"), ToolbarIconType::Home },
        { tr("结果信息"), ToolbarIconType::Results },
        { tr("查询"), ToolbarIconType::Search },
        { tr("质控"), ToolbarIconType::Quality },
        { tr("曲线"), ToolbarIconType::Curve },
        { tr("耗材管理"), ToolbarIconType::Consumables },
        { tr("日志"), ToolbarIconType::Log },
        { tr("设置"), ToolbarIconType::Settings }
    };
    const int navSpecCount = int(sizeof(navSpecs) / sizeof(navSpecs[0]));
    for (int specIndex = 0; specIndex < navSpecCount; ++specIndex) {
        const NavSpec &spec = navSpecs[specIndex];
        auto *button = createNavButton(
                    spec.text, makeToolbarIcon(spec.icon, topIconColor),
                    navContent);
        button->setCheckable(true);   // 选中态识别：当前页面导航项常亮
        if (spec.icon == ToolbarIconType::Home) {
            connect(button, &QToolButton::clicked,
                    this, &MainWindow::showHomeTab);
            m_homeButton = button;
        } else if (spec.icon == ToolbarIconType::Results) {
            connect(button, &QToolButton::clicked,
                    this, &MainWindow::openResults);
            m_resultsButton = button;
        } else if (spec.icon == ToolbarIconType::Search) {
            connect(button, &QToolButton::clicked,
                    this, &MainWindow::openQuery);
            m_queryButton = button;
        } else if (spec.icon == ToolbarIconType::Quality) {
            connect(button, &QToolButton::clicked,
                    this, &MainWindow::openQuality);
            m_qualityButton = button;
        } else if (spec.icon == ToolbarIconType::Curve) {
            connect(button, &QToolButton::clicked,
                    this, &MainWindow::openCurve);
            m_curveButton = button;
        } else if (spec.icon == ToolbarIconType::Consumables) {
            connect(button, &QToolButton::clicked,
                    this, &MainWindow::openConsumables);
            m_consumablesButton = button;
        } else if (spec.icon == ToolbarIconType::Log) {
            connect(button, &QToolButton::clicked,
                    this, &MainWindow::openLogs);
            m_logButton = button;
        } else if (spec.icon == ToolbarIconType::Settings) {
            connect(button, &QToolButton::clicked,
                    this, &MainWindow::openSettings);
            m_settingsButton = button;
        }
        navLayout->addWidget(button);
        // 导航 / 管理 / 系统三组之间留白。
        if (spec.icon == ToolbarIconType::Quality
                || spec.icon == ToolbarIconType::Consumables) {
            navLayout->addSpacing(10);
        }
    }
    navLayout->addStretch(1);

    // 导航栏底部：加样 / 试管盘 两个高频操作入口（checkable，和上面导航一致）。
    auto *addSampleNavButton = createNavButton(
                tr("加样"),
                makeToolbarIcon(ToolbarIconType::AddSample, topIconColor),
                navContent);
    addSampleNavButton->setCheckable(true);
    connect(addSampleNavButton, &QToolButton::clicked,
            this, &MainWindow::openAddSample);
    m_addSampleButton = addSampleNavButton;
    navLayout->addWidget(addSampleNavButton);

    auto *trayNavButton = createNavButton(
                tr("试管盘"),
                makeToolbarIcon(ToolbarIconType::Tray, topIconColor),
                navContent);
    trayNavButton->setCheckable(true);
    connect(trayNavButton, &QToolButton::clicked,
            this, &MainWindow::openTubeTrays);
    m_trayButton = trayNavButton;
    navLayout->addWidget(trayNavButton);

    navScroll->setWidget(navContent);
    navBarLayout->addWidget(navScroll);
    bodyLayout->addWidget(navBar);

    auto *content = new QWidget(page);
    content->setObjectName(QStringLiteral("contentArea"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(24, 24, 24, 24);
    contentLayout->setSpacing(12);

    // 模组上方的动作控制行：清洗 / 复位 / 开始 / 暂停。
    auto *runControlRow = new QWidget(content);
    runControlRow->setObjectName(QStringLiteral("runControlRow"));
    auto *runControlLayout = new QHBoxLayout(runControlRow);
    runControlLayout->setContentsMargins(0, 0, 0, 0);
    runControlLayout->setSpacing(10);

    auto *cleanButton = createBottomActionButton(
                tr("清洗"), makeToolbarIcon(ToolbarIconType::Clean,
                                             topIconColor),
                runControlRow);
    connect(cleanButton, &QToolButton::clicked,
            this, &MainWindow::handleCleaning);
    runControlLayout->addWidget(cleanButton);

    auto *resetButton = createBottomActionButton(
                tr("复位"), makeToolbarIcon(ToolbarIconType::Reset,
                                             topIconColor),
                runControlRow);
    connect(resetButton, &QToolButton::clicked,
            this, &MainWindow::handleResetTest);
    runControlLayout->addWidget(resetButton);

    m_pauseButton = createBottomActionButton(
                tr("暂停"), makeToolbarIcon(ToolbarIconType::Pause,
                                             topIconColor),
                runControlRow);
    m_pauseButton->setObjectName(QStringLiteral("runPause"));
    connect(m_pauseButton, &QToolButton::clicked,
            this, &MainWindow::handlePauseResume);
    runControlLayout->addWidget(m_pauseButton);

    runControlLayout->addSpacing(20);

    m_startButton = createBottomActionButton(
                tr("开始"), makeToolbarIcon(ToolbarIconType::Play,
                                             topIconColor),
                runControlRow);
    m_startButton->setObjectName(QStringLiteral("runStart"));
    connect(m_startButton, &QToolButton::clicked,
            this, &MainWindow::handleStartTest);
    runControlLayout->addWidget(m_startButton);

    runControlLayout->addStretch(1);
    contentLayout->addWidget(runControlRow);

    auto *moduleRow = new QWidget(content);
    auto *moduleRowLayout = new QHBoxLayout(moduleRow);
    moduleRowLayout->setContentsMargins(0, 0, 0, 0);
    moduleRowLayout->setSpacing(12);

    // 试剂余量面板移到左侧竖排放置
    m_reagentPanel = new ReagentBottlePanel(moduleRow);
    m_reagentPanel->setFixedWidth(220);
    moduleRowLayout->addWidget(m_reagentPanel, 0);

    m_moduleOverview = new ModuleOverviewWidget(moduleRow);
    m_moduleOverview->setMachineModel(m_machineModel);
    moduleRowLayout->addWidget(m_moduleOverview, 1);

    contentLayout->addWidget(moduleRow, 1);

    applyChannelSettings();

    // 小窗口时内容可滚动，保证试剂瓶区域不会被挤掉
    auto *contentScroll = new QScrollArea(page);
    contentScroll->setObjectName(QStringLiteral("mainContentScroll"));
    contentScroll->setWidgetResizable(true);
    contentScroll->setFrameShape(QFrame::NoFrame);
    contentScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    contentScroll->setWidget(content);

    m_mainTabs = new QTabWidget(page);
    m_mainTabs->setObjectName(QStringLiteral("mainTabs"));
    m_mainTabs->setDocumentMode(true);
    m_mainTabs->setMovable(false);
    m_mainTabs->tabBar()->setObjectName(QStringLiteral("mainTabBar"));
    // 左侧导航负责切换页面，标签头不显示。
    m_mainTabs->tabBar()->hide();
    m_mainTabs->setStyleSheet(QStringLiteral(
        "QTabWidget#mainTabs::pane {"
        "  border: none;"
        "  background: #F4F8FC;"
        "}"
        "QTabBar#mainTabBar::tab {"
        "  background: #E3EEF8;"
        "  color: #4E6B82;"
        "  border: 1px solid #D4E3EF;"
        "  border-bottom: none;"
        "  border-top-left-radius: 8px;"
        "  border-top-right-radius: 8px;"
        "  padding: 8px 26px;"
        "  margin-right: 4px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "}"
        "QTabBar#mainTabBar::tab:selected {"
        "  background: #FFFFFF;"
        "  color: #1F4E79;"
        "  font-weight: 600;"
        "}"
        "QTabBar#mainTabBar::tab:hover { background: #EDF5FC; }"));

    m_homeTabIndex = m_mainTabs->addTab(contentScroll, tr("主页"));
    bodyLayout->addWidget(m_mainTabs, 1);
    rootLayout->addWidget(body, 1);

    connect(m_mainTabs, &QTabWidget::currentChanged,
            this, [this](int index) {
        if (index == m_consumablesTabIndex) {
            refreshConsumablesTab();
        }
        // 切到结果/查询时把详情面板保持在最上层（仍在屏上则继续显示）。
        if (m_detailPanel
                && (index == m_resultsTabIndex || index == m_queryTabIndex)) {
            m_detailPanel->raise();
        }
    });

    createConsumablesTab();

    auto *bottomBar = new QWidget(page);
    bottomBar->setObjectName(QStringLiteral("bottomBar"));
    bottomBar->setFixedHeight(34);
    auto *bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(24, 0, 24, 0);
    bottomLayout->setSpacing(8);

    m_connectionStatus = new QLabel(tr("● 未连接"), bottomBar);
    m_connectionStatus->setObjectName(QStringLiteral("connectionStatus"));
    bottomLayout->addWidget(m_connectionStatus);

    auto *statusSeparator = new QFrame(bottomBar);
    statusSeparator->setObjectName(QStringLiteral("statusSeparator"));
    statusSeparator->setFrameShape(QFrame::VLine);
    statusSeparator->setFrameShadow(QFrame::Plain);
    statusSeparator->setFixedHeight(16);
    bottomLayout->addWidget(statusSeparator);

    m_mainboardStatus = new QLabel(tr("● 主板：未连接"), bottomBar);
    m_mainboardStatus->setObjectName(QStringLiteral("mainboardStatus"));
    m_mainboardStatus->setProperty("state", "offline");
    m_mainboardStatus->setToolTip(tr("串口未连接，无法读取主板状态。"));
    bottomLayout->addWidget(m_mainboardStatus, 1);

    // S2 外清洗液 / 废液 / 4 个托盘 / 测高检测：小图标，异常闪烁
    m_boardIndicators = new MainBoardStatusIconBar(bottomBar);
    m_boardIndicators->clearStatus(tr("等待主板状态"));
    m_boardIndicators->setHeightIconVisible(!sampleQualityIsPlasma());
    bottomLayout->addWidget(m_boardIndicators, 0, Qt::AlignVCenter);

    m_actionHint = new QLabel(bottomBar);
    m_actionHint->setObjectName(QStringLiteral("actionHint"));
    bottomLayout->addWidget(m_actionHint);

    auto *switchUserLink = new QPushButton(tr("切换用户"), bottomBar);
    switchUserLink->setObjectName(QStringLiteral("switchUserLink"));
    switchUserLink->setCursor(Qt::PointingHandCursor);
    switchUserLink->setFocusPolicy(Qt::NoFocus);
    switchUserLink->setFlat(true);
    switchUserLink->setFixedHeight(24);
    switchUserLink->setToolTip(tr("返回登录页，切换当前操作员"));
    connect(switchUserLink, &QPushButton::clicked,
            this, &MainWindow::returnToLogin);
    bottomLayout->addWidget(switchUserLink);

    rootLayout->addWidget(bottomBar);
    return page;
}

void MainWindow::handleLogin()
{
    const QString userName = m_usernameEdit
            ? m_usernameEdit->currentText().trimmed() : QString();
    const QString password = m_passwordEdit
            ? m_passwordEdit->text() : QString();

    if (userName.isEmpty()) {
        MedicalMessageBox::warning(this, tr("登录失败"), tr("请输入用户名"));
        if (m_usernameEdit) {
            m_usernameEdit->setFocus();
        }
        return;
    }

    QString role;
    if (!SettingsDialog::validateUserCredentials(userName, password, &role)) {
        qWarning() << "登录失败:" << userName;
        MedicalMessageBox::warning(this, tr("登录失败"),
                             tr("用户名或密码错误。\n\n"
                                "默认管理员：admin / 1\n"
                                "测试账号：test / 空密码"));
        if (m_passwordEdit) {
            m_passwordEdit->clear();
            m_passwordEdit->setFocus();
        }
        return;
    }

    qInfo() << "操作员登录:" << userName << "角色:" << role;
    m_hasLoggedIn = true;
    m_currentUserName = userName;
    m_currentUserRole = role;
    if (m_settingsDialog) {
        m_settingsDialog->setCurrentUser(m_currentUserName, m_currentUserRole);
    }
    // test 账号用于机械动作测试：不执行开机灌注/升温/调光。
    m_skipStartupCalibration =
            (userName.compare(QStringLiteral("test"),
                              Qt::CaseInsensitive) == 0);
    if (m_passwordEdit) {
        m_passwordEdit->clear();
    }
    if (m_actionHint) {
        m_actionHint->clear();
    }
    m_stack->setCurrentWidget(m_mainPage);
    // 顶栏按钮的选中态与登录后停留的标签页保持一致（默认主页）。
    syncActiveTopButtonToTab();
    // 坐标在登录前已从 JSON 读入内存；登录后若已联机，立即开始
    // “开机灌注 → 升温 → 调光”。test 账号跳过整套启动校准，
    // 但同样要进入 test 模式准备（恢复主板/模组周期轮询、建立泵零位）。
    if (m_skipStartupCalibration) {
        qInfo() << "test 用户登录：跳过开机灌注/升温/调光";
    }
    maybeStartStartupCalibration();
    // 注意：这里不重置通道/模拟状态，保证「切换用户 → 取消」能回到原主界面
}

void MainWindow::returnToLogin()
{
    if (!m_stack || !m_loginPage) {
        return;
    }
    qInfo() << "返回登录页，切换用户";
    // 只切换页面，不停止正在进行的测试；取消登录即可返回原主界面
    if (m_passwordEdit) {
        m_passwordEdit->clear();
    }
    refreshLoginUserNames();
    if (m_usernameEdit) {
        m_usernameEdit->setCurrentText(QString());
        m_usernameEdit->setFocus();
    }
    m_stack->setCurrentWidget(m_loginPage);
}

void MainWindow::handleStartTest()
{
    createEmptyPlaceholderTabs();
    if (!m_addSampleWidget || !m_trayModel || !m_moduleOverview) {
        showActionHint(tr("测试依赖未初始化"));
        return;
    }
    if (!m_serialConnected) {
        showActionHint(tr("仪器未连接，无法开始测试"));
        return;
    }
    if (m_startupState != StartupCalibrationState::Idle) {
        showActionHint(tr("启动校准进行中，不能开始测试"));
        return;
    }
    if ((m_perfusionController && m_perfusionController->isBusy())
            || (m_cleaningController && m_cleaningController->isBusy())
            || (m_verificationController
                && m_verificationController->isBusy())
            || (m_resetController && m_resetController->isBusy())) {
        showActionHint(tr("当前有动作正在进行，请稍后再开始"));
        return;
    }

    // test 模式没有开机灌注，开始测试前确保泵零位已建立；
    // 若首次/上次排空失败，这里会自动重试一次。
    if (m_skipStartupCalibration) {
        PumpPositionTracker &pumps = PumpPositionTracker::instance();
        if (!pumps.sampleValid() || !pumps.reagentValid()) {
            m_testModePumpEmptyStarted = false;
            startTestModePreparation();
            showActionHint(tr("test 模式正在建立泵零位，请稍后再开始测试"));
            return;
        }
    }

    if (!m_testScheduler) {
        m_testScheduler = new TestScheduler(m_addSampleWidget, m_trayModel,
                                            m_moduleOverview, this);
        connect(m_testScheduler, &TestScheduler::statusChanged,
                this, [this](const QString &status) {
            qInfo() << "测试状态:" << status;
            showActionHint(status);
        });
        connect(m_testScheduler, &TestScheduler::errorOccurred,
                this, [this](const QString &message) {
            qWarning() << "测试错误:" << message;
            showActionHint(message);
            MedicalMessageBox::warning(this, tr("测试中止"), message);
        });
        connect(m_testScheduler, &TestScheduler::pppBaselineFinished,
                this, [this](const QString &sampleNumber, int pppValue) {
            savePppBaseline(sampleNumber, pppValue);
        });
        connect(m_testScheduler, &TestScheduler::reagentTestFinished,
                this, [this](const QString &sampleNumber,
                             const TestCurveData &test) {
            saveTestResult(sampleNumber, test);
        });
        // 单个试剂被放弃/异常：把它独立落库标记为“异常结果”，
        // 让结果详情能显示「AA 完成 / ADP 异常 / …」而非空白曲线。
        // 复用 appendTest 的覆盖分支（状态型占位，无曲线），与完成态写库统一。
        connect(m_testScheduler, &TestScheduler::reagentAbandoned,
                this, [this](const QString &sampleNumber,
                             const QString &reagent) {
            if (!m_testDataStore || !m_testDataStore->isOpen()
                    || reagent.isEmpty()) {
                return;
            }
            const qint64 sampleId = m_sampleTestIds.value(sampleNumber, 0);
            if (sampleId <= 0) {
                return;
            }
            TestCurveData abnormal;
            abnormal.reagent = reagent;
            abnormal.status = tr("异常结果");
            QString error;
            if (!m_testDataStore->appendTest(sampleId, abnormal, &error)) {
                // 若该试剂行不存在（放弃发生在落库前）则无需新建；预约等重试。
                qWarning() << "(忽略) 异常标记写入失败:"
                           << sampleNumber << reagent << error;
            }
            if (m_resultsInfoWidget) {
                m_resultsInfoWidget->refresh();
            }
        });
        // 某个试剂开始测试：先在 sample_tests 落一条“测试中”占位行，
        // 让结果详情/查询树实时显示进行中的试剂（自主窗口 saveTestResult
        // 会在 300s 结束时以同一 UNIQUE(sample_id,reagent) 覆盖补齐曲线）。
        connect(m_testScheduler, &TestScheduler::reagentTestStarted,
                this, [this](const QString &sampleNumber,
                             const QString &reagent) {
            if (!m_testDataStore || !m_testDataStore->isOpen()
                    || reagent.isEmpty()) {
                return;
            }
            const qint64 sampleId = m_sampleTestIds.value(sampleNumber, 0);
            if (sampleId <= 0) {
                return;   // 样本尚未登记，等候 saveSample 建立
            }
            TestCurveData placeholder;
            placeholder.reagent = reagent;
            placeholder.status = tr("测试中");
            QString error;
            if (!m_testDataStore->appendTest(sampleId, placeholder, &error)) {
                // 该试剂行已存在（重复开始/已完成过）则无需新建占位。
                qWarning() << "(忽略) 占位写入失败:"
                           << sampleNumber << reagent << error;
            }
            if (m_resultsInfoWidget) {
                m_resultsInfoWidget->refresh();
            }
        });
        // PPP 基准值 < 1000：把样本标记为“异常结果”
        connect(m_testScheduler, &TestScheduler::sampleMarkedAbnormal,
                this, [this](const QString &sampleNumber) {
            if (m_testDataStore && m_testDataStore->isOpen()) {
                const qint64 sampleId =
                        m_sampleTestIds.value(sampleNumber, 0);
                if (sampleId > 0) {
                    m_testDataStore->updateSampleTestStatusById(
                                sampleId, tr("异常结果"));
                } else {
                    m_testDataStore->updateSampleTestStatus(
                                sampleNumber, tr("异常结果"));
                }
            }
            if (m_resultsInfoWidget) {
                m_resultsInfoWidget->refresh();
            }
        });
        // 测试中止时，把正在测试的样本标记为“异常结果”
        connect(m_testScheduler, &TestScheduler::errorOccurred,
                this, [this](const QString &message) {
            Q_UNUSED(message);
            if (!m_addSampleWidget || !m_testDataStore) {
                return;
            }
            // 加样表中状态为“测试中”的样本统一标为异常
            const QVector<QueuedSample> queued =
                    m_addSampleWidget->testQueue()->samples();
            Q_UNUSED(queued);
            if (m_testDataStore->isOpen()) {
                // 调度器中止后其活动样本不可访问，退化为：
                // 将所有 test_status=测试中 的样本更新为异常结果
                const QVector<SampleSummary> rows =
                        m_testDataStore->recentSamples(500);
                for (const SampleSummary &row : rows) {
                    if (row.testStatus == tr("测试中")) {
                        // 按样本 id 更新，避免同号跨天时改到别的记录。
                        m_testDataStore->updateSampleTestStatusById(
                                    row.id, tr("异常结果"));
                        m_addSampleWidget->setSampleAbnormal(
                                    row.sampleNumber);
                    }
                }
            }
        });

        // 动作层：把调度器的六类请求展开成串行运动命令。
        m_testActionController = new TestActionController(
                    &m_instrumentCoordinates, m_moduleOverview, this);
        m_testActionController->setProtocol(
                    &m_motionController->protocol());
        // 测试时 AD/主板轮询保持开启，单条 0x17 动作命令在途期间必须
        // 静默 0x16 读命令，否则复位/移动完成应答会被冲掉导致超时。
        connect(m_testActionController,
                &TestActionController::motionExchangeActiveChanged,
                m_commWorker, &CommunicationWorker::setMotionExchangeActive,
                Qt::QueuedConnection);
        connect(m_testActionController,
                &TestActionController::sendRequested,
                m_motionController,
                &platelet::MotionController::sendCommand);
        connect(m_motionController,
                &platelet::MotionController::reportReceived,
                m_testActionController,
                &TestActionController::handleReport);
        connect(m_testActionController,
                &TestActionController::actionFinished,
                m_testScheduler,
                &TestScheduler::handleActionFinished);
        connect(m_testActionController,
                &TestActionController::actionAbandoned,
                m_testScheduler,
                &TestScheduler::handleActionAbandoned);
        // 抓杯失败：抓手已复位后弹出，提供“重试 / 放弃”两种补救措施。
        connect(m_testActionController,
                &TestActionController::grabFailurePaused,
                this, [this](const QString &message) {
            MedicalMessageBox box(this);
            box.setIcon(MedicalMessageBox::Warning);
            box.setWindowTitle(tr("抓杯失败"));
            box.setText(message);
            box.setInformativeText(
                        tr("重试：抓手已复位，重新抓取当前杯。\n"
                           "放弃：PPP 杯抓取失败则放弃本样本；PRP 杯抓取失败"
                           "则跳过当前试剂，继续下一个试剂；没有后续试剂时"
                           "进入下一个样本。"));
            box.setStandardButtons(MedicalMessageBox::Retry
                                   | MedicalMessageBox::Discard);
            if (QPushButton *discard =
                    box.button(MedicalMessageBox::Discard)) {
                discard->setText(tr("放弃"));
            }
            box.setDefaultButton(MedicalMessageBox::Retry);
            const int result = box.exec();
            if (result == MedicalMessageBox::Discard) {
                m_testActionController->abandonPausedGrab();
            } else {
                m_testActionController->retryPausedGrab();
            }
        });
        connect(m_testActionController,
                &TestActionController::pppValueRead,
                m_testScheduler, &TestScheduler::setPppValue);
        connect(m_testActionController,
                &TestActionController::prp0ValueRead,
                m_testScheduler, &TestScheduler::setPrp0Value);
        connect(m_testActionController,
                &TestActionController::reagentDispensed,
                m_testScheduler, &TestScheduler::handleReagentDispensed);
        connect(m_testActionController,
                &TestActionController::statusChanged,
                this, [this](const QString &status) {
            qInfo() << "测试动作:" << status;
            showActionHint(status);
        });
        connect(m_testScheduler, &TestScheduler::addSampleRequested,
                m_testActionController, &TestActionController::addSample);
        connect(m_testScheduler, &TestScheduler::washSampleNeedleRequested,
                m_testActionController,
                &TestActionController::washSampleNeedle);
        connect(m_testScheduler, &TestScheduler::movePppCupRequested,
                m_testActionController, &TestActionController::movePppCup);
        connect(m_testScheduler, &TestScheduler::setupReagentRequested,
                m_testActionController, &TestActionController::setupReagent);
        connect(m_testScheduler, &TestScheduler::washReagentNeedleRequested,
                m_testActionController,
                &TestActionController::washReagentNeedle);
        connect(m_testScheduler, &TestScheduler::discardCupRequested,
                m_testActionController, &TestActionController::discardCup);
        connect(m_testScheduler, &TestScheduler::cancelActionRequested,
                m_testActionController, &TestActionController::cancel);
        connect(m_testScheduler, &TestScheduler::parkAtOriginRequested,
                m_testActionController, &TestActionController::parkAtOrigin);
        connect(m_testActionController,
                &TestActionController::cupPlacedInChannel,
                m_testScheduler, &TestScheduler::handleCupPlaced);
        connect(m_testActionController,
                &TestActionController::cupRemovedFromChannel,
                m_testScheduler, &TestScheduler::handleCupRemoved);
        // 测试进行中在加样页确认新样本后立即唤醒调度器继续做，
        // 不需要等当前批次结束再按一次「开始」。
        connect(m_addSampleWidget, &AddSampleWidget::sampleQueued,
                m_testScheduler, &TestScheduler::notifyWorkAvailable);
        m_testScheduler->setActionLayerReady(true);
    }

    showHomeTab();
    if (!m_testScheduler->isActionLayerReady()) {
        showActionHint(tr("测试动作层尚未接入"));
        return;
    }
    qInfo() << "开始测试";
    m_testScheduler->start();
    updatePauseButtonAppearance();
    updateStartButtonAppearance();
}

void MainWindow::handlePauseResume()
{
    if (!m_testScheduler || !m_testScheduler->isRunning()) {
        showActionHint(tr("请先点击「开始」"));
        return;
    }
    if (m_testScheduler->isPaused()) {
        m_testScheduler->resume();
    } else {
        m_testScheduler->pause();
        // 暂停只停机械臂动作：调度器在当前动作完成后不再下发新动作。
        // 模组和主板的周期轮询全部照常运行。
    }
    updatePauseButtonAppearance();
    updateStartButtonAppearance();
}

void MainWindow::updateStartButtonAppearance()
{
    if (!m_startButton) {
        return;
    }
    // 测试会话期间（包括当前无样本、等待加样）开始按钮保持“测试中”样式；
    // 新确认的样本会自动继续，点「复位」才结束本次测试。
    const bool running = m_testScheduler && m_testScheduler->isRunning();
    m_startButton->setEnabled(!running);
    m_startButton->setCursor(running ? Qt::ArrowCursor
                                     : Qt::PointingHandCursor);
    m_startButton->setText(running ? tr("测试中") : tr("开始"));
    m_startButton->setIcon(makeToolbarIcon(
                               running ? ToolbarIconType::Play : ToolbarIconType::Play,
                               running ? QColor("#8FA8BC")
                                       : QColor(255, 255, 255)));
    m_startButton->setToolTip(
                running
                ? tr("测试进行中：可在加样页继续确认样本，复位结束测试")
                : tr("开始测试"));
    updatePauseButtonAppearance();
}

void MainWindow::updatePauseButtonAppearance()
{
    if (!m_pauseButton) {
        return;
    }
    const bool paused = m_testScheduler && m_testScheduler->isPaused();
    const bool running = m_testScheduler && m_testScheduler->isRunning();

    if (!running) {
        // 空闲：暂停段置灰，不可点
        m_pauseButton->setEnabled(false);
        m_pauseButton->setText(tr("暂停"));
        m_pauseButton->setIcon(makeToolbarIcon(ToolbarIconType::Pause,
                                               QColor("#A9BFCF")));
        m_pauseButton->setProperty("state", QStringLiteral("idle"));
    } else if (paused) {
        m_pauseButton->setEnabled(true);
        m_pauseButton->setText(tr("继续"));
        m_pauseButton->setIcon(makeToolbarIcon(ToolbarIconType::Play, Qt::white));
        m_pauseButton->setProperty("state", QStringLiteral("go"));
    } else {
        m_pauseButton->setEnabled(true);
        m_pauseButton->setText(tr("暂停"));
        m_pauseButton->setIcon(makeToolbarIcon(ToolbarIconType::Pause,
                                               QColor("#B96E00")));
        m_pauseButton->setProperty("state", QStringLiteral("hot"));
    }
    // QSS 属性选择器需要刷新样式表
    m_pauseButton->style()->unpolish(m_pauseButton);
    m_pauseButton->style()->polish(m_pauseButton);
}

void MainWindow::handleResetTest()
{
    const bool wasTesting = testingActive()
            || (m_testScheduler && m_testScheduler->isRunning());

    if (m_moduleOverview) {
        if (m_testScheduler) {
            m_testScheduler->reset();
        }
        m_moduleOverview->resetAll();
        qInfo() << "测试已复位";
        updateStartButtonAppearance();
        updatePauseButtonAppearance();
        if (m_actionHint) {
            m_actionHint->clear();
        }
    }

    // 测试状态复位后，再执行机械复位：三 Z 复位 → XY 负速度复位 → 清洗位。
    if (!m_resetController) {
        return;
    }
    if (!m_serialConnected) {
        showActionHint(tr("仪器未连接，仅复位了测试状态"));
        return;
    }
    if (m_startupState != StartupCalibrationState::Idle) {
        showActionHint(tr("启动校准进行中，不能执行机械复位"));
        return;
    }
    if (wasTesting) {
        showActionHint(tr("测试状态已复位；请等测试动作完全停止后再点一次复位"));
        return;
    }
    if ((m_perfusionController && m_perfusionController->isBusy())
            || (m_cleaningController && m_cleaningController->isBusy())
            || (m_verificationController
                && m_verificationController->isBusy())
            || m_resetController->isBusy()) {
        showActionHint(tr("当前有动作正在进行，请稍后再复位"));
        return;
    }

    // 复位期间只走 0x17 动作帧，暂停周期采集避免干扰。
    if (m_commWorker) {
        QMetaObject::invokeMethod(m_commWorker, "pausePeriodicPolling",
                                  Qt::QueuedConnection);
    }
    m_resetController->start();
}

void MainWindow::handleCleaning()
{
    if (!m_serialConnected) {
        showActionHint(tr("仪器未连接，无法清洗"));
        return;
    }
    if (m_startupState != StartupCalibrationState::Idle) {
        showActionHint(tr("启动校准进行中，不能清洗"));
        return;
    }
    if (testingActive()
            || (m_testScheduler && m_testScheduler->isRunning())) {
        showActionHint(tr("测试进行中，不能清洗"));
        return;
    }
    if ((m_perfusionController && m_perfusionController->isBusy())
            || (m_cleaningController && m_cleaningController->isBusy())
            || (m_resetController && m_resetController->isBusy())) {
        showActionHint(tr("当前有动作正在进行，请稍后再清洗"));
        return;
    }
    if (!m_cleaningController) {
        showActionHint(tr("清洗控制器未初始化"));
        return;
    }
    if (!m_consumableValid[MainBoardConsumable::InternalCleanS1]) {
        showActionHint(tr("尚未读到内部清洗液余量，请稍后再试"));
        return;
    }

    const double needUl = sampleWashVolumeUl() + reagentWashVolumeUl();
    const double remainUl = consumableRemainingUl(
                MainBoardConsumable::InternalCleanS1);
    if (remainUl < needUl) {
        showActionHint(tr("内部清洗液不足：剩余约 %1 µL，本次需要 %2 µL")
                       .arg(remainUl, 0, 'f', 1)
                       .arg(needUl, 0, 'f', 1));
        return;
    }

    m_cleaningController->setCleaningTimeMs(
                perfusionCleaningTimeMsFromSettings());
    // 清洗期间只走 0x17 动作帧，和开机灌注一样暂停周期采集。
    if (m_commWorker) {
        QMetaObject::invokeMethod(m_commWorker, "pausePeriodicPolling",
                                  Qt::QueuedConnection);
    }
    m_cleaningController->start();
    updateConsumablesOperationState();
}

double MainWindow::consumableRemainingUl(int position) const
{
    if (position < 0 || position >= MainBoardConsumable::kPositionCount) {
        return 0.0;
    }
    if (m_consumableRemainingValid[position]) {
        return m_consumableRemainingUl[position];
    }
    if (!m_consumableValid[position]) {
        return 0.0;
    }
    const double capacity = consumableCapacityUl(position);
    if (capacity <= 0.0) {
        return 0.0;
    }
    return mainboardRatioToUl(m_consumableRatios[position], capacity);
}

bool MainWindow::consumeConsumableUl(int position, double usedUl,
                                     QString *error)
{
    if (error) {
        error->clear();
    }
    if (position < 0 || position >= MainBoardConsumable::kPositionCount) {
        if (error) {
            *error = tr("耗材位置无效");
        }
        return false;
    }
    if (usedUl <= 0.0) {
        return true;
    }
    if (!m_consumableValid[position]) {
        if (error) {
            *error = tr("尚未读到该耗材余量");
        }
        return false;
    }
    const double capacity = consumableCapacityUl(position);
    if (capacity <= 0.0) {
        if (error) {
            *error = tr("该耗材容量未设置");
        }
        return false;
    }

    double remain = consumableRemainingUl(position);
    if (remain < usedUl) {
        if (error) {
            *error = tr("余量不足：剩余约 %1 µL，需要 %2 µL")
                    .arg(remain, 0, 'f', 1)
                    .arg(usedUl, 0, 'f', 1);
        }
        return false;
    }

    remain -= usedUl;
    if (remain < 0.0) {
        remain = 0.0;
    }
    const int ratio = mainboardUlToRatio(remain, capacity);
    const int bottles = m_consumableBottles[position];
    writeConsumableRatio(position, ratio);

    m_consumableRemainingUl[position] = remain;
    m_consumableRemainingValid[position] = true;
    m_consumableRatios[position] = ratio;
    if (m_consumablesDialog) {
        m_consumablesDialog->setConsumableAmount(position, bottles, ratio);
    }
    qInfo() << "耗材扣减" << position << "用量" << usedUl
            << "µL 剩余" << remain << "µL 比例" << ratio << "/512";
    return true;
}

void MainWindow::writeConsumableRatio(int position, int ratio)
{
    if (!m_commWorker) {
        return;
    }
    const QByteArray frame = MainBoardPoller::consumableRatioWriteFrame(
                position, quint16(ratio));
    if (frame.isEmpty()) {
        qWarning() << "耗材比例写帧失败" << position << ratio;
        return;
    }
    m_lastWrittenConsumableRatio[position] = ratio;
    QMetaObject::invokeMethod(
                m_commWorker, "sendFrameRequested", Qt::QueuedConnection,
                Q_ARG(QByteArray, frame),
                Q_ARG(QString, tr("耗材扣减：位置%1 比例%2/512")
                      .arg(position).arg(ratio)));
}

void MainWindow::savePppBaseline(const QString &sampleNumber, int pppValue)
{
    if (!m_testDataStore) {
        return;
    }
    SampleTestData sample;
    sample.sampleNumber = sampleNumber;
    sample.sampleType = sampleQualityIsPlasma() ? tr("血浆") : tr("全血");
    sample.machineModel = QString::fromLatin1(
                machineModelName(m_machineModel));
    sample.testTime = QDateTime::currentDateTime();
    sample.pppValue = pppValue;

    QString error;
    qint64 sampleId = m_sampleTestIds.value(sampleNumber, 0);
    if (sampleId <= 0) {
        sampleId = m_testDataStore->ensureSample(sample, &error);
        if (sampleId > 0) {
            m_sampleTestIds.insert(sampleNumber, sampleId);
        }
    }
    if (sampleId <= 0) {
        qWarning() << "保存 PPP 基准值失败（样本）:" << sampleNumber << error;
        showActionHint(tr("PPP 基准值保存失败：%1").arg(error));
        return;
    }
    if (!m_testDataStore->setSamplePppValue(sampleId, pppValue, &error)) {
        qWarning() << "保存 PPP 基准值失败:" << sampleNumber << error;
        showActionHint(tr("PPP 基准值保存失败：%1").arg(error));
        return;
    }
    qInfo() << "PPP 基准值已落库" << sampleNumber << "PPP" << pppValue;
}

void MainWindow::saveTestResult(const QString &sampleNumber,
                                const TestCurveData &test)
{
    if (!m_testDataStore) {
        return;
    }
    SampleTestData sample;
    sample.sampleNumber = sampleNumber;
    sample.sampleType = sampleQualityIsPlasma() ? tr("血浆") : tr("全血");
    sample.machineModel = QString::fromLatin1(
                machineModelName(m_machineModel));
    sample.testTime = QDateTime::currentDateTime();

    QString error;
    qint64 sampleId = m_sampleTestIds.value(sampleNumber, 0);
    if (sampleId <= 0) {
        sampleId = m_testDataStore->ensureSample(sample, &error);
        if (sampleId > 0) {
            m_sampleTestIds.insert(sampleNumber, sampleId);
        }
    }
    if (sampleId <= 0) {
        qWarning() << "保存样本失败:" << sampleNumber << error;
        showActionHint(tr("测试数据保存失败：%1").arg(error));
        return;
    }
    if (!m_testDataStore->appendTest(sampleId, test, &error)) {
        qWarning() << "保存测试失败:" << sampleNumber
                   << test.reagent << error;
        showActionHint(tr("测试数据保存失败：%1").arg(error));
        return;
    }
    qInfo() << "测试结果已落库" << sampleNumber << test.reagent
            << "PRP0" << test.prp0 << "点数" << test.prpPoints.size();
}

void MainWindow::showHomeTab()
{
    if (m_mainTabs && m_homeTabIndex >= 0) {
        m_mainTabs->setCurrentIndex(m_homeTabIndex);
    }
    setActiveTabButton(m_homeButton);
}

void MainWindow::createConsumablesTab()
{
    if (!m_mainTabs || m_consumablesDialog) {
        return;
    }
    m_consumablesDialog = new ConsumablesDialog(m_mainTabs);
    connect(m_consumablesDialog, &ConsumablesDialog::reagentReplaceRequested,
            this, &MainWindow::handleReagentReplaceRequested);
    m_consumablesDialog->embedAsPage();
    QScrollArea *scroll = makePageScroll(m_consumablesDialog, m_mainTabs);
    m_consumablesTabIndex = m_mainTabs->addTab(scroll, tr("耗材管理"));
    updateConsumablesOperationState();
}

void MainWindow::createTubeTrayTab()
{
    if (!m_mainTabs || m_tubeTrayDialog || !m_trayModel) {
        return;
    }
    m_tubeTrayDialog = new TubeTrayDialog(m_trayModel, m_mainTabs);
    connect(m_tubeTrayDialog, &TubeTrayDialog::trayReplaceRequested,
            this, &MainWindow::handleTrayReplaceRequested);
    m_tubeTrayDialog->embedAsPage();
    QScrollArea *scroll = makePageScroll(m_tubeTrayDialog, m_mainTabs);
    m_tubeTrayTabIndex = m_mainTabs->addTab(scroll, tr("试管盘"));
}

void MainWindow::createResultTabs()
{
    if (!m_mainTabs) {
        return;
    }
    if (!m_resultsInfoWidget) {
        m_resultsInfoWidget = new ResultsInfoWidget(m_testDataStore,
                                                    m_mainTabs);
        m_resultsTabIndex = m_mainTabs->addTab(m_resultsInfoWidget,
                                               tr("结果信息"));
    }
    connectAddSampleStatusToResults();
    if (!m_queryWidget) {
        m_queryWidget = new QueryWidget(m_testDataStore, m_mainTabs);
        m_queryTabIndex = m_mainTabs->addTab(m_queryWidget, tr("查询"));
    }
    // 结果详情侧边面板：点击任意结果/查询行时从右缘滑出样本详情。
    if (!m_detailPanel) {
        m_detailPanel = new SampleDetailPanel(m_mainPage);
        m_detailPanel->setStore(m_testDataStore);
        connect(m_resultsInfoWidget, &ResultsInfoWidget::sampleSelected,
                m_detailPanel, &SampleDetailPanel::showSample);
        connect(m_queryWidget, &QueryWidget::sampleSelected,
                m_detailPanel, &SampleDetailPanel::showSample);
        m_detailPanel->raise();
    }
}

void MainWindow::connectAddSampleStatusToResults()
{
    // 加样页状态停在“加样完成”；结果信息表在加样完成时登记该样本为
    // “测试中”，后续测试结束/异常结果也只更新结果信息表。
    // 加样页和结果页都是延迟创建的，谁后创建谁负责完成接线。
    if (m_addSampleResultsWired || !m_addSampleWidget
            || !m_resultsInfoWidget) {
        return;
    }
    m_addSampleResultsWired = true;
    connect(m_addSampleWidget, &AddSampleWidget::sampleStatusChanged,
            this, [this](const QString &sampleNumber, const QString &status) {
        if (!m_testDataStore || !m_testDataStore->isOpen()) {
            return;
        }
        QString error;
        qint64 sampleId = m_sampleTestIds.value(sampleNumber, 0);
        if (status == tr("测试中") || sampleId <= 0) {
            // 每次测试在当天新建/复用 samples 行；跨天同号会自动新建，
            // 后续 PPP/每个试剂的 300 点都写入这个 id 对应的记录。
            sampleId = m_testDataStore->ensurePendingSample(
                        sampleNumber,
                        sampleQualityIsPlasma() ? tr("血浆") : tr("全血"),
                        &error);
            if (sampleId > 0) {
                m_sampleTestIds.insert(sampleNumber, sampleId);
            } else {
                qWarning() << "登记测试样本失败:"
                           << sampleNumber << error;
            }
        }
        const bool statusUpdated = sampleId > 0
                ? m_testDataStore->updateSampleTestStatusById(
                    sampleId, status, &error)
                : m_testDataStore->updateSampleTestStatus(
                    sampleNumber, status, &error);
        if (!statusUpdated) {
            qWarning() << "更新结果信息状态失败:"
                       << sampleNumber << status << error;
        }
        if (status == tr("测试结束") || status == tr("异常结果")) {
            m_sampleTestIds.remove(sampleNumber);
        }
        if (m_resultsInfoWidget) {
            m_resultsInfoWidget->refresh();
        }
    });
}

void MainWindow::createEmptyPlaceholderTabs()
{
    if (!m_mainTabs) {
        return;
    }
    if (m_qualityTabIndex < 0) {
        m_qualityTabIndex = m_mainTabs->addTab(
                    createPlaceholderPage(
                        tr("质控"),
                        tr("质量控制功能待接入，后续在这里显示质控结果。")),
                    tr("质控"));
    }
    if (m_curveTabIndex < 0) {
        m_curveTabIndex = m_mainTabs->addTab(
                    createPlaceholderPage(
                        tr("曲线"),
                        tr("曲线功能待接入，后续在这里显示聚合曲线。")),
                    tr("曲线"));
    }
    if (m_addSampleTabIndex < 0) {
        m_addSampleWidget = new AddSampleWidget(m_machineModel,
                                                m_testDataStore, m_mainTabs);
        QScrollArea *scroll = makePageScroll(m_addSampleWidget, m_mainTabs);
        m_addSampleTabIndex = m_mainTabs->addTab(scroll, tr("加样"));
        connectAddSampleStatusToResults();
    }
}

QWidget *MainWindow::createPlaceholderPage(const QString &title,
                                           const QString &description)
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("placeholderPage"));
    page->setStyleSheet(QStringLiteral(
        "QWidget#placeholderPage { background: #F4F8FC; }"
        "QLabel#pageTitle {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 21px;"
        "  font-weight: 700;"
        "}"
        "QLabel#pageSubtitle {"
        "  color: #7A8FA3;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QFrame#placeholderCard {"
        "  background: #FFFFFF;"
        "  border: 1px dashed #C9DCEB;"
        "  border-radius: 12px;"
        "}"
        "QLabel#placeholderHint {"
        "  color: #8A9CAB;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 16px;"
        "}"));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);

    auto *titleLabel = new QLabel(title, page);
    titleLabel->setObjectName(QStringLiteral("pageTitle"));
    auto *descriptionLabel = new QLabel(description, page);
    descriptionLabel->setObjectName(QStringLiteral("pageSubtitle"));
    descriptionLabel->setWordWrap(true);
    layout->addWidget(titleLabel);
    layout->addWidget(descriptionLabel);
    layout->addSpacing(10);

    auto *card = new QFrame(page);
    card->setObjectName(QStringLiteral("placeholderCard"));
    auto *cardLayout = new QVBoxLayout(card);
    auto *hint = new QLabel(tr("功能待接入"), card);
    hint->setObjectName(QStringLiteral("placeholderHint"));
    hint->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(hint);
    layout->addWidget(card, 1);

    return page;
}

void MainWindow::openResults()
{
    createResultTabs();
    if (m_resultsInfoWidget) {
        m_resultsInfoWidget->refresh();
    }
    if (m_mainTabs && m_resultsTabIndex >= 0) {
        m_mainTabs->setCurrentIndex(m_resultsTabIndex);
    }
    setActiveTabButton(m_resultsButton);
}

void MainWindow::openQuery()
{
    createResultTabs();
    if (m_queryWidget) {
        m_queryWidget->refresh();
    }
    if (m_mainTabs && m_queryTabIndex >= 0) {
        m_mainTabs->setCurrentIndex(m_queryTabIndex);
    }
    setActiveTabButton(m_queryButton);
}

void MainWindow::openQuality()
{
    createEmptyPlaceholderTabs();
    if (m_mainTabs && m_qualityTabIndex >= 0) {
        m_mainTabs->setCurrentIndex(m_qualityTabIndex);
    }
    setActiveTabButton(m_qualityButton);
}

void MainWindow::openCurve()
{
    createEmptyPlaceholderTabs();
    if (m_mainTabs && m_curveTabIndex >= 0) {
        m_mainTabs->setCurrentIndex(m_curveTabIndex);
    }
    setActiveTabButton(m_curveButton);
}

void MainWindow::openAddSample()
{
    createEmptyPlaceholderTabs();
    if (m_addSampleWidget) {
        m_addSampleWidget->setMachineModel(m_machineModel);
        m_addSampleWidget->refreshMode();
    }
    if (m_mainTabs && m_addSampleTabIndex >= 0) {
        m_mainTabs->setCurrentIndex(m_addSampleTabIndex);
    }
    setActiveTabButton(m_addSampleButton);
}

void MainWindow::createLogTab()
{
    if (!m_mainTabs || m_logTabIndex >= 0) {
        return;
    }

    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("placeholderPage"));
    page->setStyleSheet(QStringLiteral(
        "QWidget#placeholderPage { background: #F4F8FC; }"
        "QLabel#pageTitle {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 21px;"
        "  font-weight: 700;"
        "}"
        "QLabel#pageSubtitle {"
        "  color: #7A8FA3;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QPushButton#logAction {"
        "  min-width: 96px;"
        "  min-height: 32px;"
        "  background: #FFFFFF;"
        "  color: #1F4E79;"
        "  border: 1px solid #A9C8DF;"
        "  border-radius: 6px;"
        "  padding: 4px 14px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QPushButton#logAction:hover { background: #EAF4FC; }"
        "QPushButton#logAction:pressed { background: #DCEBFA; }"));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);

    auto *headerRow = new QHBoxLayout;
    headerRow->setSpacing(8);
    auto *title = new QLabel(tr("运行日志"), page);
    title->setObjectName(QStringLiteral("pageTitle"));
    auto *refreshButton = new QPushButton(tr("刷新"), page);
    refreshButton->setObjectName(QStringLiteral("logAction"));
    refreshButton->setCursor(Qt::PointingHandCursor);
    auto *openDirButton = new QPushButton(tr("打开日志目录"), page);
    openDirButton->setObjectName(QStringLiteral("logAction"));
    openDirButton->setCursor(Qt::PointingHandCursor);
    headerRow->addWidget(title);
    headerRow->addStretch(1);
    headerRow->addWidget(refreshButton);
    headerRow->addWidget(openDirButton);
    layout->addLayout(headerRow);

    m_logPathLabel = new QLabel(page);
    m_logPathLabel->setObjectName(QStringLiteral("pageSubtitle"));
    m_logPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_logPathLabel->setWordWrap(true);
    layout->addWidget(m_logPathLabel);

    m_logView = new QPlainTextEdit(page);
    m_logView->setReadOnly(true);
    m_logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_logView->setMaximumBlockCount(10000);
    QFont logFont(QStringLiteral("Consolas"));
    logFont.setStyleHint(QFont::Monospace);
    logFont.setPixelSize(12);
    m_logView->setFont(logFont);
    m_logView->setStyleSheet(QStringLiteral(
        "QPlainTextEdit {"
        "  background: #FFFFFF;"
        "  color: #1F2D3D;"
        "  border: 1px solid #D8E5EF;"
        "  border-radius: 8px;"
        "  padding: 8px;"
        "}"));
    layout->addWidget(m_logView, 1);

    connect(refreshButton, &QPushButton::clicked,
            this, &MainWindow::refreshLogView);
    connect(openDirButton, &QPushButton::clicked, this, [this]() {
        const QString directory = AppLog::logDirectory();
        if (directory.isEmpty() || !QDir(directory).exists()) {
            MedicalMessageBox::information(
                        this, tr("运行日志"),
                        tr("日志目录尚未创建，暂时没有可打开的目录。"));
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
    });

    m_logTabIndex = m_mainTabs->addTab(page, tr("日志"));
}

void MainWindow::openLogs()
{
    createLogTab();
    refreshLogView();
    if (m_mainTabs && m_logTabIndex >= 0) {
        m_mainTabs->setCurrentIndex(m_logTabIndex);
    }
    setActiveTabButton(m_logButton);
}

void MainWindow::refreshLogView()
{
    if (!m_logView || !m_logPathLabel) {
        return;
    }

    QString path = AppLog::currentLogFilePath();
    if (path.isEmpty()) {
        const QString directory = AppLog::logDirectory();
        if (!directory.isEmpty()) {
            QDir dir(directory);
            const QStringList files = dir.entryList(
                        QStringList() << QStringLiteral(
                            "PlateletAggregation_*.log"),
                        QDir::Files, QDir::Time);
            if (!files.isEmpty()) {
                path = dir.filePath(files.first());
            }
        }
    }
    if (path.isEmpty()) {
        m_logPathLabel->setText(tr("日志文件尚未创建。"));
        m_logPathLabel->setToolTip(QString());
        m_logView->setPlainText(tr("暂无日志。"));
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_logPathLabel->setText(
                    tr("无法读取日志文件：%1").arg(file.errorString()));
        m_logPathLabel->setToolTip(path);
        m_logView->setPlainText(
                    tr("无法打开日志文件：\n%1\n\n%2")
                    .arg(path, file.errorString()));
        return;
    }

    const qint64 maxBytes = 512 * 1024;
    const bool truncated = file.size() > maxBytes;
    if (truncated) {
        file.seek(file.size() - maxBytes);
    }
    QString text = QString::fromUtf8(file.readAll());
    if (truncated) {
        const int newline = text.indexOf(QLatin1Char('\n'));
        if (newline >= 0) {
            text = text.mid(newline + 1);
        }
    }
    m_logView->setPlainText(text);
    QTextCursor cursor = m_logView->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_logView->setTextCursor(cursor);
    m_logPathLabel->setText(
                truncated
                ? tr("日志文件：%1（仅显示最近 512 KB）").arg(path)
                : tr("日志文件：%1").arg(path));
    m_logPathLabel->setToolTip(path);
}

void MainWindow::createSettingsTab()
{
    if (!m_mainTabs || m_settingsDialog) {
        return;
    }
    m_settingsDialog = new SettingsDialog(m_commWorker,
                                          &m_instrumentCoordinates,
                                          m_mainTabs, m_machineModel);
    m_settingsDialog->setCurrentUser(m_currentUserName, m_currentUserRole);
    m_settingsDialog->setActiveSampleCountProvider([this]() {
        return m_addSampleWidget ? m_addSampleWidget->activeSampleCount()
                                 : 0;
    });
    connect(m_settingsDialog, &SettingsDialog::sampleQualityChanged,
            this, [this]() {
        if (m_boardIndicators) {
            m_boardIndicators->setHeightIconVisible(
                        !sampleQualityIsPlasma());
        }
        if (m_addSampleWidget) {
            m_addSampleWidget->refreshMode();
        }
    });
    connect(m_settingsDialog, &SettingsDialog::coordinateMotionRequested,
            this, [this](const InstrumentCoordinates::MoveTarget &target) {
        // 定位校验前暂停主板/模组周期采集，避免读命令干扰复位/XY 动作。
        if (m_commWorker) {
            QMetaObject::invokeMethod(m_commWorker, "pausePeriodicPolling",
                                      Qt::QueuedConnection);
        }
        m_verificationController->start(target);
    });
    connect(m_settingsDialog, &SettingsDialog::machineModelChanged,
            this, &MainWindow::setMachineModel);
    connect(m_settingsDialog, &SettingsDialog::channelEnabledChanged,
            this, [this](int globalChannel, bool enabled) {
        if (!m_moduleOverview) {
            return;
        }
        const int moduleNumber = (globalChannel - 1) / 4 + 1;
        const int localChannel = (globalChannel - 1) % 4 + 1;
        m_moduleOverview->setChannelEnabled(moduleNumber, localChannel,
                                            enabled);
    });
    m_settingsDialog->embedAsPage();
    QScrollArea *scroll = makePageScroll(m_settingsDialog, m_mainTabs);
    m_settingsTabIndex = m_mainTabs->addTab(scroll, tr("设置"));
}

void MainWindow::refreshConsumablesTab()
{
    if (!m_consumablesDialog) {
        return;
    }

    if (m_hasMainboardStatus) {
        m_consumablesDialog->setMainboardStatus(m_latestMainboardStatus);
    } else {
        m_consumablesDialog->clearStatus(tr("等待主板状态…"));
    }

    m_consumablesDialog->clearConsumableAmounts();
    for (int position = 0;
         position < MainBoardConsumable::kPositionCount; ++position) {
        if (m_consumableValid[position]) {
            m_consumablesDialog->setConsumableAmount(
                        position, m_consumableBottles[position],
                        m_consumableRatios[position]);
        }
    }
}

void MainWindow::openConsumables()
{
    createConsumablesTab();
    updateConsumablesOperationState();
    refreshConsumablesTab();
    if (m_mainTabs && m_consumablesTabIndex >= 0) {
        m_mainTabs->setCurrentIndex(m_consumablesTabIndex);
    }
    setActiveTabButton(m_consumablesButton);
}

void MainWindow::openTubeTrays()
{
    createTubeTrayTab();
    if (m_mainTabs && m_tubeTrayTabIndex >= 0) {
        m_mainTabs->setCurrentIndex(m_tubeTrayTabIndex);
    }
    setActiveTabButton(m_trayButton);
}

void MainWindow::handleTrayReplaceRequested(int trayIndex)
{
    if (!m_trayModel || !m_trayModel->isTrayActive(trayIndex)) {
        return;
    }

    // 测试中禁止更换
    if (testingActive()) {
        MedicalMessageBox::warning(
                    this, tr("更换试管盘"),
                    tr("当前正在测试，不能更换试管盘。\n"
                       "请等待所有通道测试结束后再更换。"));
        return;
    }

    if (!m_serialConnected) {
        MedicalMessageBox::warning(
                    this, tr("更换试管盘"),
                    tr("仪器未连接，无法更新主板试管盘余量。\n"
                       "请连接仪器后再更换。"));
        return;
    }

    const int position = MainBoardConsumable::TestTube;
    if (!m_consumableValid[position] || m_consumableBottles[position] < 0) {
        MedicalMessageBox::warning(
                    this, tr("更换试管盘"),
                    tr("尚未读取到主板试管盘余量，请稍后重试。"));
        return;
    }

    const int currentTrays = m_consumableBottles[position];
    if (currentTrays <= 0) {
        MedicalMessageBox::warning(
                    this, tr("更换试管盘"),
                    tr("试管盘余量不足（0 板），请先刷卡充值后再更换。"));
        return;
    }

    const int nonFreeCount = SampleTrayModel::TubesPerTray
            - m_trayModel->countStateInTray(
                trayIndex, SampleTrayModel::TubeState::Free);

    QString firstText;
    if (nonFreeCount > 0) {
        firstText = tr("确定更换试管盘 %1 吗？\n\n"
                       "盘内 %2 支非空闲试管将全部恢复为空闲。\n"
                       "当前试管盘余量：%3 板，更换后减 1。")
                .arg(trayIndex + 1)
                .arg(nonFreeCount)
                .arg(currentTrays);
    } else {
        firstText = tr("试管盘 %1 当前全部空闲。\n\n"
                       "当前试管盘余量：%2 板，更换后减 1。\n"
                       "确定更换吗？")
                .arg(trayIndex + 1)
                .arg(currentTrays);
    }
    if (MedicalMessageBox::question(this, tr("更换试管盘"), firstText,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    // 二次确认：重新读取余量，避免轮询期间数值变化
    if (!m_serialConnected || !m_consumableValid[position]
            || m_consumableBottles[position] <= 0) {
        MedicalMessageBox::warning(
                    this, tr("更换试管盘"),
                    tr("试管盘余量已变化或仪器已断开，请重试。"));
        return;
    }
    const int latestTrays = m_consumableBottles[position];
    const int newTrayCount = latestTrays - 1;
    const QString secondText =
            tr("再次确认：更换试管盘 %1 后，该盘恢复为空闲，\n"
               "试管盘余量由 %2 板减为 %3 板。\n\n"
               "确定继续吗？")
            .arg(trayIndex + 1)
            .arg(latestTrays)
            .arg(newTrayCount);
    if (MedicalMessageBox::question(this, tr("再次确认更换"), secondText,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    if (!m_commWorker) {
        MedicalMessageBox::warning(
                    this, tr("更换试管盘"),
                    tr("通讯线程未启动，无法更新主板数量。"));
        return;
    }

    const QByteArray frame = MainBoardPoller::consumableTotalWriteFrame(
                position, newTrayCount);
    if (frame.isEmpty()) {
        MedicalMessageBox::warning(
                    this, tr("更换试管盘"),
                    tr("生成主板写入命令失败，请重试。"));
        return;
    }

    QMetaObject::invokeMethod(
                m_commWorker, "sendFrameRequested", Qt::QueuedConnection,
                Q_ARG(QByteArray, frame),
                Q_ARG(QString, tr("更换试管盘：写入试管盘余量 %1 板")
                      .arg(newTrayCount)));

    updateConsumableAmount(position, newTrayCount, 521);
    m_trayModel->clearTray(trayIndex);

    qInfo() << "更换试管盘" << (trayIndex + 1)
            << "，余量" << latestTrays << "->" << newTrayCount;
    showActionHint(tr("试管盘 %1 已更换，余量 %2 板")
                   .arg(trayIndex + 1)
                   .arg(newTrayCount));
}

bool MainWindow::reagentReplacementBlocked(QString *reason) const
{
    const auto blocked = [reason](const QString &text) {
        if (reason) {
            *reason = text;
        }
        return true;
    };

    if (!m_serialConnected) {
        return blocked(tr("仪器未连接，不能更换试剂"));
    }
    if (m_startupState != StartupCalibrationState::Idle) {
        return blocked(tr("启动校准进行中，不能更换试剂"));
    }
    if (m_cardRechargeState == CardRechargeState::Writing) {
        return blocked(tr("刷卡充值写入中，不能更换试剂"));
    }
    if (testingActive()
            || (m_testScheduler && m_testScheduler->isRunning())
            || (m_testActionController && m_testActionController->isBusy())
            || (m_perfusionController && m_perfusionController->isBusy())
            || (m_cleaningController && m_cleaningController->isBusy())
            || (m_verificationController
                && m_verificationController->isBusy())
            || (m_resetController && m_resetController->isBusy())) {
        return blocked(tr("测试机械臂正在运动或测试进行中，不能更换试剂"));
    }
    return false;
}

void MainWindow::updateConsumablesOperationState()
{
    if (!m_consumablesDialog) {
        return;
    }
    QString reason;
    const bool blocked = reagentReplacementBlocked(&reason);
    m_consumablesDialog->setReplaceAllowed(!blocked, reason);
}

void MainWindow::handleReagentReplaceRequested(int position)
{
    if (!MainBoardConsumable::isReagentPosition(position)) {
        return;
    }

    QScopedValueRollback<bool> replaceGuard(m_reagentReplaceInProgress, true);
    QString reason;
    if (reagentReplacementBlocked(&reason)) {
        MedicalMessageBox::warning(this, tr("更换试剂"), reason);
        return;
    }
    if (!m_commWorker) {
        MedicalMessageBox::warning(this, tr("更换试剂"),
                             tr("通讯线程未启动，无法更新主板。"));
        return;
    }

    const int mainPosition =
            MainBoardConsumable::mainPositionForReagent(position);
    if (!m_consumableValid[mainPosition]
            || m_consumableBottles[mainPosition] < 0) {
        MedicalMessageBox::warning(this, tr("更换试剂"),
                             tr("尚未读取到该试剂的总瓶数，请稍后重试。"));
        return;
    }

    const QString name =
            MainBoardConsumable::positionDisplayName(position);
    const int currentTotal = m_consumableBottles[mainPosition];
    if (currentTotal <= 0) {
        MedicalMessageBox::warning(
                    this, tr("更换试剂"),
                    tr("%1 的总瓶数为 0，不能更换。\n"
                       "请先刷卡充值，再更换试剂。").arg(name));
        return;
    }
    if (currentTotal <= 1) {
        // 仍允许更换最后一瓶，但提醒更换后库存为 0。
        qInfo() << "更换试剂：当前总瓶数仅剩" << currentTotal;
    }

    const QString firstText =
            tr("确定更换 %1 吗？\n\n"
               "当前该试剂总瓶数：%2 瓶\n"
               "更换后总瓶数：%3 瓶（消耗 1 瓶库存）\n"
               "更换后该侧单瓶余量补满 100%（主板比例 512）。")
            .arg(name)
            .arg(currentTotal)
            .arg(currentTotal - 1);
    if (MedicalMessageBox::question(this, tr("更换试剂"), firstText,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    // 第一次确认后再次校验，避免期间开始测试或余量被充值改变。
    if (reagentReplacementBlocked(&reason)) {
        MedicalMessageBox::warning(this, tr("更换试剂"), reason);
        return;
    }
    const int latestTotal = m_consumableBottles[mainPosition];
    if (latestTotal != currentTotal || latestTotal <= 0) {
        MedicalMessageBox::warning(
                    this, tr("更换试剂"),
                    tr("总瓶数已变化（%1 → %2），请重新操作。")
                    .arg(currentTotal).arg(latestTotal));
        return;
    }

    const int newTotal = latestTotal - 1;
    const QString secondText =
            tr("再次确认：即将更换 %1。\n\n"
               "主板总瓶数：%2 → %3\n"
               "该侧单瓶余量：写入 512（100%）\n\n"
               "确定继续吗？")
            .arg(name)
            .arg(latestTotal)
            .arg(newTotal);
    if (MedicalMessageBox::question(this, tr("再次确认更换试剂"), secondText,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    struct WriteRequest {
        QByteArray frame;
        QString description;
    };
    QVector<WriteRequest> requests;

    const QByteArray primary = MainBoardPoller::consumableTotalWriteFrame(
                position, newTotal);
    if (primary.isEmpty()) {
        MedicalMessageBox::warning(this, tr("更换试剂"),
                             tr("生成主板写入命令失败，请重试。"));
        return;
    }
    requests.append({ primary,
                      tr("更换%1：写入总瓶数%2")
                      .arg(name).arg(newTotal) });

    // AA~RIS 左右瓶共享总瓶数：同步另一侧的总瓶数，保留该侧单瓶比例。
    int otherPosition = -1;
    if (position >= MainBoardConsumable::AA_Second) {
        otherPosition = mainPosition;
    } else {
        otherPosition = position + (MainBoardConsumable::AA_Second
                                    - MainBoardConsumable::AA);
    }
    if (otherPosition >= 0
            && otherPosition < MainBoardConsumable::kPositionCount
            && m_consumableValid[otherPosition]
            && m_consumableRatios[otherPosition] >= 0) {
        const QByteArray secondary = MainBoardPoller::consumableTotalWriteFrame(
                    otherPosition, newTotal);
        if (!secondary.isEmpty()) {
            requests.append(
            { secondary,
              tr("同步%1总瓶数%2")
              .arg(MainBoardConsumable::positionDisplayName(otherPosition))
              .arg(newTotal) });
        }
    }

    for (const WriteRequest &request : requests) {
        QMetaObject::invokeMethod(
                    m_commWorker, "sendFrameRequested", Qt::QueuedConnection,
                    Q_ARG(QByteArray, request.frame),
                    Q_ARG(QString, request.description));
    }

    // 软件缓存立即更新，主板下一轮读取会回填真实值。
    m_consumableBottles[mainPosition] = newTotal;
    m_consumableValid[mainPosition] = true;
    m_consumableBottles[position] = newTotal;
    m_consumableValid[position] = true;
    m_consumableRatios[position] = MainBoardConsumable::kRatioMax;
    m_lastWrittenConsumableRatio[position] = MainBoardConsumable::kRatioMax;
    m_lastSeenConsumableRatio[position] = MainBoardConsumable::kRatioMax;
    if (otherPosition >= 0
            && otherPosition < MainBoardConsumable::kPositionCount) {
        m_consumableBottles[otherPosition] = newTotal;
        m_consumableValid[otherPosition] = true;
    }

    const double capacity = consumableCapacityUl(position);
    if (capacity > 0.0) {
        m_consumableRemainingUl[position] = capacity;
        m_consumableRemainingValid[position] = true;
    }

    if (m_consumablesDialog) {
        m_consumablesDialog->setConsumableAmount(
                    mainPosition, newTotal,
                    m_consumableRatios[mainPosition]);
        if (position != mainPosition) {
            m_consumablesDialog->setConsumableAmount(
                        position, newTotal, MainBoardConsumable::kRatioMax);
        }
    }
    const int bottle = reagentBottleForConsumablePosition(position);
    if (bottle >= 0 && m_reagentPanel) {
        m_reagentPanel->setLevel(bottle, 100.0);
    }

    qInfo() << "更换试剂" << name << "总瓶数" << latestTotal
            << "->" << newTotal << "，该侧补满512";
    showActionHint(tr("%1 已更换，总瓶数 %2 瓶")
                   .arg(name).arg(newTotal));
}

QString MainWindow::cardBatchText(int year, int month)
{
    if (year <= 0 || month <= 0 || month > 12) {
        return QStringLiteral("--");
    }
    const int fullYear = year < 100 ? 2000 + year : year;
    return QStringLiteral("%1-%2")
            .arg(fullYear)
            .arg(month, 2, 10, QLatin1Char('0'));
}

void MainWindow::handleCardRechargeStatus(const MainBoardStatus &status)
{
    const quint8 mfc = status.mfcStatus;

    if (m_cardRechargeState == CardRechargeState::Writing) {
        // 正常结果由充值命令的直接回执处理；这里兼容 0xFE 状态帧先到的情况。
        if (mfc == 5) {
            finishCardRecharge(true, tr("主板回报充值成功"));
        } else if (mfc == 6) {
            finishCardRecharge(false, tr("主板回报充值失败"));
        } else if (mfc == 3 || mfc == 4) {
            finishCardRecharge(false, tr("读卡/刷卡异常，充值未完成"));
        }
        m_lastCardMfcStatus = mfc;
        return;
    }

    if (mfc == 0) {
        // 只有真正回到“正常”才允许下一张卡，避免写完卡后连续重复充值。
        m_cardReaderNormalSeen = true;
        m_cardWaitingReset = false;
        m_lastCardFailureSignature = -1;
    } else if (mfc == 1 || mfc == 7) {
        m_cardReaderNormalSeen = true;
    } else if (mfc == 2) {
        if (!m_hasLoggedIn) {
            // 登录前不自动写卡，避免上电/维护时误充值。
            m_lastCardMfcStatus = mfc;
            return;
        }
        if (m_cardWaitingReset || !m_cardReaderNormalSeen) {
            m_lastCardMfcStatus = mfc;
            return;
        }
        if (m_lastCardMfcStatus == 2) {
            return;
        }
        if (m_startupState != StartupCalibrationState::Idle) {
            m_cardWaitingReset = true;
            showActionHint(tr("启动校准进行中，刷卡充值暂不可用"));
        } else {
            beginCardRecharge(status);
        }
    } else if (mfc == 3 || mfc == 4 || mfc == 6) {
        const int signature = int(mfc)
                | (int(status.regType) << 4)
                | (int(status.regRemain) << 8);
        if (signature != m_lastCardFailureSignature) {
            m_lastCardFailureSignature = signature;
            const QString reason = mfc == 3
                    ? tr("读卡失败")
                    : (mfc == 4 ? tr("刷卡设备异常") : tr("主板充值失败"));
            showActionHint(tr("刷卡失败：%1").arg(reason));
            if (m_consumablesDialog) {
                m_consumablesDialog->setRechargeStatus(
                            tr("刷卡失败：%1").arg(reason), false);
            }
        }
        m_cardWaitingReset = true;
    }

    m_lastCardMfcStatus = mfc;
}

void MainWindow::beginCardRecharge(const MainBoardStatus &status)
{
    const auto reject = [this](const QString &message) {
        m_cardWaitingReset = true;
        showActionHint(message);
        if (m_consumablesDialog) {
            m_consumablesDialog->setRechargeStatus(message, false);
        }
        qWarning() << "刷卡充值未开始:" << message;
    };

    if (!m_serialConnected || !m_commWorker) {
        reject(tr("仪器未连接，充值未开始"));
        return;
    }
    if (m_reagentReplaceInProgress) {
        reject(tr("正在更换试剂，请更换完成后再刷卡"));
        return;
    }
    // 灌注/清洗/定位校验期间会暂停主板轮询保护运动命令；此时不能
    // 插入刷卡充值，否则动作结束恢复轮询会打断 RFID 写卡窗口。
    if (testingActive()
            || (m_perfusionController && m_perfusionController->isBusy())
            || (m_cleaningController && m_cleaningController->isBusy())
            || (m_verificationController
                && m_verificationController->isBusy())
            || (m_resetController && m_resetController->isBusy())) {
        reject(tr("仪器正在执行动作，请动作完成后再刷卡"));
        return;
    }

    const int cardType = int(status.regType);
    const int cardQty = int(status.regRemain);
    if (cardQty <= 0) {
        reject(tr("卡内数量为 0，未写入"));
        return;
    }

    const int position = MainBoardConsumable::positionForCardType(cardType);
    if (position < 0) {
        reject(tr("未知刷卡类型（编码 %1），未写入").arg(cardType));
        return;
    }

    const bool reagent = MainBoardConsumable::isReagentPosition(position);
    const int mainPosition = reagent
            ? MainBoardConsumable::mainPositionForReagent(position)
            : position;
    if (!m_consumableValid[mainPosition]
            || m_consumableBottles[mainPosition] < 0) {
        reject(tr("尚未读取到 %1 的总瓶数，请稍后重新刷卡")
               .arg(MainBoardConsumable::positionDisplayName(mainPosition)));
        return;
    }

    const int newTotal = m_consumableBottles[mainPosition] + cardQty;
    if (newTotal > 0xFF) {
        reject(tr("%1 充值后总瓶数 %2 超过上限 255，未写入")
               .arg(MainBoardConsumable::positionDisplayName(position))
               .arg(newTotal));
        return;
    }

    const QByteArray frame = MainBoardPoller::cardRechargeConfirmFrame(
                cardType, newTotal);
    if (frame.isEmpty()) {
        reject(tr("生成充值确认命令失败"));
        return;
    }

    const QString name =
            MainBoardConsumable::positionDisplayName(position);
    m_cardRechargeState = CardRechargeState::Writing;
    m_cardWaitingReset = true;
    updateConsumablesOperationState();
    m_rechargeCardType = cardType;
    m_rechargeCardQty = cardQty;
    m_rechargePosition = position;
    m_rechargeNewTotal = newTotal;
    m_rechargeBatch = cardBatchText(status.regYear, status.regMonth);
    m_cardRechargeTimer->start();

    QMetaObject::invokeMethod(
                m_commWorker, "sendCardRechargeConfirm", Qt::QueuedConnection,
                Q_ARG(QByteArray, frame),
                Q_ARG(int, cardType),
                Q_ARG(QString, tr("确认刷卡充值：%1 +%2 瓶")
                      .arg(name).arg(cardQty)));

    const QString progress =
            tr("刷卡充值：%1 +%2 瓶，批号 %3，正在写入主板…")
            .arg(name).arg(cardQty).arg(m_rechargeBatch);
    showActionHint(progress);
    if (m_consumablesDialog) {
        m_consumablesDialog->setRechargeStatus(progress, true);
    }
    qInfo() << "刷卡充值开始" << name << "卡数量" << cardQty
            << "原总瓶数" << (newTotal - cardQty)
            << "新总瓶数" << newTotal << "批号" << m_rechargeBatch;
}

void MainWindow::handleCardRechargeResult(int mfcStatus, int cardType,
                                          int cardQty, int year, int month)
{
    if (m_cardRechargeState != CardRechargeState::Writing) {
        qInfo() << "忽略非充值中的刷卡回执" << mfcStatus
                << "类型" << cardType << "数量" << cardQty;
        return;
    }

    if (m_rechargeBatch == QLatin1String("--")) {
        m_rechargeBatch = cardBatchText(year, month);
    }
    if (mfcStatus == 5) {
        finishCardRecharge(true, tr("充值写入成功"));
    } else if (mfcStatus == 6) {
        finishCardRecharge(false, tr("主板回报充值写入失败"));
    } else if (mfcStatus == 3 || mfcStatus == 4) {
        finishCardRecharge(false, tr("读卡异常，充值未完成"));
    } else {
        qInfo() << "充值回执中间状态" << mfcStatus << "，继续等待";
    }
}

void MainWindow::finishCardRecharge(bool ok, const QString &message)
{
    if (m_cardRechargeState != CardRechargeState::Writing) {
        return;
    }
    m_cardRechargeTimer->stop();
    m_cardRechargeState = CardRechargeState::Idle;
    m_cardWaitingReset = true;
    updateConsumablesOperationState();

    const int position = m_rechargePosition;
    const QString name = position >= 0
            ? MainBoardConsumable::positionDisplayName(position)
            : QStringLiteral("--");
    const int cardQty = m_rechargeCardQty;
    const int newTotal = m_rechargeNewTotal;

    if (ok && position >= 0) {
        const int mainPosition =
                MainBoardConsumable::isReagentPosition(position)
                ? MainBoardConsumable::mainPositionForReagent(position)
                : position;
        if (mainPosition >= 0
                && mainPosition < MainBoardConsumable::kPositionCount) {
            m_consumableBottles[mainPosition] = newTotal;
            m_consumableValid[mainPosition] = true;
        }
        if (position != mainPosition) {
            m_consumableBottles[position] = newTotal;
            m_consumableValid[position] = true;
        }

        // 左右瓶共享总瓶数：把另一侧槽位也同步为新总瓶数，保留其单瓶比例。
        int otherPosition = -1;
        if (MainBoardConsumable::isReagentPosition(position)) {
            otherPosition = position >= MainBoardConsumable::AA_Second
                    ? mainPosition
                    : position + (MainBoardConsumable::AA_Second
                                  - MainBoardConsumable::AA);
        }
        if (otherPosition >= 0
                && otherPosition < MainBoardConsumable::kPositionCount) {
            m_consumableBottles[otherPosition] = newTotal;
            m_consumableValid[otherPosition] = true;
            if (m_commWorker
                    && m_consumableRatios[otherPosition] >= 0) {
                const QByteArray sync =
                        MainBoardPoller::consumableTotalWriteFrame(
                            otherPosition, newTotal);
                if (!sync.isEmpty()) {
                    QMetaObject::invokeMethod(
                                m_commWorker, "sendFrameRequested",
                                Qt::QueuedConnection,
                                Q_ARG(QByteArray, sync),
                                Q_ARG(QString,
                                      tr("充值后同步%1总瓶数%2")
                                      .arg(MainBoardConsumable::
                                           positionDisplayName(otherPosition))
                                      .arg(newTotal)));
                }
            }
        }

        if (m_consumablesDialog) {
            m_consumablesDialog->setConsumableAmount(
                        mainPosition, newTotal,
                        m_consumableRatios[mainPosition]);
            if (position != mainPosition) {
                m_consumablesDialog->setConsumableAmount(
                            position, newTotal,
                            m_consumableRatios[position]);
            }
            m_consumablesDialog->setRechargeStatus(
                        tr("充值成功：%1 +%2 瓶，总瓶数 %3")
                        .arg(name).arg(cardQty).arg(newTotal), true);
        }
        showActionHint(tr("刷卡充值成功：%1 +%2 瓶，总瓶数 %3")
                       .arg(name).arg(cardQty).arg(newTotal));
        qInfo() << "刷卡充值成功" << name << "+" << cardQty
                << "总瓶数" << newTotal << "批号" << m_rechargeBatch;
    } else {
        if (m_consumablesDialog) {
            m_consumablesDialog->setRechargeStatus(
                        tr("充值失败：%1").arg(message), false);
        }
        showActionHint(tr("刷卡充值失败：%1").arg(message));
        qWarning() << "刷卡充值失败" << name << message;
    }

    // 成功时上面已把“同步另一侧总瓶数”命令排入队列；这里再恢复轮询，
    // 保证同步写命令先于新一轮状态/耗材读取发出。
    if (m_commWorker) {
        QMetaObject::invokeMethod(m_commWorker, "resumeMainboardPolling",
                                  Qt::QueuedConnection);
    }

    QStringList history;
    history << QDateTime::currentDateTime().toString(
                   QStringLiteral("yyyy-MM-dd HH:mm:ss"))
            << name
            << QStringLiteral("+%1").arg(cardQty)
            << QStringLiteral("总%1").arg(newTotal)
            << QStringLiteral("批号%1").arg(m_rechargeBatch)
            << (ok ? tr("成功") : tr("失败"));
    if (!ok) {
        history << message;
    }
    appendConsumableRechargeHistory(history.join(QLatin1Char('|')));

    if (!ok) {
        MedicalMessageBox::warning(this, tr("刷卡充值失败"), message);
    }

    m_rechargeCardType = -1;
    m_rechargeCardQty = 0;
    m_rechargePosition = -1;
    m_rechargeNewTotal = 0;
    m_rechargeBatch.clear();
}

void MainWindow::resetCardRechargeState()
{
    if (m_cardRechargeTimer) {
        m_cardRechargeTimer->stop();
    }
    m_cardRechargeState = CardRechargeState::Idle;
    m_cardReaderNormalSeen = false;
    m_cardWaitingReset = false;
    m_lastCardMfcStatus = 0xFF;
    m_lastCardFailureSignature = -1;
    m_rechargeCardType = -1;
    m_rechargeCardQty = 0;
    m_rechargePosition = -1;
    m_rechargeNewTotal = 0;
    m_rechargeBatch.clear();
}

void MainWindow::appendConsumableRechargeHistory(const QString &entry)
{
    QSettings settings;
    QStringList history = settings.value(
                QStringLiteral("consumable/rechargeHistory")).toStringList();
    history.append(entry);
    while (history.size() > 200) {
        history.removeFirst();
    }
    settings.setValue(QStringLiteral("consumable/rechargeHistory"), history);
    settings.sync();
}

void MainWindow::showPendingFunction(const QString &name)
{
    showActionHint(tr("%1：功能待接入").arg(name));
    setActiveTabButton(nullptr);
}


/// 让左侧导航按钮高亮反映当前页：仅保持当前页面对应的按钮为选中态。
void MainWindow::setActiveTabButton(QToolButton *button)
{
    if (m_activeTopButton == button) {
        if (button) {
            button->setChecked(true);
        }
        return;
    }
    if (m_activeTopButton) {
        QSignalBlocker blocker(m_activeTopButton);
        m_activeTopButton->setChecked(false);
    }
    m_activeTopButton = button;
    if (button) {
        QSignalBlocker blocker(button);
        button->setChecked(true);
    }
}

void MainWindow::syncActiveTopButtonToTab()
{
    if (!m_mainTabs) {
        return;
    }
    const int index = m_mainTabs->currentIndex();
    if (index >= 0 && index == m_homeTabIndex) {
        setActiveTabButton(m_homeButton);
    } else if (index >= 0 && index == m_resultsTabIndex) {
        setActiveTabButton(m_resultsButton);
    } else if (index >= 0 && index == m_queryTabIndex) {
        setActiveTabButton(m_queryButton);
    } else if (index >= 0 && index == m_qualityTabIndex) {
        setActiveTabButton(m_qualityButton);
    } else if (index >= 0 && index == m_curveTabIndex) {
        setActiveTabButton(m_curveButton);
    } else if (index >= 0 && index == m_addSampleTabIndex) {
        setActiveTabButton(m_addSampleButton);
    } else if (index >= 0 && index == m_tubeTrayTabIndex) {
        setActiveTabButton(m_trayButton);
    } else if (index >= 0 && index == m_consumablesTabIndex) {
        setActiveTabButton(m_consumablesButton);
    } else if (index >= 0 && index == m_logTabIndex) {
        setActiveTabButton(m_logButton);
    } else if (index >= 0 && index == m_settingsTabIndex) {
        setActiveTabButton(m_settingsButton);
    } else {
        setActiveTabButton(nullptr);
    }
}

void MainWindow::showActionHint(const QString &text)
{
    if (!m_actionHint) {
        return;
    }
    m_actionHint->setText(text);

    if (!m_actionHintTimer) {
        m_actionHintTimer = new QTimer(this);
        m_actionHintTimer->setSingleShot(true);
        m_actionHintTimer->setInterval(3000);
        connect(m_actionHintTimer, &QTimer::timeout, this, [this]() {
            if (m_actionHint) {
                m_actionHint->clear();
            }
        });
    }
    m_actionHintTimer->start();
}

void MainWindow::applyChannelSettings()
{
    if (!m_moduleOverview) {
        return;
    }

    const int moduleCount = machineModuleCount(m_machineModel);
    for (int moduleNumber = 1; moduleNumber <= moduleCount; ++moduleNumber) {
        for (int localChannel = 1; localChannel <= 4; ++localChannel) {
            const int globalChannel = (moduleNumber - 1) * 4 + localChannel;
            m_moduleOverview->setChannelEnabled(
                        moduleNumber, localChannel,
                        channelEnabledFromSettings(m_machineModel,
                                                   globalChannel));
        }
    }
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    frameless::enableNativeResize(this);
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message,
                             long *result)
{
    if (frameless::handleNativeResizeEvent(this, eventType, message, result)) {
        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_mainPage && event->type() == QEvent::Resize
            && m_startupMask) {
        m_startupMask->setGeometry(m_mainPage->rect());
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::createStartupMask()
{
    if (!m_mainPage || m_startupMask) {
        return;
    }

    m_startupMask = new QWidget(m_mainPage);
    m_startupMask->setObjectName(QStringLiteral("startupMask"));
    m_startupMask->setAttribute(Qt::WA_StyledBackground, true);
    m_startupMask->setFocusPolicy(Qt::StrongFocus);
    m_startupMask->setGeometry(m_mainPage->rect());
    m_startupMask->setStyleSheet(QStringLiteral(
        "QWidget#startupMask { background: rgba(12, 31, 49, 205); }"
        "QFrame#startupMaskCard {"
        "  background: #FFFFFF;"
        "  border: 1px solid #D6E5F1;"
        "  border-radius: 18px;"
        "}"
        "QLabel#startupMaskTitle {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 24px;"
        "  font-weight: 700;"
        "}"
        "QLabel#startupMaskDetail {"
        "  color: #5C7488;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "}"
        "QLabel#startupMaskCountdown {"
        "  color: #8AA2B5;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QProgressBar#startupMaskProgress {"
        "  background: #EAF3F9;"
        "  border: 1px solid #D4E5F1;"
        "  border-radius: 6px;"
        "  min-height: 16px;"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 11px;"
        "  text-align: center;"
        "}"
        "QProgressBar#startupMaskProgress::chunk {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "      stop:0 #6CC6F2, stop:1 #2C7BBF);"
        "  border-radius: 5px;"
        "}"));

    auto *rootLayout = new QVBoxLayout(m_startupMask);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addStretch(1);

    auto *cardRow = new QHBoxLayout;
    cardRow->addStretch(1);
    auto *card = new QFrame(m_startupMask);
    card->setObjectName(QStringLiteral("startupMaskCard"));
    card->setFixedWidth(560);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(34, 28, 34, 30);
    cardLayout->setSpacing(14);

    m_startupMaskTitle = new QLabel(tr("模组升温中"), card);
    m_startupMaskTitle->setObjectName(QStringLiteral("startupMaskTitle"));
    m_startupMaskTitle->setAlignment(Qt::AlignCenter);

    m_startupMaskDetail = new QLabel(
                tr("正在将所有模组升温至 37℃…"), card);
    m_startupMaskDetail->setObjectName(QStringLiteral("startupMaskDetail"));
    m_startupMaskDetail->setAlignment(Qt::AlignCenter);
    m_startupMaskDetail->setWordWrap(true);

    m_startupMaskProgress = new QProgressBar(card);
    m_startupMaskProgress->setObjectName(QStringLiteral("startupMaskProgress"));
    m_startupMaskProgress->setRange(0, 100);
    m_startupMaskProgress->setValue(0);
    m_startupMaskProgress->hide();

    m_startupMaskCountdown = new QLabel(card);
    m_startupMaskCountdown->setObjectName(
                QStringLiteral("startupMaskCountdown"));
    m_startupMaskCountdown->setAlignment(Qt::AlignRight);
    m_startupMaskCountdown->hide();

    cardLayout->addWidget(m_startupMaskTitle);
    cardLayout->addWidget(m_startupMaskDetail);
    cardLayout->addWidget(m_startupMaskProgress);
    // 升温倒计时固定在提示框右下角
    cardLayout->addWidget(m_startupMaskCountdown, 0, Qt::AlignRight);

    cardRow->addWidget(card);
    cardRow->addStretch(1);
    rootLayout->addLayout(cardRow);
    rootLayout->addStretch(1);

    m_startupMask->hide();
    m_mainPage->installEventFilter(this);
}

void MainWindow::setMainPageInteractionEnabled(bool enabled)
{
    if (!m_mainPage) {
        return;
    }
    if (QWidget *navBar = m_mainPage->findChild<QWidget *>(
                QStringLiteral("mainNavBar"))) {
        navBar->setEnabled(enabled);
    }
    if (QWidget *actionBar = m_mainPage->findChild<QWidget *>(
                QStringLiteral("runControlRow"))) {
        actionBar->setEnabled(enabled);
    }
    if (m_mainTabs) {
        m_mainTabs->setEnabled(enabled);
    }
    if (QWidget *scroll = m_mainPage->findChild<QWidget *>(
                QStringLiteral("mainContentScroll"))) {
        scroll->setEnabled(enabled);
    }
    if (QWidget *bottomBar = m_mainPage->findChild<QWidget *>(
                QStringLiteral("bottomBar"))) {
        bottomBar->setEnabled(enabled);
    }
}

void MainWindow::sendModuleFrame(int moduleNumber, const QByteArray &frame,
                                 const QString &description)
{
    Q_UNUSED(moduleNumber);
    if (!m_commWorker || frame.isEmpty()) {
        return;
    }
    QMetaObject::invokeMethod(m_commWorker, "sendFrameRequested",
                              Qt::QueuedConnection,
                              Q_ARG(QByteArray, frame),
                              Q_ARG(QString, description));
}

void MainWindow::setStartupMaskStatus(const QString &title,
                                      const QString &detail,
                                      bool progressVisible, int progress)
{
    if (m_startupMaskTitle) {
        m_startupMaskTitle->setText(title);
    }
    if (m_startupMaskDetail) {
        m_startupMaskDetail->setText(detail);
    }
    if (m_startupMaskProgress) {
        m_startupMaskProgress->setVisible(progressVisible);
        m_startupMaskProgress->setValue(qBound(0, progress, 100));
    }
    if (m_startupMask) {
        if (m_mainPage) {
            m_startupMask->setGeometry(m_mainPage->rect());
        }
        m_startupMask->show();
        m_startupMask->raise();
        m_startupMask->setFocus();
    }
}

void MainWindow::startSimulatedProgress(int durationMs,
                                        const std::function<void()> &onDone)
{
    if (!m_startupMaskProgress) {
        if (onDone) {
            onDone();
        }
        return;
    }

    m_startupProgressDurationMs = qMax(1, durationMs);
    m_startupProgressDone = onDone;
    m_startupProgressClock.start();
    m_startupMaskProgress->setVisible(true);
    m_startupMaskProgress->setValue(0);

    if (!m_startupProgressTimer) {
        m_startupProgressTimer = new QTimer(this);
        m_startupProgressTimer->setInterval(50);
        connect(m_startupProgressTimer, &QTimer::timeout,
                this, &MainWindow::handleStartupProgressTick);
    }
    m_startupProgressTimer->start();
}

void MainWindow::handleStartupProgressTick()
{
    if (!m_startupMaskProgress) {
        return;
    }
    const int percent = qBound(
                0,
                static_cast<int>(m_startupProgressClock.elapsed() * 100
                                 / qMax(1, m_startupProgressDurationMs)),
                100);
    m_startupMaskProgress->setValue(percent);
    if (percent < 100) {
        return;
    }

    if (m_startupProgressTimer) {
        m_startupProgressTimer->stop();
    }
    const std::function<void()> done = m_startupProgressDone;
    m_startupProgressDone = nullptr;
    if (done) {
        done();
    }
}

bool MainWindow::allModulesAtTargetTemperature() const
{
    const int moduleCount = machineModuleCount(m_machineModel);
    if (moduleCount <= 0) {
        return false;
    }
    for (int moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex) {
        if (!m_latestModuleDataValid[moduleIndex]
                || !m_latestTemperatureValid[moduleIndex]) {
            return false;
        }
        const double target =
                moduleTargetTemperatureFromSettings(moduleIndex + 1);
        if (m_latestTemperature[moduleIndex] < target - 0.001) {
            return false;
        }
    }
    return true;
}

void MainWindow::maybeStartStartupCalibration()
{
    if (m_skipStartupCalibration) {
        startTestModePreparation();
        return;
    }
    if (!m_hasLoggedIn || !m_startupConnected || !m_commWorker) {
        return;
    }
    if (m_startupState != StartupCalibrationState::Idle
            || m_startupCalibrationAttempted) {
        return;
    }
    // 测试进行中掉线重连时不要打断测试去做灌注/升温/调光。
    if (testingActive()) {
        return;
    }
    startStartupCalibration();
}

void MainWindow::startTestModePreparation()
{
    if (!m_hasLoggedIn || !m_startupConnected || !m_commWorker) {
        return;
    }

    // test 模式跳过灌注，但主板/模组周期采集仍要启动，否则界面一直显示
    // 未连接、通道没有 AD/温度，模组状态也不会刷新。
    if (!m_testModePollingResumed) {
        m_testModePollingResumed = true;
        QMetaObject::invokeMethod(m_commWorker, "resumePeriodicPolling",
                                  Qt::QueuedConnection);
        qInfo() << "test 用户：跳过开机灌注，恢复主板/模组周期轮询";
    }

    // 没有灌注就没有泵的已知位置。这里只把两泵排空到 0 建立零位，
    // 让开始测试/清洗可以按绝对位置计算步数，不动 Z/XY 和液路。
    PumpPositionTracker &pumps = PumpPositionTracker::instance();
    if (pumps.sampleValid() && pumps.reagentValid()) {
        return;
    }
    if (m_testModePumpEmptyStarted || !m_perfusionController
            || m_perfusionController->isBusy()) {
        return;
    }
    if (m_cleaningController && m_cleaningController->isBusy()) {
        return;
    }
    if (m_resetController && m_resetController->isBusy()) {
        return;
    }
    if (m_verificationController && m_verificationController->isBusy()) {
        return;
    }
    if (testingActive()) {
        return;
    }

    m_testModePumpEmptyStarted = true;
    showActionHint(tr("test 模式：正在排空样本泵/试剂泵，建立泵零位…"));
    // 泵动作期间暂停 0x16 周期采集，避免和 0x17 动作应答抢处理；
    // 排空结束的 finished 处理里会统一恢复轮询。
    QMetaObject::invokeMethod(m_commWorker, "pausePeriodicPolling",
                              Qt::QueuedConnection);
    m_perfusionController->startPumpEmpty();
}

void MainWindow::startStartupCalibration()
{
    if (m_startupState != StartupCalibrationState::Idle || !m_commWorker
            || m_startupCalibrationAttempted
            || !m_hasLoggedIn || !m_startupConnected) {
        return;
    }

    // 测试进行中掉线重连时不要打断测试去做灌注/升温/调光。
    if (testingActive()) {
        return;
    }
    m_startupCalibrationAttempted = true;

    m_dimmingCheckWaitCount = 0;
    m_cupRetryCount = 0;
    m_ledCurrentReadLocked = false;
    m_dimmingPreviousValid = false;
    m_dimmingStableSamples = 0;
    for (int moduleIndex = 0; moduleIndex < 3; ++moduleIndex) {
        m_ledTuningSettleClock[moduleIndex].invalidate();
        m_ledTuningConverged[moduleIndex] = false;
        for (int channelIndex = 0; channelIndex < 4; ++channelIndex) {
            m_ledCurrentValid[moduleIndex][channelIndex] = false;
            m_invalidAdCount[moduleIndex][channelIndex] = 0;
        }
    }

    setMainPageInteractionEnabled(false);
    beginStartupPerfusion();
}

void MainWindow::beginStartupPerfusion()
{
    m_startupState = StartupCalibrationState::Perfusion;
    updateConsumablesOperationState();
    setStartupMaskStatus(
                tr("开机灌注中"),
                tr("正在复位三根 Z 轴，随后灌注样本泵和试剂泵；"
                   "请勿操作仪器。"),
                true, 0);

    if (!m_perfusionController) {
        abortStartupCalibration(tr("灌注控制器未初始化"), true);
        return;
    }
    // 基础设置里的泵清洗时间（老软件 Config.ini 的 WashesTime）在每次灌注
    // 启动时读取，修改后下次开机灌注生效。
    m_perfusionController->setCleaningTimeMs(
                perfusionCleaningTimeMsFromSettings());

    // 与老软件一致：开机灌注（老软件 boot clean）完成前不启动主板/模组
    // 周期采集。灌注期间总线上只保留 0x17 运动命令，避免 0x16 轮询
    // 读命令与主板 XY/泵动作抢处理，导致复位完成应答丢失。
    // 该调用与灌注首帧同走通讯线程队列，入队顺序保证先暂停再发首帧。
    qInfo() << "开机灌注开始（灌注完成前不启动主板/模组周期轮询）";
    if (m_commWorker) {
        QMetaObject::invokeMethod(m_commWorker, "pausePeriodicPolling",
                                  Qt::QueuedConnection);
    }
    // 老软件灌注动作的命令号从 0 开始（00、01、02、03…），
    // 这里也从头开始，避免第一条主板 0x17 动作使用 04 被固件忽略。
    if (m_motionController) {
        m_motionController->protocol().resetCommandNumber(0);
    }
    m_perfusionController->start();
}

void MainWindow::beginStartupHeating()
{
    if (m_startupState != StartupCalibrationState::Perfusion) {
        return;
    }

    m_startupState = StartupCalibrationState::Heating;
    setStartupMaskStatus(
                tr("模组升温中"),
                tr("正在将各模组升温至设定温度，达到后开始调光。"),
                false, 0);

    // 灌注期间模组 AD 轮询是暂停的，先作废旧温度值，
    // 等恢复轮询后的新样本再判断是否到设定目标温度。
    for (int moduleIndex = 0; moduleIndex < 3; ++moduleIndex) {
        m_latestTemperatureValid[moduleIndex] = false;
    }

    const int moduleCount = machineModuleCount(m_machineModel);
    for (int moduleNumber = 1; moduleNumber <= moduleCount; ++moduleNumber) {
        platelet::DetectionModule &module =
                m_moduleProtocols[moduleNumber - 1];
        const double target =
                moduleTargetTemperatureFromSettings(moduleNumber);
        module.setTargetTemperature(target);
        sendModuleFrame(moduleNumber,
                        module.setTemperature(target),
                        tr("设置模组%1目标温度 %2℃")
                        .arg(moduleNumber)
                        .arg(target, 0, 'f', 2));
    }

    // 升温超时时间从基础设置读取（默认 60 秒），并显示倒计时
    m_heatingTimeoutMs =
            qMax(1, moduleHeatingTimeoutSecondsFromSettings()) * 1000;
    m_startupHeatingClock.start();
    if (m_startupMaskCountdown) {
        const int totalSeconds = m_heatingTimeoutMs / 1000;
        m_startupMaskCountdown->setText(
                    tr("剩余 %1:%2")
                    .arg(totalSeconds / 60, 2, 10, QLatin1Char('0'))
                    .arg(totalSeconds % 60, 2, 10, QLatin1Char('0')));
        m_startupMaskCountdown->show();
        m_startupMaskCountdown->raise();
    }

    if (!m_startupCheckTimer) {
        m_startupCheckTimer = new QTimer(this);
        m_startupCheckTimer->setInterval(500);
        connect(m_startupCheckTimer, &QTimer::timeout,
                this, &MainWindow::handleStartupTick);
    }
    m_startupCheckTimer->start();
}

void MainWindow::handleStartupTick()
{
    if (m_startupState != StartupCalibrationState::Heating) {
        if (m_startupCheckTimer) {
            m_startupCheckTimer->stop();
        }
        return;
    }

    if (allModulesAtTargetTemperature()) {
        if (m_startupCheckTimer) {
            m_startupCheckTimer->stop();
        }
        if (m_startupMaskCountdown) {
            m_startupMaskCountdown->hide();
        }
        beginStartupDimming();
        return;
    }

    const qint64 elapsedMs = m_startupHeatingClock.elapsed();
    if (m_startupMaskCountdown) {
        const int remainingSeconds = qMax(
                    0, m_heatingTimeoutMs - static_cast<int>(elapsedMs)) / 1000;
        m_startupMaskCountdown->setText(
                    tr("剩余 %1:%2")
                    .arg(remainingSeconds / 60, 2, 10, QLatin1Char('0'))
                    .arg(remainingSeconds % 60, 2, 10, QLatin1Char('0')));
    }

    if (elapsedMs >= m_heatingTimeoutMs) {
        if (m_startupCheckTimer) {
            m_startupCheckTimer->stop();
        }
        if (m_startupMaskCountdown) {
            m_startupMaskCountdown->hide();
        }
        abortStartupCalibration(
                    tr("模组升温超时，%1 秒内未达到 37℃，"
                       "已停止启动校准。")
                    .arg(m_heatingTimeoutMs / 1000),
                    true);
    }
}

void MainWindow::beginStartupDimming()
{
    if (!m_commWorker) {
        return;
    }

    m_startupState = StartupCalibrationState::Dimming;
    m_dimmingCheckWaitCount = 0;
    m_cupRetryCount = 0;
    if (m_startupMaskCountdown) {
        m_startupMaskCountdown->hide();
    }
    setStartupMaskStatus(
                tr("控温调光中"),
                tr("正在把各通道 AD 对齐到 3300；偏差超过 ±100 时会提示检查残留试杯。"),
                false, 0);

    const int moduleCount = machineModuleCount(m_machineModel);
    for (int moduleNumber = 1; moduleNumber <= moduleCount; ++moduleNumber) {
        sendModuleFrame(moduleNumber,
                        m_moduleProtocols[moduleNumber - 1].setDimming(
                            static_cast<quint16>(kDimmingTargetAd)),
                        tr("模组%1 调光 3300").arg(moduleNumber));
    }

    m_dimmingSettleClock.start();
    m_dimmingPreviousValid = false;
    m_dimmingStableSamples = 0;
    QTimer::singleShot(1000, this, &MainWindow::checkStartupDimming);
}

bool MainWindow::dimmingAdSettled()
{
    const int moduleCount = machineModuleCount(m_machineModel);
    const qint64 elapsed = m_dimmingSettleClock.elapsed();
    if (elapsed >= kDimmingSettleMaxMs) {
        return true;
    }

    if (!m_dimmingPreviousValid) {
        for (int moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex) {
            for (int channelIndex = 0; channelIndex < 4; ++channelIndex) {
                m_dimmingPreviousAd[moduleIndex][channelIndex] =
                        m_latestChannelAd[moduleIndex][channelIndex];
            }
        }
        m_dimmingPreviousValid = true;
        m_dimmingStableSamples = 0;
        return false;
    }

    bool stable = true;
    for (int moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex) {
        for (int channelIndex = 0; channelIndex < 4; ++channelIndex) {
            const int ad = m_latestChannelAd[moduleIndex][channelIndex];
            if (qAbs(ad - m_dimmingPreviousAd[moduleIndex][channelIndex])
                    > kDimmingStableDelta) {
                stable = false;
            }
            m_dimmingPreviousAd[moduleIndex][channelIndex] = ad;
        }
    }

    m_dimmingStableSamples = stable ? m_dimmingStableSamples + 1 : 0;
    return elapsed >= kDimmingSettleMinMs
            && m_dimmingStableSamples >= kDimmingStableSamples;
}

void MainWindow::checkStartupDimming()
{
    if (m_startupState != StartupCalibrationState::Dimming) {
        return;
    }

    const int moduleCount = machineModuleCount(m_machineModel);
    bool dataReady = true;
    for (int moduleNumber = 1; moduleNumber <= moduleCount; ++moduleNumber) {
        const int moduleIndex = moduleNumber - 1;
        if (!m_latestModuleDataValid[moduleIndex]) {
            dataReady = false;
            continue;
        }
        for (int localChannel = 1; localChannel <= 4; ++localChannel) {
            if (m_latestChannelAd[moduleIndex][localChannel - 1] <= 0) {
                dataReady = false;
            }
        }
    }

    if (!dataReady) {
        if (++m_dimmingCheckWaitCount <= 6) {
            QTimer::singleShot(1000, this, &MainWindow::checkStartupDimming);
        } else {
            abortStartupCalibration(tr("等待通道 AD 数据超时"), true);
        }
        return;
    }

    QStringList failedChannels;
    for (int moduleNumber = 1; moduleNumber <= moduleCount; ++moduleNumber) {
        const int moduleIndex = moduleNumber - 1;
        for (int localChannel = 1; localChannel <= 4; ++localChannel) {
            const int ad = m_latestChannelAd[moduleIndex][localChannel - 1];
            if (qAbs(ad - kDimmingTargetAd) > kDimmingResidualThreshold) {
                failedChannels << tr("模组%1 CH%2：AD=%3")
                                  .arg(moduleNumber)
                                  .arg((moduleNumber - 1) * 4 + localChannel)
                                  .arg(ad);
            }
        }
    }

    const bool settled = dimmingAdSettled();
    const bool withinRange = failedChannels.isEmpty();
    const bool minWaitElapsed =
            m_dimmingSettleClock.elapsed() >= kDimmingSettleMinMs;

    // AD 还在慢慢爬升时不要急着判断；全部进入 ±100 且等够最短时间后可以直接微调。
    if (!settled && !(withinRange && minWaitElapsed)) {
        setStartupMaskStatus(
                    tr("控温调光中"),
                    tr("正在等待通道 AD 稳定，调光值上升需要时间…"),
                    false, 0);
        QTimer::singleShot(1000, this, &MainWindow::checkStartupDimming);
        return;
    }

    if (!withinRange) {
        if (m_cupRetryCount >= kMaxCupRetries) {
            abortStartupCalibration(
                        tr("多次丢杯后通道 AD 仍超出 ±100：\n%1")
                        .arg(failedChannels.join(QLatin1Char('\n'))),
                        true);
            return;
        }
        promptResidualCups(failedChannels);
        return;
    }

    beginLedCurrentRead();
}

void MainWindow::promptResidualCups(const QStringList &failedChannels)
{
    if (m_startupState != StartupCalibrationState::Dimming) {
        return;
    }

    MedicalMessageBox box(this);
    box.applyMedicalStyle();
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("疑似残留试杯"));
    box.setText(tr("以下通道 AD 与 3300 的偏差超过 ±100，可能有遗留试杯："));
    box.setInformativeText(
                failedChannels.join(QLatin1Char('\n'))
                + tr("\n\n丢杯动作尚未接入。点击“确定”代表已完成这些通道的"
                     "丢杯并重新调光；点击“取消”停止启动调光。"));
    box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Ok);
    if (QAbstractButton *ok = box.button(QMessageBox::Ok)) {
        ok->setText(tr("确定（模拟丢杯）"));
    }
    if (QAbstractButton *cancel = box.button(QMessageBox::Cancel)) {
        cancel->setText(tr("取消"));
    }

    if (box.exec() != QMessageBox::Ok) {
        abortStartupCalibration(tr("用户取消调光"), true);
        return;
    }
    if (m_startupState != StartupCalibrationState::Dimming) {
        return;   // 弹窗期间断线/中止
    }

    ++m_cupRetryCount;
    setStartupMaskStatus(
                tr("正在丢弃疑似残留试杯…"),
                tr("丢杯动作尚未接入，使用模拟进度占位。"),
                true, 0);
    startSimulatedProgress(kDiscardSimulatedMs, [this]() {
        if (m_startupState != StartupCalibrationState::Dimming) {
            return;
        }
        const int moduleCount = machineModuleCount(m_machineModel);
        for (int moduleNumber = 1; moduleNumber <= moduleCount; ++moduleNumber) {
            sendModuleFrame(moduleNumber,
                            m_moduleProtocols[moduleNumber - 1].setDimming(
                                static_cast<quint16>(kDimmingTargetAd)),
                            tr("丢杯后模组%1重新调光 3300").arg(moduleNumber));
        }
        m_dimmingCheckWaitCount = 0;
        m_dimmingSettleClock.start();
        m_dimmingPreviousValid = false;
        m_dimmingStableSamples = 0;
        setStartupMaskStatus(
                    tr("控温调光中"),
                    tr("正在重新对齐各通道 AD 到 3300…"),
                    false, 0);
        QTimer::singleShot(1000, this, &MainWindow::checkStartupDimming);
    });
}

void MainWindow::beginLedCurrentRead()
{
    if (m_startupState != StartupCalibrationState::Dimming) {
        return;
    }

    m_startupState = StartupCalibrationState::ReadingLedCurrent;
    setStartupMaskStatus(
                tr("控温调光中"),
                tr("正在读取各通道当前 LED 电流，读取后开始微调…"),
                false, 0);

    const int moduleCount = machineModuleCount(m_machineModel);
    for (int moduleNumber = 1; moduleNumber <= moduleCount; ++moduleNumber) {
        const int moduleIndex = moduleNumber - 1;
        for (int channelIndex = 0; channelIndex < 4; ++channelIndex) {
            m_ledCurrentValid[moduleIndex][channelIndex] = false;
        }

        platelet::DetectionModule &module =
                m_moduleProtocols[moduleIndex];
        sendModuleFrame(moduleNumber, module.readLedCurrent(),
                        tr("读取模组%1 LED 电流").arg(moduleNumber));
    }

    // 给电流回读留出时间；没有回读的通道在微调时从 300 起调。
    QTimer::singleShot(1200, this, [this]() {
        if (m_startupState == StartupCalibrationState::ReadingLedCurrent) {
            beginLedCurrentTuning();
        }
    });
}

void MainWindow::beginLedCurrentTuning()
{
    if (m_startupState != StartupCalibrationState::ReadingLedCurrent) {
        return;
    }

    m_startupState = StartupCalibrationState::LedTuning;
    m_ledCurrentReadLocked = true;
    setStartupMaskStatus(
                tr("控温调光中"),
                tr("通道已对齐到 3300 附近，正在逐个微调 LED 电流（允许误差 ±3）。"),
                false, 0);

    const int moduleCount = machineModuleCount(m_machineModel);
    for (int moduleNumber = 1; moduleNumber <= moduleCount; ++moduleNumber) {
        const int moduleIndex = moduleNumber - 1;
        m_ledTuningSettleClock[moduleIndex].invalidate();
        m_ledTuningConverged[moduleIndex] = false;

        bool sent = false;
        for (int localChannel = 1; localChannel <= 4; ++localChannel) {
            const int channelIndex = localChannel - 1;
            m_invalidAdCount[moduleIndex][channelIndex] = 0;
            if (m_ledCurrentValid[moduleIndex][channelIndex]) {
                continue;
            }
            // 没有读回电流时使用 300 作为起点，再按 AD 逐点调整。
            m_ledCurrent[moduleIndex][channelIndex] = 300;
            m_ledCurrentValid[moduleIndex][channelIndex] = true;
            sendModuleFrame(
                        moduleNumber,
                        m_moduleProtocols[moduleIndex].setChannelLedCurrent(
                            localChannel, 300),
                        tr("模组%1 CH%2 LED 电流初始化为 300")
                        .arg(moduleNumber)
                        .arg((moduleNumber - 1) * 4 + localChannel));
            sent = true;
        }
        if (sent) {
            m_ledTuningSettleClock[moduleIndex].start();
        }
    }
}

void MainWindow::processLedTuningData(int moduleNumber)
{
    if (m_startupState != StartupCalibrationState::LedTuning) {
        return;
    }

    const int moduleIndex = moduleNumber - 1;
    const int moduleCount = machineModuleCount(m_machineModel);
    if (moduleIndex < 0 || moduleIndex >= moduleCount
            || m_ledTuningConverged[moduleIndex]) {
        return;
    }
    const QElapsedTimer &settleClock = m_ledTuningSettleClock[moduleIndex];
    if (settleClock.isValid()
            && settleClock.elapsed() < kLedCurrentSettleMs) {
        // 刚下发过电流调整，AD 会慢慢变化，等稳定时间后再判断。
        return;
    }

    bool converged = true;
    bool changed = false;
    bool currentChanged[4] = { false, false, false, false };
    QStringList errors;

    for (int channelIndex = 0; channelIndex < 4; ++channelIndex) {
        const int ad = m_latestChannelAd[moduleIndex][channelIndex];
        const int globalChannel = (moduleNumber - 1) * 4 + channelIndex + 1;
        if (ad <= 0) {
            if (++m_invalidAdCount[moduleIndex][channelIndex]
                    > kMaxInvalidAdSamples) {
                errors << tr("模组%1 CH%2 未读取到有效 AD")
                          .arg(moduleNumber).arg(globalChannel);
            }
            converged = false;
            continue;
        }
        m_invalidAdCount[moduleIndex][channelIndex] = 0;

        int current = m_ledCurrent[moduleIndex][channelIndex];
        if (ad < kDimmingTargetAd - kLedTuningTolerance) {
            if (current >= static_cast<int>(
                        platelet::DetectionModule::LedCurrentMax)) {
                errors << tr("模组%1 CH%2 在 LED 电流 600 时 AD 仍只有 %3，"
                             "无法对准 3300")
                          .arg(moduleNumber).arg(globalChannel).arg(ad);
            } else {
                ++current;
                changed = true;
                currentChanged[channelIndex] = true;
                converged = false;
            }
        } else if (ad > kDimmingTargetAd + kLedTuningTolerance) {
            if (current <= static_cast<int>(
                        platelet::DetectionModule::LedCurrentMin)) {
                errors << tr("模组%1 CH%2 在 LED 电流 0 时 AD 仍有 %3，"
                             "无法对准 3300")
                          .arg(moduleNumber).arg(globalChannel).arg(ad);
            } else {
                --current;
                changed = true;
                currentChanged[channelIndex] = true;
                converged = false;
            }
        }
        m_ledCurrent[moduleIndex][channelIndex] = current;
    }

    if (!errors.isEmpty()) {
        abortStartupCalibration(errors.join(QLatin1Char('\n')), true);
        return;
    }

    if (changed) {
        for (int channelIndex = 0; channelIndex < 4; ++channelIndex) {
            if (!currentChanged[channelIndex]) {
                continue;
            }
            const int globalChannel =
                    (moduleNumber - 1) * 4 + channelIndex + 1;
            sendModuleFrame(
                        moduleNumber,
                        m_moduleProtocols[moduleIndex].setChannelLedCurrent(
                            channelIndex + 1,
                            static_cast<quint16>(
                                m_ledCurrent[moduleIndex][channelIndex])),
                        tr("模组%1 CH%2 LED 电流 -> %3")
                        .arg(moduleNumber).arg(globalChannel)
                        .arg(m_ledCurrent[moduleIndex][channelIndex]));
        }
        m_ledTuningSettleClock[moduleIndex].start();
    }

    if (!converged) {
        return;
    }

    m_ledTuningConverged[moduleIndex] = true;
    for (int index = 0; index < moduleCount; ++index) {
        if (!m_ledTuningConverged[index]) {
            return;
        }
    }
    finishLedCurrentTuning();
}

void MainWindow::finishLedCurrentTuning()
{
    if (m_startupState != StartupCalibrationState::LedTuning) {
        return;
    }

    m_startupState = StartupCalibrationState::Saving;
    setStartupMaskStatus(
                tr("正在保存模组参数…"),
                tr("LED 电流已对准 3300，正在保存参数；下次开机可直接使用。"),
                false, 0);

    const int moduleCount = machineModuleCount(m_machineModel);
    for (int moduleNumber = 1; moduleNumber <= moduleCount; ++moduleNumber) {
        sendModuleFrame(moduleNumber,
                        m_moduleProtocols[moduleNumber - 1].saveParameters(),
                        tr("保存模组%1参数").arg(moduleNumber));
    }

    QTimer::singleShot(1000, this, [this]() {
        if (m_startupState != StartupCalibrationState::Saving) {
            return;
        }
        // 校准成功且蒙版已撤掉后回到 Idle，清洗/开始测试才允许执行。
        m_startupState = StartupCalibrationState::Idle;
        updateConsumablesOperationState();
        if (m_startupMask) {
            m_startupMask->hide();
        }
        setMainPageInteractionEnabled(true);
        showActionHint(tr("模组升温、调光与参数保存完成"));
    });
}

void MainWindow::handleStartupLedCurrentFrame(const QByteArray &frame)
{
    if (m_ledCurrentReadLocked) {
        return;
    }

    QString error;
    const platelet::DetectionModule::Frame parsed =
            platelet::DetectionModule::parse(frame, &error);
    if (!parsed.valid || !parsed.isLedCurrentReport()) {
        return;
    }

    const int moduleNumber = int(parsed.slaveAddress);
    if (moduleNumber < 1 || moduleNumber > 3) {
        return;
    }

    int values[4] = { 0, 0, 0, 0 };
    for (int channelIndex = 0; channelIndex < 4; ++channelIndex) {
        const quint16 value = parsed.channel(channelIndex + 1);
        if (value > platelet::DetectionModule::LedCurrentMax) {
            // 可能是调光写命令的回执（通道值为 2000~3300），不是电流回读。
            return;
        }
        values[channelIndex] = value;
    }
    for (int channelIndex = 0; channelIndex < 4; ++channelIndex) {
        m_ledCurrent[moduleNumber - 1][channelIndex] = values[channelIndex];
        m_ledCurrentValid[moduleNumber - 1][channelIndex] = true;
    }
}

void MainWindow::abortStartupCalibration(const QString &reason,
                                         bool userVisible)
{
    if (m_startupState == StartupCalibrationState::Idle) {
        return;
    }

    if (m_startupCheckTimer) {
        m_startupCheckTimer->stop();
    }
    if (m_startupProgressTimer) {
        m_startupProgressTimer->stop();
    }
    m_startupProgressDone = nullptr;
    m_ledCurrentReadLocked = false;
    if (m_startupMaskCountdown) {
        m_startupMaskCountdown->hide();
    }

    if (m_startupMask) {
        m_startupMask->hide();
    }
    setMainPageInteractionEnabled(true);

    if (userVisible) {
        m_startupState = StartupCalibrationState::Failed;
        if (!reason.isEmpty()) {
            MedicalMessageBox::warning(this, tr("启动校准未完成"), reason);
        }
    } else {
        // 断线：本次运行已经尝试过启动校准，重连后不再重复
        // 灌注/升温/调光；程序重启后才会按第一次连接重新执行。
        m_startupState = StartupCalibrationState::Idle;
    }

    // 状态已不是 Cleaning，cancel() 触发的 finished 不会再次进入本函数。
    if (m_perfusionController && m_perfusionController->isBusy()) {
        m_perfusionController->cancel();
    }
    updateConsumablesOperationState();
}

void MainWindow::startInstrumentConnection()
{
    if (m_connectionStarted) {
        return;
    }
    m_connectionStarted = true;

    setupCommunicationThread();
    m_connectDeadlineTimer->start();

    setConnectionStatus(tr("● 正在查找仪器…"), "searching",
                        tr("VID 0x0483 / PID 0x5740，波特率 1000000"));
}

void MainWindow::setupCommunicationThread()
{
    if (m_commThread) {
        return;
    }

    m_commThread = new QThread(this);
    m_commWorker = new CommunicationWorker;
    m_commWorker->moveToThread(m_commThread);
    if (m_motionController) {
        m_motionController->setCommunicationWorker(m_commWorker);
    }

    connect(m_commThread, &QThread::started,
            m_commWorker, &CommunicationWorker::startCommunication);
    connect(m_commWorker, &CommunicationWorker::connectedChanged,
            this, &MainWindow::updateSerialStatus);
    connect(m_commWorker, &CommunicationWorker::errorOccurred,
            this, &MainWindow::handleSerialError);

    // 每秒滤波后的通道 AD 同步到主界面模组玻璃管口；
    // 测试状态下也会自动追加到该通道的测试曲线。
    connect(m_commWorker, &CommunicationWorker::moduleAdUpdated,
            this, [this](const ModuleAdPoller::ModuleAdData &data) {
        const int moduleIndex = data.moduleNumber - 1;
        if (moduleIndex >= 0 && moduleIndex < 3) {
            m_latestModuleDataValid[moduleIndex] = true;
            m_latestTemperatureValid[moduleIndex] = data.temperatureValid;
            m_latestTemperature[moduleIndex] =
                    data.temperatureValid ? data.temperatureCelsius : -1.0;
            for (int channelIndex = 0; channelIndex < 4; ++channelIndex) {
                const bool valid = data.channelsValid
                        && channelIndex < data.channels.size()
                        && data.channels.at(channelIndex) > 0;
                m_latestChannelAd[moduleIndex][channelIndex] =
                        valid ? data.channels.at(channelIndex) : 0;
            }
        }

        if (m_startupState == StartupCalibrationState::LedTuning) {
            processLedTuningData(data.moduleNumber);
        }

        if (!m_moduleOverview) {
            return;
        }
        m_moduleOverview->setModuleTemperature(
                    data.moduleNumber,
                    data.temperatureValid ? data.temperatureCelsius : -1.0);
        for (int channel = 1; channel <= 4; ++channel) {
            const int index = channel - 1;
            const int rawValue = (data.channelsValid
                                  && index < data.channels.size())
                    ? static_cast<int>(data.channels.at(index))
                    : 0;
            // 0 表示本秒没有有效样本，界面显示 AD --
            m_moduleOverview->setChannelAdValue(data.moduleNumber, channel,
                                                rawValue > 0 ? rawValue : -1);
            if (m_testScheduler) {
                m_testScheduler->handleChannelAd(
                            data.moduleNumber, channel,
                            rawValue > 0 ? rawValue : -1);
            }
        }
    });

    connect(m_commWorker, &CommunicationWorker::mainBoardStatusUpdated,
            this, &MainWindow::updateMainboardStatus);
    connect(m_commWorker, &CommunicationWorker::mainBoardConsumableUpdated,
            this, &MainWindow::updateConsumableAmount);
    connect(m_commWorker, &CommunicationWorker::cardRechargeResult,
            this, &MainWindow::handleCardRechargeResult);
    connect(m_commWorker, &CommunicationWorker::cardRechargeCommandSent,
            this, [this](bool ok, const QString &error) {
        if (!ok && m_cardRechargeState == CardRechargeState::Writing) {
            finishCardRecharge(
                        false,
                        tr("充值命令发送失败：%1")
                        .arg(error.isEmpty() ? tr("未知错误") : error));
        }
    });
    connect(m_commWorker, &CommunicationWorker::frameReceived,
            this, &MainWindow::handleStartupLedCurrentFrame);

    m_commThread->start();

    // 通讯线程启动后把当前机型应用下去
    QMetaObject::invokeMethod(m_commWorker, "setMachineModel",
                              Qt::QueuedConnection,
                              Q_ARG(MachineModel, m_machineModel));
}

void MainWindow::shutdownCommunicationThread()
{
    if (m_commThread && m_commThread->isRunning()) {
        if (m_commWorker) {
            m_commWorker->disconnect(this);
            QMetaObject::invokeMethod(m_commWorker, "stopCommunication",
                                      Qt::BlockingQueuedConnection);
        }
        m_commThread->quit();
        m_commThread->wait();
    }
    delete m_commWorker;
    m_commWorker = nullptr;
    // m_commThread 的父对象是本窗口，随窗口析构
}

void MainWindow::handleStartupTimeout()
{
    if (m_startupConnected) {
        return;
    }

    m_startupTimedOut = true;
    setConnectionStatus(tr("● 串口连接异常，正在重试…"), "offline",
                        tr("启动后 30 秒内未检测到仪器，通讯线程仍会自动重连。"));

    MedicalMessageBox::warning(this, tr("串口连接异常"),
                         tr("启动后 30 秒内未检测到仪器（VID 0x0483 / PID 0x5740）。\n\n"
                            "请检查仪器电源、USB 数据线和驱动。\n"
                            "程序会继续自动重连；如果 30 秒后才连接上，"
                            "请关闭软件后重新启动。"));
}

void MainWindow::updateSerialStatus(bool connected, const QString &portName,
                                    qint32 baudRate)
{
    if (!m_connectionStarted) {
        return;
    }
    m_serialConnected = connected;
    resetCardRechargeState();
    updateConsumablesOperationState();

    if (!connected) {
        abortStartupCalibration(QString(), false);
        m_startupCalibrationAttempted = false;
        for (int position = 0;
             position < MainBoardConsumable::kPositionCount; ++position) {
            m_consumableValid[position] = false;
            m_consumableRemainingValid[position] = false;
            m_lastSeenConsumableRatio[position] = -1;
            m_lastWrittenConsumableRatio[position] = -1;
            m_consumableBottles[position] = 0;
            m_consumableRatios[position] = 0;
        }
        if (m_reagentPanel) {
            m_reagentPanel->clearLevels();
        }
        if (m_reagentPanel) {
            m_reagentPanel->setConnected(false);
        }
        if (m_consumablesDialog) {
            m_consumablesDialog->clearConsumableAmounts();
        }
        for (int moduleIndex = 0; moduleIndex < 3; ++moduleIndex) {
            m_latestModuleDataValid[moduleIndex] = false;
            m_latestTemperatureValid[moduleIndex] = false;
        }
    }

    if (m_boardIndicators) {
        m_boardIndicators->clearStatus(
                    connected ? tr("已连接，等待主板状态…")
                              : tr("串口未连接，等待主板状态…"));
    }
    m_hasMainboardStatus = false;
    if (m_consumablesDialog && m_consumablesDialog->isVisible()) {
        m_consumablesDialog->clearStatus(
                    connected ? tr("已连接，等待主板状态…")
                              : tr("串口未连接，等待主板状态…"));
    }

    if (connected) {
        setMainboardStatusText(tr("等待状态…"), "warning",
                               tr("串口已连接，等待主板每秒状态帧。"));
    } else {
        setMainboardStatusText(tr("未连接"), "offline",
                               tr("串口未连接，无法读取主板状态。"));
        // 断线后清空模组温度，避免保留过期值
        if (m_moduleOverview) {
            const int moduleCount = machineModuleCount(m_machineModel);
            for (int moduleNumber = 1; moduleNumber <= moduleCount;
                 ++moduleNumber) {
                m_moduleOverview->setModuleTemperature(moduleNumber, -1.0);
            }
        }
    }

    if (connected) {
        const QString port = portName.isEmpty() ? tr("仪器") : portName;

        if (!m_startupConnected) {
            if (m_startupTimedOut) {
                // 30 秒之后才连上：按约定提示关闭软件重新连接
                setConnectionStatus(tr("● 仪器已连接 %1，请重启软件").arg(port),
                                    "offline",
                                    tr("启动连接已超时，请关闭软件后重新启动。"));
                if (!m_lateRestartPromptShown) {
                    m_lateRestartPromptShown = true;
                    MedicalMessageBox::warning(this, tr("检测到仪器"),
                                         tr("启动后 30 秒内未连接仪器，现在检测到 %1 已连接。\n\n"
                                            "请关闭软件后重新启动，以保证仪器初始化正确。")
                                         .arg(port));
                }
                qWarning() << "仪器在启动超时后才连接:" << port;
                return;
            }

            m_startupConnected = true;
            if (m_connectDeadlineTimer) {
                m_connectDeadlineTimer->stop();
            }
        }

        setConnectionStatus(tr("● 仪器已连接 %1").arg(port), "online",
                            tr("仪器串口：%1\nVID 0x0483 / PID 0x5740\n波特率：%2")
                            .arg(port)
                            .arg(baudRate));
        if (m_reagentPanel) {
            m_reagentPanel->setConnected(true);
        }
        qInfo() << "仪器串口已连接:" << port
                << "波特率:" << baudRate;
        if (m_consumablesDialog) {
            m_consumablesDialog->setRechargeStatus(
                        tr("已连接，等待刷卡…"), true);
        }
        updateConsumablesOperationState();
        maybeStartStartupCalibration();
        return;
    }

    if (startupConnectionAbnormal()) {
        setConnectionStatus(tr("● 串口连接异常，正在重试…"), "offline",
                            tr("启动后 30 秒内未检测到仪器，通讯线程仍在自动重连。"));
    } else {
        setConnectionStatus(tr("● 仪器未连接，正在重连…"), "offline",
                            tr("仪器串口已断开，正在自动重连。"));
    }
    if (m_consumablesDialog) {
        m_consumablesDialog->setRechargeStatus(
                    tr("串口未连接，等待重连…"), false);
    }
    updateConsumablesOperationState();
    qWarning() << "仪器串口未连接，等待自动重连";
}

void MainWindow::handleSerialError(const QString &message, bool connected)
{
    qWarning() << "串口错误:" << message << "连接中:" << connected;

    if (!m_connectionStatus) {
        return;
    }

    if (connected) {
        // 连接还在，只把错误放到提示里，不把状态改成断开
        m_connectionStatus->setToolTip(message);
        return;
    }

    setMainboardStatusText(tr("未连接"), "offline", message);

    if (startupConnectionAbnormal()) {
        setConnectionStatus(tr("● 串口连接异常，正在重试…"), "offline", message);
    } else {
        setConnectionStatus(tr("● 仪器未连接，正在重连…"), "offline", message);
    }
}

bool MainWindow::startupConnectionAbnormal() const
{
    return m_startupTimedOut && !m_startupConnected;
}

void MainWindow::setConnectionStatus(const QString &text, const char *state,
                                     const QString &toolTip)
{
    if (!m_connectionStatus) {
        return;
    }
    const QString stateText = QString::fromLatin1(state);
    if (m_connectionStatus->text() == text
            && m_connectionStatus->property("state").toString() == stateText
            && m_connectionStatus->toolTip() == toolTip) {
        return;   // 自动重连轮询时状态没变就不重绘
    }
    m_connectionStatus->setText(text);
    m_connectionStatus->setProperty("state", stateText);
    m_connectionStatus->setToolTip(toolTip);
    m_connectionStatus->style()->unpolish(m_connectionStatus);
    m_connectionStatus->style()->polish(m_connectionStatus);
    m_connectionStatus->update();
}

void MainWindow::setMainboardStatusText(const QString &summary,
                                        const char *state,
                                        const QString &toolTip)
{
    if (!m_mainboardStatus) {
        return;
    }

    const QString text = tr("● 主板：%1").arg(summary);
    const QString stateText = QString::fromLatin1(state);
    const bool stateChanged =
            m_mainboardStatus->property("state").toString() != stateText;

    if (m_mainboardStatus->text() != text) {
        m_mainboardStatus->setText(text);
    }
    if (stateChanged) {
        m_mainboardStatus->setProperty("state", stateText);
        m_mainboardStatus->style()->unpolish(m_mainboardStatus);
        m_mainboardStatus->style()->polish(m_mainboardStatus);
    }
    m_mainboardStatus->setToolTip(toolTip);
    m_mainboardStatus->update();
}

void MainWindow::updateMainboardStatus(const MainBoardStatus &status)
{
    applyMainboardStatus(status);
}

void MainWindow::updateConsumableAmount(int position, int bottles, int ratio)
{
    if (position < 0 || position >= MainBoardConsumable::kPositionCount) {
        return;
    }

    const int previousBottles = m_consumableBottles[position];
    const int previousRatio = m_lastSeenConsumableRatio[position];
    const bool externalRatioChange =
            previousRatio >= 0
            && ratio != previousRatio
            && ratio != m_lastWrittenConsumableRatio[position];

    m_consumableBottles[position] = bottles;
    m_consumableRatios[position] = ratio;
    m_consumableValid[position] = bottles >= 0;
    m_lastSeenConsumableRatio[position] = ratio;

    const double capacity = consumableCapacityUl(position);
    if (!m_consumableRemainingValid[position]
            || bottles != previousBottles
            || externalRatioChange) {
        m_consumableRemainingUl[position] =
                mainboardRatioToUl(ratio, capacity);
        m_consumableRemainingValid[position] = capacity > 0.0;
    }

    if (m_consumablesDialog) {
        m_consumablesDialog->setConsumableAmount(position, bottles, ratio);
    }

    const int bottle = reagentBottleForConsumablePosition(position);
    if (bottle >= 0 && m_reagentPanel) {
        m_reagentPanel->setLevel(bottle, mainboardRatioPercent(ratio));
    }
}

void MainWindow::applyMainboardStatus(const MainBoardStatus &status)
{
    if (!status.valid) {
        return;
    }

    m_latestMainboardStatus = status;
    m_hasMainboardStatus = true;
    handleCardRechargeStatus(status);
    if (m_consumablesDialog && m_consumablesDialog->isVisible()) {
        m_consumablesDialog->setMainboardStatus(status);
    }

    const int cupCount = activeCupCount(m_machineModel);
    if (m_boardIndicators) {
        m_boardIndicators->setHeightIconVisible(!sampleQualityIsPlasma());
        m_boardIndicators->setStatus(status.cleanNormal, status.wasteNormal,
                                     status.cupPresent, status.heightOk,
                                     cupCount);
    }

    // 主板 byte5.bit1 为测高触发信号：只在全血模式按上升沿加一个样本。
    const bool heightRising = m_heightTriggerBaselineSet
            && status.heightOk && !m_lastHeightDetectionTrigger;
    m_lastHeightDetectionTrigger = status.heightOk;
    m_heightTriggerBaselineSet = true;
    if (heightRising && m_hasLoggedIn && !sampleQualityIsPlasma()
            && m_addSampleWidget) {
        qInfo() << "Mainboard height detection trigger: add one sample";
        m_addSampleWidget->handleHeightDetectionTrigger();
    }

    QStringList issues;
    if (status.cmdStatus == 3) {
        issues << tr("命令错误");
    }
    if (!status.cleanNormal) {
        issues << tr("S2清洗液不足");
    }
    if (!status.wasteNormal) {
        issues << tr("废液满");
    }

    QStringList missingCups;
    for (int i = 0; i < cupCount; ++i) {
        if (!status.cupPresent[i]) {
            missingCups << QString::number(i + 1);
        }
    }
    if (!missingCups.isEmpty()) {
        issues << tr("托盘%1缺失").arg(missingCups.join(QStringLiteral("/")));
    }
    if (status.mfcStatus == 3) {
        issues << tr("读卡失败");
    } else if (status.mfcStatus == 4) {
        issues << tr("刷卡异常");
    } else if (status.mfcStatus == 6) {
        issues << tr("充值失败");
    }

    QString summary;
    const char *state = "ok";
    if (!issues.isEmpty()) {
        summary = issues.join(QStringLiteral(" · "));
        state = "error";
    } else if (status.cmdStatus == 0 || status.cmdStatus == 1) {
        summary = mainBoardCmdStatusText(status.cmdStatus);
        state = "warning";
    } else {
        summary = tr("正常");
    }

    QStringList lines;
    lines << tr("主板状态");
    lines << tr("命令状态：%1").arg(mainBoardCmdStatusText(status.cmdStatus));
    lines << tr("帧类型：%1")
             .arg(status.type ? tr("报告(R)") : tr("命令(W)"));
    lines << tr("外部清洗液S2：%1")
             .arg(status.cleanNormal ? tr("正常") : tr("不足/断开"));
    for (int i = 0; i < cupCount; ++i) {
        lines << tr("托盘%1：%2")
                 .arg(i + 1)
                 .arg(status.cupPresent[i] ? tr("正常") : tr("缺少"));
    }
    lines << tr("废液：%1")
             .arg(status.wasteNormal ? tr("正常") : tr("满/断开"));
    if (!sampleQualityIsPlasma()) {
        lines << tr("测高检测：%1")
                 .arg(status.heightOk ? tr("成功") : tr("失败"));
    }
    lines << tr("蜂鸣器：%1    制冷：%2")
             .arg(onOffText(status.fanOn), onOffText(status.coolOn));
    lines << tr("隔膜泵1：%1    隔膜泵2：%2")
             .arg(onOffText(status.pump1On), onOffText(status.pump2On));
    lines << tr("三通阀1：%1    三通阀2：%2")
             .arg(onOffText(status.threeWay1Open),
                  onOffText(status.threeWay2Open));
    lines << tr("MFC 状态：%1").arg(mainBoardMfcStatusText(status.mfcStatus));
    lines << tr("刷卡类型：%1    刷卡瓶数：%2")
             .arg(mainBoardRegTypeText(status.regType))
             .arg(status.regRemain);
    lines << tr("耗材余量状态：%1")
             .arg(mainBoardConsStatusText(status.consStatus));
    lines << tr("耗材种类：%1").arg(mainBoardConsTypeText(status.consType));
    const double percent = status.consumable / 512.0 * 100.0;
    lines << tr("耗材余量：%1 / 512（%2%）")
             .arg(status.consumable)
             .arg(percent, 0, 'f', 1);
    if (status.regYear != 0 && status.regMonth != 0) {
        lines << tr("试剂有效期：%1 年 %2 月")
                 .arg(2000 + status.regYear)
                 .arg(status.regMonth, 2, 10, QLatin1Char('0'));
    } else {
        lines << tr("试剂有效期：--");
    }
    lines << tr("耗材详情：请打开顶部「耗材管理」查看。");
    if (!status.raw.isEmpty()) {
        lines << tr("原始帧：%1")
                 .arg(QString::fromLatin1(status.raw.toHex(' ').toUpper()));
    }

    setMainboardStatusText(summary, state, lines.join(QLatin1Char('\n')));
}

void MainWindow::setMachineModel(MachineModel model)
{
    m_machineModel = model;
    setMachineModelInSettings(model);
    m_instrumentCoordinates.setMachineModel(model);
    if (m_moduleOverview) {
        m_moduleOverview->setMachineModel(model);
    }
    if (m_trayModel) {
        m_trayModel->setMachineModel(model);
    }
    if (m_addSampleWidget) {
        m_addSampleWidget->setMachineModel(model);
    }
    applyChannelSettings();
    if (m_commWorker) {
        QMetaObject::invokeMethod(m_commWorker, "setMachineModel",
                                  Qt::QueuedConnection,
                                  Q_ARG(MachineModel, model));
    }
}

void MainWindow::setTestingActive(bool active)
{
    TestSession::instance().setTestingActive(active);
}

MachineModel MainWindow::machineModel() const
{
    return m_machineModel;
}

bool MainWindow::testingActive() const
{
    return TestSession::instance().isTestingActive();
}

ModuleOverviewWidget *MainWindow::moduleOverview() const
{
    return m_moduleOverview;
}

ReagentBottlePanel *MainWindow::reagentPanel() const
{
    return m_reagentPanel;
}

TestDataStore *MainWindow::testDataStore() const
{
    return m_testDataStore;
}

SampleTrayModel *MainWindow::trayModel() const
{
    return m_trayModel;
}

InstrumentCoordinates *MainWindow::instrumentCoordinates()
{
    return &m_instrumentCoordinates;
}

platelet::MotionController *MainWindow::motionController() const
{
    return m_motionController;
}

void MainWindow::loadInstrumentCoordinates()
{
    const QString path = CoordinateFileIo::defaultFilePath();
    QString loadPath = path;

    // 旧版本把坐标文件放在 AppData；首次升级时搬到程序目录。
    if (!QFileInfo::exists(loadPath)) {
        const QString legacyPath = CoordinateFileIo::legacyDefaultFilePath();
        if (!legacyPath.isEmpty() && QFileInfo::exists(legacyPath)) {
            if (QFile::copy(legacyPath, loadPath)) {
                qInfo() << "已迁移旧坐标文件:" << legacyPath
                        << "→" << loadPath;
            } else {
                qWarning() << "旧坐标文件迁移失败，继续读取旧文件:"
                           << legacyPath << "→" << loadPath;
                loadPath = legacyPath;
            }
        }
    }

    if (!QFileInfo::exists(loadPath)) {
        qInfo() << "Instrument coordinates file not found:" << loadPath;
        return;
    }

    CoordinateDocument document;
    QString error;
    if (!CoordinateFileIo::load(loadPath, &document, &error)) {
        qWarning() << "Instrument coordinates load failed:"
                   << loadPath << error;
        return;
    }

    if (!document.machineModel.isEmpty()
            && document.machineModel
               != QLatin1String(machineModelName(m_machineModel))) {
        qWarning() << "Coordinate file model" << document.machineModel
                   << "differs from current model"
                   << machineModelName(m_machineModel);
    }

    const CoordinateFileIo::ApplyResult result =
            CoordinateFileIo::apply(document, &m_instrumentCoordinates);
    qInfo() << "Instrument coordinates loaded from" << loadPath
            << "applied" << result.applied
            << "skipped" << result.skipped
            << "ignoredTools" << result.ignoredTools;
}

void MainWindow::openSettings()
{
    createSettingsTab();
    if (m_mainTabs && m_settingsTabIndex >= 0) {
        m_mainTabs->setCurrentIndex(m_settingsTabIndex);
    }
    setActiveTabButton(m_settingsButton);
}
