#ifndef CONSUMABLESDIALOG_H
#define CONSUMABLESDIALOG_H

#include "FramelessDialog.h"
#include "mainboardconsumables.h"
#include "mainboardstatus.h"

#include <QString>

class QLabel;
class QProgressBar;
class QPushButton;

/**
 * 耗材管理：集中显示主板每秒上报的耗材信息。
 *
 * 包括 MFC 读卡状态、刷卡类型/瓶数、耗材种类/状态/余量、
 * 试剂有效期等；数据由 MainWindow 收到主板状态后实时推送。
 */
class ConsumablesDialog : public FramelessDialog
{
    Q_OBJECT

public:
    explicit ConsumablesDialog(QWidget *parent = nullptr);

public slots:
    void setMainboardStatus(const MainBoardStatus &status);
    void clearStatus(const QString &reason = QString());
    /** 主板逐个读取的某个位置剩余总量；试管单位为板，其余为瓶 */
    void setConsumableAmount(int position, int bottles, int ratio);
    void clearConsumableAmounts();
    /** 测试机械臂运动/未联机时禁用更换按钮并显示原因 */
    void setReplaceAllowed(bool allowed, const QString &reason = QString());
    /** 更新「充值状态」显示（成功/进行中为绿色，失败为红色） */
    void setRechargeStatus(const QString &text, bool ok);

signals:
    /** 请求更换某个试剂位（0~4 左，8~12 右） */
    void reagentReplaceRequested(int position);

private:
    enum AmountRow {
        AmountAA = 0,
        AmountEPI,
        AmountADP,
        AmountCOL,
        AmountRIS,
        AmountInternalClean,
        AmountTestTube,
        AmountExternalClean,
        AmountRowCount
    };

    QWidget *createContent();
    QWidget *createStatCard(const QString &caption, QLabel **valueLabel,
                            QWidget *parent);
    QWidget *createRemainingCard(QWidget *parent);
    QWidget *createAmountCard(QWidget *parent);
    QWidget *createDetailCard(QWidget *parent);
    void updateConsumableAmountLabels();
    void setLabelState(QLabel *label, const char *state);
    static QString expiryText(quint8 year, quint8 month, bool *expired);

    QLabel *m_sourceBadge = nullptr;
    QLabel *m_updatedLabel = nullptr;

    QLabel *m_consTypeValue = nullptr;
    QLabel *m_remainPercentValue = nullptr;
    QLabel *m_remainDetailValue = nullptr;
    QProgressBar *m_remainBar = nullptr;
    QLabel *m_expiryValue = nullptr;
    QLabel *m_regRemainValue = nullptr;

    QLabel *m_amountValues[AmountRowCount] = {};
    QLabel *m_amountLeftValues[AmountRowCount] = {};
    QLabel *m_amountRightValues[AmountRowCount] = {};
    int m_positionBottles[MainBoardConsumable::kPositionCount] = {};
    int m_positionRatios[MainBoardConsumable::kPositionCount] = {};
    bool m_positionValid[MainBoardConsumable::kPositionCount] = {};

    QLabel *m_detailMfc = nullptr;
    QLabel *m_detailRegType = nullptr;
    QLabel *m_detailRegRemain = nullptr;
    QLabel *m_detailConsStatus = nullptr;
    QLabel *m_detailConsType = nullptr;
    QLabel *m_detailConsumable = nullptr;
    QLabel *m_detailExpiry = nullptr;
    QLabel *m_detailUpdated = nullptr;
    QLabel *m_detailRecharge = nullptr;

    QPushButton *m_replaceButtons[MainBoardConsumable::kPositionCount] = {};
    bool m_replaceAllowed = false;
    QString m_replaceDisabledReason;
    QString m_replaceEnabledTip;
};

#endif // CONSUMABLESDIALOG_H
