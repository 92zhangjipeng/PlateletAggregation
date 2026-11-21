#ifndef TESTSESSION_H
#define TESTSESSION_H

#include <QObject>

/**
 * 全局测试状态。
 *
 * 任意通道开始测试时置为 true，所有通道都结束测试后置为 false。
 * 模组视图只有在 testingActive() 为 true、并且对应玻璃管内已经插入试管时，
 * 才允许点击玻璃管打开该通道的实时曲线。
 */
class TestSession : public QObject
{
    Q_OBJECT

public:
    static TestSession &instance();

    bool isTestingActive() const;

public slots:
    void setTestingActive(bool active);

signals:
    void testingActiveChanged(bool active);

private:
    explicit TestSession(QObject *parent = nullptr);

    bool m_testingActive = false;
};

#endif // TESTSESSION_H
