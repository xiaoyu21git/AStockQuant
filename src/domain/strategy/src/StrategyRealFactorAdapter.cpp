#include "../include/IStrategyService.h"

namespace domain::strategy {

CallbackRuntimeFactorServiceAdapter::CallbackRuntimeFactorServiceAdapter(Callbacks callbacks)
    : callbacks_(std::move(callbacks))
{
}

StrategyServiceFlowResult CallbackRuntimeFactorServiceAdapter::updateIncremental(
    const MarketDataPoint& marketDataPoint)
{
    if (!isValid()) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidState);
    }
    return callbacks_.updateIncremental(marketDataPoint);
}

StrategyServiceFlowResult CallbackRuntimeFactorServiceAdapter::updateBatch(
    const std::vector<MarketDataPoint>& batch)
{
    if (!isValid()) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidState);
    }
    return callbacks_.updateBatch(batch);
}

void CallbackRuntimeFactorServiceAdapter::copySnapshots(
    std::vector<RuntimeFactorSnapshot>& outputSnapshots) const
{
    if (!isValid()) {
        outputSnapshots.clear();
        return;
    }
    callbacks_.copySnapshots(outputSnapshots);
}

bool CallbackRuntimeFactorServiceAdapter::isValid() const
{
    return static_cast<bool>(callbacks_.updateIncremental)
        && static_cast<bool>(callbacks_.updateBatch)
        && static_cast<bool>(callbacks_.copySnapshots);
}

} // namespace domain::strategy
