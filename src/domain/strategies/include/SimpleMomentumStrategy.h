// domain/strategies/SimpleMomentumStrategy.h
#pragma once
#include "Strategy.h"

namespace domain {

class SimpleMomentumStrategy : public Strategy {
public:
    StrategyAction onBar(
        const model::Bar& bar,
        const model::Position& position
    ) override
    {
        if (!position.hasPosition && bar.close > bar.open) {
            return StrategyAction::OpenLong;
        }

        if (position.hasPosition && bar.close < bar.open) {
            return StrategyAction::CloseLong;
        }

        return StrategyAction::None;
    }
};

}
