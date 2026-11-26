#ifndef CURVEDISPLAYWIDGET_H
#define CURVEDISPLAYWIDGET_H

#include "testdatastore.h"

#include <QVector>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * 「曲线」标签页：左侧树形浏览全部样本（T 开头），右侧叠加显示
 * 样本内各试剂（AA/ADP/EPI/COL/RIS，最多 5 条）的聚集曲线，
 * 曲线下方为数据分析表格。
 *
 * - 树形：根「全部样本」→ 年 → 月 → 日 → 样本号，便于样本随时间增多折叠。
 * - 勾选图例可单条/多条显示，默认显示该样本的全部曲线。
 * - 聚集率曲线由 PRP/PPP/PRP0 按对数公式现算（库里未存时）。
 * - 表格列：试剂 | 60S | 180S | 300S | Max | 斜率 | 面积AUC | TMA时间 |
 *   延时时间 | 粘附率 | 参考范围 | 状态。
 */
class CurveDisplayWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CurveDisplayWidget(TestDataStore *store, QWidget *parent = nullptr);

public slots:
    void refresh();

private:
    /** 计算某试剂聚集率曲线（%）；越界/无效时返回空 */
    QVector<double> aggregationCurveFor(const TestCurveData &test,
                                        double ppp) const;

    void rebuildSampleTree();
    void loadSample(qint64 sampleId);
    void updateSeriesVisibility();
    void applySeriesVisibility();
    void populateTable(const SampleTestData &sample);

    /** 叠绘自定义控件：支持多条聚集率曲线，带图例颜色。 */
    class CurvePlotPanel;
    CurvePlotPanel *m_plot = nullptr;

    TestDataStore *m_store = nullptr;
    QTreeWidget *m_tree = nullptr;
    QWidget *m_plotArea = nullptr;
    QHBoxLayout *m_legendBox = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_statusLabel = nullptr;

    /** 当前样本及其试剂数据 */
    SampleTestData m_sample;
    qint64 m_currentSampleId = 0;
    /** 每个试剂一条曲线；index 与 m_sample.tests 对齐 */
    QVector<bool> m_reagentVisible;
};

#endif // CURVEDISPLAYWIDGET_H