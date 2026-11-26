#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "FramelessDialog.h"
#include "MachineModel.h"
#include "instrumentcoordinates.h"
#include "medicalwidgets.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>

class CommunicationWorker;
class CoordinateMaintenanceWidget;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QLabel;
class QTableWidget;
class QVBoxLayout;
class ModuleControlWidget;

class SettingsDialog : public FramelessDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(CommunicationWorker *commWorker,
                            InstrumentCoordinates *coordinates,
                            QWidget *parent = nullptr,
                            MachineModel model = defaultMachineModel());

    /** 加样页提供待测/测试中样本数；大于 0 时禁止改样本质量和测试模式 */
    void setActiveSampleCountProvider(std::function<int()> provider);

    /**
     * 校验登录用户名/密码。成功返回 true，role 返回 admin/operator。
     * 用户记录保存在 QSettings security/users，密码为加盐 SHA-256。
     */
    static bool validateUserCredentials(const QString &name,
                                        const QString &password,
                                        QString *role = nullptr);
    /** 已保存的用户名，供登录页下拉显示。 */
    static QStringList existingUserNames();
    /** 确保内置测试账号 test（空密码、维护人员）存在；程序启动时调用一次。 */
    static void ensureTestUser();
    /** 当前登录用户。用户管理按此权限限制增删改。 */
    void setCurrentUser(const QString &name, const QString &role);
    /** 机型切换后刷新坐标维护页（重建树以匹配新机型范围）。 */
    void refreshCoordinates();

public slots:
    /** 串口/主控收到模组应答帧后调用，转发到模组控制页显示 */
    void handleIncomingFrame(const QByteArray &frame);

signals:
    /** 模组控制页生成的命令帧，便于外部监听/记录 */
    void frameReady(const QByteArray &frame, const QString &description);
    /** 基础设置中通道启用/禁用状态变化 */
    void channelEnabledChanged(int globalChannel, bool enabled);
    /** 样本质量（全血/血浆）切换完成 */
    void sampleQualityChanged();
    /** 仪器参数页切换机型 */
    void machineModelChanged(MachineModel model);
    /** 坐标维护页「触发移动」请求执行定位校验 */
    void coordinateMotionRequested(
            const InstrumentCoordinates::MoveTarget &target);

private slots:
    void sendFrameToInstrument(const QByteArray &frame, const QString &description);
    void updateSerialState(bool connected);

private:
    QWidget *createBasicPage();
    QWidget *createMenuPage();
    QWidget *createModuleControlPage();
    QWidget *createCoordinatePage();
    QWidget *createInstrumentParamsPage();
    QWidget *createSampleNeedlePage();
    QWidget *createReagentNeedlePage();
    QWidget *createGripperPage();
    QWidget *createReferenceRangePage();
    QWidget *createInformationEntryPage();

    QVBoxLayout *createMaintenancePageLayout(QWidget *page,
                                             const QString &title,
                                             const QString &subtitle);
    QWidget *createIntParamField(QWidget *parent, const QString &label,
                                 int minimum, int maximum, int value,
                                 const QString &suffix,
                                 const std::function<void(int)> &setter);
    QWidget *createDoubleParamField(QWidget *parent, const QString &label,
                                    double minimum, double maximum,
                                    int decimals, double value,
                                    const QString &suffix,
                                    const std::function<void(double)> &setter);

    void promptMaintenanceLogin();
    void showBasicPage();
    void showMaintenanceMenu();
    void updateBasicMachineInfo();
    void rebuildBasicPage();
    int activeSampleCount() const;
    bool ensureSampleShapeEditable(const QString &what);

    void reloadUserTable(const QString &filter = QString());
    void addUserFromForm();
    void updateUserFromForm();
    void deleteSelectedUser();
    QString selectedUserName() const;
    void applyUserManagementAccess();

    CommunicationWorker *m_commWorker = nullptr;
    QStackedWidget *m_stack = nullptr;
    ModuleControlWidget *m_moduleControl = nullptr;

    int m_basicPageIndex = 0;
    int m_menuPageIndex = 1;
    int m_modulePageIndex = 2;
    int m_coordinatePageIndex = -1;
    int m_instrumentParamsPageIndex = -1;
    int m_sampleNeedlePageIndex = -1;
    int m_reagentNeedlePageIndex = -1;
    int m_gripperPageIndex = -1;
    int m_referenceRangePageIndex = -1;
    int m_informationEntryPageIndex = -1;
    bool m_serialConnected = false;
    MachineModel m_machineModel = defaultMachineModel();
    std::function<int()> m_activeSampleCountProvider;

    InstrumentCoordinates m_ownCoordinates;
    InstrumentCoordinates *m_coordinates = nullptr;
    CoordinateMaintenanceWidget *m_coordinateWidget = nullptr;

    QLabel *m_basicModelValue = nullptr;
    QLabel *m_basicModuleValue = nullptr;
    QLabel *m_basicChannelValue = nullptr;
    QLabel *m_basicSerialValue = nullptr;

    QLineEdit *m_userSearchEdit = nullptr;
    QTableWidget *m_userTable = nullptr;
    QLineEdit *m_userNameEdit = nullptr;
    QLineEdit *m_userPasswordEdit = nullptr;
    MedicalComboBox *m_userRoleCombo = nullptr;
    QPushButton *m_userAddButton = nullptr;
    QPushButton *m_userEditButton = nullptr;
    QPushButton *m_userDeleteButton = nullptr;
    QString m_currentUserName;
    QString m_currentUserRole;
};

#endif // SETTINGSDIALOG_H
