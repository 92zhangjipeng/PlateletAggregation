#ifndef RESULTSTABLEWIDGET_H
#define RESULTSTABLEWIDGET_H

#include "testdatastore.h"

#include <QVector>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;

/**
 * 自绘的测试结果表格。
 *
 * 用于「结果信息」和「查询」标签页：不依赖 QTableWidget，
 * 完全用 QPainter 绘制表头、斑马纹行、网格线和文本。
 * 外层放在 QScrollArea 中，行数超过可视高度时由外层滚动。
 */
class ResultsTableWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ResultsTableWidget(QWidget *parent = nullptr);

    void setRows(const QVector<SampleSummary> &rows);
    int rowCount() const;

signals:
    /** 点击某一行时发出对应的样本 id */
    void rowActivated(qint64 sampleId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    QSize sizeHint() const override;

private:
    int rowAt(int y) const;

    QVector<SampleSummary> m_rows;
};

#endif // RESULTSTABLEWIDGET_H
