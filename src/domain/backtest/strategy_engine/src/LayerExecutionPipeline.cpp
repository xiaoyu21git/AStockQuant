#include "LayerExecutionPipeline.h"

#include <cstdint>
#include <exception>

namespace domain::backtest::strategy_engine {

namespace {

constexpr std::uint64_t kLotSize = 100U;

CandidateScoreList filterCandidateScores(const CandidateScoreList& candidateScores,
                                         const SymbolIdList& selectedSymbols,
                                         const RuleDecisionList& blockedDecisions)
{
    CandidateScoreList filteredScores;

    if (candidateScores.empty()) {
        return filteredScores;
    }

    for (const CandidateScore& candidateScore : candidateScores) {
        bool symbolSelected = false;
        for (const SymbolId symbolId : selectedSymbols) {
            if (symbolId == candidateScore.symbolId) {
                symbolSelected = true;
                break;
            }
        }

        if (!symbolSelected) {
            continue;
        }

        bool blocked = false;
        for (const RuleDecision& decision : blockedDecisions) {
            if (decision.symbolId == candidateScore.symbolId) {
                blocked = true;
                break;
            }
        }

        if (!blocked) {
            filteredScores.add(candidateScore);
        }
    }

    return filteredScores;
}

PriceValue findClosePrice(const MarketDataSlice& marketData, const SymbolId symbolId)
{
    for (const MarketBar& marketBar : marketData.bars) {
        if (marketBar.symbolId == symbolId) {
            return marketBar.closePrice;
        }
    }

    throw std::exception();
}

ShareQuantity normalizeToLot(const ShareQuantity quantity)
{
    return ShareQuantity((quantity.value() / kLotSize) * kLotSize);
}

OrderId buildSyntheticOrderId(const StrategyContext& context, const SymbolId symbolId, const bool sellSide)
{
    const std::uint64_t tradingDayPart = static_cast<std::uint64_t>(context.tradingDay.value()) << 32U;
    const std::uint64_t layerPart = (context.activeLayerId.value() & 0xFFFFU) << 16U;
    const std::uint64_t symbolPart = static_cast<std::uint64_t>(symbolId.value()) << 1U;
    return OrderId(tradingDayPart ^ layerPart ^ symbolPart ^ static_cast<std::uint64_t>(sellSide ? 1U : 0U));
}

ExecutionOrder createForceExitOrder(const StrategyContext& context,
                                    const PositionSnapshot& positionSnapshot,
                                    const MarketDataSlice& marketData)
{
    return ExecutionOrder{buildSyntheticOrderId(context, positionSnapshot.symbolId, true),
                          positionSnapshot.symbolId,
                          OrderSide::Sell,
                          context.executionSpec.defaultOrderType,
                          normalizeToLot(positionSnapshot.quantity),
                          findClosePrice(marketData, positionSnapshot.symbolId),
                          context.tradingDay,
                          context.activeLayerId};
}

void appendForcedExitOrders(const StrategyContext& context,
                            const MarketDataSlice& marketData,
                            const IRuleChecker& ruleChecker,
                            const IExecutionSimulator& executionSimulator,
                            LayerExecutionState& layerState)
{
    for (const PositionSnapshot& positionSnapshot : context.portfolioState.positions) {
        const RuleCheckResult ruleCheckResult =
            ruleChecker.checkRules(context, positionSnapshot.symbolId, DecisionType::Hold);

        if (ruleCheckResult.decision.isValid()) {
            layerState.ruleDecisions.add(ruleCheckResult.decision);
        }

        if (!ruleCheckResult.forceExit) {
            continue;
        }

        const ExecutionOrder forceExitOrder = createForceExitOrder(context, positionSnapshot, marketData);
        if (!forceExitOrder.quantity.isPositive()) {
            continue;
        }

        const ExecutionFill executionFill = executionSimulator.execute(forceExitOrder, marketData);
        if (!executionFill.isValid()) {
            throw std::exception();
        }

        layerState.executionOrders.add(forceExitOrder);
        layerState.executionFills.add(executionFill);
    }
}

} // namespace

LayerExecutionPipeline::LayerExecutionPipeline(const ILayerSelectionStrategy& layerSelectionStrategy,
                                               const IRuleChecker& ruleChecker,
                                               const IPortfolioOptimizer& portfolioOptimizer,
                                               const IExecutionPolicyStrategy& executionPolicyStrategy,
                                               const IExecutionSimulator& executionSimulator)
    : layerSelectionStrategy_(layerSelectionStrategy)
    , ruleChecker_(ruleChecker)
    , portfolioOptimizer_(portfolioOptimizer)
    , executionPolicyStrategy_(executionPolicyStrategy)
    , executionSimulator_(executionSimulator)
{
}

LayerExecutionState LayerExecutionPipeline::execute(const DecisionLayer& decisionLayer,
                                                    const StrategyContext& context,
                                                    const MarketDataSlice& marketData) const
{
    const LayerSelectionResult selectionResult =
        layerSelectionStrategy_.select(decisionLayer, context, marketData);
    if (!selectionResult.isValid()) {
        throw std::exception();
    }

    LayerExecutionState layerState;
    layerState.layerId = decisionLayer.id;
    layerState.inputUniverseId = context.activeUniverseId;
    layerState.outputUniverseId = selectionResult.outputUniverseId;
    layerState.selectedSymbols = selectionResult.selectedSymbols;
    layerState.candidateScores = selectionResult.candidateScores;

    appendForcedExitOrders(context, marketData, ruleChecker_, executionSimulator_, layerState);

    SymbolIdList eligibleSymbols;
    eligibleSymbols.reserve(selectionResult.selectedSymbols.size());

    for (const SymbolId symbolId : selectionResult.selectedSymbols) {
        const RuleCheckResult ruleCheckResult =
            ruleChecker_.checkRules(context, symbolId, DecisionType::Entry);

        if (ruleCheckResult.decision.isValid()) {
            layerState.ruleDecisions.add(ruleCheckResult.decision);
        }

        if (!ruleCheckResult.blocked) {
            eligibleSymbols.add(symbolId);
        }
    }

    if (eligibleSymbols.empty()) {
        return layerState;
    }

    const CandidateScoreList eligibleScores =
        filterCandidateScores(selectionResult.candidateScores, eligibleSymbols, layerState.ruleDecisions);
    const TargetWeightList targetWeights =
        portfolioOptimizer_.computeWeights(eligibleSymbols,
                                           eligibleScores,
                                           context.portfolioState,
                                           context.riskSpec);
    layerState.targetWeights = targetWeights;

    const ExecutionPolicyDecision executionPolicyDecision =
        executionPolicyStrategy_.evaluate(context.executionSpec, context);
    if (!executionPolicyDecision.isValid() || !executionPolicyDecision.rebalanceRequired) {
        return layerState;
    }

    const ExecutionOrderList rawOrders = executionPolicyStrategy_.buildOrders(targetWeights,
                                                                              context.portfolioState,
                                                                              marketData,
                                                                              executionPolicyDecision);
    for (const ExecutionOrder& rawOrder : rawOrders) {
        ExecutionOrder normalizedOrder = rawOrder;
        normalizedOrder.quantity = normalizeToLot(rawOrder.quantity);
        if (!normalizedOrder.quantity.isPositive()) {
            continue;
        }

        layerState.executionOrders.add(normalizedOrder);

        const ExecutionFill fill = executionSimulator_.execute(normalizedOrder, marketData);
        if (!fill.isValid()) {
            throw std::exception();
        }

        layerState.executionFills.add(fill);
    }

    return layerState;
}

} // namespace domain::backtest::strategy_engine