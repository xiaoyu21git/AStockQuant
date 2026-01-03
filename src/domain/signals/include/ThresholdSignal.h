#pragma once
#include "Signal.h"
#include "Indicator.h"

namespace domain {

class ThresholdSignal : public Signal
{
public:
    ThresholdSignal(Indicator& indicator,
                    double buyLevel,
                    double sellLevel);

    domain::signals::SignalType update(const domain::model::Bar& bar) override;

private:
    Indicator& m_indicator;
    double m_buyLevel;
    double m_sellLevel;
};

}
