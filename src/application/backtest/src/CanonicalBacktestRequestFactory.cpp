#include "../include/BacktestRequestFactory.h"

namespace application::backtest {

namespace {

using domain::backtest::strategy_engine::BacktestRequest;
using domain::backtest::strategy_engine::CandidateCount;
using domain::backtest::strategy_engine::DecisionLayer;
using domain::backtest::strategy_engine::ExecutionSpec;
using domain::backtest::strategy_engine::FactorOverlayConfig;
using domain::backtest::strategy_engine::FactorWeight;
using domain::backtest::strategy_engine::OrderType;
using domain::backtest::strategy_engine::PositionSizingMethod;
using domain::backtest::strategy_engine::Ratio;
using domain::backtest::strategy_engine::ScoreThreshold;
using domain::backtest::strategy_engine::StrategyBehaviorKind;
using domain::backtest::strategy_engine::UniverseSelectionMode;
using domain::backtest::strategy_engine::Weight;

[[noreturn]] void fail(const BacktestRequestFactoryErrorCode code)
{
    throw BacktestRequestFactoryError{code};
}

StrategyBehaviorKind mapBehaviorKind(const domain::backtest::StrategyBehaviorKind behaviorKind)
{
    switch (behaviorKind) {
    case domain::backtest::StrategyBehaviorKind::TrendFollowing:
        return StrategyBehaviorKind::TrendFollowing;
    case domain::backtest::StrategyBehaviorKind::MeanReversion:
        return StrategyBehaviorKind::MeanReversion;
    case domain::backtest::StrategyBehaviorKind::Momentum:
        return StrategyBehaviorKind::Momentum;
    case domain::backtest::StrategyBehaviorKind::Arbitrage:
        return StrategyBehaviorKind::Arbitrage;
    case domain::backtest::StrategyBehaviorKind::MultiFactor:
        return StrategyBehaviorKind::MultiFactor;
    case domain::backtest::StrategyBehaviorKind::MachineLearning:
        return StrategyBehaviorKind::MachineLearning;
    case domain::backtest::StrategyBehaviorKind::EventDriven:
        return StrategyBehaviorKind::EventDriven;
    case domain::backtest::StrategyBehaviorKind::HighFrequency:
        return StrategyBehaviorKind::HighFrequency;
    case domain::backtest::StrategyBehaviorKind::Custom:
        return StrategyBehaviorKind::Custom;
    }

    fail(BacktestRequestFactoryErrorCode::UnsupportedBehaviorKind);
}

PositionSizingMethod mapPositionSizingMethod(const domain::strategy::PositionSizingMethod method)
{
    switch (method) {
    case domain::strategy::PositionSizingMethod::FixedFraction:
        return PositionSizingMethod::FixedFraction;
    case domain::strategy::PositionSizingMethod::EqualWeight:
        return PositionSizingMethod::EqualWeight;
    case domain::strategy::PositionSizingMethod::SpreadNeutral:
        return PositionSizingMethod::SpreadNeutral;
    case domain::strategy::PositionSizingMethod::Discretionary:
        return PositionSizingMethod::Discretionary;
    }

    fail(BacktestRequestFactoryErrorCode::UnsupportedPositionSizingMethod);
}

UniverseSelectionMode mapUniverseMode(const domain::strategy::UniverseMode mode)
{
    switch (mode) {
    case domain::strategy::UniverseMode::ExplicitSymbols:
        return UniverseSelectionMode::ExplicitSymbols;
    case domain::strategy::UniverseMode::IndexConstituents:
        return UniverseSelectionMode::IndexConstituents;
    case domain::strategy::UniverseMode::SavedUniverse:
        return UniverseSelectionMode::SavedUniverse;
    case domain::strategy::UniverseMode::LinkedWatchlist:
        return UniverseSelectionMode::LinkedUniverse;
    }

    fail(BacktestRequestFactoryErrorCode::UnsupportedUniverseMode);
}

bool allowsShortSelling(const domain::strategy::ShortSellingMode mode)
{
    return mode == domain::strategy::ShortSellingMode::Enabled;
}

OrderType mapDefaultOrderType(const domain::strategy::DefaultOrderType orderType)
{
    switch (orderType) {
    case domain::strategy::DefaultOrderType::Market:
        return OrderType::Market;
    case domain::strategy::DefaultOrderType::MarketOnClose:
        return OrderType::MarketOnClose;
    }

    return OrderType::MarketOnClose;
}


FactorOverlayConfig buildOverlayConfig(const domain::strategy::FactorOverlaySpec& overlaySpec,
                                       const BacktestRunOverrides& overrides)
{
    FactorOverlayConfig overlayConfig;
    overlayConfig.enabled = overlaySpec.enabled;
    if (!overlaySpec.enabled) {
        return overlayConfig;
    }

    overlayConfig.minimumCompositeScore = ScoreThreshold(overlaySpec.minimumCompositeScore);
    overlayConfig.targetPositionCount = CandidateCount(static_cast<std::uint32_t>(overlaySpec.targetPositionCount));
    overlayConfig.factorIds = overrides.resolvedOverlayFactorIds;
    for (std::size_t index = 0; index < overlaySpec.allocations.size(); ++index) {
        overlayConfig.weights.add(FactorWeight{overrides.resolvedOverlayFactorIds.values().at(index),
                                               Weight(overlaySpec.allocations.at(static_cast<int>(index)).weightPercent / 100.0)});
    }

    return overlayConfig;
}

BacktestRequest buildRequest(const domain::strategy::StrategyAggregate& aggregate,
                             const BacktestRunOverrides& overrides)
{
    BacktestRequest request;
    request.overlayBindingScopeId = overrides.overlayBindingScopeId;
    request.identity.strategyId = overrides.strategyId;
    request.identity.behaviorKind = mapBehaviorKind(aggregate.identity.behaviorKind);
    request.identity.executionMode = overrides.executionMode;

    DecisionLayer layer;
    layer.id = overrides.layerId;
    layer.type = overrides.layerType;
    layer.inputUniverseId = overrides.universeId;
    layer.overlay = buildOverlayConfig(aggregate.spec.factorOverlay, overrides);
    layer.targetPositionCount = aggregate.spec.factorOverlay.enabled
        ? CandidateCount(static_cast<std::uint32_t>(aggregate.spec.factorOverlay.targetPositionCount))
        : overrides.targetPositionCount;
    layer.evaluationIntervalDays = CandidateCount(
        static_cast<std::uint32_t>(aggregate.spec.executionPolicy.rebalanceFrequencyDays.value));
    request.spec.layers.add(layer);

    request.universeSpec.mode = mapUniverseMode(aggregate.spec.strategyScopeContext.universe.universeMode);
    request.universeSpec.universeId = overrides.universeId;
    request.universeSpec.explicitSymbols = overrides.resolvedExplicitSymbols;
    request.universeSpec.datasetId = overrides.universeDatasetId;

    request.marketEnvironmentSpec = overrides.marketEnvironmentSpec;
    request.costSpec.initialCapital = domain::backtest::strategy_engine::CashAmount(
        aggregate.spec.backtestAssumptions.initialCapital.value);
    request.costSpec.commissionRate = Ratio(aggregate.spec.backtestAssumptions.commissionRate.value);
    request.costSpec.slippageRate = Ratio(aggregate.spec.backtestAssumptions.slippageRate.value);
    request.costSpec.taxRate = Ratio(aggregate.spec.backtestAssumptions.taxRate.value);

    request.riskSpec.maxPositionRatio = Ratio(aggregate.spec.ruleProfile.maxTotalExposureRatio.value);
    request.riskSpec.maxSinglePositionRatio = Ratio(aggregate.spec.ruleProfile.maxPositionRatio.value);
    request.riskSpec.maxDrawdownLimit = Ratio(aggregate.spec.ruleProfile.takeProfitRatio.value);
    request.riskSpec.stopLossRate = Ratio(aggregate.spec.ruleProfile.stopLossRatio.value);

    request.executionSpec = ExecutionSpec{overrides.executionMode,
                                          mapPositionSizingMethod(aggregate.spec.executionPolicy.positionSizingMethod),
                                          allowsShortSelling(aggregate.spec.executionPolicy.shortSellingMode),
                                          CandidateCount(static_cast<std::uint32_t>(aggregate.spec.executionPolicy.rebalanceFrequencyDays.value)),
                                          mapDefaultOrderType(aggregate.spec.executionPolicy.defaultOrderType)};

    request.dataSourceSpec = overrides.dataSourceSpec;
    request.runtimeOptions = overrides.runtimeOptions;
    request.window = overrides.window;
    request.benchmarkSymbol = overrides.benchmarkSymbol;
    return request;
}

} // namespace

BacktestRequest CanonicalBacktestRequestFactory::buildFromStrategy(
    const domain::strategy::StrategyAggregate& aggregate,
    const BacktestRunOverrides& overrides) const
{
    if (!aggregate.isValid()) {
        fail(BacktestRequestFactoryErrorCode::InvalidAggregate);
    }
    if (!overrides.isValid()) {
        fail(BacktestRequestFactoryErrorCode::InvalidOverrides);
    }

    const domain::strategy::UniverseSpec& universe = aggregate.spec.strategyScopeContext.universe;
    if (universe.universeMode == domain::strategy::UniverseMode::ExplicitSymbols) {
        if (overrides.resolvedExplicitSymbols.empty()) {
            fail(BacktestRequestFactoryErrorCode::InvalidUniverseResolution);
        }
    } else if (!overrides.universeDatasetId.isValid()) {
        fail(BacktestRequestFactoryErrorCode::InvalidUniverseResolution);
    }

    if (aggregate.identity.executionKind == domain::strategy::StrategyExecutionKind::FactorWeightedPortfolio
        || aggregate.spec.factorOverlay.enabled) {
        if (overrides.resolvedOverlayFactorIds.empty()
            || overrides.resolvedOverlayFactorIds.size() != static_cast<std::size_t>(aggregate.spec.factorOverlay.selectedFactors.size())
            || overrides.resolvedOverlayFactorIds.size() != static_cast<std::size_t>(aggregate.spec.factorOverlay.allocations.size())) {
            fail(BacktestRequestFactoryErrorCode::InvalidFactorOverlayResolution);
        }
    }

    const BacktestRequest request = buildRequest(aggregate, overrides);
    if (!request.isValid()) {
        fail(BacktestRequestFactoryErrorCode::InvalidRequest);
    }

    return request;
}

} // namespace application::backtest






