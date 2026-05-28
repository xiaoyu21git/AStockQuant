#pragma once

#include "../../../domain/backtest/strategy_engine/include/StrategyBacktestEngineInterfaces.h"

namespace application::backtest {

using domain::backtest::strategy_engine::BacktestExecutionCallbacks;
using domain::backtest::strategy_engine::BacktestRequest;
using domain::backtest::strategy_engine::BacktestResultDto;

class BacktestEngineGateway {
public:
    virtual ~BacktestEngineGateway() = default;

    [[nodiscard]] BacktestResultDto execute(const BacktestRequest& request) const
    {
        return execute(request, BacktestExecutionCallbacks{});
    }

    [[nodiscard]] virtual BacktestResultDto execute(const BacktestRequest& request,
                                                    const BacktestExecutionCallbacks& callbacks) const = 0;
};

} // namespace application::backtest
