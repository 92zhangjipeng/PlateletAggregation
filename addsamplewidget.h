#ifndef ADDSAMPLEWIDGET_H
#define ADDSAMPLEWIDGET_H

#include "MachineModel.h"

#include <QWidget>

class QFrame;
class QLabel;
class QShowEvent;
class BloodSampleAreaWidget;

/**
 * 加样界面：只保留血样区。
 *
 * 血样区按机型绘制采血管架，每组 5 行 × 2 列 = 10 孔，
 * 组名使用 A、B、C……；孔内插入采血管，液体颜色跟随样本质量：
 * - 全血：下层红色 60%，上层淡黄色 40%；
 * - 血浆：采血管内全部淡黄色。
 */
class AddSampleWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AddSampleWidget(MachineModel model = defaultMachineModel(),
                             QWidget *parent = nullptr);

    void setMachineModel(MachineModel model);

    /** 重新读取样本质量，刷新采血管液体颜色 */
    void refreshMode();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void buildUi();
    QFrame *createBloodAreaCard();
    void updateSampleQuality();

    MachineModel m_model = defaultMachineModel();
    QLabel *m_subtitle = nullptr;
    QLabel *m_bloodCaption = nullptr;
    BloodSampleAreaWidget *m_bloodArea = nullptr;
};

#endif // ADDSAMPLEWIDGET_H
