#pragma once

#include "BacktestEngineGateway.h"

namespace application::backtest {

using domain::backtest::strategy_engine::IStrategyBacktestEngine;

class StrategyBacktestEngineGateway final : public BacktestEngineGateway {
public:
    explicit StrategyBacktestEngineGateway(const IStrategyBacktestEngine& engine);

    [[nodiscard]] BacktestResultDto execute(const BacktestRequest& request,
                                            const BacktestExecutionCallbacks& callbacks) const override;

private:
    const IStrategyBacktestEngine& engine_;
};

} // namespace application::backtest