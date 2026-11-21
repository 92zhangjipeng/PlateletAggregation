#include "testsession.h"

TestSession &TestSession::instance()
{
    static TestSession session;
    return session;
}

TestSession::TestSession(QObject *parent)
    : QObject(parent)
{
}

bool TestSession::isTestingActive() const
{
    return m_testingActive;
}

void TestSession::setTestingActive(bool active)
{
    if (m_testingActive == active) {
        return;
    }
    m_testingActive = active;
    emit testingActiveChanged(active);
}
