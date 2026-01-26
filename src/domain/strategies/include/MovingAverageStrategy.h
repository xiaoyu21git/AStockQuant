#pragma once
#include "Strategy.h"
#include "CrossSignal.h"
#include "StrategyAction.h"

namespace domain {

class MovingAverageStrategy : public Strategy {
public:
    MovingAverageStrategy(domain::CrossSignal& signal);

    StrategyAction onBar(const domain::model::Bar& bar) ;
    std::string MovingAverageStrategy::name() const override;

private:
    domain::CrossSignal& m_signal;
    bool m_hasPosition = false;
};

} // namespace domain
