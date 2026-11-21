#ifndef ADDSAMPLEWIDGET_H
#define ADDSAMPLEWIDGET_H

#include "MachineModel.h"
#include "testsamplequeue.h"

#include <QStringList>
#include <QWidget>

class QFrame;
class QLabel;
class QLineEdit;
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
 * 新增样本状态为“待定”；点「确认」后转“待测”并进入 TestQueue。
 * 待定样本点击“样本位置”后，可在左侧血样区拖动孔位（PPP+PRP 两根一起移动）。
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

    /** 测试流程调用：按样本号把状态改成“测试中”或“待测” */
    void setSampleTesting(const QString &sampleNumber, bool testing);

    /** 待测 + 测试中样本数；用于锁住会影响孔位的设置 */
    int activeSampleCount() const;

    /** 已确认待测样本队列（测试流程后续从这里取样本） */
    TestQueue *testQueue();
    const TestQueue *testQueue() const;

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void addSample();
    void confirmSamples();
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
    void updateScanHint();
    void updateQuickSampleNumber();
    void updatePositionEditability();
    void updateRowEditability(int row);
    void reallocatePositionsForMode();
    bool queuedSampleFromRow(int row, QueuedSample *sample) const;
    void rebuildTestQueueFromConfirmedRows();
    void focusBarcodeCell(int row);
    void modifyTestItems(int row);
    void syncBloodAreaSamples();
    void handleBloodAreaMoveCommitted(const QStringList &positions);
    void handleBloodAreaMoveCancelled();

    QStringList allPositions() const;
    QStringList positionsForRow(int row) const;
    QStringList positionParts(const QString &position) const;
    QString nextFreePosition() const;
    QString generateNextSampleNumber(QString *error = nullptr) const;
    bool sampleNumberInTable(const QString &number,
                             int exceptRow = -1) const;
    bool holeInTable(const QString &hole,
                     int exceptRow = -1) const;
    bool positionStringValid(const QString &position,
                             int exceptRow) const;
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
    QLabel *m_tableHint = nullptr;
    QTableWidget *m_sampleTable = nullptr;
    QLineEdit *m_quickSampleNoEdit = nullptr;
    QLineEdit *m_quickBarcodeEdit = nullptr;
    QPushButton *m_addSampleButton = nullptr;
    QPushButton *m_confirmButton = nullptr;
    QPushButton *m_removeSampleButton = nullptr;
    QPushButton *m_modifySampleButton = nullptr;
    BloodSampleAreaWidget *m_bloodArea = nullptr;
    TestQueue m_testQueue;
    bool m_updatingTable = false;
    int m_movingRow = -1;
    bool m_lastPppPrpMode = false;
    bool m_quickSampleNoEdited = false;
};

#endif // ADDSAMPLEWIDGET_H
