#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "FramelessDialog.h"
#include "MachineModel.h"
#include "instrumentcoordinates.h"
#include "medicalwidgets.h"

#include <QByteArray>
#include <QString>

class CommunicationWorker;
class CoordinateMaintenanceWidget;
class QLineEdit;
class QStackedWidget;
class QLabel;
class QTableWidget;
class ModuleControlWidget;

class SettingsDialog : public FramelessDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(CommunicationWorker *commWorker,
                            InstrumentCoordinates *coordinates,
                            QWidget *parent = nullptr,
                            MachineModel model = defaultMachineModel());

public slots:
    /** 串口/主控收到模组应答帧后调用，转发到模组控制页显示 */
    void handleIncomingFrame(const QByteArray &frame);

signals:
    /** 模组控制页生成的命令帧，便于外部监听/记录 */
    void frameReady(const QByteArray &frame, const QString &description);
    /** 基础设置中通道启用/禁用状态变化 */
    void channelEnabledChanged(int globalChannel, bool enabled);
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

    void promptMaintenanceLogin();
    void showBasicPage();
    void showMaintenanceMenu();
    void updateBasicMachineInfo();

    void reloadUserTable(const QString &filter = QString());
    void addUserFromForm();
    void updateUserFromForm();
    void deleteSelectedUser();
    QString selectedUserName() const;

    CommunicationWorker *m_commWorker = nullptr;
    QStackedWidget *m_stack = nullptr;
    ModuleControlWidget *m_moduleControl = nullptr;

    int m_basicPageIndex = 0;
    int m_menuPageIndex = 1;
    int m_modulePageIndex = 2;
    int m_coordinatePageIndex = -1;
    bool m_serialConnected = false;
    MachineModel m_machineModel = defaultMachineModel();

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
};

#endif // SETTINGSDIALOG_H
