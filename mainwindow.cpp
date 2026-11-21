#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "mainwindow.h"
#include "consumablesdialog.h"
#include "settingsdialog.h"
#include "CommunicationWorker.h"
#include "coordinatefileio.h"
#include "coordinateverificationcontroller.h"
#include "motioncontroller.h"
#include "perfusioncontroller.h"
#include "AppIcon.h"
#include "CustomTitleBar.h"
#include "FramelessWindowSupport.h"
#include "moduleoverviewwidget.h"
#include "modulerackwidget.h"
#include "reagentbottlepanel.h"
#include "mainboardstatusicons.h"
#include "mainboardpoller.h"
#include "querywidget.h"
#include "resultsinfowidget.h"
#include "sampletraymodel.h"
#include "testdatastore.h"
#include "testsession.h"
#include "tubetraydialog.h"
#include "appconfig.h"

#include <QAbstractButton>
#include <QCloseEvent>
#include <QDateTime>
#include <QDebug>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QStackedWidget>
#include <QStringList>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <QtMath>
#include <cmath>

namespace {

constexpr int kStartupConnectTimeoutMs = 30000;   // 启动后 30 秒内必须连上仪器

// 联机启动校准参数
constexpr double kModuleTargetTemperatureCelsius = 37.0;
constexpr int kHeatingTimeoutMs = 5 * 60 * 1000;  // 升温 37℃ 超时保护
constexpr int kDimmingTargetAd = 3300;
constexpr int kDimmingResidualThreshold = 100;    // 超 ±100 视为疑似残留试杯
constexpr int kLedTuningTolerance = 3;            // AD 允许 ±3
constexpr int kMaxCupRetries = 1;   // 丢杯后只重新调光一次
constexpr int kMaxInvalidAdSamples = 8;
constexpr int kDiscardSimulatedMs = 2000;

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
    Play,
    Pause,
    Reset,
    Clean,
    AddSample,
    Tray,
    Settings
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

QToolButton *createTopFunctionButton(const QString &text, const QIcon &icon,
                                     QWidget *parent, int width)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("topFunctionButton"));
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(width, 64);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIconSize(QSize(26, 26));
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
    case 2: return QStringLiteral("ADR");
    case 3: return QStringLiteral("COL");
    case 4: return QStringLiteral("RIS");
    case 5: return QObject::tr("内部清洗剂");
    case 6: return QObject::tr("试管/外部清洗液");
    case 7: return QObject::tr("无");
    default: return QObject::tr("未知(%1)").arg(type);
    }
}

