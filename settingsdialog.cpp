#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "settingsdialog.h"
#include "coordinatemaintenancewidget.h"
#include "modulecontrolwidget.h"
#include "CommunicationWorker.h"
#include "appconfig.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDebug>
#include <QDialog>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QRadioButton>
#include <QScrollArea>
#include <QSettings>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>

namespace {

/** 维护密码；后续可改成从配置/用户权限表读取 */
constexpr char kMaintenancePassword[] = "1";

QPushButton *createSettingCard(const QString &title,
                               const QString &description,
                               QWidget *parent)
{
    auto *card = new QPushButton(parent);
    card->setObjectName(QStringLiteral("settingCard"));
    card->setCursor(Qt::PointingHandCursor);
    card->setMinimumSize(220, 112);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(6);

    auto *titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("cardTitle"));
    titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto *descriptionLabel = new QLabel(description, card);
    descriptionLabel->setObjectName(QStringLiteral("cardDesc"));
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    layout->addWidget(titleLabel);
    layout->addStretch();
    layout->addWidget(descriptionLabel);
    return card;
}

QString makeUserSalt()
{
    QString salt = QUuid::createUuid().toString();
    salt.remove(QLatin1Char('{'));
    salt.remove(QLatin1Char('}'));
    return salt;
}

QString hashUserPassword(const QString &password, const QString &salt)
{
    QByteArray data = salt.toUtf8();
    data.append(password.toUtf8());
    return QString::fromLatin1(
                QCryptographicHash::hash(data, QCryptographicHash::Sha256)
                .toHex());
}

QString userRoleDisplayName(const QString &role)
{
    return role == QLatin1String("admin") ? QObject::tr("管理员")
                                          : QObject::tr("操作员");
}

QJsonArray loadUserRecords()
{
    QSettings settings;
    const QByteArray raw = settings.value(QStringLiteral("security/users"))
            .toByteArray();
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    return doc.isArray() ? doc.array() : QJsonArray();
}

void saveUserRecords(const QJsonArray &users)
{
    QSettings settings;
    settings.setValue(QStringLiteral("security/users"),
                      QJsonDocument(users).toJson(QJsonDocument::Compact));
}

QJsonArray ensureUserRecords()
{
    QJsonArray users = loadUserRecords();
    if (!users.isEmpty()) {
        return users;
    }

    QJsonObject admin;
    const QString salt = makeUserSalt();
    admin.insert(QStringLiteral("name"), QStringLiteral("admin"));
    admin.insert(QStringLiteral("salt"), salt);
    admin.insert(QStringLiteral("passwordHash"),
                 hashUserPassword(QStringLiteral("1"), salt));
    admin.insert(QStringLiteral("role"), QStringLiteral("admin"));
    users.append(admin);
    saveUserRecords(users);
    return users;
}

int findUserRecord(const QJsonArray &users, const QString &name)
{
    for (int i = 0; i < users.size(); ++i) {
        if (users.at(i).toObject().value(QStringLiteral("name")).toString()
                == name) {
            return i;
        }
    }
    return -1;
}

void refreshChannelCheckBox(QCheckBox *check, int channel, bool enabled)
{
    check->setText(QObject::tr("CH%1 %2")
                   .arg(channel)
                   .arg(enabled ? QObject::tr("启用") : QObject::tr("禁用")));
    check->setProperty("state", enabled ? "enabled" : "disabled");
    check->style()->unpolish(check);
    check->style()->polish(check);
}

/** 维护登录小对话框：输入正确密码后 exec() 返回 Accepted */
class MaintenancePasswordDialog : public FramelessDialog
{
public:
    explicit MaintenancePasswordDialog(QWidget *parent = nullptr)
        : FramelessDialog(tr("维护登录"), parent)
    {
        setModal(true);
        resize(460, 320);
        setMinimumSize(420, 300);

        auto *content = new QWidget;
        auto *layout = new QVBoxLayout(content);
        layout->setContentsMargins(30, 22, 30, 22);
        layout->setSpacing(10);

        auto *title = new QLabel(tr("进入维护设置"), content);
        title->setObjectName(QStringLiteral("maintenanceDialogTitle"));

        auto *caption = new QLabel(
                    tr("样本针、试剂针、抓手、模组控制、打印和坐标维护属于维护功能，"
                       "请输入维护密码后继续。"),
                    content);
        caption->setObjectName(QStringLiteral("maintenanceDialogCaption"));
        caption->setWordWrap(true);

        m_passwordEdit = new QLineEdit(content);
        m_passwordEdit->setEchoMode(QLineEdit::Password);
        m_passwordEdit->setPlaceholderText(tr("请输入维护密码"));
        m_passwordEdit->setMinimumHeight(36);

        m_errorLabel = new QLabel(content);
        m_errorLabel->setObjectName(QStringLiteral("maintenanceDialogError"));
        m_errorLabel->setFixedHeight(20);

        auto *buttonRow = new QHBoxLayout;
        buttonRow->setSpacing(10);

        auto *cancelButton = new QPushButton(tr("取消"), content);
        cancelButton->setObjectName(QStringLiteral("dialogCancelButton"));
        cancelButton->setCursor(Qt::PointingHandCursor);
        cancelButton->setMinimumSize(96, 34);

        auto *okButton = new QPushButton(tr("进入维护"), content);
        okButton->setObjectName(QStringLiteral("dialogPrimaryButton"));
        okButton->setCursor(Qt::PointingHandCursor);
        okButton->setMinimumSize(110, 34);

        buttonRow->addStretch(1);
        buttonRow->addWidget(cancelButton);
        buttonRow->addWidget(okButton);

        layout->addWidget(title);
        layout->addWidget(caption);
        layout->addSpacing(4);
        layout->addWidget(m_passwordEdit);
        layout->addWidget(m_errorLabel);
        layout->addStretch(1);
        layout->addLayout(buttonRow);

        setContentWidget(content);

        setStyleSheet(QStringLiteral(
            "QDialog#framelessDialog { background: #F4F8FC; }"
            "QLabel#maintenanceDialogTitle {"
            "  color: #1F4E79;"
            "  font-family: 'Microsoft YaHei';"
            "  font-size: 18px;"
            "  font-weight: 700;"
            "}"
            "QLabel#maintenanceDialogCaption {"
            "  color: #7A8FA3;"
            "  font-family: 'Microsoft YaHei';"
            "  font-size: 12px;"
            "}"
            "QLineEdit {"
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
            "QLineEdit:focus {"
            "  border: 1px solid #2C7BBF;"
            "  background: #F7FBFF;"
            "}"
            "QLabel#maintenanceDialogError {"
            "  color: #C0392B;"
            "  font-family: 'Microsoft YaHei';"
            "  font-size: 12px;"
            "}"
            "QPushButton#dialogPrimaryButton {"
            "  background: #1F4E79;"
            "  color: #FFFFFF;"
            "  border: none;"
            "  border-radius: 6px;"
            "  padding: 0px 18px;"
            "  font-family: 'Microsoft YaHei';"
            "  font-size: 13px;"
            "  font-weight: 600;"
            "}"
            "QPushButton#dialogPrimaryButton:hover { background: #2C6398; }"
            "QPushButton#dialogPrimaryButton:pressed { background: #163B5C; }"
            "QPushButton#dialogCancelButton {"
            "  background: #FFFFFF;"
            "  color: #1F4E79;"
            "  border: 1px solid #8FB4D6;"
            "  border-radius: 6px;"
            "  padding: 0px 18px;"
            "  font-family: 'Microsoft YaHei';"
            "  font-size: 13px;"
            "}"
            "QPushButton#dialogCancelButton:hover { background: #E8F1FA; }"
            "QPushButton#dialogCancelButton:pressed { background: #D3E6F5; }"));

        connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
        connect(okButton, &QPushButton::clicked, this, [this]() { tryAccept(); });
        connect(m_passwordEdit, &QLineEdit::returnPressed,
                this, [this]() { tryAccept(); });
        m_passwordEdit->setFocus();
    }

