#pragma once

#include "../../../domain/backtest/include/BacktestRequest.h"
#include "../../../domain/trading/include/TradingTypes.h"

namespace application::trading {

class StrategyBacktestTradingAdapter {
public:
    virtual ~StrategyBacktestTradingAdapter() = default;

    [[nodiscard]] virtual domain::trading::TradeIntentBatch buildIntentBatch(
        const domain::backtest::BacktestRequest& request) const = 0;

    [[nodiscard]] virtual domain::trading::TradingExecutionContext buildExecutionContext(
        const domain::backtest::BacktestRequest& request) const = 0;
};

} // namespace application::trading