#ifndef TESTSAMPLEQUEUE_H
#define TESTSAMPLEQUEUE_H

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

/**
 * 已确认待测的样本（测试队列条目）。
 *
 * 加样列表里点「确认」后，待定样本会转成待测并进入 TestQueue；
 * 测试流程后续从这里取样本启动测试。
 */
struct QueuedSample
{
    QString sampleNumber;
    QString barcode;
    QStringList testItems;   // AA / ADP / COL / EPI / RIS，可多项
    QString height;          // 识别高度/吸样高度
    QStringList positions;   // 全局血样孔号，PRP 1 个，PPP+PRP 2 个
    bool pppPrp = false;     // 确认时的测试模式
    QDateTime queuedAt;
};

/**
 * 待测样本队列。
 *
 * 按确认顺序 FIFO；同一队列内样本号不能重复。
 */
class TestQueue
{
public:
    /** 入队；样本号为空或已存在时返回 false 并给出原因 */
    bool enqueue(const QueuedSample &sample, QString *error = nullptr);

    /** 从队列移除指定样本号（删除待测行时调用） */
    bool removeBySampleNumber(const QString &sampleNumber);

    /** 队列中是否已有该样本号 */
    bool contains(const QString &sampleNumber) const;

    /** 取队首并移除；队列为空返回 false */
    bool dequeue(QueuedSample *sample);

    /** 当前队列快照（按入队顺序） */
    QVector<QueuedSample> samples() const;

    int size() const;
    void clear();

private:
    QVector<QueuedSample> m_samples;
};

#endif // TESTSAMPLEQUEUE_H