    QString password() const
    {
        return m_passwordEdit ? m_passwordEdit->text() : QString();
    }

private:
    void tryAccept()
    {
        if (password() == QString::fromLatin1(kMaintenancePassword)) {
            qInfo() << "维护登录成功";
            accept();
            return;
        }
        qWarning() << "维护登录失败：密码错误";
        m_errorLabel->setText(tr("维护密码错误，请重新输入。"));
        m_passwordEdit->selectAll();
        m_passwordEdit->setFocus();
    }

    QLineEdit *m_passwordEdit = nullptr;
    QLabel *m_errorLabel = nullptr;
};

} // namespace

SettingsDialog::SettingsDialog(CommunicationWorker *commWorker,
                               InstrumentCoordinates *coordinates,
                               QWidget *parent,
                               MachineModel model)
    : FramelessDialog(tr("系统设置"), parent)
    , m_commWorker(commWorker)
    , m_machineModel(model)
    , m_ownCoordinates(model)
    , m_coordinates(coordinates ? coordinates : &m_ownCoordinates)
{
    setModal(true);
    resize(1120, 760);
    setMinimumSize(940, 640);

    // 先应用样式，再创建子控件：Qt 5.10 下构造后再设置 QSS，
    // 首个可见的 QStackedWidget 页面可能不会重新 polish。
    setStyleSheet(QStringLiteral(
        "QDialog#framelessDialog {"
        "  background: #F4F8FC;"
        "  border: 1px solid #C5D4E4;"
        "}"
        "QLabel#dialogSubtitle {"
        "  color: #7A8FA3;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "}"
        "QLabel#pageTitle {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "}"
        "QPushButton#settingCard {"
        "  background: #FFFFFF;"
        "  border: 1px solid #D5E3F0;"
        "  border-radius: 10px;"
        "  padding: 0px;"
        "}"
        "QPushButton#settingCard:hover {"
        "  background: #F3F9FE;"
        "  border: 1px solid #8FB4D6;"
        "}"
        "QPushButton#settingCard:pressed {"
        "  background: #E4F0FA;"
        "}"
        "QLabel#cardTitle {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 15px;"
        "  font-weight: 600;"
        "}"
        "QLabel#cardDesc {"
        "  color: #8296A9;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QPushButton#backButton {"
        "  background: #FFFFFF;"
        "  color: #1F4E79;"
        "  border: 1px solid #8FB4D6;"
        "  border-radius: 6px;"
        "  padding: 0px 18px;"
        "  min-height: 32px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "}"
        "QPushButton#backButton:hover {"
        "  background: #E8F1FA;"
        "}"
        "QPushButton#backButton:pressed {"
        "  background: #D3E6F5;"
        "}"
        "QFrame#basicInfoCard {"
        "  background: #FFFFFF;"
        "  border: 1px solid #DCE7F1;"
        "  border-radius: 12px;"
        "}"
        "QFrame#basicNoteCard {"
        "  background: #FFF9EC;"
        "  border: 1px solid #F0DFB6;"
        "  border-radius: 10px;"
        "}"
        "QFrame#statSeparator {"
        "  color: #DCE7F1;"
        "}"
        "QLabel#statCaption {"
        "  color: #8296A9;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QLabel#statValue {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 19px;"
        "  font-weight: 700;"
        "}"
        "QLabel#statValue[state=\"online\"] { color: #1E7B45; }"
        "QLabel#statValue[state=\"offline\"] { color: #B7791F; }"
        "QLabel#basicNoteTitle {"
        "  color: #8A6D3B;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 14px;"
        "  font-weight: 600;"
        "}"
        "QLabel#basicNoteText {"
        "  color: #8A6D3B;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QLabel#maintenanceBadge {"
        "  color: #1E7B45;"
        "  background: #EDF8F1;"
        "  border: 1px solid #B8E0C8;"
        "  border-radius: 12px;"
        "  padding: 4px 12px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QPushButton#maintenanceButton {"
        "  background: #B7791F;"
        "  color: #FFFFFF;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 0px 22px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 14px;"
        "  font-weight: 600;"
        "}"
        "QPushButton#maintenanceButton:hover { background: #C98A2A; }"
        "QPushButton#maintenanceButton:pressed { background: #9A6517; }"
        "QScrollArea#basicScroll { background: transparent; border: none; }"
        "QWidget#basicConfigContent { background: #F4F8FC; }"
        "QGroupBox#basicParamGroup {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "      stop:0 #FFFFFF, stop:1 #F4FAFF);"
        "  border: 1px solid #D8E8F3;"
        "  border-radius: 10px;"
        "  margin-top: 16px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "  color: #1F4E79;"
        "}"
        "QGroupBox#userGroup {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "      stop:0 #FFFFFF, stop:1 #F4FAFF);"
        "  border: 1px solid #D8E8F3;"
        "  border-radius: 10px;"
        "  margin-top: 16px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "  color: #1F4E79;"
        "}"
        "QGroupBox#basicParamGroup::title {"
        "  subcontrol-origin: margin;"
        "  left: 16px;"
        "  padding: 0px 6px;"
        "}"
        "QGroupBox#userGroup::title {"
        "  subcontrol-origin: margin;"
        "  left: 16px;"
        "  padding: 0px 6px;"
        "}"
        "QLabel#paramCaption {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "}"
        "QLabel#paramHint {"
        "  color: #8296A9;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 11px;"
        "}"
        "QFrame#paramSeparator { color: #E3EDF5; }"
        "QRadioButton {"
        "  color: #3A5065;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  spacing: 6px;"
        "}"
        "QCheckBox#channelCheck {"
        "  color: #3A5065;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  spacing: 6px;"
        "}"
        "QCheckBox#channelCheck[state=\"disabled\"] { color: #C0392B; }"
        "QTableWidget#userTable {"
        "  background: #FFFFFF;"
        "  alternate-background-color: #F7FBFE;"
        "  border: 1px solid #DCE7F1;"
        "  border-radius: 6px;"
        "  gridline-color: #E8F0F6;"
        "  selection-background-color: #DBEDFB;"
        "  selection-color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QHeaderView::section {"
        "  background: #EAF4FB;"
        "  color: #1F4E79;"
        "  border: none;"
        "  padding: 5px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QLineEdit#userInput {"
        "  background: #FFFFFF;"
        "  color: #1F2D3D;"
        "  border: 1px solid #C5D4E4;"
        "  border-radius: 5px;"
        "  padding: 4px 8px;"
        "  min-height: 24px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  selection-background-color: #2C7BBF;"
        "  selection-color: #FFFFFF;"
        "}"
        "QLineEdit#userInput:focus {"
        "  border: 1px solid #2C7BBF;"
        "  background: #F7FBFF;"
        "}"
        "QPushButton#userPrimaryButton {"
        "  background: #1F4E79;"
        "  color: #FFFFFF;"
        "  border: none;"
        "  border-radius: 5px;"
        "  padding: 0px 14px;"
        "  min-height: 28px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QPushButton#userPrimaryButton:hover { background: #2C6398; }"
        "QPushButton#userPrimaryButton:pressed { background: #163B5C; }"
        "QPushButton#userActionButton {"
        "  background: #FFFFFF;"
        "  color: #1F4E79;"
        "  border: 1px solid #8FB4D6;"
        "  border-radius: 5px;"
        "  padding: 0px 12px;"
        "  min-height: 28px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QPushButton#userActionButton:hover { background: #E8F1FA; }"
        "QPushButton#userActionButton:pressed { background: #D3E6F5; }"));

    m_stack = new QStackedWidget(this);
    m_basicPageIndex = m_stack->addWidget(createBasicPage());
    m_menuPageIndex = m_stack->addWidget(createMenuPage());
    m_modulePageIndex = m_stack->addWidget(createModuleControlPage());
    m_coordinatePageIndex = m_stack->addWidget(createCoordinatePage());
    m_stack->setCurrentIndex(m_basicPageIndex);
    setContentWidget(m_stack);

    if (m_commWorker && m_moduleControl) {
        connect(m_moduleControl, &ModuleControlWidget::frameReady,
                this, &SettingsDialog::sendFrameToInstrument);

        connect(m_commWorker, &CommunicationWorker::frameReceived,
                this, [this](const QByteArray &frame) {
            if (m_moduleControl) {
                m_moduleControl->handleIncomingFrame(frame);
            }
        });
        connect(m_commWorker, &CommunicationWorker::connectedChanged,
                this, &SettingsDialog::updateSerialState);
        connect(m_commWorker, &CommunicationWorker::errorOccurred,
                this, [this](const QString &message) {
            // 自动重连失败的信息显示在主界面状态栏；已连接时的运行错误才写日志
            if (m_moduleControl && m_serialConnected) {
                m_moduleControl->appendSystemMessage(message);
            }
        });
        connect(m_commWorker, &CommunicationWorker::sendResult,
                this, [this](const QString &description, bool ok,
                             const QString &error) {
            if (!ok && m_moduleControl) {
                m_moduleControl->appendSystemMessage(
                            tr("命令未发送：%1（%2）").arg(description, error));
            }
        });

        // 对话框创建时主动取一次当前连接状态，避免基础配置页显示过期状态
        QMetaObject::invokeMethod(m_commWorker, "requestConnectionState",
                                  Qt::QueuedConnection);
    }
    updateSerialState(false);
    updateBasicMachineInfo();
}

