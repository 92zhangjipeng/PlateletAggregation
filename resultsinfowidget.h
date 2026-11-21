#ifndef RESULTSINFOWIDGET_H
#define RESULTSINFOWIDGET_H

#include <QWidget>

class QLabel;
class QShowEvent;
class TestDataStore;
class ResultsTableWidget;

/**
 * 「结果信息」标签页：用自绘表格显示最近测试结果。
 */
class ResultsInfoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ResultsInfoWidget(TestDataStore *store,
                               QWidget *parent = nullptr);

public slots:
    void refresh();

protected:
    void showEvent(QShowEvent *event) override;

private:
    TestDataStore *m_store = nullptr;
    ResultsTableWidget *m_table = nullptr;
    QLabel *m_countLabel = nullptr;
};

#endif // RESULTSINFOWIDGET_H