QString mainBoardConsTypeText(quint8 type)
{
    switch (type) {
    case 0: return QStringLiteral("AA");
    case 1: return QStringLiteral("EPI");
    case 2: return QStringLiteral("ADR");
    case 3: return QStringLiteral("COL");
    case 4: return QStringLiteral("RIS");
    case 5: return QObject::tr("内部清洗剂");
    case 6: return QObject::tr("试管");
    case 7: return QStringLiteral("AA1");
    case 8: return QStringLiteral("EPI1");
    case 9: return QStringLiteral("ADR1");
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
        "QWidget#topBar {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 #F9FDFF, stop:0.42 #E7F5FD, stop:1 #D6EAF8);"
        "  border-bottom: 1px solid #B7D4E7;"
        "}"
        "QLabel#mainPageTitle {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 16px;"
        "  font-weight: 700;"
        "}"
        "QWidget#bottomBar {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "      stop:0 #FFFFFF, stop:1 #F0F9FE);"
        "  border-top: 1px solid #D6E6F2;"
        "}"
        "QLabel#footerHint {"
        "  color: #A0AAB5;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 11px;"
        "}"
        "QToolButton#topFunctionButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "      stop:0 rgba(255,255,255,245),"
        "      stop:0.45 rgba(226,245,255,230),"
        "      stop:1 rgba(183,220,243,235));"
        "  color: #174A6D;"
        "  border: 1px solid rgba(255,255,255,235);"
        "  border-top: 2px solid rgba(255,255,255,245);"
        "  border-bottom: 1px solid rgba(116,166,201,170);"
        "  border-radius: 12px;"
        "  padding: 0px 3px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "}"
        "QToolButton#topFunctionButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "      stop:0 rgba(255,255,255,255),"
        "      stop:0.42 rgba(240,251,255,250),"
        "      stop:1 rgba(201,235,255,250));"
        "  border: 1px solid #FFFFFF;"
        "  border-bottom: 1px solid #7EB3D3;"
        "}"
        "QToolButton#topFunctionButton:pressed {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "      stop:0 rgba(220,241,253,245),"
        "      stop:1 rgba(170,211,238,245));"
        "  border: 1px solid #A8D2E9;"
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
        "  border-radius: 45px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 17px;"
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
        showActionHint(message);
        if (!ok) {
            QMessageBox::warning(this, tr("定位校验"), message);
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
        if (m_startupState != StartupCalibrationState::Perfusion) {
            return;
        }
        if (ok) {
            beginStartupHeating();
        } else {
            abortStartupCalibration(message, true);
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
    const bool testing = testingActive()
            || (m_moduleOverview
                && m_moduleOverview->isChannelOneSimulationRunning());
    const QString message = testing
            ? tr("当前有测试正在进行，关闭软件会中断测试。\n\n确定要关闭吗？")
            : tr("确定要关闭血小板聚集分析仪吗？");
    QMessageBox box(this);
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

    m_usernameEdit = new QLineEdit(card);
    m_usernameEdit->setObjectName(QStringLiteral("loginInput"));
    m_usernameEdit->setPlaceholderText(tr("请输入用户名"));
    m_usernameEdit->setMinimumHeight(36);

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

    auto *hint = new QLabel(tr("开发阶段：暂不校验用户名和密码"), card);
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
    connect(m_usernameEdit, &QLineEdit::returnPressed, this, &MainWindow::handleLogin);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &MainWindow::handleLogin);

    return page;
}

QWidget *MainWindow::createMainPage()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("mainPage"));

    auto *rootLayout = new QVBoxLayout(page);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *topBar = new QWidget(page);
    topBar->setObjectName(QStringLiteral("topBar"));
    topBar->setFixedHeight(112);
    auto *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(12, 11, 12, 11);
    topLayout->setSpacing(6);

    const QColor topIconColor(QStringLiteral("#1F4E79"));

    // 左侧功能按钮：主页 / 结果信息 / 耗材管理 / 试管盘 / 查询 / 质控
    auto *leftGroup = new QWidget(topBar);
    leftGroup->setFixedSize(404, 64);
    auto *leftLayout = new QHBoxLayout(leftGroup);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(4);

    struct TopFunctionSpec {
        QString text;
        ToolbarIconType icon;
    };
    const TopFunctionSpec leftSpecs[] = {
        { tr("主页"), ToolbarIconType::Home },
        { tr("结果信息"), ToolbarIconType::Results },
        { tr("耗材管理"), ToolbarIconType::Consumables },
        { tr("试管盘"), ToolbarIconType::Tray },
        { tr("查询"), ToolbarIconType::Search },
        { tr("质控"), ToolbarIconType::Quality }
    };
    for (const TopFunctionSpec &spec : leftSpecs) {
        auto *button = createTopFunctionButton(
                    spec.text, makeToolbarIcon(spec.icon, topIconColor),
                    leftGroup, 64);
        if (spec.icon == ToolbarIconType::Home) {
            connect(button, &QToolButton::clicked,
                    this, &MainWindow::showHomeTab);
        } else if (spec.icon == ToolbarIconType::Results) {
            connect(button, &QToolButton::clicked,
                    this, &MainWindow::openResults);
        } else if (spec.icon == ToolbarIconType::Consumables) {
            connect(button, &QToolButton::clicked,
                    this, &MainWindow::openConsumables);
        } else if (spec.icon == ToolbarIconType::Tray) {
            connect(button, &QToolButton::clicked,
                    this, &MainWindow::openTubeTrays);
        } else if (spec.icon == ToolbarIconType::Search) {
            connect(button, &QToolButton::clicked,
                    this, &MainWindow::openQuery);
        } else if (spec.icon == ToolbarIconType::Quality) {
            connect(button, &QToolButton::clicked,
                    this, &MainWindow::openQuality);
        } else {
            const QString text = spec.text;
            connect(button, &QToolButton::clicked, this,
                    [this, text]() { showPendingFunction(text); });
        }
        leftLayout->addWidget(button);
    }

    m_startButton = new QToolButton(topBar);
    m_startButton->setObjectName(QStringLiteral("startButton"));
    m_startButton->setCursor(Qt::PointingHandCursor);
    m_startButton->setFocusPolicy(Qt::NoFocus);
    m_startButton->setFixedSize(90, 90);
    m_startButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    m_startButton->setIconSize(QSize(30, 30));
    m_startButton->setText(tr("开始"));
    m_startButton->setIcon(makeToolbarIcon(ToolbarIconType::Play, Qt::white,
                                           QColor("#5F7A8F")));
    m_startButton->setToolTip(tr("模拟通道 1 开始测试"));
    connect(m_startButton, &QToolButton::clicked,
            this, &MainWindow::handleStartTest);

    // 右侧操作按钮：暂停/继续 / 复位 / 清洗 / 加样 / 设置
    auto *rightGroup = new QWidget(topBar);
    rightGroup->setFixedSize(404, 64);
    auto *rightLayout = new QHBoxLayout(rightGroup);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(4);
    rightLayout->addStretch(1);

    m_pauseButton = createTopFunctionButton(
                tr("暂停"), makeToolbarIcon(ToolbarIconType::Pause,
                                             topIconColor),
                rightGroup, 64);
    connect(m_pauseButton, &QToolButton::clicked,
            this, &MainWindow::handlePauseResume);
    rightLayout->addWidget(m_pauseButton);

    auto *resetButton = createTopFunctionButton(
                tr("复位"), makeToolbarIcon(ToolbarIconType::Reset,
                                             topIconColor),
                rightGroup, 64);
    connect(resetButton, &QToolButton::clicked,
            this, &MainWindow::handleResetTest);
    rightLayout->addWidget(resetButton);

    auto *cleanButton = createTopFunctionButton(
                tr("清洗"), makeToolbarIcon(ToolbarIconType::Clean,
                                             topIconColor),
                rightGroup, 64);
    connect(cleanButton, &QToolButton::clicked, this,
            [this]() { showPendingFunction(tr("清洗")); });
    rightLayout->addWidget(cleanButton);

    auto *addSampleButton = createTopFunctionButton(
                tr("加样"), makeToolbarIcon(ToolbarIconType::AddSample,
                                             topIconColor),
                rightGroup, 64);
    connect(addSampleButton, &QToolButton::clicked,
            this, &MainWindow::openAddSample);
    rightLayout->addWidget(addSampleButton);

    auto *settingsButton = createTopFunctionButton(
                tr("设置"), makeToolbarIcon(ToolbarIconType::Settings,
                                             topIconColor),
                rightGroup, 64);
    connect(settingsButton, &QToolButton::clicked,
            this, &MainWindow::openSettings);
    rightLayout->addWidget(settingsButton);

    topLayout->addWidget(leftGroup, 0, Qt::AlignVCenter);
    topLayout->addStretch(1);
    topLayout->addWidget(m_startButton, 0, Qt::AlignCenter);
    topLayout->addStretch(1);
    topLayout->addWidget(rightGroup, 0, Qt::AlignVCenter);

    rootLayout->addWidget(topBar);

    auto *content = new QWidget(page);
    content->setObjectName(QStringLiteral("contentArea"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(24, 24, 24, 24);
    contentLayout->setSpacing(12);

    m_moduleOverview = new ModuleOverviewWidget(content);
    m_moduleOverview->setMachineModel(m_machineModel);
    contentLayout->addWidget(m_moduleOverview, 1);

    // 模组下方：11 个 3D 试剂瓶的余量面板
    m_reagentPanel = new ReagentBottlePanel(content);
    m_reagentPanel->setFixedHeight(160);
    contentLayout->addWidget(m_reagentPanel);

    connect(m_moduleOverview, &ModuleOverviewWidget::channelOneSimulationFinished,
            this, [this]() {
        // 仅在自然跑到 300 秒完成时保存；手动复位/中断不保存。
        saveCompletedSimulationData();
        if (m_startButton) {
            m_startButton->setEnabled(true);
        }
        m_channelOneDemoPaused = false;
        updatePauseButtonAppearance();
    });
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
    // 顶部按钮负责切换页面，标签头不显示。
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
    rootLayout->addWidget(m_mainTabs, 1);

    connect(m_mainTabs, &QTabWidget::currentChanged,
            this, [this](int index) {
        if (index == m_consumablesTabIndex) {
            refreshConsumablesTab();
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
    // 开发阶段：不校验用户名和密码，后期在这里接入用户管理
    const QString userName = m_usernameEdit
            ? m_usernameEdit->text().trimmed() : QString();
    qInfo() << "操作员登录:"
            << (userName.isEmpty() ? QStringLiteral("(未填写)") : userName);
    m_hasLoggedIn = true;
    if (m_passwordEdit) {
        m_passwordEdit->clear();
    }
    if (m_actionHint) {
        m_actionHint->clear();
    }
    m_stack->setCurrentWidget(m_mainPage);
    // 坐标在登录前已从 JSON 读入内存；登录后若已联机，立即开始
    // “开机灌注 → 升温到 37℃ → 调光”。
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
    if (m_usernameEdit) {
        m_usernameEdit->clear();
        m_usernameEdit->setFocus();
    }
    m_stack->setCurrentWidget(m_loginPage);
}

void MainWindow::handleStartTest()
{
    if (!m_moduleOverview) {
        return;
    }
    if (!m_moduleOverview->isChannelEnabled(1, 1)) {
        showActionHint(tr("通道1已禁用，请先在「基础配置」中启用"));
        return;
    }
    showHomeTab();
    if (m_startButton) {
        m_startButton->setEnabled(false);
    }
    m_channelOneDemoPaused = false;
    updatePauseButtonAppearance();
    if (m_actionHint) {
        m_actionHint->clear();
    }

    // 临时演示：开始按钮模拟通道1测试；接入真实流程后替换为下发启动命令
    qInfo() << "开始测试：通道1模拟";
    m_moduleOverview->startChannelOneSimulation();
}

void MainWindow::handlePauseResume()
{
    if (!m_moduleOverview || !m_moduleOverview->isChannelOneSimulationRunning()) {
        showActionHint(tr("请先点击「开始」"));
        return;
    }

    m_channelOneDemoPaused = !m_channelOneDemoPaused;
    m_moduleOverview->setChannelOneSimulationPaused(m_channelOneDemoPaused);
    updatePauseButtonAppearance();
    qInfo() << (m_channelOneDemoPaused ? "测试已暂停" : "测试已继续");
}

void MainWindow::updatePauseButtonAppearance()
{
    if (!m_pauseButton) {
        return;
    }

    const QString text = m_channelOneDemoPaused ? tr("继续") : tr("暂停");
    const ToolbarIconType iconType = m_channelOneDemoPaused
            ? ToolbarIconType::Play : ToolbarIconType::Pause;
    m_pauseButton->setText(text);
    m_pauseButton->setIcon(makeToolbarIcon(iconType, QColor("#1F4E79")));
}

void MainWindow::handleResetTest()
{
    if (!m_moduleOverview) {
        return;
    }
    m_moduleOverview->stopChannelOneSimulation();
    m_moduleOverview->resetAll();
    qInfo() << "测试已复位";
    m_channelOneDemoPaused = false;
    if (m_startButton) {
        m_startButton->setEnabled(true);
    }
    updatePauseButtonAppearance();
    if (m_actionHint) {
        m_actionHint->clear();
    }
}

void MainWindow::showHomeTab()
{
    if (m_mainTabs && m_homeTabIndex >= 0) {
        m_mainTabs->setCurrentIndex(m_homeTabIndex);
    }
}

void MainWindow::createConsumablesTab()
{
    if (!m_mainTabs || m_consumablesDialog) {
        return;
    }
    m_consumablesDialog = new ConsumablesDialog(m_mainTabs);
    m_consumablesDialog->embedAsPage();
    QScrollArea *scroll = makePageScroll(m_consumablesDialog, m_mainTabs);
    m_consumablesTabIndex = m_mainTabs->addTab(scroll, tr("耗材管理"));
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
    if (!m_queryWidget) {
        m_queryWidget = new QueryWidget(m_testDataStore, m_mainTabs);
        m_queryTabIndex = m_mainTabs->addTab(m_queryWidget, tr("查询"));
    }
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
    if (m_addSampleTabIndex < 0) {
        m_addSampleTabIndex = m_mainTabs->addTab(
                    createPlaceholderPage(
                        tr("加样"),
                        tr("加样功能待接入，后续在这里进行样本/试剂加样。")),
                    tr("加样"));
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
}

void MainWindow::openQuality()
{
    createEmptyPlaceholderTabs();
    if (m_mainTabs && m_qualityTabIndex >= 0) {
        m_mainTabs->setCurrentIndex(m_qualityTabIndex);
    }
}

void MainWindow::openAddSample()
{
    createEmptyPlaceholderTabs();
    if (m_mainTabs && m_addSampleTabIndex >= 0) {
        m_mainTabs->setCurrentIndex(m_addSampleTabIndex);
    }
}

void MainWindow::createSettingsTab()
{
    if (!m_mainTabs || m_settingsDialog) {
        return;
    }
    m_settingsDialog = new SettingsDialog(m_commWorker,
                                          &m_instrumentCoordinates,
                                          m_mainTabs, m_machineModel);
    connect(m_settingsDialog, &SettingsDialog::coordinateMotionRequested,
            m_verificationController,
            &CoordinateVerificationController::start);
    connect(m_settingsDialog, &SettingsDialog::channelEnabledChanged,
            this, [this](int globalChannel, bool enabled) {
        if (!m_moduleOverview) {
            return;
        }
        const int moduleNumber = (globalChannel - 1) / 4 + 1;
        const int localChannel = (globalChannel - 1) % 4 + 1;
        m_moduleOverview->setChannelEnabled(moduleNumber, localChannel,
                                            enabled);

        // 正在模拟的 CH1 被禁用时立即停止测试并恢复开始按钮
        if (globalChannel == 1 && !enabled
                && m_moduleOverview->isChannelOneSimulationRunning()) {
            m_moduleOverview->stopChannelOneSimulation();
            m_channelOneDemoPaused = false;
            updatePauseButtonAppearance();
            if (m_startButton) {
                m_startButton->setEnabled(true);
            }
        }
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
    refreshConsumablesTab();
    if (m_mainTabs && m_consumablesTabIndex >= 0) {
        m_mainTabs->setCurrentIndex(m_consumablesTabIndex);
    }
}

void MainWindow::openTubeTrays()
{
    createTubeTrayTab();
    if (m_mainTabs && m_tubeTrayTabIndex >= 0) {
        m_mainTabs->setCurrentIndex(m_tubeTrayTabIndex);
    }
}

void MainWindow::handleTrayReplaceRequested(int trayIndex)
{
    if (!m_trayModel || !m_trayModel->isTrayActive(trayIndex)) {
        return;
    }

    // 测试中禁止更换
    if (testingActive()
            || (m_moduleOverview
                && m_moduleOverview->isChannelOneSimulationRunning())) {
        QMessageBox::warning(
                    this, tr("更换试管盘"),
                    tr("当前正在测试，不能更换试管盘。\n"
                       "请等待所有通道测试结束后再更换。"));
        return;
    }

    if (!m_serialConnected) {
        QMessageBox::warning(
                    this, tr("更换试管盘"),
                    tr("仪器未连接，无法更新主板试管盘余量。\n"
                       "请连接仪器后再更换。"));
        return;
    }

    const int position = MainBoardConsumable::TestTube;
    if (!m_consumableValid[position] || m_consumableBottles[position] < 0) {
        QMessageBox::warning(
                    this, tr("更换试管盘"),
                    tr("尚未读取到主板试管盘余量，请稍后重试。"));
        return;
    }

    const int currentTrays = m_consumableBottles[position];
    if (currentTrays <= 0) {
        QMessageBox::warning(
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
    if (QMessageBox::question(this, tr("更换试管盘"), firstText,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    // 二次确认：重新读取余量，避免轮询期间数值变化
    if (!m_serialConnected || !m_consumableValid[position]
            || m_consumableBottles[position] <= 0) {
        QMessageBox::warning(
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
    if (QMessageBox::question(this, tr("再次确认更换"), secondText,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    if (!m_commWorker) {
        QMessageBox::warning(
                    this, tr("更换试管盘"),
                    tr("通讯线程未启动，无法更新主板数量。"));
        return;
    }

    const QByteArray frame = MainBoardPoller::consumableWriteFrame(
                position, newTrayCount, 521, true);
    if (frame.isEmpty()) {
        QMessageBox::warning(
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

void MainWindow::showPendingFunction(const QString &name)
{
    showActionHint(tr("%1：功能待接入").arg(name));
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

void MainWindow::saveCompletedSimulationData()
{
    if (!m_moduleOverview || !m_testDataStore) {
        return;
    }
    ModuleRackWidget *rack = m_moduleOverview->moduleRack(1);
    if (!rack || rack->channelProgress(1) < 100) {
        return;   // 复位/中断不保存
    }

    const QVector<QPointF> curve = rack->testCurve(1);
    if (curve.size() < 300) {
        return;   // 完整试剂测试应有 300 个 PRP 点
    }

    const ModuleRackWidget::ChannelTestInfo info =
            rack->channelTestInfo(1);
    SampleTestData sample;
    sample.sampleNumber = info.sampleNumber.isEmpty()
            ? QStringLiteral("0001") : info.sampleNumber;
    sample.sampleType = sampleQualityIsPlasma()
            ? tr("血浆") : tr("全血");
    sample.machineModel = QString::fromLatin1(
                machineModelName(m_machineModel));
    sample.testTime = QDateTime::currentDateTime();
    sample.remark = tr("通道1模拟测试自动保存");

    TestCurveData test;
    test.reagent = info.reagentName.isEmpty()
            ? QStringLiteral("AA") : info.reagentName;
    test.prp0 = info.prp0Value >= 0 ? info.prp0Value : info.prpValue;
    test.moduleNumber = 1;
    test.localChannel = 1;
    test.globalChannel = 1;
    test.status = tr("完成");
    for (int i = 0; i < qMin(curve.size(), 300); ++i) {
        test.prpPoints.append(qRound(curve.at(i).y()));
    }
    const QVector<QPointF> aggregation = rack->aggregationCurve(1);
    if (aggregation.size() == test.prpPoints.size()) {
        for (int i = 0; i < aggregation.size(); ++i) {
            test.aggregationPoints.append(aggregation.at(i).y());
        }
    }
    sample.tests.append(test);

    QString error;
    const qint64 sampleId = m_testDataStore->saveSample(sample, &error);
    if (sampleId <= 0) {
        qWarning() << "Save test data failed:" << error;
        showActionHint(tr("测试数据保存失败：%1").arg(error));
        return;
    }
    showActionHint(tr("模拟测试完成，已保存测试数据（样本 %1，%2 个点）")
                   .arg(sample.sampleNumber)
                   .arg(test.prpPoints.size()));
    if (m_resultsInfoWidget) {
        m_resultsInfoWidget->refresh();
    }
    if (m_queryWidget) {
        m_queryWidget->refresh();
    }
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

    cardLayout->addWidget(m_startupMaskTitle);
    cardLayout->addWidget(m_startupMaskDetail);
    cardLayout->addWidget(m_startupMaskProgress);

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
    if (QWidget *topBar = m_mainPage->findChild<QWidget *>(
                QStringLiteral("topBar"))) {
        topBar->setEnabled(enabled);
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
        if (m_latestTemperature[moduleIndex]
                < kModuleTargetTemperatureCelsius - 0.001) {
            return false;
        }
    }
    return true;
}

void MainWindow::maybeStartStartupCalibration()
{
    if (!m_hasLoggedIn || !m_startupConnected || !m_commWorker) {
        return;
    }
    if (m_startupState != StartupCalibrationState::Idle
            || m_startupCalibrationAttempted) {
        return;
    }
    // 测试进行中掉线重连时不要打断测试去做灌注/升温/调光。
    if (testingActive()
            || (m_moduleOverview
                && m_moduleOverview->isChannelOneSimulationRunning())) {
        return;
    }
    startStartupCalibration();
}

void MainWindow::startStartupCalibration()
{
    if (m_startupState != StartupCalibrationState::Idle || !m_commWorker
            || m_startupCalibrationAttempted
            || !m_hasLoggedIn || !m_startupConnected) {
        return;
    }

    // 测试进行中掉线重连时不要打断测试去做灌注/升温/调光。
    if (testingActive()
            || (m_moduleOverview
                && m_moduleOverview->isChannelOneSimulationRunning())) {
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
    setStartupMaskStatus(
                tr("开机灌注中"),
                tr("正在复位三根 Z 轴，随后灌注样本泵和试剂泵；"
                   "请勿操作仪器。"),
                true, 0);

    if (!m_perfusionController) {
        abortStartupCalibration(tr("灌注控制器未初始化"), true);
        return;
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
                tr("正在将所有模组升温至 37℃，达到后开始调光。"),
                false, 0);

    const int moduleCount = machineModuleCount(m_machineModel);
    for (int moduleNumber = 1; moduleNumber <= moduleCount; ++moduleNumber) {
        platelet::DetectionModule &module =
                m_moduleProtocols[moduleNumber - 1];
        module.setTargetTemperature(kModuleTargetTemperatureCelsius);
        sendModuleFrame(moduleNumber,
                        module.setTemperature(kModuleTargetTemperatureCelsius),
                        tr("设置模组%1目标温度 37℃").arg(moduleNumber));
    }

    m_startupHeatingClock.start();
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
        beginStartupDimming();
        return;
    }

    if (m_startupHeatingClock.elapsed() >= kHeatingTimeoutMs) {
        if (m_startupCheckTimer) {
            m_startupCheckTimer->stop();
        }
        abortStartupCalibration(
                    tr("模组升温超时，%1 分钟内未达到 37℃，"
                       "已停止启动校准。")
                    .arg(kHeatingTimeoutMs / 60000),
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

    QMessageBox box(this);
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
        m_startupState = StartupCalibrationState::Done;
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

    if (m_startupMask) {
        m_startupMask->hide();
    }
    setMainPageInteractionEnabled(true);

    if (userVisible) {
        m_startupState = StartupCalibrationState::Failed;
        if (!reason.isEmpty()) {
            QMessageBox::warning(this, tr("启动校准未完成"), reason);
        }
    } else {
        // 断线：允许重新连接后重新开始整套流程。
        m_startupState = StartupCalibrationState::Idle;
        m_startupCalibrationAttempted = false;
    }

    // 状态已不是 Cleaning，cancel() 触发的 finished 不会再次进入本函数。
    if (m_perfusionController && m_perfusionController->isBusy()) {
        m_perfusionController->cancel();
    }
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
            // 临时演示通道使用自身的模拟 AD，不被真实轮询覆盖
            if (data.moduleNumber == 1 && channel == 1
                    && m_moduleOverview->isChannelOneSimulationRunning()) {
                continue;
            }
            const int index = channel - 1;
            const int rawValue = (data.channelsValid
                                  && index < data.channels.size())
                    ? static_cast<int>(data.channels.at(index))
                    : 0;
            // 0 表示本秒没有有效样本，界面显示 AD --
            m_moduleOverview->setChannelAdValue(data.moduleNumber, channel,
                                                rawValue > 0 ? rawValue : -1);
        }
    });

    connect(m_commWorker, &CommunicationWorker::mainBoardStatusUpdated,
            this, &MainWindow::updateMainboardStatus);
    connect(m_commWorker, &CommunicationWorker::mainBoardConsumableUpdated,
            this, &MainWindow::updateConsumableAmount);
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

    QMessageBox::warning(this, tr("串口连接异常"),
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

    if (!connected) {
        abortStartupCalibration(QString(), false);
        m_startupCalibrationAttempted = false;
        for (int position = 0;
             position < MainBoardConsumable::kPositionCount; ++position) {
            m_consumableValid[position] = false;
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
                    QMessageBox::warning(this, tr("检测到仪器"),
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
        qInfo() << "仪器串口已连接:" << port
                << "波特率:" << baudRate;
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

    m_consumableBottles[position] = bottles;
    m_consumableRatios[position] = ratio;
    m_consumableValid[position] = bottles >= 0;
    if (m_consumablesDialog) {
        m_consumablesDialog->setConsumableAmount(position, bottles, ratio);
    }
}

void MainWindow::applyMainboardStatus(const MainBoardStatus &status)
{
    if (!status.valid) {
        return;
    }

    m_latestMainboardStatus = status;
    m_hasMainboardStatus = true;
    if (m_consumablesDialog && m_consumablesDialog->isVisible()) {
        m_consumablesDialog->setMainboardStatus(status);
    }

    const int cupCount = activeCupCount(m_machineModel);
    if (m_boardIndicators) {
        m_boardIndicators->setStatus(status.cleanNormal, status.wasteNormal,
                                     status.cupPresent, status.heightOk,
                                     cupCount);
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
    lines << tr("测高检测：%1")
             .arg(status.heightOk ? tr("成功") : tr("失败"));
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
    m_instrumentCoordinates.setMachineModel(model);
    if (m_moduleOverview) {
        m_moduleOverview->setMachineModel(model);
    }
    if (m_trayModel) {
        m_trayModel->setMachineModel(model);
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
}
