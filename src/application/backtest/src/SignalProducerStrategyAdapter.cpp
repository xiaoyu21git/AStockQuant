#include "SignalProducers.h"

#include "../../../domain/strategy/include/IStrategyService.h"

#include <vector>

namespace application::backtest {

StrategySignalProducerAdapter::StrategySignalProducerAdapter(
    const domain::strategy::IStrategyService& strategyService)
    : strategyService_(strategyService)
{
}

StageResult StrategySignalProducerAdapter::generateSignal(RunContext& context) const
{
    StageResult stageResult;
    stageResult.stage = RunStage::GenerateSignal;
    stageResult.code = RunErrorCode::None;

    std::vector<domain::strategy::OrderRequest> pendingOrders;
    strategyService_.copyPendingOrders(pendingOrders);

    if (pendingOrders.empty()) {
        stageResult.code = RunErrorCode::StageExecutionFailed;
        context.workingSet.signalBatch.factorSignalSet.reset();
        context.workingSet.signalBatch.strategySignalCount = 0U;
        return stageResult;
    }

    context.workingSet.signalBatch.factorSignalSet.reset();
    context.workingSet.signalBatch.strategySignalCount =
        static_cast<std::uint32_t>(pendingOrders.size());
    return stageResult;
}

} // namespace application::backtest