void SettingsDialog::sendFrameToInstrument(const QByteArray &frame,
                                           const QString &description)
{
    if (!m_commWorker) {
        if (m_moduleControl) {
            m_moduleControl->appendSystemMessage(tr("通讯线程未初始化，命令未发送。"));
        }
        return;
    }

    QMetaObject::invokeMethod(m_commWorker, "sendFrameRequested",
                              Qt::QueuedConnection,
                              Q_ARG(QByteArray, frame),
                              Q_ARG(QString, description));
}

void SettingsDialog::updateSerialState(bool connected)
{
    m_serialConnected = connected;
    if (m_moduleControl) {
        m_moduleControl->setSerialConnected(connected);
    }
    if (m_basicSerialValue) {
        m_basicSerialValue->setText(connected
                                    ? tr("已连接")
                                    : tr("未连接 / 自动重连中"));
        m_basicSerialValue->setProperty("state",
                                        connected ? "online" : "offline");
        m_basicSerialValue->style()->unpolish(m_basicSerialValue);
        m_basicSerialValue->style()->polish(m_basicSerialValue);
        m_basicSerialValue->update();
    }
}

void SettingsDialog::handleIncomingFrame(const QByteArray &frame)
{
    if (m_moduleControl) {
        m_moduleControl->handleIncomingFrame(frame);
    }
}

