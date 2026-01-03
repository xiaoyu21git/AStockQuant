#pragma once
#include "Signal.h"
#include "Indicator.h"

namespace domain {

class CrossSignal : public Signal
{
public:
    CrossSignal(domain::Indicator& fast, domain::Indicator& slow);

    domain::signals::SignalType update(const domain::model::Bar& bar) override;

private:
    domain::Indicator& m_fast;
    domain::Indicator& m_slow;
    double m_prevDiff = 0.0;
    bool m_initialized = false;
};

}
