#ifndef ADDSAMPLEWIDGET_H
#define ADDSAMPLEWIDGET_H

#include "MachineModel.h"

#include <QWidget>

class QFrame;
class QLabel;
class QShowEvent;
class QStackedWidget;
class BloodSampleAreaWidget;

/**
 * 加样界面。
 *
 * 根据基础配置“样本质量”显示两套界面：
 * - 全血模式：测高 + 摄像头拍照识别血样高度（本次只做占位，后续设计）；
 * - 血降模式：另一套加样界面（本次只做占位，后续设计）。
 *
 * 两套界面共用下方同一块「血样区」：按机型绘制 3D 血样孔，
 * 每组 5 列 × 2 行 = 10 孔，KS600=6 组、KS800=8 组、KS1200=12 组。
 */
class AddSampleWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AddSampleWidget(MachineModel model = defaultMachineModel(),
                             QWidget *parent = nullptr);

    void setMachineModel(MachineModel model);

    /** 按当前 QSettings 中的样本质量切换全血/血降界面 */
    void refreshMode();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void buildUi();
    QFrame *createWholeBloodCard();
    QFrame *createPlasmaCard();
    QFrame *createBloodAreaCard();
    void updateModeLabels();

    MachineModel m_model = defaultMachineModel();
    QLabel *m_modeBadge = nullptr;
    QLabel *m_subtitle = nullptr;
    QLabel *m_bloodCaption = nullptr;
    QStackedWidget *m_modeStack = nullptr;
    BloodSampleAreaWidget *m_bloodArea = nullptr;
};

#endif // ADDSAMPLEWIDGET_H