void SettingsDialog::showBasicPage()
{
    if (m_stack) {
        m_stack->setCurrentIndex(m_basicPageIndex);
    }
}

void SettingsDialog::showMaintenanceMenu()
{
    if (m_stack) {
        m_stack->setCurrentIndex(m_menuPageIndex);
    }
}

void SettingsDialog::promptMaintenanceLogin()
{
    MaintenancePasswordDialog dialog(this);

    // SettingsDialog 是嵌在主窗口标签页里的子控件，frameGeometry()
    // 是父级坐标，直接用来居中会把对话框放到屏幕左上角；
    // 这里改用顶层窗口的屏幕坐标居中。
    QWidget *anchor = window();
    if (anchor) {
        const QRect anchorRect = anchor->frameGeometry();
        dialog.move(anchorRect.center()
                    - QPoint(dialog.width() / 2, dialog.height() / 2));
    }

    if (dialog.exec() == QDialog::Accepted) {
        showMaintenanceMenu();
    }
}

void SettingsDialog::updateBasicMachineInfo()
{
    const int moduleCount = machineModuleCount(m_machineModel);
    if (m_basicModelValue) {
        m_basicModelValue->setText(
                    QString::fromLatin1(machineModelName(m_machineModel)));
    }
    if (m_basicModuleValue) {
        m_basicModuleValue->setText(tr("%1 个").arg(moduleCount));
    }
    if (m_basicChannelValue) {
        m_basicChannelValue->setText(tr("%1 路").arg(moduleCount * 4));
    }
}

