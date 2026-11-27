#ifndef ADDSAMPLEWIDGET_H
#define ADDSAMPLEWIDGET_H

#include "MachineModel.h"
#include "testsamplequeue.h"

#include <QSet>
#include <QImage>
#include <QStringList>
#include <QWidget>

class FramelessDialog;
class QDialog;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QTableWidget;
class QTableWidgetItem;
class QVBoxLayout;
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

    /**
     * 测试流程调用：标记样本进入/退出测试。
     * 加样页表格状态保持“加样完成”不变，测试状态只更新到结果信息表。
     */
    void setSampleTesting(const QString &sampleNumber, bool testing);
    /** 测试流程调用：加样完成 → 表格状态固定为“加样完成”，血样孔释放，
     *  同时通知结果信息表把该样本记为“测试中” */
    void setSampleAdded(const QString &sampleNumber);
    /** 测试流程调用：样本所有试剂完成；加样页状态不变，结果表记“测试结束” */
    void setSampleCompleted(const QString &sampleNumber);

    /** 测试流程调用：样本中止/异常；加样页状态不变，结果表记“异常结果” */
    void setSampleAbnormal(const QString &sampleNumber);

    /** 测试复位/中止时清除“正在测试”标记（加样页状态不变） */
    void clearTestingSamples();

signals:
    /** 样本确认进入待测队列后发出（样本号、样本类型），
     *  供结果信息表插入一条“待测”占位记录 */
    void sampleQueued(const QString &sampleNumber, const QString &sampleType);

    /** 样本测试状态变化：待测 / 测试中 / 测试结束 / 异常结果 */
    void sampleStatusChanged(const QString &sampleNumber,
                             const QString &status);

public:
    /** 待测 + 已加样 + 测试中 + 完成样本数；用于锁住会影响孔位的设置 */
    int activeSampleCount() const;

    /** 全血模式主板测高上升沿触发：每次触发自动添加一个样本 */
    void handleHeightDetectionTrigger();

    /**
     * 测高拍照完成后由 MainWindow 调用：把拍到的 QImage 显示到
     * 左侧实时面板，并绑定到最新一个（测高触发添加的）待定样本的
     * 「图像」列；识别高度写入该行 HeightColumn。
     */
    void showCapturedImage(const QImage &image, double heightMm = -1.0);
    /** 实时取景帧回调（全血预览），测高触发定格后停止刷新。 */
    void showLivePreview(const QImage &image);

    /** 已确认待测样本队列（测试流程后续从这里取样本） */
    TestQueue *testQueue();
    const TestQueue *testQueue() const;

protected:
    void showEvent(QShowEvent *event) override;
    /** 实时图像点击弹出大图、面板缩放时重新适配。 */
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void addSample();
    void confirmSamples();
    void removeSelectedSamples();
    void modifySampleNumber();
    void handleTableItemChanged(QTableWidgetItem *item);

private:
    void addSampleInternal(bool fromHeightTrigger);
    void buildUi();
    QFrame *createSampleTableCard();
    QFrame *createBloodAreaCard();
    QFrame *createLiveImageCard();
    QWidget *createQuickEntryWidget();
    void createSampleListPopup();
    /** 全血/血浆切换右侧布局：表格在右栏与独立样本列表窗口之间搬迁。 */
    void applyModeLayout(bool plasma);
    /** 测高触发后显示样本列表窗口（已显示则不重复弹）。 */
    void showSampleListPopup();
    void updateSampleQuality();
    void updateHeightHeader();
    void updateSampleHeightValues();
    void updateSampleCount();
    void updateScanHint();
    void updateQuickSampleNumber();
    void updatePositionEditability();
    void updateRowEditability(int row);
    bool setSampleStatus(const QString &sampleNumber, const QString &status);
    /** 已加样/测试中/完成的样本，其血样孔已释放，可被后续样本复用 */
    bool isRowPositionReleased(int row) const;
    void reallocatePositionsForMode();
    bool queuedSampleFromRow(int row, QueuedSample *sample) const;
    void rebuildTestQueueFromConfirmedRows();
    void focusBarcodeCell(int row);
    void modifyTestItems(int row);
    void syncBloodAreaSamples();
    void handleBloodAreaMoveStarted(const QStringList &positions);
    void handleBloodAreaMoveCommitted(const QStringList &positions);
    void handleBloodAreaMoveCancelled();
    void adjustSampleRowHeight(int row);
    void adjustAllSampleRowHeights();

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
    QWidget *m_plasmaRight = nullptr;       // 血浆模式右侧列
    QVBoxLayout *m_plasmaRightLayout = nullptr;
    QHBoxLayout *m_rightButtonsLayout = nullptr;
    QWidget *m_quickEntryWidget = nullptr;  // 样本号/条码快速录入
    QLabel *m_quickSampleNoCaption = nullptr;
    QFrame *m_sampleTableCard = nullptr;
    QVBoxLayout *m_sampleTableLayout = nullptr;
    QFrame *m_wholeBloodBar = nullptr;      // 全血底部条码/状态条
    QHBoxLayout *m_wholeBloodBarLayout = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_sampleListButton = nullptr;
    FramelessDialog *m_sampleListPopup = nullptr;   // 非模态样本列表窗口
    bool m_popupPositioned = false;         // 首次弹出时摆到主窗口右侧
    bool m_modeLayoutPlasma = false;
    bool m_modeLayoutReady = false;
    QFrame *m_liveImageCard = nullptr;      // 全血实时图像卡片（右侧整列）
    QLabel *m_liveImageCaption = nullptr;   // 全血实时图像标题/提示
    QLabel *m_liveImage = nullptr;          // 全血实时图像
    QImage m_lastLiveImage;                 // 最近一帧（供放大/缩放重绘）
    bool m_liveFrozen = false;              // 测高触发已定格拍照
    TestQueue m_testQueue;
    QSet<QString> m_testingSamples;   // 正在测试的样本号（不改变表格状态文字）
    bool m_updatingTable = false;
    int m_movingRow = -1;
    bool m_lastPppPrpMode = false;
    bool m_quickSampleNoEdited = false;
};

#endif // ADDSAMPLEWIDGET_H
