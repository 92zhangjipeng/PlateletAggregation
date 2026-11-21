#ifndef ADDSAMPLEWIDGET_H
#define ADDSAMPLEWIDGET_H

#include "MachineModel.h"

#include <QStringList>
#include <QWidget>

class QFrame;
class QLabel;
class QPushButton;
class QShowEvent;
class QTableWidget;
class QTableWidgetItem;
class TestDataStore;
class BloodSampleAreaWidget;

/**
 * 加样界面：上方样本列表 + 下方血样区。
 *
 * 样本列表列：样本号、条码、测试项目、识别高度/吸样高度、样本位置。
 * 样本号规则：T + yyyyMMdd + 4 位序号，默认取测试数据库当天最大号 + 1；
 * 修改时要求表格和数据库都不存在重复样本号。
 *
 * 血样区按机型绘制采血管架，每组 5 行 × 2 列 = 10 孔，
 * 组名使用 A、B、C……；采血管液体颜色跟随样本质量：
 * - 全血：下层红色 60%，上层淡黄色 40%；
 * - 血浆：采血管内全部淡黄色。
 */
class AddSampleWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AddSampleWidget(MachineModel model = defaultMachineModel(),
                             TestDataStore *dataStore = nullptr,
                             QWidget *parent = nullptr);

    void setMachineModel(MachineModel model);

    /** 重新读取样本质量，刷新采血管液体颜色和高度列标题 */
    void refreshMode();

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void addSample();
    void removeSelectedSamples();
    void modifySampleNumber();
    void handleTableItemChanged(QTableWidgetItem *item);

private:
    void buildUi();
    QFrame *createSampleTableCard();
    QFrame *createBloodAreaCard();
    void updateSampleQuality();
    void updateHeightHeader();
    void updateSampleCount();

    QStringList allPositions() const;
    QStringList positionsForRow(int row) const;
    QString nextFreePosition() const;
    QString generateNextSampleNumber(QString *error = nullptr) const;
    bool sampleNumberInTable(const QString &number,
                             int exceptRow = -1) const;
    bool positionInTable(const QString &position,
                         int exceptRow = -1) const;
    bool validateSampleNumber(const QString &number, int exceptRow,
                              QString *reason) const;
    void appendSampleRow(const QString &sampleNumber,
                         const QString &barcode,
                         const QString &testItem,
                         const QString &height,
                         const QString &position);
    void restoreTableItem(QTableWidgetItem *item);

    MachineModel m_model = defaultMachineModel();
    TestDataStore *m_dataStore = nullptr;
    QLabel *m_subtitle = nullptr;
    QLabel *m_bloodCaption = nullptr;
    QLabel *m_countLabel = nullptr;
    QTableWidget *m_sampleTable = nullptr;
    QPushButton *m_addSampleButton = nullptr;
    QPushButton *m_removeSampleButton = nullptr;
    QPushButton *m_modifySampleButton = nullptr;
    BloodSampleAreaWidget *m_bloodArea = nullptr;
    bool m_updatingTable = false;
};

#endif // ADDSAMPLEWIDGET_H
