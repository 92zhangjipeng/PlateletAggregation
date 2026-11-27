#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "settingsdialog.h"
#include "coordinatemaintenancewidget.h"
#include "modulecontrolwidget.h"
#include "CommunicationWorker.h"
#include "appconfig.h"
#include "heightcamcontroller.h"
#include "instrumentparameters.h"
#include "informationentry.h"
#include "referenceinterval.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
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
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include "MedicalMessageBox.h"
#include <QMetaObject>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QRadioButton>
#include <QScrollArea>
#include <QSettings>
#include <QStringList>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTabWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>

namespace {

/** 维护密码；后续可改成从配置/用户权限表读取 */
constexpr char kMaintenancePassword[] = "1";

QPushButton *createSettingCard(const QString &icon,
                               const QString &title,
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

    auto *iconLabel = new QLabel(icon, card);
    iconLabel->setObjectName(QStringLiteral("cardIcon"));
    iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto *titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("cardTitle"));
    titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto *descriptionLabel = new QLabel(description, card);
    descriptionLabel->setObjectName(QStringLiteral("cardDesc"));
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    layout->addWidget(iconLabel);
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
    if (role == QLatin1String("maintenance")) {
        return QObject::tr("维护人员");
    }
    if (role == QLatin1String("admin")) {
        return QObject::tr("管理员");
    }
    return QObject::tr("操作员");
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

/** 内置测试账号 test / 空密码，权限为维护人员。
 *  每次启动都把已存在的 test 记录归位为空密码：旧版本可能留下 test/1 等
 *  密码，导致登录界面选中 test 不输入密码时提示“用户名或密码错误”。 */
void ensureTestUserRecord()
{
    QJsonArray users = ensureUserRecords();
    const int index = findUserRecord(users, QStringLiteral("test"));

    QJsonObject user = index >= 0 ? users.at(index).toObject() : QJsonObject();
    bool changed = (index < 0);

    QString salt = user.value(QStringLiteral("salt")).toString();
    if (salt.isEmpty()) {
        salt = makeUserSalt();
        user.insert(QStringLiteral("salt"), salt);
        changed = true;
    }

    const QString emptyPasswordHash = hashUserPassword(QString(), salt);
    if (user.value(QStringLiteral("passwordHash")).toString()
            != emptyPasswordHash) {
        user.insert(QStringLiteral("passwordHash"), emptyPasswordHash);
        changed = true;
    }

    if (user.value(QStringLiteral("role")).toString()
            != QLatin1String("maintenance")) {
        user.insert(QStringLiteral("role"), QStringLiteral("maintenance"));
        changed = true;
    }

    if (!changed) {
        return;
    }
    user.insert(QStringLiteral("name"), QStringLiteral("test"));
    if (index >= 0) {
        users.replace(index, user);
    } else {
        users.append(user);
    }
    saveUserRecords(users);
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

bool SettingsDialog::validateUserCredentials(const QString &name,
                                             const QString &password,
                                             QString *role)
{
    const QJsonArray users = ensureUserRecords();
    const int index = findUserRecord(users, name);
    if (index < 0) {
        return false;
    }
    const QJsonObject user = users.at(index).toObject();
    const QString salt = user.value(QStringLiteral("salt")).toString();
    const QString expected =
            user.value(QStringLiteral("passwordHash")).toString();
    if (expected.isEmpty()
            || hashUserPassword(password, salt) != expected) {
        return false;
    }
    if (role) {
        *role = user.value(QStringLiteral("role")).toString();
    }
    return true;
}

void SettingsDialog::ensureTestUser()
{
    ensureTestUserRecord();
}

void SettingsDialog::setCurrentUser(const QString &name, const QString &role)
{
    m_currentUserName = name.trimmed();
    m_currentUserRole = role.trimmed();
    applyUserManagementAccess();
}

void SettingsDialog::refreshCoordinates()
{
    if (m_coordinateWidget) {
        m_coordinateWidget->refreshAll();
    }
}

void SettingsDialog::applyUserManagementAccess()
{
    const bool maintenance =
            m_currentUserRole == QLatin1String("maintenance");
    const bool canManage = maintenance
            || m_currentUserRole == QLatin1String("admin");
    if (m_userAddButton) {
        m_userAddButton->setEnabled(canManage);
    }
    if (m_userEditButton) {
        m_userEditButton->setEnabled(canManage);
    }
    if (m_userDeleteButton) {
        m_userDeleteButton->setEnabled(canManage);
    }
    // 只有维护人员可以指定其它权限；管理员新增时固定为操作员。
    if (m_userRoleCombo) {
        m_userRoleCombo->setEnabled(maintenance);
    }
}

QStringList SettingsDialog::existingUserNames()
{
    const QJsonArray users = ensureUserRecords();
    QStringList names;
    names.reserve(users.size());
    for (const QJsonValue &value : users) {
        const QString name = value.toObject()
                .value(QStringLiteral("name")).toString().trimmed();
        if (!name.isEmpty() && !names.contains(name)) {
            names.append(name);
        }
    }
    return names;
}

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
        "QLabel#cardIcon {"
        "  font-size: 22px;"
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
        "QCheckBox#channelCheck, QCheckBox#scanCheck, "
        "QCheckBox#backupReagentCheck {"
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
        "QListWidget#informationCategoryList,"
        "QListWidget#informationValueList {"
        "  background: #FFFFFF;"
        "  border: 1px solid #DCE7F1;"
        "  border-radius: 6px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  color: #3A5065;"
        "  outline: none;"
        "}"
        "QListWidget#informationCategoryList::item,"
        "QListWidget#informationValueList::item {"
        "  padding: 7px 10px;"
        "  border-bottom: 1px solid #EEF4F8;"
        "}"
        "QListWidget#informationCategoryList::item:selected,"
        "QListWidget#informationValueList::item:selected {"
        "  background: #DBEDFB;"
        "  color: #1F4E79;"
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
    m_instrumentParamsPageIndex = m_stack->addWidget(
                createInstrumentParamsPage());
    m_sampleNeedlePageIndex = m_stack->addWidget(
                createSampleNeedlePage());
    m_reagentNeedlePageIndex = m_stack->addWidget(
                createReagentNeedlePage());
    m_gripperPageIndex = m_stack->addWidget(createGripperPage());
    m_referenceRangePageIndex = m_stack->addWidget(
                createReferenceRangePage());
    m_informationEntryPageIndex = m_stack->addWidget(
                createInformationEntryPage());
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

void SettingsDialog::setActiveSampleCountProvider(
        std::function<int()> provider)
{
    m_activeSampleCountProvider = provider;
}

int SettingsDialog::activeSampleCount() const
{
    return m_activeSampleCountProvider ? m_activeSampleCountProvider() : 0;
}

bool SettingsDialog::ensureSampleShapeEditable(const QString &what)
{
    const int count = activeSampleCount();
    if (count <= 0) {
        return true;
    }
    MedicalMessageBox::warning(
                this, tr("无法修改"),
                tr("当前有 %1 个待测/测试中样本，不能修改%2。\n"
                   "请先完成测试或删除待测样本后再修改。")
                .arg(count)
                .arg(what));
    return false;
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
    basicGroup->setMinimumHeight(330);
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

    auto *qualityLockHint = new QLabel(
                tr("存在待测/测试中样本时，样本质量不可修改；"
                   "请先完成测试或删除待测样本。"),
                basicGroup);
    qualityLockHint->setObjectName(QStringLiteral("paramHint"));
    qualityLockHint->setWordWrap(true);
    basicLayout->addWidget(qualityLockHint);

    auto *plasmaHeightRow = new QHBoxLayout;
    plasmaHeightRow->setSpacing(12);
    auto *plasmaHeightCaption = new QLabel(
                tr("血浆模式吸PRP高度"), basicGroup);
    plasmaHeightCaption->setObjectName(QStringLiteral("paramCaption"));
    auto *plasmaHeightSpin = new MedicalSpinBox(basicGroup);
    plasmaHeightSpin->setRange(20, 99);
    plasmaHeightSpin->setSuffix(tr(" mm"));
    plasmaHeightSpin->setValue(plasmaPrpAspirateHeightFromSettings());
    plasmaHeightSpin->setMinimumWidth(150);
    plasmaHeightRow->addWidget(plasmaHeightCaption);
    plasmaHeightRow->addWidget(plasmaHeightSpin);
    plasmaHeightRow->addStretch(1);
    basicLayout->addLayout(plasmaHeightRow);

    auto *plasmaHeightHint = new QLabel(
                tr("血浆模式不测高，添加样本时「吸样高度」直接使用该值；"
                   "修改后对之后添加的待定样本生效，已确认样本保持确认时高度。"),
                basicGroup);
    plasmaHeightHint->setObjectName(QStringLiteral("paramHint"));
    plasmaHeightHint->setWordWrap(true);
    basicLayout->addWidget(plasmaHeightHint);

    connect(plasmaHeightSpin,
            static_cast<void (QSpinBox::*)(int)>(
                &QSpinBox::valueChanged),
            this, [](int value) {
        setPlasmaPrpAspirateHeightInSettings(value);
    });

    auto *washTimeRow = new QHBoxLayout;
    washTimeRow->setSpacing(12);
    auto *washTimeCaption = new QLabel(tr("泵清洗时间"), basicGroup);
    washTimeCaption->setObjectName(QStringLiteral("paramCaption"));
    auto *washTimeSpin = new MedicalSpinBox(basicGroup);
    washTimeSpin->setRange(500, 65535);
    washTimeSpin->setSingleStep(100);
    washTimeSpin->setSuffix(tr(" ms"));
    washTimeSpin->setValue(perfusionCleaningTimeMsFromSettings());
    washTimeSpin->setMinimumWidth(150);
    washTimeRow->addWidget(washTimeCaption);
    washTimeRow->addWidget(washTimeSpin);
    washTimeRow->addStretch(1);
    basicLayout->addLayout(washTimeRow);

    auto *washTimeHint = new QLabel(
                tr("开机灌注时，样本泵/试剂泵吐回清洗池后保持冲洗的毫秒数；"
                   "默认 6000ms，修改后下次开机灌注生效。"),
                basicGroup);
    washTimeHint->setObjectName(QStringLiteral("paramHint"));
    washTimeHint->setWordWrap(true);
    basicLayout->addWidget(washTimeHint);

    connect(washTimeSpin,
            static_cast<void (QSpinBox::*)(int)>(
                &QSpinBox::valueChanged),
            this, [](int value) {
        setPerfusionCleaningTimeMsInSettings(value);
    });

    auto *heatTimeoutRow = new QHBoxLayout;
    heatTimeoutRow->setSpacing(12);
    auto *heatTimeoutCaption = new QLabel(tr("模组升温超时"), basicGroup);
    heatTimeoutCaption->setObjectName(QStringLiteral("paramCaption"));
    auto *heatTimeoutSpin = new MedicalSpinBox(basicGroup);
    heatTimeoutSpin->setRange(10, 3600);
    heatTimeoutSpin->setSingleStep(10);
    heatTimeoutSpin->setSuffix(tr(" 秒"));
    heatTimeoutSpin->setValue(moduleHeatingTimeoutSecondsFromSettings());
    heatTimeoutSpin->setMinimumWidth(150);
    heatTimeoutRow->addWidget(heatTimeoutCaption);
    heatTimeoutRow->addWidget(heatTimeoutSpin);
    heatTimeoutRow->addStretch(1);
    basicLayout->addLayout(heatTimeoutRow);

    auto *heatTimeoutHint = new QLabel(
                tr("联机启动校准中，所有模组升温到 37℃ 的最长等待时间；"
                   "默认 60 秒。升温提示框右下角会显示剩余时间，"
                   "超时仍未达到 37℃ 则中止启动校准。"),
                basicGroup);
    heatTimeoutHint->setObjectName(QStringLiteral("paramHint"));
    heatTimeoutHint->setWordWrap(true);
    basicLayout->addWidget(heatTimeoutHint);

    connect(heatTimeoutSpin,
            static_cast<void (QSpinBox::*)(int)>(
                &QSpinBox::valueChanged),
            this, [](int value) {
        setModuleHeatingTimeoutSecondsInSettings(value);
    });

    auto restoreQualitySelection = [wholeBlood, plasma]() {
        QSignalBlocker wholeBlocker(wholeBlood);
        QSignalBlocker plasmaBlocker(plasma);
        (sampleQualityIsPlasma() ? plasma : wholeBlood)->setChecked(true);
    };
    connect(wholeBlood, &QRadioButton::toggled, this,
            [this, restoreQualitySelection](bool checked) {
        if (!checked) {
            return;
        }
        if (!ensureSampleShapeEditable(tr("样本质量"))) {
            restoreQualitySelection();
            return;
        }
        QSettings s;
        s.setValue(QString::fromLatin1(sampleQualitySettingsKey()),
                   QStringLiteral("wholeBlood"));
        emit sampleQualityChanged();
    });
    connect(plasma, &QRadioButton::toggled, this,
            [this, restoreQualitySelection](bool checked) {
        if (!checked) {
            return;
        }
        if (!ensureSampleShapeEditable(tr("样本质量"))) {
            restoreQualitySelection();
            return;
        }
        QSettings s;
        s.setValue(QString::fromLatin1(sampleQualitySettingsKey()),
                   QStringLiteral("plasma"));
        emit sampleQualityChanged();
    });

    auto *scanRow = new QHBoxLayout;
    scanRow->setSpacing(12);
    auto *scanCaption = new QLabel(tr("加样扫码"), basicGroup);
    scanCaption->setObjectName(QStringLiteral("paramCaption"));
    auto *scanCheck = new MedicalCheckBox(
                tr("每添加一个样本必须扫描/输入条码"), basicGroup);
    scanCheck->setObjectName(QStringLiteral("scanCheck"));
    scanCheck->setChecked(addSampleScanBarcodeEnabled());
    scanRow->addWidget(scanCaption);
    scanRow->addWidget(scanCheck);
    scanRow->addStretch(1);
    basicLayout->addLayout(scanRow);

    auto *scanHint = new QLabel(
                tr("开启后添加样本会立即定位到条码栏，扫描或手动输入条码后"
                   "才能继续添加；关闭时条码可留空。"),
                basicGroup);
    scanHint->setObjectName(QStringLiteral("paramHint"));
    scanHint->setWordWrap(true);
    basicLayout->addWidget(scanHint);

    connect(scanCheck, &QCheckBox::toggled, this, [](bool checked) {
        setAddSampleScanBarcodeEnabled(checked);
    });

    auto *backupRow = new QHBoxLayout;
    backupRow->setSpacing(12);
    auto *backupCaption = new QLabel(tr("预备试剂位"), basicGroup);
    backupCaption->setObjectName(QStringLiteral("paramCaption"));
    auto *backupCheck = new MedicalCheckBox(
                tr("启用预备试剂位"), basicGroup);
    backupCheck->setObjectName(QStringLiteral("backupReagentCheck"));
    backupCheck->setChecked(reagentBackupPositionEnabled());
    backupRow->addWidget(backupCaption);
    backupRow->addWidget(backupCheck);
    backupRow->addStretch(1);
    basicLayout->addLayout(backupRow);

    auto *backupHint = new QLabel(
                tr("当前试剂位单瓶余量降到该试剂的单瓶%报警限时：启用则"
                   "自动切换到另一侧（左/右）继续测试；未启用则暂停测试并"
                   "提示更换试剂。报警限 0 表示不检查；单瓶%报警限在"
                   "「仪器参数」中按试剂设置。"),
                basicGroup);
    backupHint->setObjectName(QStringLiteral("paramHint"));
    backupHint->setWordWrap(true);
    basicLayout->addWidget(backupHint);

    connect(backupCheck, &QCheckBox::toggled, this, [](bool checked) {
        setReagentBackupPositionEnabled(checked);
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
        auto *check = new MedicalCheckBox(basicGroup);
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
    m_userRoleCombo->addItem(tr("维护人员"), QStringLiteral("maintenance"));
    m_userRoleCombo->addItem(tr("管理员"), QStringLiteral("admin"));
    m_userRoleCombo->addItem(tr("操作员"), QStringLiteral("operator"));
    m_userRoleCombo->setCurrentIndex(2);   // 新增用户默认操作员

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
    m_userAddButton = new QPushButton(tr("新增"), userGroup);
    m_userAddButton->setObjectName(QStringLiteral("userPrimaryButton"));
    m_userAddButton->setCursor(Qt::PointingHandCursor);
    m_userAddButton->setFixedHeight(28);
    m_userEditButton = new QPushButton(tr("修改"), userGroup);
    m_userEditButton->setObjectName(QStringLiteral("userActionButton"));
    m_userEditButton->setCursor(Qt::PointingHandCursor);
    m_userEditButton->setFixedHeight(28);
    m_userDeleteButton = new QPushButton(tr("删除"), userGroup);
    m_userDeleteButton->setObjectName(QStringLiteral("userActionButton"));
    m_userDeleteButton->setCursor(Qt::PointingHandCursor);
    m_userDeleteButton->setFixedHeight(28);
    userButtons->addWidget(m_userAddButton);
    userButtons->addWidget(m_userEditButton);
    userButtons->addWidget(m_userDeleteButton);
    userButtons->addStretch(1);
    userLayout->addLayout(userButtons);

    connect(m_userAddButton, &QPushButton::clicked,
            this, &SettingsDialog::addUserFromForm);
    connect(m_userEditButton, &QPushButton::clicked,
            this, &SettingsDialog::updateUserFromForm);
    connect(m_userDeleteButton, &QPushButton::clicked,
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
        MedicalMessageBox::warning(this, tr("用户管理"),
                             tr("用户名和密码不能为空。"));
        return;
    }

    QJsonArray users = ensureUserRecords();
    if (findUserRecord(users, name) >= 0) {
        MedicalMessageBox::warning(this, tr("用户管理"),
                             tr("用户名“%1”已存在。").arg(name));
        return;
    }

    QString role = m_userRoleCombo->currentData().toString();
    if (role.isEmpty()) {
        role = QStringLiteral("operator");
    }
    // 管理员只能管理操作人员：新增时固定为操作员，
    // 避免下拉框残留上一次选中的维护人员/管理员权限造成提权。
    if (m_currentUserRole == QLatin1String("admin")) {
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
        MedicalMessageBox::warning(this, tr("用户管理"),
                             tr("请先在列表中选择要修改的用户。"));
        return;
    }

    const QString newName = m_userNameEdit->text().trimmed();
    if (newName.isEmpty()) {
        MedicalMessageBox::warning(this, tr("用户管理"), tr("用户名不能为空。"));
        return;
    }

    QJsonArray users = ensureUserRecords();
    const int index = findUserRecord(users, originalName);
    if (index < 0) {
        return;
    }
    if (newName != originalName && findUserRecord(users, newName) >= 0) {
        MedicalMessageBox::warning(this, tr("用户管理"),
                             tr("用户名“%1”已存在。").arg(newName));
        return;
    }

    QJsonObject user = users.at(index).toObject();
    const QString oldRole = user.value(QStringLiteral("role")).toString();
    // 管理员只能修改操作员账号：不能改维护人员/管理员的资料和密码。
    if (m_currentUserRole == QLatin1String("admin")
            && oldRole != QLatin1String("operator")) {
        MedicalMessageBox::warning(this, tr("用户管理"),
                             tr("管理员只能修改操作员账号。"));
        return;
    }
    QString newRole = m_userRoleCombo->currentData().toString();
    if (newRole.isEmpty()) {
        newRole = QStringLiteral("operator");
    }
    // 管理员不能修改用户权限，保持原权限不变。
    if (m_currentUserRole == QLatin1String("admin")) {
        newRole = oldRole;
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
            MedicalMessageBox::warning(this, tr("用户管理"),
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
        MedicalMessageBox::warning(this, tr("用户管理"),
                             tr("请先在列表中选择要删除的用户。"));
        return;
    }

    QJsonArray users = ensureUserRecords();
    const int index = findUserRecord(users, name);
    if (index < 0) {
        return;
    }

    const QJsonObject user = users.at(index).toObject();
    const bool isMaintenance =
            m_currentUserRole == QLatin1String("maintenance");
    const bool isAdmin = m_currentUserRole == QLatin1String("admin");
    if (!isMaintenance && !isAdmin) {
        MedicalMessageBox::warning(this, tr("删除用户"),
                             tr("当前账号没有删除用户的权限。"));
        return;
    }

    const QString targetRole =
            user.value(QStringLiteral("role")).toString();
    // 维护人员账号权限最高，维护人员和管理员都不能删除。
    if (targetRole == QLatin1String("maintenance")) {
        MedicalMessageBox::warning(this, tr("删除用户"),
                             tr("维护人员账号“%1”不能删除。").arg(name));
        return;
    }
    // 管理员只能删除操作人员；维护人员可以删除管理员和操作人员。
    if (isAdmin && targetRole != QLatin1String("operator")) {
        MedicalMessageBox::warning(this, tr("删除用户"),
                             tr("管理员只能删除操作人员账号。"));
        return;
    }

    if (targetRole == QLatin1String("admin")) {
        int adminCount = 0;
        for (const QJsonValue &value : users) {
            if (value.toObject().value(QStringLiteral("role")).toString()
                    == QLatin1String("admin")) {
                ++adminCount;
            }
        }
        if (adminCount <= 1) {
            MedicalMessageBox::warning(this, tr("用户管理"),
                                 tr("至少需要保留一个管理员账号。"));
            return;
        }
    }

    if (MedicalMessageBox::question(this, tr("删除用户"),
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
        Coordinates,
        InstrumentParams,
        SampleNeedle,
        ReagentNeedle,
        Gripper,
        ReferenceRange,
        InformationEntry
    };
    struct SettingEntry {
        QString icon;
        QString title;
        QString description;
        MenuTarget target;
    };

    const SettingEntry entries[] = {
        { QStringLiteral("🩸"), tr("样本针参数"), tr("吸样量、空气段/防挂珠、液面探测与清洗"), MenuTarget::SampleNeedle },
        { QStringLiteral("🧪"), tr("试剂针参数"), tr("单试剂吸样量、液面探测与清洗"),         MenuTarget::ReagentNeedle },
        { QStringLiteral("🦾"), tr("抓手参数"),   tr("负压判定、抓杯/弃杯延时与重试"),         MenuTarget::Gripper },
        { QStringLiteral("🌡"), tr("模组控制"),   tr("温度、调光、LED 电流与电机转速"), MenuTarget::ModuleControl },
        { QStringLiteral("🖨"), tr("打印设置"),   tr("报告模板、打印机与纸张设置"),     MenuTarget::Placeholder },
        { QStringLiteral("🎯"), tr("坐标维护"),   tr("各工位 XY 与 Z 轴坐标维护"),       MenuTarget::Coordinates },
        { QStringLiteral("⚙"),  tr("仪器参数"),   tr("测试模式：PPP+PRP / PRP"),        MenuTarget::InstrumentParams },
        { QStringLiteral("📏"), tr("参考区间"),   tr("AA/ADP/EPI/COL/RIS 男女参考范围"), MenuTarget::ReferenceRange },
        { QStringLiteral("📝"), tr("信息录入"),   tr("科别、医生、诊断等常用信息子项"),   MenuTarget::InformationEntry }
    };
    const int columnCount = 3;
    const int entryCount = int(sizeof(entries) / sizeof(entries[0]));

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(16);

    for (int i = 0; i < entryCount; ++i) {
        auto *card = createSettingCard(entries[i].icon, entries[i].title, entries[i].description, page);
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
            if (target == MenuTarget::InstrumentParams) {
                m_stack->setCurrentIndex(m_instrumentParamsPageIndex);
                return;
            }
            if (target == MenuTarget::SampleNeedle) {
                m_stack->setCurrentIndex(m_sampleNeedlePageIndex);
                return;
            }
            if (target == MenuTarget::ReagentNeedle) {
                m_stack->setCurrentIndex(m_reagentNeedlePageIndex);
                return;
            }
            if (target == MenuTarget::Gripper) {
                m_stack->setCurrentIndex(m_gripperPageIndex);
                return;
            }
            if (target == MenuTarget::ReferenceRange) {
                m_stack->setCurrentIndex(m_referenceRangePageIndex);
                return;
            }
            if (target == MenuTarget::InformationEntry) {
                m_stack->setCurrentIndex(m_informationEntryPageIndex);
                return;
            }
            MedicalMessageBox::information(this, entryTitle,
                                     tr("该设置界面尚未实现，后续再补充。"));
        });
    }

    for (int column = 0; column < columnCount; ++column) {
        grid->setColumnStretch(column, 1);
    }
    const int rowCount = (entryCount + columnCount - 1) / columnCount;
    for (int row = 0; row < rowCount; ++row) {
        grid->setRowStretch(row, 1);
    }
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

QWidget *SettingsDialog::createInstrumentParamsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 20, 28, 24);
    layout->setSpacing(16);

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

    auto *title = new QLabel(tr("仪器参数"), page);
    title->setObjectName(QStringLiteral("pageTitle"));
    header->addWidget(title);
    header->addStretch();

    auto *badge = new QLabel(tr("● 维护模式"), page);
    badge->setObjectName(QStringLiteral("maintenanceBadge"));
    header->addWidget(badge, 0, Qt::AlignVCenter);
    layout->addLayout(header);

    auto *subtitle = new QLabel(
                tr("设置机型、测试模式和通道测试方式。修改后立即保存；"
                   "测试模式变化后返回加样页会按新模式重新分配血样孔。"),
                page);
    subtitle->setObjectName(QStringLiteral("dialogSubtitle"));
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    auto *instrumentGroup = new QGroupBox(tr("仪器"), page);
    instrumentGroup->setObjectName(QStringLiteral("basicParamGroup"));
    auto *instrumentLayout = new QVBoxLayout(instrumentGroup);
    instrumentLayout->setContentsMargins(18, 22, 18, 18);
    instrumentLayout->setSpacing(10);

    auto *modelRow = new QHBoxLayout;
    modelRow->setSpacing(12);
    auto *modelCaption = new QLabel(tr("机型"), instrumentGroup);
    modelCaption->setObjectName(QStringLiteral("paramCaption"));
    auto *modelCombo = new MedicalComboBox(instrumentGroup);
    modelCombo->addItem(QStringLiteral("KS600"),
                        int(MachineModel::KS600));
    modelCombo->addItem(QStringLiteral("KS800"),
                        int(MachineModel::KS800));
    modelCombo->addItem(QStringLiteral("KS1200"),
                        int(MachineModel::KS1200));
    modelCombo->setCurrentIndex(modelCombo->findData(int(m_machineModel)));
    modelRow->addWidget(modelCaption);
    modelRow->addWidget(modelCombo, 1);
    instrumentLayout->addLayout(modelRow);

    auto *modelHint = new QLabel(
                tr("切换机型会改变模组、通道、试管盘和血样孔数量；"
                   "主界面、加样页和通道状态会按新机型刷新。"),
                instrumentGroup);
    modelHint->setObjectName(QStringLiteral("paramHint"));
    modelHint->setWordWrap(true);
    instrumentLayout->addWidget(modelHint);

    connect(modelCombo,
            static_cast<void (QComboBox::*)(int)>(
                &QComboBox::currentIndexChanged),
            this, [this, modelCombo](int) {
        const MachineModel model = static_cast<MachineModel>(
                    modelCombo->currentData().toInt());
        if (model == m_machineModel) {
            return;
        }
        setMachineModelInSettings(model);
        m_machineModel = model;
        rebuildBasicPage();
        updateBasicMachineInfo();
        emit machineModelChanged(model);
        if (m_coordinateWidget) {
            m_coordinateWidget->refreshAll();
        }
    });

    // —— 测高相机：按设备名保存，保证全血模式打开的是仪器摄像头 ——
    instrumentLayout->addSpacing(4);
    auto *cameraRow = new QHBoxLayout;
    cameraRow->setSpacing(12);
    auto *cameraCaption = new QLabel(tr("测高相机"), instrumentGroup);
    cameraCaption->setObjectName(QStringLiteral("paramCaption"));
    m_heightCameraCombo = new MedicalComboBox(instrumentGroup);
    m_heightCameraCombo->setSizeAdjustPolicy(
                QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_heightCameraCombo->setMinimumContentsLength(18);
    auto *refreshCameraButton = new QPushButton(tr("刷新"), instrumentGroup);
    refreshCameraButton->setObjectName(QStringLiteral("userActionButton"));
    refreshCameraButton->setCursor(Qt::PointingHandCursor);
    cameraRow->addWidget(cameraCaption);
    cameraRow->addWidget(m_heightCameraCombo, 1);
    cameraRow->addWidget(refreshCameraButton);
    instrumentLayout->addLayout(cameraRow);

    auto *cameraHint = new QLabel(
                tr("全血模式测高使用此摄像头。按设备名称保存，"
                   "USB 拔插或换电脑后仍能找回仪器摄像头；"
                   "插拔摄像头后点「刷新」重新枚举。"),
                instrumentGroup);
    cameraHint->setObjectName(QStringLiteral("paramHint"));
    cameraHint->setWordWrap(true);
    instrumentLayout->addWidget(cameraHint);

    populateHeightCameraCombo();
    connect(refreshCameraButton, &QPushButton::clicked,
            this, [this]() { populateHeightCameraCombo(); });
    // 用 activated：用户选中当前已高亮的项也会触发，避免“点了同一项却
    // 没保存设备名”的陷阱；程序内部 populate 时不会触发。
    connect(m_heightCameraCombo,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::activated),
            this, [this](int) {
        if (!m_heightCameraCombo) {
            return;
        }
        const int index = m_heightCameraCombo
                ->currentData(Qt::UserRole + 1).toInt();
        if (index < 0) {
            return;   // 占位项（未检测到摄像头），不覆盖已保存配置
        }
        const QString name = m_heightCameraCombo->currentData().toString();
        QSettings settings;
        settings.setValue(QStringLiteral("heightCamera/deviceName"), name);
        settings.setValue(QStringLiteral("heightCamera/deviceIndex"), index);
        emit heightCameraChanged(name, index);
    });

    auto *testGroup = new QGroupBox(tr("测试模式"), page);
    testGroup->setObjectName(QStringLiteral("basicParamGroup"));
    auto *testLayout = new QVBoxLayout(testGroup);
    testLayout->setContentsMargins(18, 22, 18, 18);
    testLayout->setSpacing(12);

    auto *pppPrp = new QRadioButton(
                tr("PPP+PRP 模式（每个样本 2 管血，占用 2 个连续血样孔）"),
                testGroup);
    auto *prp = new QRadioButton(
                tr("PRP 模式（每个样本 1 管血，占用 1 个血样孔）"),
                testGroup);
    auto *modeGroup = new QButtonGroup(testGroup);
    modeGroup->setExclusive(true);
    modeGroup->addButton(pppPrp);
    modeGroup->addButton(prp);
    (testModeIsPppPrp() ? pppPrp : prp)->setChecked(true);

    auto *example = new QLabel(
                tr("PPP+PRP 示例：样本1占 0+1（PPP 在前、PRP 在后），"
                   "样本2占 2+3……（两个孔不能重复使用）"),
                testGroup);
    example->setObjectName(QStringLiteral("paramHint"));
    example->setWordWrap(true);

    testLayout->addWidget(pppPrp);
    testLayout->addWidget(prp);
    testLayout->addWidget(example);

    // —— 通道测试模式并入测试模式组 ——
    auto *channelCaption = new QLabel(
                tr("样本在通道上的分布方式"), testGroup);
    channelCaption->setObjectName(QStringLiteral("paramCaption"));
    channelCaption->setWordWrap(true);
    testLayout->addWidget(channelCaption);

    auto *sameChannel = new QRadioButton(tr("同通道测试（每个样本同一通道）"),
                                         testGroup);
    auto *differentChannel = new QRadioButton(
                tr("不同通道测试（每个试剂一个通道）"), testGroup);
    auto *channelModeGroup = new QButtonGroup(testGroup);
    channelModeGroup->setExclusive(true);
    channelModeGroup->addButton(sameChannel);
    channelModeGroup->addButton(differentChannel);

    // 默认随测试模式联动：PPP+PRP → 同通道；PRP → 不同通道。
    // 用户也可在组里手动改（仍会保存），切换测试模式时重置为该模式的默认。
    auto refreshChannelSelection = [sameChannel, differentChannel]() {
        if (testModeIsPppPrp()) {
            sameChannel->setChecked(true);
        } else {
            differentChannel->setChecked(true);
        }
    };
    refreshChannelSelection();

    auto *channelHint = new QLabel(
                tr("这两个选项先保存到仪器参数，具体测试逻辑后续接入。"),
                testGroup);
    channelHint->setObjectName(QStringLiteral("paramHint"));
    channelHint->setWordWrap(true);

    testLayout->addWidget(sameChannel);
    testLayout->addWidget(differentChannel);
    testLayout->addWidget(channelHint);

    auto *testModeLockHint = new QLabel(
                tr("存在待测/测试中样本时，测试模式不可修改；"
                   "请先完成测试或删除待测样本。"),
                testGroup);
    testModeLockHint->setObjectName(QStringLiteral("paramHint"));
    testModeLockHint->setWordWrap(true);
    testLayout->addWidget(testModeLockHint);

    auto restoreTestModeSelection = [pppPrp, prp]() {
        QSignalBlocker pppBlocker(pppPrp);
        QSignalBlocker prpBlocker(prp);
        (testModeIsPppPrp() ? pppPrp : prp)->setChecked(true);
    };
    connect(pppPrp, &QRadioButton::toggled, this,
            [this, restoreTestModeSelection, refreshChannelSelection](bool checked) {
        if (!checked) {
            return;
        }
        if (!ensureSampleShapeEditable(tr("测试模式"))) {
            restoreTestModeSelection();
            return;
        }
        setTestModePppPrp(true);
        refreshChannelSelection();   // 切到 PPP+PRP：通道默认同通道
    });
    connect(prp, &QRadioButton::toggled, this,
            [this, restoreTestModeSelection, refreshChannelSelection](bool checked) {
        if (!checked) {
            return;
        }
        if (!ensureSampleShapeEditable(tr("测试模式"))) {
            restoreTestModeSelection();
            return;
        }
        setTestModePppPrp(false);
        refreshChannelSelection();   // 切到 PRP：通道默认不同通道
    });

    connect(sameChannel, &QRadioButton::toggled, this, [](bool checked) {
        if (checked) {
            setChannelTestModeSameChannel(true);
        }
    });
    connect(differentChannel, &QRadioButton::toggled,
            this, [](bool checked) {
        if (checked) {
            setChannelTestModeSameChannel(false);
        }
    });

    layout->addWidget(instrumentGroup);
    layout->addWidget(testGroup);
    layout->addStretch(1);
    return page;
}

void SettingsDialog::populateHeightCameraCombo()
{
    if (!m_heightCameraCombo) {
        return;
    }
    QSettings settings;
    const QString savedName = settings.value(
                QStringLiteral("heightCamera/deviceName")).toString().trimmed();
    const int savedIndex = settings.value(
                QStringLiteral("heightCamera/deviceIndex"), 0).toInt();

    const QStringList names = HeightCamController::cameraDeviceNames();
    const QSignalBlocker blocker(m_heightCameraCombo);
    m_heightCameraCombo->clear();

    if (names.isEmpty()) {
        m_heightCameraCombo->addItem(tr("未检测到摄像头"), QString());
        m_heightCameraCombo->setItemData(
                    m_heightCameraCombo->count() - 1, -1, Qt::UserRole + 1);
        m_heightCameraCombo->setEnabled(false);
        return;
    }
    m_heightCameraCombo->setEnabled(true);

    int selectedRow = -1;
    for (int i = 0; i < names.size(); ++i) {
        const QString label = names.at(i).isEmpty()
                ? tr("设备 %1（名称未知）").arg(i)
                : tr("设备 %1：%2").arg(i).arg(names.at(i));
        m_heightCameraCombo->addItem(label, names.at(i));
        const int row = m_heightCameraCombo->count() - 1;
        m_heightCameraCombo->setItemData(row, i, Qt::UserRole + 1);
        if (!savedName.isEmpty()
                && names.at(i).compare(savedName, Qt::CaseInsensitive) == 0) {
            selectedRow = row;
        }
    }

    if (selectedRow < 0 && !savedName.isEmpty()) {
        // 已保存的设备当前不在线：保留这一项，提示用户插回仪器摄像头。
        m_heightCameraCombo->addItem(
                    tr("%1（未连接）").arg(savedName), savedName);
        const int row = m_heightCameraCombo->count() - 1;
        m_heightCameraCombo->setItemData(row, savedIndex, Qt::UserRole + 1);
        selectedRow = row;
    }
    if (selectedRow < 0) {
        for (int i = 0; i < m_heightCameraCombo->count(); ++i) {
            if (m_heightCameraCombo->itemData(i, Qt::UserRole + 1).toInt()
                    == savedIndex) {
                selectedRow = i;
                break;
            }
        }
    }
    if (selectedRow < 0) {
        selectedRow = 0;
    }
    m_heightCameraCombo->setCurrentIndex(selectedRow);
}

QVBoxLayout *SettingsDialog::createMaintenancePageLayout(
        QWidget *page, const QString &title, const QString &subtitle)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 20, 28, 24);
    layout->setSpacing(16);

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

    auto *titleLabel = new QLabel(title, page);
    titleLabel->setObjectName(QStringLiteral("pageTitle"));
    header->addWidget(titleLabel);
    header->addStretch();

    auto *badge = new QLabel(tr("● 维护模式"), page);
    badge->setObjectName(QStringLiteral("maintenanceBadge"));
    header->addWidget(badge, 0, Qt::AlignVCenter);
    layout->addLayout(header);

    auto *subtitleLabel = new QLabel(subtitle, page);
    subtitleLabel->setObjectName(QStringLiteral("dialogSubtitle"));
    subtitleLabel->setWordWrap(true);
    layout->addWidget(subtitleLabel);

    return layout;
}

QWidget *SettingsDialog::createIntParamField(
        QWidget *parent, const QString &labelText, int minimum, int maximum,
        int value, const QString &suffix,
        const std::function<void(int)> &setter)
{
    auto *cell = new QWidget(parent);
    auto *layout = new QHBoxLayout(cell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *label = new QLabel(labelText, cell);
    label->setObjectName(QStringLiteral("paramCaption"));

    auto *spin = new MedicalSpinBox(cell);
    spin->setRange(minimum, maximum);
    spin->setSuffix(suffix);
    spin->setValue(value);
    spin->setMinimumWidth(150);

    layout->addWidget(label);
    layout->addWidget(spin);
    layout->addStretch(1);

    connect(spin,
            static_cast<void (QSpinBox::*)(int)>(
                &QSpinBox::valueChanged),
            this, [setter](int v) { setter(v); });
    return cell;
}

QWidget *SettingsDialog::createDoubleParamField(
        QWidget *parent, const QString &labelText, double minimum,
        double maximum, int decimals, double value, const QString &suffix,
        const std::function<void(double)> &setter)
{
    auto *cell = new QWidget(parent);
    auto *layout = new QHBoxLayout(cell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *label = new QLabel(labelText, cell);
    label->setObjectName(QStringLiteral("paramCaption"));

    auto *spin = new MedicalDoubleSpinBox(cell);
    spin->setRange(minimum, maximum);
    spin->setDecimals(decimals);
    spin->setSuffix(suffix);
    spin->setValue(value);
    spin->setMinimumWidth(150);

    layout->addWidget(label);
    layout->addWidget(spin);
    layout->addStretch(1);

    connect(spin,
            static_cast<void (QDoubleSpinBox::*)(double)>(
                &QDoubleSpinBox::valueChanged),
            this, [setter](double v) { setter(v); });
    return cell;
}

QWidget *SettingsDialog::createSampleNeedlePage()
{
    auto *page = new QWidget;
    auto *layout = createMaintenancePageLayout(
                page, tr("样本针参数"),
                tr("血样泵换算、吸样量、空气段/防挂珠、液面探测与清洗延时。"));

    auto *pumpGroup = new QGroupBox(tr("泵与吸样"), page);
    pumpGroup->setObjectName(QStringLiteral("basicParamGroup"));
    auto *pumpLayout = new QVBoxLayout(pumpGroup);
    pumpLayout->setContentsMargins(18, 22, 18, 18);
    pumpLayout->setSpacing(10);
    auto *pumpGrid = new QGridLayout;
    pumpGrid->setHorizontalSpacing(18);
    pumpGrid->setVerticalSpacing(10);
    int row = 0;
    int column = 0;
    auto addPumpField = [&](QWidget *field) {
        pumpGrid->addWidget(field, row, column);
        if (++column >= 2) {
            column = 0;
            ++row;
        }
    };
    addPumpField(createDoubleParamField(
                     pumpGroup, tr("血样泵系数"), 0.1, 100.0, 2,
                     bloodPumpStepsPerUl(), tr(" 步/µL"),
                     [](double v) { setBloodPumpStepsPerUl(v); }));
    addPumpField(createIntParamField(
                     pumpGroup, tr("PPP吸样量"), 1,
                     static_cast<int>(kSamplePumpCapacityUl),
                     sampleAspirateVolumeUl(), tr(" µL"),
                     [](int v) { setSampleAspirateVolumeUl(v); }));
    addPumpField(createIntParamField(
                     pumpGroup, tr("PRP单份量"), 1, 500,
                     samplePrpDoseUl(), tr(" µL"),
                     [](int v) { setSamplePrpDoseUl(v); }));
    addPumpField(createIntParamField(
                     pumpGroup, tr("洗样本针量"), 1, 2000,
                     sampleWashVolumeUl(), tr(" µL"),
                     [](int v) { setSampleWashVolumeUl(v); }));
    pumpLayout->addLayout(pumpGrid);
    auto *pumpHint = new QLabel(
                tr("血样泵满量程 7200 步对应 2500 µL，即 2.88 步/µL；"
                   "样本吸样量按实际加入血样的微升数设置。"),
                pumpGroup);
    pumpHint->setObjectName(QStringLiteral("paramHint"));
    pumpHint->setWordWrap(true);
    pumpLayout->addWidget(pumpHint);
    layout->addWidget(pumpGroup);

    auto *detectGroup = new QGroupBox(tr("液面探测与延时"), page);
    detectGroup->setObjectName(QStringLiteral("basicParamGroup"));
    auto *detectLayout = new QVBoxLayout(detectGroup);
    detectLayout->setContentsMargins(18, 22, 18, 18);
    detectLayout->setSpacing(10);
    auto *detectGrid = new QGridLayout;
    detectGrid->setHorizontalSpacing(18);
    detectGrid->setVerticalSpacing(10);
    row = 0;
    column = 0;
    auto addDetectField = [&](QWidget *field) {
        detectGrid->addWidget(field, row, column);
        if (++column >= 2) {
            column = 0;
            ++row;
        }
    };
    addDetectField(createIntParamField(
                       detectGroup, tr("吸样探液失败高度"), 0, 150,
                       sampleDetectionFailHeightMm(), tr(" mm"),
                       [](int v) { setSampleDetectionFailHeightMm(v); }));
    addDetectField(createIntParamField(
                       detectGroup, tr("洗针探液失败高度"), 0, 150,
                       sampleWashDetectionFailHeightMm(), tr(" mm"),
                       [](int v) { setSampleWashDetectionFailHeightMm(v); }));
    addDetectField(createIntParamField(
                       detectGroup, tr("针尖到采血管口"), 0, 99,
                       sampleTubeMouthMm(), tr(" mm"),
                       [](int v) { setSampleTubeMouthMm(v); }));
    addDetectField(createIntParamField(
                       detectGroup, tr("吸样后抬针延时"), 0, 5000,
                       sampleSuckLiftDelayMs(), tr(" ms"),
                       [](int v) { setSampleSuckLiftDelayMs(v); }));
    addDetectField(createIntParamField(
                       detectGroup, tr("吐样后抬针延时"), 0, 5000,
                       sampleSpitLiftDelayMs(), tr(" ms"),
                       [](int v) { setSampleSpitLiftDelayMs(v); }));
    detectLayout->addLayout(detectGrid);
    auto *detectHint = new QLabel(
                tr("防挂珠按老软件逻辑：液面探测返回的高度比"
                   "「针尖到采血管口」浅，判为针尖挂珠，"
                   "下到「吸样探液失败高度」再吸；"
                   "探到管口或更深就在探到的液面吸；一直探不到也下到失败高度。"
                   "不做吸空气段。"),
                detectGroup);
    detectHint->setObjectName(QStringLiteral("paramHint"));
    detectHint->setWordWrap(true);
    detectLayout->addWidget(detectHint);
    layout->addWidget(detectGroup);

    auto *cleanGroup = new QGroupBox(tr("清洗"), page);
    cleanGroup->setObjectName(QStringLiteral("basicParamGroup"));
    auto *cleanLayout = new QVBoxLayout(cleanGroup);
    cleanLayout->setContentsMargins(18, 22, 18, 18);
    cleanLayout->setSpacing(10);
    auto *cleanGrid = new QGridLayout;
    cleanGrid->setHorizontalSpacing(18);
    cleanGrid->setVerticalSpacing(10);
    cleanGrid->addWidget(createIntParamField(
                             cleanGroup, tr("清洗吐液高度"), 0, 150,
                             cleaningDumpHeightMm(), tr(" mm"),
                             [](int v) { setCleaningDumpHeightMm(v); }),
                         0, 0);
    cleanLayout->addLayout(cleanGrid);
    auto *cleanHint = new QLabel(
                tr("清洗双针在机器原点吐液时双针下降高度（老软件 45mm）。"
                   "清洗位 XY 在「坐标维护」标定；洗样本针量/洗试剂针量在对应页面；"
                   "清洗时间用基础设置「泵清洗时间」。"),
                cleanGroup);
    cleanHint->setObjectName(QStringLiteral("paramHint"));
    cleanHint->setWordWrap(true);
    cleanLayout->addWidget(cleanHint);
    layout->addWidget(cleanGroup);

    layout->addStretch(1);
    return page;
}

QWidget *SettingsDialog::createReagentNeedlePage()
{
    auto *page = new QWidget;
    auto *layout = createMaintenancePageLayout(
                page, tr("试剂针参数"),
                tr("单试剂吸样量、液面探测、空气段/防挂珠、清洗与延时。"));

    auto *pumpGroup = new QGroupBox(tr("泵、清洗与延时"), page);
    pumpGroup->setObjectName(QStringLiteral("basicParamGroup"));
    auto *pumpLayout = new QVBoxLayout(pumpGroup);
    pumpLayout->setContentsMargins(18, 22, 18, 18);
    pumpLayout->setSpacing(10);
    auto *pumpGrid = new QGridLayout;
    pumpGrid->setHorizontalSpacing(18);
    pumpGrid->setVerticalSpacing(10);
    int row = 0;
    int column = 0;
    auto addPumpField = [&](QWidget *field) {
        pumpGrid->addWidget(field, row, column);
        if (++column >= 2) {
            column = 0;
            ++row;
        }
    };
    addPumpField(createDoubleParamField(
                     pumpGroup, tr("试剂泵系数"), 1.0, 1000.0, 2,
                     reagentPumpStepsPerUl(), tr(" 步/µL"),
                     [](double v) { setReagentPumpStepsPerUl(v); }));
    addPumpField(createIntParamField(
                     pumpGroup, tr("洗试剂针量"), 1, 100,
                     reagentWashVolumeUl(), tr(" µL"),
                     [](int v) { setReagentWashVolumeUl(v); }));
    addPumpField(createIntParamField(
                     pumpGroup, tr("吸试剂后抬针延时"), 0, 5000,
                     reagentSuckLiftDelayMs(), tr(" ms"),
                     [](int v) { setReagentSuckLiftDelayMs(v); }));
    addPumpField(createIntParamField(
                     pumpGroup, tr("吐试剂后抬针延时"), 0, 5000,
                     reagentSpitLiftDelayMs(), tr(" ms"),
                     [](int v) { setReagentSpitLiftDelayMs(v); }));
    addPumpField(createIntParamField(
                     pumpGroup, tr("洗针探液失败高度"), 0, 150,
                     reagentWashDetectionFailHeightMm(), tr(" mm"),
                     [](int v) { setReagentWashDetectionFailHeightMm(v); }));
    addPumpField(createIntParamField(
                     pumpGroup, tr("针尖到试剂瓶口"), 0, 99,
                     reagentBottleMouthMm(), tr(" mm"),
                     [](int v) { setReagentBottleMouthMm(v); }));
    pumpLayout->addLayout(pumpGrid);
    auto *pumpHint = new QLabel(
                tr("试剂泵 7200 步对应 100 µL，即 72 步/µL。"
                   "防挂珠按老软件逻辑：液面探测返回的高度比"
                   "「针尖到试剂瓶口」浅，判为针尖挂珠，"
                   "下到该试剂的探液失败高度再吸；"
                   "探到瓶口或更深就在探到的液面吸；一直探不到也下到失败高度。"
                   "不做吸空气段。"),
                pumpGroup);
    pumpHint->setObjectName(QStringLiteral("paramHint"));
    pumpHint->setWordWrap(true);
    pumpLayout->addWidget(pumpHint);
    layout->addWidget(pumpGroup);

    auto *reagentGroup = new QGroupBox(tr("单试剂参数与报警限"), page);
    reagentGroup->setObjectName(QStringLiteral("basicParamGroup"));
    auto *reagentLayout = new QVBoxLayout(reagentGroup);
    reagentLayout->setContentsMargins(18, 22, 18, 18);
    reagentLayout->setSpacing(10);
    auto *reagentGrid = new QGridLayout;
    reagentGrid->setHorizontalSpacing(10);
    reagentGrid->setVerticalSpacing(8);

    const QStringList headers = {
        tr("试剂"), tr("吸试剂"), tr("探液(mm)"),
        tr("容量"), tr("单瓶%"), tr("瓶数"), tr("总剩余%")
    };
    for (int c = 0; c < headers.size(); ++c) {
        auto *label = new QLabel(headers.at(c), reagentGroup);
        label->setObjectName(QStringLiteral("paramCaption"));
        reagentGrid->addWidget(label, 0, c);
    }

    auto makeSpin = [this, reagentGroup](int minimum, int maximum,
                                         int value, const QString &suffix,
                                         const std::function<void(int)> &setter) {
        auto *spin = new MedicalSpinBox(reagentGroup);
        spin->setRange(minimum, maximum);
        spin->setSuffix(suffix);
        spin->setValue(value);
        spin->setMinimumWidth(90);
        connect(spin,
                static_cast<void (QSpinBox::*)(int)>(
                    &QSpinBox::valueChanged),
                this, [setter](int v) { setter(v); });
        return spin;
    };

    const char *reagentNames[] = { "AA", "ADP", "EPI", "COL", "RIS" };
    for (int i = 0; i < 5; ++i) {
        auto *name = new QLabel(QString::fromLatin1(reagentNames[i]),
                                reagentGroup);
        name->setObjectName(QStringLiteral("paramCaption"));
        reagentGrid->addWidget(name, i + 1, 0);

        const int uiIndex = i;
        const int position = reagentUiToConsumablePosition(i);
        reagentGrid->addWidget(
                    makeSpin(1, 100, reagentVolumeUl(uiIndex), tr(" µL"),
                             [uiIndex](int v) {
            setReagentVolumeUl(uiIndex, v);
        }), i + 1, 1);
        reagentGrid->addWidget(
                    makeSpin(0, 150, reagentDetectionFailHeightMm(uiIndex),
                             tr(" mm"), [uiIndex](int v) {
            setReagentDetectionFailHeightMm(uiIndex, v);
        }), i + 1, 2);
        reagentGrid->addWidget(
                    makeSpin(1, 60000,
                             qRound(consumableCapacityUl(position)),
                             tr(" µL"), [position](int v) {
            setConsumableCapacityUl(position, v);
        }), i + 1, 3);
        reagentGrid->addWidget(
                    makeSpin(0, 100,
                             consumableSingleAlarmPercent(position),
                             tr(" %"), [position](int v) {
            setConsumableSingleAlarmPercent(position, v);
        }), i + 1, 4);
        reagentGrid->addWidget(
                    makeSpin(0, 255, consumableBottleAlarm(position),
                             tr(" 瓶"), [position](int v) {
            setConsumableBottleAlarm(position, v);
        }), i + 1, 5);
        reagentGrid->addWidget(
                    makeSpin(0, 100,
                             consumableTotalPercentAlarm(position),
                             tr(" %"), [position](int v) {
            setConsumableTotalPercentAlarm(position, v);
        }), i + 1, 6);
    }

    const int s1Row = 6;
    auto *s1Name = new QLabel(tr("S1 内部清洗液"), reagentGroup);
    s1Name->setObjectName(QStringLiteral("paramCaption"));
    reagentGrid->addWidget(s1Name, s1Row, 0);
    reagentGrid->addWidget(new QLabel(QStringLiteral("—"), reagentGroup),
                           s1Row, 1);
    reagentGrid->addWidget(new QLabel(QStringLiteral("—"), reagentGroup),
                           s1Row, 2);
    const int s1Position = MainBoardConsumable::InternalCleanS1;
    reagentGrid->addWidget(
                makeSpin(1, 60000, qRound(consumableCapacityUl(s1Position)),
                         tr(" µL"), [s1Position](int v) {
        setConsumableCapacityUl(s1Position, v);
    }), s1Row, 3);
    reagentGrid->addWidget(
                makeSpin(0, 100, consumableSingleAlarmPercent(s1Position),
                         tr(" %"), [s1Position](int v) {
        setConsumableSingleAlarmPercent(s1Position, v);
    }), s1Row, 4);
    reagentGrid->addWidget(
                makeSpin(0, 255, consumableBottleAlarm(s1Position),
                         tr(" 瓶"), [s1Position](int v) {
        setConsumableBottleAlarm(s1Position, v);
    }), s1Row, 5);
    reagentGrid->addWidget(
                makeSpin(0, 100, consumableTotalPercentAlarm(s1Position),
                         tr(" %"), [s1Position](int v) {
        setConsumableTotalPercentAlarm(s1Position, v);
    }), s1Row, 6);

    reagentLayout->addLayout(reagentGrid);
    auto *reagentHint = new QLabel(
                tr("吸试剂单位 µL；探液(mm) 是该试剂的吸试剂探液失败高度；"
                   "容量单位 µL；单瓶% 是单瓶剩余百分比报警；瓶数是总瓶数报警；"
                   "总剩余% 按 剩余整瓶数 + 当前瓶比例/512 计算（满 100 瓶=100%）。"
                   "容量和三类报警限同一种试剂左右瓶共用。"),
                reagentGroup);
    reagentHint->setObjectName(QStringLiteral("paramHint"));
    reagentHint->setWordWrap(true);
    reagentLayout->addWidget(reagentHint);
    layout->addWidget(reagentGroup);

    layout->addStretch(1);
    return page;
}

QWidget *SettingsDialog::createGripperPage()
{
    auto *page = new QWidget;
    auto *layout = createMaintenancePageLayout(
                page, tr("抓手参数"),
                tr("负压判定、抓杯/弃杯延时与失败重试。"
                   "各工位下降高度在「坐标维护」里设置。"));

    auto *group = new QGroupBox(tr("负压与延时"), page);
    group->setObjectName(QStringLiteral("basicParamGroup"));
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->setContentsMargins(18, 22, 18, 18);
    groupLayout->setSpacing(10);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(10);
    int row = 0;
    int column = 0;
    auto addField = [&](QWidget *field) {
        grid->addWidget(field, row, column);
        if (++column >= 2) {
            column = 0;
            ++row;
        }
    };
    addField(createIntParamField(
                 group, tr("抓杯吸住阈值（负压≤）"), 0, 65535,
                 gripperGrabVacuumMax(), QString(),
                 [](int v) { setGripperGrabVacuumMax(v); }));
    addField(createIntParamField(
                 group, tr("弃杯松开阈值（负压≥）"), 0, 65535,
                 gripperReleaseVacuumMin(), QString(),
                 [](int v) { setGripperReleaseVacuumMin(v); }));
    addField(createIntParamField(
                 group, tr("抓杯等待"), 0, 10000,
                 gripperSuctionDelayMs(), tr(" ms"),
                 [](int v) { setGripperSuctionDelayMs(v); }));
    addField(createIntParamField(
                 group, tr("弃杯等待负压释放"), 0, 10000,
                 gripperReleaseDelayMs(), tr(" ms"),
                 [](int v) { setGripperReleaseDelayMs(v); }));
    addField(createIntParamField(
                 group, tr("抓杯/弃杯失败重试"), 0, 9,
                 gripperRetryCount(), tr(" 次"),
                 [](int v) { setGripperRetryCount(v); }));
    groupLayout->addLayout(grid);

    auto *hint = new QLabel(
                tr("抓杯：gripper_status（bit4）=1 开空气阀，等抓杯等待时间后"
                   "读负压，负压 ≤ 吸住阈值算成功。"
                   "弃杯：bit4=0 关阀，等弃杯等待负压释放时间后再抬抓手。"
                   "负压值来自动作报告帧 distance2。"),
                group);
    hint->setObjectName(QStringLiteral("paramHint"));
    hint->setWordWrap(true);
    groupLayout->addWidget(hint);
    layout->addWidget(group);

    layout->addStretch(1);
    return page;
}

QWidget *SettingsDialog::createReferenceRangePage()
{
    auto *page = new QWidget;
    auto *layout = createMaintenancePageLayout(
                page, tr("参考区间"),
                tr("按试剂和 60S/180S/300S/MAX 设置聚集率参考下限/上限（%）。"
                   "0~0 表示该时间点未设置，不参与异常判断。"));

    auto *tabs = new QTabWidget(page);
    tabs->setObjectName(QStringLiteral("referenceTabs"));
    tabs->tabBar()->setObjectName(QStringLiteral("referenceTabBar"));
    tabs->setStyleSheet(QStringLiteral(
        "QTabWidget#referenceTabs::pane {"
        "  border: 1px solid #D4E3EF;"
        "  background: #FFFFFF;"
        "  border-radius: 0 0 8px 8px;"
        "  top: -1px;"
        "}"
        "QTabBar#referenceTabBar::tab {"
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
        "QTabBar#referenceTabBar::tab:selected {"
        "  background: #FFFFFF;"
        "  color: #1F4E79;"
        "  font-weight: 600;"
        "}"
        "QTabBar#referenceTabBar::tab:hover { background: #EDF5FC; }"));

    const ReferenceInterval::Timepoint timepoints[] = {
        ReferenceInterval::Timepoint::S60,
        ReferenceInterval::Timepoint::S180,
        ReferenceInterval::Timepoint::S300,
        ReferenceInterval::Timepoint::Max
    };

    for (int i = 0; i < 5; ++i) {
        auto *tab = new QWidget(tabs);
        auto *tabLayout = new QVBoxLayout(tab);
        tabLayout->setContentsMargins(12, 12, 12, 12);
        tabLayout->setSpacing(12);

        const int index = i;
        auto *group = new QGroupBox(tab);
        group->setObjectName(QStringLiteral("basicParamGroup"));
        auto *groupLayout = new QVBoxLayout(group);
        groupLayout->setContentsMargins(18, 22, 18, 18);
        groupLayout->setSpacing(8);

        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(18);
        grid->setVerticalSpacing(8);
        const QStringList headers = {
            tr("时间点"), tr("下限(%)"), tr("上限(%)")
        };
        for (int c = 0; c < headers.size(); ++c) {
            auto *label = new QLabel(headers.at(c), group);
            label->setObjectName(QStringLiteral("paramCaption"));
            grid->addWidget(label, 0, c);
        }

        auto makeSpin = [this, group](
                int value,
                const std::function<void(int)> &setter) {
            auto *spin = new MedicalSpinBox(group);
            spin->setRange(0, 100);
            spin->setSuffix(tr(" %"));
            spin->setValue(value);
            spin->setMinimumWidth(120);
            connect(spin,
                    static_cast<void (QSpinBox::*)(int)>(
                        &QSpinBox::valueChanged),
                    this, [setter](int v) { setter(v); });
            return spin;
        };

        for (int r = 0; r < 4; ++r) {
            const ReferenceInterval::Timepoint timepoint = timepoints[r];
            auto *name = new QLabel(
                        ReferenceInterval::timepointName(timepoint), group);
            name->setObjectName(QStringLiteral("paramCaption"));
            grid->addWidget(name, r + 1, 0);
            grid->addWidget(
                        makeSpin(ReferenceInterval::low(index, timepoint),
                                 [index, timepoint](int v) {
                    ReferenceInterval::setLow(index, timepoint, v);
                }), r + 1, 1);
            grid->addWidget(
                        makeSpin(ReferenceInterval::high(index, timepoint),
                                 [index, timepoint](int v) {
                    ReferenceInterval::setHigh(index, timepoint, v);
                }), r + 1, 2);
        }
        groupLayout->addLayout(grid);
        tabLayout->addWidget(group);
        tabLayout->addStretch(1);
        tabs->addTab(tab,
                     QString::fromLatin1(ReferenceInterval::reagentName(i)));
    }

    layout->addWidget(tabs);

    auto *hint = new QLabel(
                tr("MAX 默认：AA 30~70%、ADP 40~80%、EPI 35~75%、"
                   "COL 50~90%、RIS 45~85%；"
                   "60S/180S/300S 默认 0~0，表示未设置、不参与异常判断。"
                   "修改后保存到 QSettings，结果页/曲线页按试剂和时间点取用。"),
                page);
    hint->setObjectName(QStringLiteral("paramHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    layout->addStretch(1);
    return page;
}

QWidget *SettingsDialog::createInformationEntryPage()
{
    auto *page = new QWidget;
    auto *layout = createMaintenancePageLayout(
                page, tr("信息录入"),
                tr("维护医院常用信息。左侧选择信息类别，右侧添加、重命名、"
                   "删除或调整子项顺序；修改后立即保存，供样本信息和报告使用。"));

    ensureInformationEntryDefaults();

    auto *content = new QWidget(page);
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(16);

    auto *categoryGroup = new QGroupBox(tr("信息类别"), content);
    categoryGroup->setObjectName(QStringLiteral("userGroup"));
    categoryGroup->setMinimumWidth(200);
    categoryGroup->setMaximumWidth(280);
    auto *categoryLayout = new QVBoxLayout(categoryGroup);
    categoryLayout->setContentsMargins(12, 18, 12, 12);
    categoryLayout->setSpacing(8);

    auto *categoryList = new QListWidget(categoryGroup);
    categoryList->setObjectName(QStringLiteral("informationCategoryList"));
    categoryList->setSelectionMode(QAbstractItemView::SingleSelection);
    categoryList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    categoryLayout->addWidget(categoryList, 1);

    auto *valueGroup = new QGroupBox(tr("子信息"), content);
    valueGroup->setObjectName(QStringLiteral("userGroup"));
    auto *valueLayout = new QVBoxLayout(valueGroup);
    valueLayout->setContentsMargins(16, 18, 16, 14);
    valueLayout->setSpacing(10);

    auto *currentTitleLabel = new QLabel(valueGroup);
    currentTitleLabel->setObjectName(QStringLiteral("paramCaption"));
    valueLayout->addWidget(currentTitleLabel);

    auto *valueList = new QListWidget(valueGroup);
    valueList->setObjectName(QStringLiteral("informationValueList"));
    valueList->setSelectionMode(QAbstractItemView::SingleSelection);
    valueList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    valueList->setMinimumHeight(220);
    valueLayout->addWidget(valueList, 1);

    auto *emptyHint = new QLabel(
                tr("该类别还没有子项，在下方输入名称后点击「添加」。"),
                valueGroup);
    emptyHint->setObjectName(QStringLiteral("paramHint"));
    emptyHint->setWordWrap(true);
    valueLayout->addWidget(emptyHint);

    auto *editRow = new QHBoxLayout;
    editRow->setSpacing(8);
    auto *valueEdit = new QLineEdit(valueGroup);
    valueEdit->setObjectName(QStringLiteral("userInput"));
    valueEdit->setClearButtonEnabled(true);
    auto *addButton = new QPushButton(tr("添加"), valueGroup);
    addButton->setObjectName(QStringLiteral("userPrimaryButton"));
    addButton->setCursor(Qt::PointingHandCursor);
    addButton->setFixedHeight(30);
    editRow->addWidget(valueEdit, 1);
    editRow->addWidget(addButton);
    valueLayout->addLayout(editRow);

    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(8);
    auto *renameButton = new QPushButton(tr("重命名"), valueGroup);
    auto *deleteButton = new QPushButton(tr("删除"), valueGroup);
    auto *upButton = new QPushButton(tr("上移"), valueGroup);
    auto *downButton = new QPushButton(tr("下移"), valueGroup);
    const QList<QPushButton *> actionButtons = {
        renameButton, deleteButton, upButton, downButton
    };
    for (QPushButton *button : actionButtons) {
        button->setObjectName(QStringLiteral("userActionButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(28);
        actionRow->addWidget(button);
    }
    actionRow->addStretch(1);
    valueLayout->addLayout(actionRow);

    contentLayout->addWidget(categoryGroup);
    contentLayout->addWidget(valueGroup, 1);
    layout->addWidget(content, 1);

    auto *hint = new QLabel(
                tr("子项会保存到 QSettings 的 informationEntry/<类别>，"
                   "删除某项不会影响历史测试数据。"),
                page);
    hint->setObjectName(QStringLiteral("paramHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    const QVector<InformationEntryCategory> categories =
            informationEntryCategories();
    for (const InformationEntryCategory &category : categories) {
        auto *item = new QListWidgetItem(
                    QString::fromUtf8(category.title), categoryList);
        item->setData(Qt::UserRole, QString::fromLatin1(category.key));
        item->setData(Qt::UserRole + 1,
                      QString::fromUtf8(category.title));
        item->setData(Qt::UserRole + 2,
                      QString::fromUtf8(category.placeholder));
    }

    auto currentKey = [categoryList]() -> QString {
        const QListWidgetItem *item = categoryList->currentItem();
        return item ? item->data(Qt::UserRole).toString() : QString();
    };
    auto currentTitle = [categoryList]() -> QString {
        const QListWidgetItem *item = categoryList->currentItem();
        return item ? item->data(Qt::UserRole + 1).toString() : QString();
    };
    auto currentPlaceholder = [categoryList]() -> QString {
        const QListWidgetItem *item = categoryList->currentItem();
        return item ? item->data(Qt::UserRole + 2).toString() : QString();
    };

    auto updateButtonStates = [=]() {
        const bool hasSelection = valueList->currentItem() != nullptr;
        const int row = valueList->currentRow();
        renameButton->setEnabled(hasSelection);
        deleteButton->setEnabled(hasSelection);
        upButton->setEnabled(hasSelection && row > 0);
        downButton->setEnabled(hasSelection && row >= 0
                               && row + 1 < valueList->count());
    };

    auto reloadValues = [=]() {
        const QString key = currentKey();
        valueList->clear();
        if (key.isEmpty()) {
            currentTitleLabel->clear();
            emptyHint->setVisible(true);
            updateButtonStates();
            return;
        }

        const QStringList values = informationEntryValues(key);
        for (const QString &value : values) {
            valueList->addItem(value);
        }
        currentTitleLabel->setText(
                    tr("「%1」的子信息（%2 项）")
                    .arg(currentTitle())
                    .arg(values.size()));
        emptyHint->setVisible(values.isEmpty());
        if (!values.isEmpty()) {
            valueList->setCurrentRow(0);
        }
        updateButtonStates();
    };

    auto selectValue = [=](const QString &value) {
        const QList<QListWidgetItem *> matches =
                valueList->findItems(value, Qt::MatchExactly);
        if (!matches.isEmpty()) {
            valueList->setCurrentItem(matches.first());
        }
    };

    auto addValue = [=]() {
        const QString key = currentKey();
        if (key.isEmpty()) {
            return;
        }
        const QString value = valueEdit->text().trimmed();
        if (value.isEmpty()) {
            valueEdit->setFocus();
            return;
        }
        QStringList values = informationEntryValues(key);
        if (!values.contains(value)) {
            values.append(value);
            setInformationEntryValues(key, values);
        }
        valueEdit->clear();
        reloadValues();
        selectValue(value);
    };

    auto renameValue = [=]() {
        QListWidgetItem *item = valueList->currentItem();
        if (!item) {
            return;
        }
        const QString key = currentKey();
        if (key.isEmpty()) {
            return;
        }
        const QString oldValue = item->text();
        bool ok = false;
        const QString newValue = QInputDialog::getText(
                    this, tr("重命名子信息"),
                    tr("「%1」的新名称：").arg(oldValue),
                    QLineEdit::Normal, oldValue, &ok).trimmed();
        if (!ok || newValue.isEmpty() || newValue == oldValue) {
            return;
        }

        QStringList values = informationEntryValues(key);
        const int index = values.indexOf(oldValue);
        if (index < 0) {
            return;
        }
        if (values.contains(newValue)) {
            MedicalMessageBox::information(
                        this, tr("重命名子信息"),
                        tr("「%1」已经存在。").arg(newValue));
            reloadValues();
            selectValue(newValue);
            return;
        }
        values[index] = newValue;
        setInformationEntryValues(key, values);
        reloadValues();
        selectValue(newValue);
    };

    auto deleteValue = [=]() {
        QListWidgetItem *item = valueList->currentItem();
        if (!item) {
            return;
        }
        const QString key = currentKey();
        if (key.isEmpty()) {
            return;
        }
        const QString value = item->text();
        if (MedicalMessageBox::question(
                    this, tr("删除子信息"),
                    tr("确定删除「%1」？").arg(value),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
        QStringList values = informationEntryValues(key);
        values.removeAll(value);
        setInformationEntryValues(key, values);
        reloadValues();
    };

    auto moveValue = [=](int delta) {
        QListWidgetItem *item = valueList->currentItem();
        if (!item) {
            return;
        }
        const int row = valueList->row(item);
        const int target = row + delta;
        if (row < 0 || target < 0 || target >= valueList->count()) {
            return;
        }
        const QString key = currentKey();
        QStringList values = informationEntryValues(key);
        if (row >= values.size() || target >= values.size()) {
            return;
        }
        const QString moved = values.at(row);
        values[row] = values.at(target);
        values[target] = moved;
        setInformationEntryValues(key, values);
        reloadValues();
        valueList->setCurrentRow(target);
    };

    connect(categoryList, &QListWidget::currentRowChanged,
            this, [=](int) {
        valueEdit->clear();
        valueEdit->setPlaceholderText(
                    tr("输入%1子项，%2")
                    .arg(currentTitle(), currentPlaceholder()));
        reloadValues();
    });
    connect(valueList, &QListWidget::currentRowChanged,
            this, [=](int) { updateButtonStates(); });
    connect(valueList, &QListWidget::itemDoubleClicked,
            this, [=](QListWidgetItem *) { renameValue(); });
    connect(addButton, &QPushButton::clicked, this, addValue);
    connect(valueEdit, &QLineEdit::returnPressed, this, addValue);
    connect(renameButton, &QPushButton::clicked, this, renameValue);
    connect(deleteButton, &QPushButton::clicked, this, deleteValue);
    connect(upButton, &QPushButton::clicked, this, [=]() { moveValue(-1); });
    connect(downButton, &QPushButton::clicked, this, [=]() { moveValue(1); });

    categoryList->setCurrentRow(0);
    valueEdit->setPlaceholderText(
                tr("输入%1子项，%2").arg(currentTitle(), currentPlaceholder()));
    reloadValues();
    return page;
}

void SettingsDialog::rebuildBasicPage()
{
    if (!m_stack) {
        return;
    }
    QWidget *current = m_stack->currentWidget();
    QWidget *oldPage = m_stack->widget(m_basicPageIndex);
    if (!oldPage) {
        return;
    }
    m_stack->removeWidget(oldPage);
    oldPage->deleteLater();

    QWidget *newPage = createBasicPage();
    m_stack->insertWidget(m_basicPageIndex, newPage);
    if (current) {
        m_stack->setCurrentWidget(current);
    }
    applyUserManagementAccess();
    updateBasicMachineInfo();
}