QWidget *SettingsDialog::createBasicPage()
{
    auto *page = new QWidget;
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    auto *scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("basicScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);

    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("basicConfigContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(28, 20, 28, 24);
    layout->setSpacing(16);

    auto *header = new QHBoxLayout;
    header->setSpacing(16);

    auto *titleBox = new QVBoxLayout;
    titleBox->setSpacing(2);

    auto *title = new QLabel(tr("基础配置"), content);
    title->setObjectName(QStringLiteral("pageTitle"));
    auto *subtitle = new QLabel(tr("仪器基础信息、基础参数与用户管理"), content);
    subtitle->setObjectName(QStringLiteral("dialogSubtitle"));
    titleBox->addWidget(title);
    titleBox->addWidget(subtitle);
    header->addLayout(titleBox);
    header->addStretch(1);

    auto *maintenanceButton = new QPushButton(tr("维护"), content);
    maintenanceButton->setObjectName(QStringLiteral("maintenanceButton"));
    maintenanceButton->setCursor(Qt::PointingHandCursor);
    maintenanceButton->setMinimumSize(120, 38);
    connect(maintenanceButton, &QPushButton::clicked,
            this, &SettingsDialog::promptMaintenanceLogin);
    header->addWidget(maintenanceButton, 0, Qt::AlignVCenter);

    layout->addLayout(header);

    auto *infoCard = new QFrame(content);
    infoCard->setObjectName(QStringLiteral("basicInfoCard"));
    auto *infoLayout = new QHBoxLayout(infoCard);
    infoLayout->setContentsMargins(24, 18, 24, 18);
    infoLayout->setSpacing(18);

    auto addStat = [infoCard, infoLayout](const QString &caption,
                                          QLabel **valueOut) {
        auto *block = new QWidget(infoCard);
        auto *blockLayout = new QVBoxLayout(block);
        blockLayout->setContentsMargins(0, 0, 0, 0);
        blockLayout->setSpacing(5);

        auto *captionLabel = new QLabel(caption, block);
        captionLabel->setObjectName(QStringLiteral("statCaption"));
        auto *valueLabel = new QLabel(QStringLiteral("--"), block);
        valueLabel->setObjectName(QStringLiteral("statValue"));

        blockLayout->addWidget(captionLabel);
        blockLayout->addWidget(valueLabel);
        infoLayout->addWidget(block, 1);
        *valueOut = valueLabel;
    };

    auto addSeparator = [infoCard, infoLayout]() {
        auto *line = new QFrame(infoCard);
        line->setObjectName(QStringLiteral("statSeparator"));
        line->setFrameShape(QFrame::VLine);
        line->setFrameShadow(QFrame::Plain);
        infoLayout->addWidget(line);
    };

    addStat(tr("仪器机型"), &m_basicModelValue);
    addSeparator();
    addStat(tr("模组数量"), &m_basicModuleValue);
    addSeparator();
    addStat(tr("通道数量"), &m_basicChannelValue);
    addSeparator();
    addStat(tr("通讯状态"), &m_basicSerialValue);

    layout->addWidget(infoCard);

    auto *basicGroup = new QGroupBox(tr("基础参数"), content);
    basicGroup->setObjectName(QStringLiteral("basicParamGroup"));
    basicGroup->setMinimumHeight(280);
    auto *basicLayout = new QVBoxLayout(basicGroup);
    basicLayout->setContentsMargins(16, 20, 16, 14);
    basicLayout->setSpacing(10);

    auto *qualityCaption = new QLabel(tr("样本质量"), basicGroup);
    qualityCaption->setObjectName(QStringLiteral("paramCaption"));

    auto *qualityRow = new QHBoxLayout;
    qualityRow->setSpacing(18);
    auto *wholeBlood = new QRadioButton(tr("全血"), basicGroup);
    auto *plasma = new QRadioButton(tr("血浆"), basicGroup);
    auto *qualityGroup = new QButtonGroup(basicGroup);
    qualityGroup->setExclusive(true);
    qualityGroup->addButton(wholeBlood);
    qualityGroup->addButton(plasma);

    (sampleQualityIsPlasma() ? plasma : wholeBlood)->setChecked(true);

    qualityRow->addWidget(wholeBlood);
    qualityRow->addWidget(plasma);
    qualityRow->addStretch(1);
    basicLayout->addWidget(qualityCaption);
    basicLayout->addLayout(qualityRow);

    connect(wholeBlood, &QRadioButton::toggled, this, [](bool checked) {
        if (!checked) {
            return;
        }
        QSettings s;
        s.setValue(QString::fromLatin1(sampleQualitySettingsKey()),
                   QStringLiteral("wholeBlood"));
    });
    connect(plasma, &QRadioButton::toggled, this, [](bool checked) {
        if (!checked) {
            return;
        }
        QSettings s;
        s.setValue(QString::fromLatin1(sampleQualitySettingsKey()),
                   QStringLiteral("plasma"));
    });

    auto *separator = new QFrame(basicGroup);
    separator->setObjectName(QStringLiteral("paramSeparator"));
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Plain);
    basicLayout->addWidget(separator);

    auto *channelCaption = new QLabel(tr("通道状态"), basicGroup);
    channelCaption->setObjectName(QStringLiteral("paramCaption"));
    auto *channelHint = new QLabel(
                tr("按当前机型显示通道；禁用的通道在主界面显示红叉，不参与测试。"),
                basicGroup);
    channelHint->setObjectName(QStringLiteral("paramHint"));
    channelHint->setWordWrap(true);
    basicLayout->addWidget(channelCaption);
    basicLayout->addWidget(channelHint);

    auto *channelGrid = new QGridLayout;
    channelGrid->setHorizontalSpacing(12);
    channelGrid->setVerticalSpacing(6);
    const int channelCount = machineModuleCount(m_machineModel) * 4;
    for (int channel = 1; channel <= channelCount; ++channel) {
        auto *check = new QCheckBox(basicGroup);
        check->setObjectName(QStringLiteral("channelCheck"));
        const bool enabled = channelEnabledFromSettings(m_machineModel, channel);
        refreshChannelCheckBox(check, channel, enabled);
        check->setChecked(enabled);
        connect(check, &QCheckBox::toggled, this,
                [this, channel, check](bool checked) {
            setChannelEnabledInSettings(m_machineModel, channel, checked);
            refreshChannelCheckBox(check, channel, checked);
            emit channelEnabledChanged(channel, checked);
        });
        channelGrid->addWidget(check, (channel - 1) / 4, (channel - 1) % 4);
    }
    basicLayout->addLayout(channelGrid);
    basicLayout->addStretch(1);

    auto *userGroup = new QGroupBox(tr("用户管理"), content);
    userGroup->setObjectName(QStringLiteral("userGroup"));
    userGroup->setMinimumHeight(330);
    auto *userLayout = new QVBoxLayout(userGroup);
    userLayout->setContentsMargins(16, 20, 16, 14);
    userLayout->setSpacing(8);

    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(8);
    m_userSearchEdit = new QLineEdit(userGroup);
    m_userSearchEdit->setObjectName(QStringLiteral("userInput"));
    m_userSearchEdit->setPlaceholderText(tr("按用户名查询"));
    auto *queryButton = new QPushButton(tr("查询"), userGroup);
    queryButton->setObjectName(QStringLiteral("userActionButton"));
    queryButton->setCursor(Qt::PointingHandCursor);
    queryButton->setFixedHeight(28);
    searchRow->addWidget(m_userSearchEdit, 1);
    searchRow->addWidget(queryButton);
    userLayout->addLayout(searchRow);

    m_userTable = new QTableWidget(0, 2, userGroup);
    m_userTable->setObjectName(QStringLiteral("userTable"));
    m_userTable->setHorizontalHeaderLabels(
                QStringList() << tr("用户名") << tr("权限"));
    m_userTable->horizontalHeader()->setSectionResizeMode(
                0, QHeaderView::Stretch);
    m_userTable->horizontalHeader()->setSectionResizeMode(
                1, QHeaderView::ResizeToContents);
    m_userTable->verticalHeader()->hide();
    m_userTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_userTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_userTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_userTable->setAlternatingRowColors(true);
    m_userTable->setMinimumHeight(150);
    userLayout->addWidget(m_userTable, 1);

    auto *formGrid = new QGridLayout;
    formGrid->setHorizontalSpacing(8);
    formGrid->setVerticalSpacing(4);

    auto *nameCaption = new QLabel(tr("用户名"), userGroup);
    nameCaption->setObjectName(QStringLiteral("paramHint"));
    auto *passwordCaption = new QLabel(tr("密码"), userGroup);
    passwordCaption->setObjectName(QStringLiteral("paramHint"));
    auto *roleCaption = new QLabel(tr("权限"), userGroup);
    roleCaption->setObjectName(QStringLiteral("paramHint"));

    m_userNameEdit = new QLineEdit(userGroup);
    m_userNameEdit->setObjectName(QStringLiteral("userInput"));
    m_userNameEdit->setPlaceholderText(tr("用户名"));
    m_userPasswordEdit = new QLineEdit(userGroup);
    m_userPasswordEdit->setObjectName(QStringLiteral("userInput"));
    m_userPasswordEdit->setEchoMode(QLineEdit::Password);
    m_userPasswordEdit->setPlaceholderText(tr("修改时留空表示不改密码"));
    m_userRoleCombo = new MedicalComboBox(userGroup);
    m_userRoleCombo->setObjectName(QStringLiteral("userRoleCombo"));
    m_userRoleCombo->addItem(tr("管理员"), QStringLiteral("admin"));
    m_userRoleCombo->addItem(tr("操作员"), QStringLiteral("operator"));
    m_userRoleCombo->setCurrentIndex(1);   // 新增用户默认操作员，更安全

    formGrid->addWidget(nameCaption, 0, 0);
    formGrid->addWidget(passwordCaption, 0, 1);
    formGrid->addWidget(roleCaption, 0, 2);
    formGrid->addWidget(m_userNameEdit, 1, 0);
    formGrid->addWidget(m_userPasswordEdit, 1, 1);
    formGrid->addWidget(m_userRoleCombo, 1, 2);
    formGrid->setColumnStretch(0, 1);
    formGrid->setColumnStretch(1, 1);
    formGrid->setColumnStretch(2, 1);
    userLayout->addLayout(formGrid);

    auto *userButtons = new QHBoxLayout;
    userButtons->setSpacing(8);
    auto *addButton = new QPushButton(tr("新增"), userGroup);
    addButton->setObjectName(QStringLiteral("userPrimaryButton"));
    addButton->setCursor(Qt::PointingHandCursor);
    addButton->setFixedHeight(28);
    auto *editButton = new QPushButton(tr("修改"), userGroup);
    editButton->setObjectName(QStringLiteral("userActionButton"));
    editButton->setCursor(Qt::PointingHandCursor);
    editButton->setFixedHeight(28);
    auto *deleteButton = new QPushButton(tr("删除"), userGroup);
    deleteButton->setObjectName(QStringLiteral("userActionButton"));
    deleteButton->setCursor(Qt::PointingHandCursor);
    deleteButton->setFixedHeight(28);
    userButtons->addWidget(addButton);
    userButtons->addWidget(editButton);
    userButtons->addWidget(deleteButton);
    userButtons->addStretch(1);
    userLayout->addLayout(userButtons);

    connect(addButton, &QPushButton::clicked,
            this, &SettingsDialog::addUserFromForm);
    connect(editButton, &QPushButton::clicked,
            this, &SettingsDialog::updateUserFromForm);
    connect(deleteButton, &QPushButton::clicked,
            this, &SettingsDialog::deleteSelectedUser);
    connect(queryButton, &QPushButton::clicked, this, [this]() {
        reloadUserTable(m_userSearchEdit->text().trimmed());
    });
    connect(m_userSearchEdit, &QLineEdit::returnPressed, this, [this]() {
        reloadUserTable(m_userSearchEdit->text().trimmed());
    });
    connect(m_userTable, &QTableWidget::itemSelectionChanged,
            this, [this]() {
        const int row = m_userTable->currentRow();
        if (row < 0 || !m_userTable->item(row, 0)) {
            return;
        }
        m_userNameEdit->setText(m_userTable->item(row, 0)->text());
        m_userPasswordEdit->clear();
        const QTableWidgetItem *roleItem = m_userTable->item(row, 1);
        const QString role = roleItem
                ? roleItem->data(Qt::UserRole).toString()
                : QStringLiteral("operator");
        const int index = m_userRoleCombo->findData(role);
        if (index >= 0) {
            m_userRoleCombo->setCurrentIndex(index);
        }
    });

    auto *settingsRow = new QHBoxLayout;
    settingsRow->setSpacing(16);
    settingsRow->addWidget(basicGroup, 5);
    settingsRow->addWidget(userGroup, 5);
    layout->addLayout(settingsRow, 1);

    auto *noteCard = new QFrame(content);
    noteCard->setObjectName(QStringLiteral("basicNoteCard"));
    auto *noteLayout = new QVBoxLayout(noteCard);
    noteLayout->setContentsMargins(20, 16, 20, 16);
    noteLayout->setSpacing(6);

    auto *noteTitle = new QLabel(tr("维护设置"), noteCard);
    noteTitle->setObjectName(QStringLiteral("basicNoteTitle"));
    auto *noteText = new QLabel(
                tr("样本质量、通道状态和用户管理在此页设置；样本针、试剂针、抓手、"
                   "模组控制、打印和坐标维护属于维护功能，点击右上角「维护」"
                   "并输入维护密码后进入。"),
                noteCard);
    noteText->setObjectName(QStringLiteral("basicNoteText"));
    noteText->setWordWrap(true);

    noteLayout->addWidget(noteTitle);
    noteLayout->addWidget(noteText);
    layout->addWidget(noteCard);

    scroll->setWidget(content);
    pageLayout->addWidget(scroll);

    ensureUserRecords();
    reloadUserTable();

    updateBasicMachineInfo();
    return page;
}

QString SettingsDialog::selectedUserName() const
{
    if (!m_userTable) {
        return QString();
    }
    const int row = m_userTable->currentRow();
    if (row < 0) {
        return QString();
    }
    const QTableWidgetItem *item = m_userTable->item(row, 0);
    return item ? item->text() : QString();
}

void SettingsDialog::reloadUserTable(const QString &filter)
{
    if (!m_userTable) {
        return;
    }

    const QJsonArray users = ensureUserRecords();
    m_userTable->setRowCount(0);
    m_userTable->clearSelection();

    int row = 0;
    for (const QJsonValue &value : users) {
        const QJsonObject user = value.toObject();
        const QString name = user.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) {
            continue;
        }
        if (!filter.isEmpty()
                && !name.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }

        const QString role = user.value(QStringLiteral("role")).toString();
        m_userTable->insertRow(row);
        auto *nameItem = new QTableWidgetItem(name);
        nameItem->setData(Qt::UserRole, role);
        auto *roleItem = new QTableWidgetItem(userRoleDisplayName(role));
        roleItem->setData(Qt::UserRole, role);
        m_userTable->setItem(row, 0, nameItem);
        m_userTable->setItem(row, 1, roleItem);
        ++row;
    }
}

