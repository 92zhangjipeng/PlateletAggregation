#ifndef RESULTSTABLEWIDGET_H
#define RESULTSTABLEWIDGET_H

#include "testdatastore.h"

#include <QTableWidget>
#include <QVector>

class QLabel;
class QResizeEvent;
class QTableWidgetItem;

/**
 * 测试结果表格（QTableWidget + 单元格代理）。
 *
 * 用于「结果信息」和「查询」标签页。列：
 * 测试状态 / 样本号 / 姓名 / 样本类型 / 试剂数 / 检测时间 /
 * 科别 / 性别 / 病区 / 送检医生 / 检验医生 / 审核医生 / 临床诊断 / 备注。
 *
 * - 姓名列可直接编辑；
 * - 科别、性别、病区、送检医生、检验医生、审核医生、临床诊断、备注
 *   使用统一下拉代理：单击只选中，双击（或按键进入编辑）才弹出下拉；
 * - 单元格编辑完成后发出 entryValueChanged，由外层写入 SQLite。
 */
class ResultsTableWidget : public QTableWidget
{
    Q_OBJECT

public:
    explicit ResultsTableWidget(QWidget *parent = nullptr);

    void setRows(const QVector<SampleSummary> &rows);
    QSize sizeHint() const override;

signals:
    /** 点击不可编辑单元格时发出对应样本 id */
    void rowActivated(qint64 sampleId);

    /** 单元格编辑完成：样本 id、数据库列名、新值 */
    void entryValueChanged(qint64 sampleId, const QString &entryKey,
                           const QString &value);

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void handleItemChanged(QTableWidgetItem *item);

private:
    void applyColumnWidths();
    void updateTableHeight();
    void updateEmptyLabel();

    QVector<SampleSummary> m_rows;
    QLabel *m_emptyLabel = nullptr;
    bool m_populating = false;
};

#endif // RESULTSTABLEWIDGET_H
