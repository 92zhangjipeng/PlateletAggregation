#ifndef QUERYWIDGET_H
#define QUERYWIDGET_H

#include <QWidget>

class QLabel;
class QLineEdit;
class QShowEvent;
class TestDataStore;
class ResultsTableWidget;

/**
 * 「查询」标签页：按样本号或患者姓名查询最近测试结果。
 *
 * 顶部输入关键字，下面仍使用自绘表格显示匹配结果；
 * 关键字为空时显示全部最近记录。
 */
class QueryWidget : public QWidget
{
    Q_OBJECT

public:
    explicit QueryWidget(TestDataStore *store, QWidget *parent = nullptr);

public slots:
    void refresh();

protected:
    void showEvent(QShowEvent *event) override;

private:
    TestDataStore *m_store = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    ResultsTableWidget *m_table = nullptr;
    QLabel *m_countLabel = nullptr;
};

#endif // QUERYWIDGET_H