void SettingsDialog::addUserFromForm()
{
    if (!m_userNameEdit || !m_userPasswordEdit || !m_userRoleCombo) {
        return;
    }

    const QString name = m_userNameEdit->text().trimmed();
    const QString password = m_userPasswordEdit->text();
    if (name.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, tr("用户管理"),
                             tr("用户名和密码不能为空。"));
        return;
    }

    QJsonArray users = ensureUserRecords();
    if (findUserRecord(users, name) >= 0) {
        QMessageBox::warning(this, tr("用户管理"),
                             tr("用户名“%1”已存在。").arg(name));
        return;
    }

    QString role = m_userRoleCombo->currentData().toString();
    if (role.isEmpty()) {
        role = QStringLiteral("operator");
    }
    const QString salt = makeUserSalt();
    QJsonObject user;
    user.insert(QStringLiteral("name"), name);
    user.insert(QStringLiteral("salt"), salt);
    user.insert(QStringLiteral("passwordHash"),
                hashUserPassword(password, salt));
    user.insert(QStringLiteral("role"), role);
    users.append(user);
    saveUserRecords(users);

    m_userPasswordEdit->clear();
    if (m_userSearchEdit) {
        m_userSearchEdit->clear();
    }
    reloadUserTable();
}

void SettingsDialog::updateUserFromForm()
{
    if (!m_userNameEdit || !m_userPasswordEdit || !m_userRoleCombo) {
        return;
    }

    const QString originalName = selectedUserName();
    if (originalName.isEmpty()) {
        QMessageBox::warning(this, tr("用户管理"),
                             tr("请先在列表中选择要修改的用户。"));
        return;
    }

    const QString newName = m_userNameEdit->text().trimmed();
    if (newName.isEmpty()) {
        QMessageBox::warning(this, tr("用户管理"), tr("用户名不能为空。"));
        return;
    }

    QJsonArray users = ensureUserRecords();
    const int index = findUserRecord(users, originalName);
    if (index < 0) {
        return;
    }
    if (newName != originalName && findUserRecord(users, newName) >= 0) {
        QMessageBox::warning(this, tr("用户管理"),
                             tr("用户名“%1”已存在。").arg(newName));
        return;
    }

    QJsonObject user = users.at(index).toObject();
    const QString oldRole = user.value(QStringLiteral("role")).toString();
    QString newRole = m_userRoleCombo->currentData().toString();
    if (newRole.isEmpty()) {
        newRole = QStringLiteral("operator");
    }

    if (oldRole == QLatin1String("admin")
            && newRole != QLatin1String("admin")) {
        int adminCount = 0;
        for (const QJsonValue &value : users) {
            if (value.toObject().value(QStringLiteral("role")).toString()
                    == QLatin1String("admin")) {
                ++adminCount;
            }
        }
        if (adminCount <= 1) {
            QMessageBox::warning(this, tr("用户管理"),
                                 tr("至少需要保留一个管理员账号。"));
            return;
        }
    }

    user.insert(QStringLiteral("name"), newName);
    user.insert(QStringLiteral("role"), newRole);
    if (!m_userPasswordEdit->text().isEmpty()) {
        const QString salt = makeUserSalt();
        user.insert(QStringLiteral("salt"), salt);
        user.insert(QStringLiteral("passwordHash"),
                    hashUserPassword(m_userPasswordEdit->text(), salt));
    }
    users.replace(index, user);
    saveUserRecords(users);

    m_userPasswordEdit->clear();
    if (m_userSearchEdit) {
        m_userSearchEdit->clear();
    }
    reloadUserTable();
}

