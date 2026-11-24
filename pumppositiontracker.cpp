#include "pumppositiontracker.h"

#include <QtGlobal>

PumpPositionTracker &PumpPositionTracker::instance()
{
    static PumpPositionTracker tracker;
    return tracker;
}

bool PumpPositionTracker::sampleValid() const
{
    return m_sampleValid;
}

bool PumpPositionTracker::reagentValid() const
{
    return m_reagentValid;
}

int PumpPositionTracker::samplePosition() const
{
    return m_samplePosition;
}

int PumpPositionTracker::reagentPosition() const
{
    return m_reagentPosition;
}

void PumpPositionTracker::setSamplePosition(int position)
{
    m_samplePosition = qBound(0, position, 7200);
    m_sampleValid = true;
}

void PumpPositionTracker::setReagentPosition(int position)
{
    m_reagentPosition = qBound(0, position, 7200);
    m_reagentValid = true;
}

void PumpPositionTracker::setBootPrimePositions()
{
    m_samplePosition = 360;
    m_sampleValid = true;
    m_reagentPosition = 720;
    m_reagentValid = true;
}

void PumpPositionTracker::invalidate()
{
    m_sampleValid = false;
    m_reagentValid = false;
}
