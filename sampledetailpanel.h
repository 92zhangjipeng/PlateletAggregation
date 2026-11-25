#ifndef SAMPLEDETAILPANEL_H
#define SAMPLEDETAILPANEL_H

#include "testdatastore.h"

#include <QVector>
#include <QWidget>

class QLabel;
class QScrollArea;
class QToolButton;
class QVBoxLayout;
class QPropertyAnimation;
class TestDataStore;

// 聚集率缺失哨兵：远小于任何真实值（真实值可为负但不会低至此）。
// 由 cpp 统一引用，避免 magic number 散落。
static constexpr double kSentinelMissing = -9999.0;

/**
 * 结果详情侧边面板（树形可折叠）。
 *
 * 点击结果表格某一行时，从窗口右边缘滑出覆盖在前面板。内容为树形：
 * - 每个试剂一行（可折叠）：试剂名 + 状态徽标（完成/测试中/异常/待测）
 *   收起态显示一行概要（60S/180S/300S/MAX 聚集率）；
 * - 展开该试剂行后显示：测试曲线 + 集聚曲线（可切换）+ 完整参数网格
 *   + 参考区间判定。
 *
 * 顶部为样本信息卡（样本号/姓名/年龄/性别/类型/时间/PPP）。
 * 数据通过 TestDataStore::loadSample 一次性读取；曲线复用库中已存好的
 * prpPoints / aggregationPoints（聚集率已逐点算好）。
 */
class SampleDetailPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SampleDetailPanel(QWidget *parent = nullptr);
    ~SampleDetailPanel() override;

    void setStore(TestDataStore *store);

    /** 读取并展示指定样本；已展示时下次点击刷新内容 */
    void showSample(qint64 sampleId);

    void popOut();
    bool isPanelVisible() const;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    /** 单个试剂在固定时间点的聚集率 */
    struct ReagentParams {
        int reagentIndex = -1;      // 参考区间下标 AA0/ADP1/EPI2/COL3/RIS4
        double s60 = kSentinelMissing;      // 哨兵=缺失；真实值可能为负
        double s180 = kSentinelMissing;
        double s300 = kSentinelMissing;
        double maxPercent = kSentinelMissing;   // 整段最大聚集率
        int maxSecond = -1;
        bool hasCurve = false;
        bool hasAggregation = false;
        QString status;             // 完成 / 测试中 / 异常结果 / 待测
    };

    QWidget *buildHeaderCard(const SampleTestData &sample);
    QWidget *buildReagentCard(const TestCurveData &test,
                              const SampleTestData &sample);
    ReagentParams computeParams(const TestCurveData &test);
    void clearTabs();

    void startPopIn();
    QWidget *currentParentWidget() const;

    TestDataStore *m_store = nullptr;
    QScrollArea *m_scroll = nullptr;
    QVBoxLayout *m_cardLayout = nullptr;
    QPropertyAnimation *m_slideAnimation = nullptr;
    qint64 m_currentSampleId = 0;
    bool m_animating = false;
};

#endif // SAMPLEDETAILPANEL_H