void SettingsDialog::deleteSelectedUser()
{
    const QString name = selectedUserName();
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("用户管理"),
                             tr("请先在列表中选择要删除的用户。"));
        return;
    }

    QJsonArray users = ensureUserRecords();
    const int index = findUserRecord(users, name);
    if (index < 0) {
        return;
    }

    const QJsonObject user = users.at(index).toObject();
    if (user.value(QStringLiteral("role")).toString()
            == QLatin1String("admin")) {
        int adminCount = 0;
        for (const QJsonValue &value : users) {
            if (value.toObject().value(QStringLiteral("role")).toString()
                    == QLatin1String("admin")) {
                ++adminCount;
            }
        }
        if (adminCount <= 1) {
            QMessageBox::warning(this, tr("用户管理"),
                                 tr("至少需要保留一个管理员账号。"));
            return;
        }
    }

    if (QMessageBox::question(this, tr("删除用户"),
                              tr("确定删除用户“%1”吗？").arg(name),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    users.removeAt(index);
    saveUserRecords(users);
    if (m_userNameEdit) {
        m_userNameEdit->clear();
    }
    if (m_userPasswordEdit) {
        m_userPasswordEdit->clear();
    }
    if (m_userSearchEdit) {
        m_userSearchEdit->clear();
    }
    reloadUserTable();
}

QWidget *SettingsDialog::createMenuPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 20, 28, 24);
    layout->setSpacing(16);

    auto *header = new QHBoxLayout;
    header->setSpacing(12);

    auto *backButton = new QPushButton(tr("← 基础配置"), page);
    backButton->setObjectName(QStringLiteral("backButton"));
    backButton->setCursor(Qt::PointingHandCursor);
    backButton->setFixedHeight(34);
    connect(backButton, &QPushButton::clicked,
            this, &SettingsDialog::showBasicPage);
    header->addWidget(backButton);

    auto *title = new QLabel(tr("维护设置"), page);
    title->setObjectName(QStringLiteral("pageTitle"));
    header->addWidget(title);
    header->addStretch(1);

    auto *maintenanceBadge = new QLabel(tr("● 维护模式"), page);
    maintenanceBadge->setObjectName(QStringLiteral("maintenanceBadge"));
    header->addWidget(maintenanceBadge, 0, Qt::AlignVCenter);

    layout->addLayout(header);

    auto *subtitle = new QLabel(tr("选择要调整的参数模块"), page);
    subtitle->setObjectName(QStringLiteral("dialogSubtitle"));
    layout->addWidget(subtitle);

    enum class MenuTarget {
        Placeholder,
        ModuleControl,
        Coordinates
    };
    struct SettingEntry {
        QString title;
        QString description;
        MenuTarget target;
    };

    const SettingEntry entries[] = {
        { tr("样本针参数"), tr("吸样位置、清洗位置与移动速度"), MenuTarget::Placeholder },
        { tr("试剂针参数"), tr("试剂吸取、液面检测与清洗参数"), MenuTarget::Placeholder },
        { tr("抓手参数"),   tr("抓杯、丢杯、气路与延时参数"),     MenuTarget::Placeholder },
        { tr("模组控制"),   tr("温度、调光、LED 电流与电机转速"), MenuTarget::ModuleControl },
        { tr("打印设置"),   tr("报告模板、打印机与纸张设置"),     MenuTarget::Placeholder },
        { tr("坐标维护"),   tr("各工位 XY 与 Z 轴坐标维护"),       MenuTarget::Coordinates }
    };
    const int columnCount = 3;
    const int entryCount = int(sizeof(entries) / sizeof(entries[0]));

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(16);

    for (int i = 0; i < entryCount; ++i) {
        auto *card = createSettingCard(entries[i].title, entries[i].description, page);
        grid->addWidget(card, i / columnCount, i % columnCount);

        const QString entryTitle = entries[i].title;
        const MenuTarget target = entries[i].target;
        connect(card, &QPushButton::clicked, this,
                [this, entryTitle, target]() {
            if (target == MenuTarget::ModuleControl) {
                m_stack->setCurrentIndex(m_modulePageIndex);
                return;
            }
            if (target == MenuTarget::Coordinates) {
                if (m_coordinateWidget) {
                    m_coordinateWidget->refreshAll();
                }
                m_stack->setCurrentIndex(m_coordinatePageIndex);
                return;
            }
            QMessageBox::information(this, entryTitle,
                                     tr("该设置界面尚未实现，后续再补充。"));
        });
    }

    for (int column = 0; column < columnCount; ++column) {
        grid->setColumnStretch(column, 1);
    }
    grid->setRowStretch(0, 1);
    grid->setRowStretch(1, 1);
    layout->addLayout(grid, 1);

    return page;
}

