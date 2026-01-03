#pragma once
#include "Strategy.h"
#include "../signals/CrossSignal.h"

namespace domain {

class MovingAverageStrategy : public Strategy
{
public:
    MovingAverageStrategy(CrossSignal& signal);

    StrategyAction onBar(const Bar& bar) override;

private:
    CrossSignal& m_signal;
};

}
