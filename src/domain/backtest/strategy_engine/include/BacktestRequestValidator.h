#pragma once

#include "StrategyBacktestEngineInterfaces.h"

namespace domain::backtest::strategy_engine {

class BacktestRequestValidator final {
public:
    [[nodiscard]] ValidationIssueList validate(const BacktestRequest& request) const;
    [[nodiscard]] bool accepts(const BacktestRequest& request) const;
};

} // namespace domain::backtest::strategy_engine