QWidget *SettingsDialog::createModuleControlPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 16, 20, 18);
    layout->setSpacing(12);

    auto *header = new QHBoxLayout;
    header->setSpacing(12);

    auto *backButton = new QPushButton(tr("← 返回"), page);
    backButton->setObjectName(QStringLiteral("backButton"));
    backButton->setCursor(Qt::PointingHandCursor);
    backButton->setFixedHeight(34);
    connect(backButton, &QPushButton::clicked, this, [this]() {
        m_stack->setCurrentIndex(m_menuPageIndex);
    });
    header->addWidget(backButton);

    auto *title = new QLabel(tr("模组控制"), page);
    title->setObjectName(QStringLiteral("pageTitle"));
    header->addWidget(title);
    header->addStretch();

    layout->addLayout(header);

    m_moduleControl = new ModuleControlWidget(page);
    m_moduleControl->bindCommunicationWorker(m_commWorker);
    connect(m_moduleControl, &ModuleControlWidget::frameReady,
            this, &SettingsDialog::frameReady);
    layout->addWidget(m_moduleControl, 1);

    return page;
}

QWidget *SettingsDialog::createCoordinatePage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 16, 20, 18);
    layout->setSpacing(12);

    auto *header = new QHBoxLayout;
    header->setSpacing(12);

    auto *backButton = new QPushButton(tr("← 返回"), page);
    backButton->setObjectName(QStringLiteral("backButton"));
    backButton->setCursor(Qt::PointingHandCursor);
    backButton->setFixedHeight(34);
    connect(backButton, &QPushButton::clicked, this, [this]() {
        m_stack->setCurrentIndex(m_menuPageIndex);
    });
    header->addWidget(backButton);

    auto *title = new QLabel(tr("坐标维护"), page);
    title->setObjectName(QStringLiteral("pageTitle"));
    header->addWidget(title);
    header->addStretch();

    auto *badge = new QLabel(tr("● 程序目录坐标文件"), page);
    badge->setObjectName(QStringLiteral("maintenanceBadge"));
    header->addWidget(badge, 0, Qt::AlignVCenter);
    layout->addLayout(header);

    auto *subtitle = new QLabel(
                tr("维护原点、清洗孔、弃杯孔、通道、试管、试剂位和样本孔的"
                   "XY 与各 Z 轴高度；修改后写入程序目录下的坐标文件，"
                   "软件启动时自动恢复。"), page);
    subtitle->setObjectName(QStringLiteral("dialogSubtitle"));
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    m_coordinateWidget = new CoordinateMaintenanceWidget(m_coordinates, page);
    connect(m_coordinateWidget,
            &CoordinateMaintenanceWidget::motionRequested,
            this, &SettingsDialog::coordinateMotionRequested);
    layout->addWidget(m_coordinateWidget, 1);

    return page;
}
