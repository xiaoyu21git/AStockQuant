#include "../include/StrategyBacktestEngineGateway.h"

namespace application::backtest {

StrategyBacktestEngineGateway::StrategyBacktestEngineGateway(const IStrategyBacktestEngine& engine)
    : engine_(engine)
{
}

BacktestResultDto StrategyBacktestEngineGateway::execute(const BacktestRequest& request,
                                                         const BacktestExecutionCallbacks& callbacks) const
{
    return engine_.execute(request, callbacks);
}

} // namespace application::backtest