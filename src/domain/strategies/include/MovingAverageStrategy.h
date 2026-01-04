#pragma once
#include "Strategy.h"
#include "CrossSignal.h"

namespace domain {

class MovingAverageStrategy : public Strategy
{
public:
    MovingAverageStrategy(domain::CrossSignal& signal);

    StrategyAction onBar(const domain::model::Bar& bar) override;

//private:
    domain::CrossSignal& m_signal;
    // ⭐ 策略内部状态
    bool m_hasPosition = false;
};

}
