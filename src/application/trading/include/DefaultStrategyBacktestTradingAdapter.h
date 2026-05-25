#pragma once

#include "StrategyBacktestTradingAdapter.h"

namespace application::trading {

class DefaultStrategyBacktestTradingAdapter final : public StrategyBacktestTradingAdapter {
public:
    [[nodiscard]] domain::trading::TradeIntentBatch buildIntentBatch(
        const domain::backtest::BacktestRequest& request) const override;

    [[nodiscard]] domain::trading::TradingExecutionContext buildExecutionContext(
        const domain::backtest::BacktestRequest& request) const override;
};

} // namespace application::trading