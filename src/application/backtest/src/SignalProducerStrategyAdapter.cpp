#include "SignalProducers.h"

#include "../../../domain/strategy/include/IStrategyService.h"
#include "../../../domain/strategy/include/IStrategySignalEngine.h"
#include "../../../domain/backtest/include/BacktestRequest.h"

#include <cstdint>
#include <string>

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

    auto* mutableService = const_cast<domain::strategy::IStrategyService*>(&strategyService_);
    auto* signalEngine = dynamic_cast<domain::strategy::IStrategySignalEngine*>(mutableService);
    if (signalEngine != nullptr && context.spec.request) {
        const domain::backtest::BacktestRequest& request = *context.spec.request;
        const factor::compute::DateRange dateRange{
            factor::compute::DateKey{request.window.startDate},
            factor::compute::DateKey{request.window.endDate}};

        constexpr std::uint32_t kFNV1aOffsetBasis = 2166136261U;
        constexpr std::uint32_t kFNV1aPrime = 16777619U;
        std::vector<factor::compute::InstrumentId> universe;
        const auto& symbols = request.universeSpec.resolvedSymbols;
        universe.reserve(symbols.size());
        for (const auto& symbol : symbols) {
            const std::string symbolText = symbol.text();
            if (symbolText.empty()) {
                continue;
            }
            std::uint32_t hash = kFNV1aOffsetBasis;
            for (char ch : symbolText) {
                hash ^= static_cast<std::uint8_t>(ch);
                hash *= kFNV1aPrime;
            }
            factor::compute::InstrumentId id;
            id.value = hash;
            universe.push_back(id);
        }

        factor::compute::FactorResult<factor::compute::SignalSet> batchResult =
            signalEngine->evaluateBatch(dateRange, universe);

        if (batchResult.hasValue() && batchResult.value().isValid()) {
            const std::uint32_t sigCount = static_cast<std::uint32_t>(batchResult.value().signalIds.size());
            context.workingSet.signalBatch.factorSignalSet =
                std::make_shared<const factor::compute::SignalSet>(std::move(batchResult.value()));
            context.workingSet.signalBatch.strategySignalCount = sigCount;
            return stageResult;
        }
    }

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