#ifndef TESTSESSION_H
#define TESTSESSION_H

#include <QObject>

/**
 * 全局测试状态。
 *
 * 点「开始」进入测试会话后置为 true；当前队列及所有在测样本
 * （含 300s 反应）都完成后置为 false，开始/耗材操作随之恢复。
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
