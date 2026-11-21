#include "testsamplequeue.h"

#include <QObject>

bool TestQueue::enqueue(const QueuedSample &sample, QString *error)
{
    if (error) {
        error->clear();
    }
    const QString number = sample.sampleNumber.trimmed();
    if (number.isEmpty()) {
        if (error) {
            *error = QObject::tr("样本号不能为空。");
        }
        return false;
    }
    if (contains(number)) {
        if (error) {
            *error = QObject::tr("待测队列中已存在样本号 %1。").arg(number);
        }
        return false;
    }
    m_samples.append(sample);
    return true;
}

bool TestQueue::removeBySampleNumber(const QString &sampleNumber)
{
    const QString trimmed = sampleNumber.trimmed();
    for (int i = 0; i < m_samples.size(); ++i) {
        if (m_samples.at(i).sampleNumber.trimmed() == trimmed) {
            m_samples.removeAt(i);
            return true;
        }
    }
    return false;
}

bool TestQueue::contains(const QString &sampleNumber) const
{
    const QString trimmed = sampleNumber.trimmed();
    for (const QueuedSample &sample : m_samples) {
        if (sample.sampleNumber.trimmed() == trimmed) {
            return true;
        }
    }
    return false;
}

bool TestQueue::dequeue(QueuedSample *sample)
{
    if (m_samples.isEmpty()) {
        return false;
    }
    if (sample) {
        *sample = m_samples.first();
    }
    m_samples.removeFirst();
    return true;
}

QVector<QueuedSample> TestQueue::samples() const
{
    return m_samples;
}

int TestQueue::size() const
{
    return m_samples.size();
}

void TestQueue::clear()
{
    m_samples.clear();
}
