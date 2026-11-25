#ifndef QUERYWIDGET_H
#define QUERYWIDGET_H

#include <QDateTime>
#include <QWidget>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QRadioButton;
class QShowEvent;
class TestDataStore;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * 「查询」标签页：树形结构展示测试结果。
 *
 * - 顶部：关键字输入 + 时间范围（当天 / 本周 / 本月 / 本年 / 自定义时间）。
 * - 结果以 QTreeWidget 呈现：样本作为一级节点，其下挂每个试剂的子节点
 *   （试剂名 + 状态 + 60S/180S/300S/MAX 聚集率）。
 * - 默认查询空间：无关键字时也要求一个时间范围（默认当天），避免一次
 *   列出全部历史记录；点「查询」才加载。
 */
class QueryWidget : public QWidget
{
    Q_OBJECT

public:
    explicit QueryWidget(TestDataStore *store, QWidget *parent = nullptr);

public slots:
    void refresh();

signals:
    /** 用户点击某个样本节点，请求展示详情；sampleId 为该样本 id */
    void sampleSelected(qint64 sampleId);

protected:
    void showEvent(QShowEvent *event) override;

private:
    /** 化简为一组时间过滤条件（toIso 含当天 23:59:59） */
    struct TimeRange {
        QString fromIso;
        QString toIso;
        bool valid = false;
    };
    TimeRange currentTimeRange() const;
    QDateTime dayStart(const QDate &d) const;  // 当天 00:00:00
    QDateTime dayEnd(const QDate &d) const;    // 当天 23:59:59

    TestDataStore *m_store = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QRadioButton *m_radioToday = nullptr;
    QRadioButton *m_radioThisWeek = nullptr;
    QRadioButton *m_radioThisMonth = nullptr;
    QRadioButton *m_radioThisYear = nullptr;
    QRadioButton *m_radioCustom = nullptr;
    QLineEdit *m_fromEdit = nullptr;
    QLineEdit *m_toEdit = nullptr;
    QButtonGroup *m_radioGroup = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_countLabel = nullptr;
};

#endif // QUERYWIDGET_H