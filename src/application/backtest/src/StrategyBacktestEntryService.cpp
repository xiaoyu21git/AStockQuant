#include "../include/StrategyBacktestEntryService.h"

namespace application::backtest {

namespace {

[[noreturn]] void fail(const StrategyBacktestEntryServiceErrorCode code)
{
    throw StrategyBacktestEntryServiceError{code};
}

MarketEnvironmentSpec mapMarketEnvironment(const factor::MarketEnvironmentProfile profile)
{
    switch (profile) {
    case factor::MarketEnvironmentProfile::CN_A_SHARE:
        return MarketEnvironmentSpec{domain::backtest::strategy_engine::MarketProfile::AshareEquity};
    case factor::MarketEnvironmentProfile::GENERIC_EQUITY:
        return MarketEnvironmentSpec{domain::backtest::strategy_engine::MarketProfile::GenericEquity};
    }

    fail(StrategyBacktestEntryServiceErrorCode::UnsupportedMarketEnvironment);
}

bool requiresOverlayBindings(const domain::strategy::StrategyAggregate& aggregate)
{
    return aggregate.identity.executionKind == domain::strategy::StrategyExecutionKind::FactorWeightedPortfolio
        || aggregate.spec.factorOverlay.enabled;
}

} // namespace

StrategyBacktestEntryService::StrategyBacktestEntryService(const BacktestRequestFactory& requestFactory,
                                                           IOverlayFactorBindingSink* overlayFactorBindingSink)
    : requestFactory_(requestFactory)
    , overlayFactorBindingSink_(overlayFactorBindingSink)
{
}

StrategyBacktestEntryService::StrategyBacktestEntryService(const BacktestRequestFactory& requestFactory,
                                                           BacktestApplicationService& applicationService,
                                                           IOverlayFactorBindingSink* overlayFactorBindingSink)
    : requestFactory_(requestFactory)
    , applicationService_(&applicationService)
    , overlayFactorBindingSink_(overlayFactorBindingSink)
{
}

bool StrategyBacktestEntryService::hasRuntime() const
{
    return applicationService_ != nullptr;
}

BacktestRunOverrides StrategyBacktestEntryService::buildOverrides(
    const domain::strategy::StrategyAggregate& aggregate,
    const StrategyBacktestEntrySpec& entrySpec) const
{
    if (!entrySpec.isValid()) {
        fail(StrategyBacktestEntryServiceErrorCode::InvalidEntrySpec);
    }

    BacktestRunOverrides overrides;
    overrides.strategyId = entrySpec.strategyId;
    overrides.overlayBindingScopeId = entrySpec.overlayBindingScopeId;
    overrides.universeId = entrySpec.universeId;
    overrides.layerId = entrySpec.layerId;
    overrides.layerType = entrySpec.layerType;
    overrides.resolvedExplicitSymbols = entrySpec.resolvedExplicitSymbols;
    overrides.resolvedOverlayFactorIds = entrySpec.resolvedOverlayFactorIds;
    overrides.targetPositionCount = entrySpec.targetPositionCount;
    overrides.window = entrySpec.window;
    overrides.marketEnvironmentSpec = mapMarketEnvironment(aggregate.spec.strategyScopeContext.marketEnvironmentProfile);
    overrides.dataSourceSpec = DataSourceSpec{entrySpec.dataSourceMode, entrySpec.dataSourceDatasetId};
    overrides.runtimeOptions = RuntimeOptions{entrySpec.maxThreads, entrySpec.enableCache, entrySpec.cacheTtl};
    overrides.universeDatasetId = entrySpec.universeDatasetId;
    overrides.executionMode = entrySpec.executionMode;
    overrides.benchmarkSymbol = entrySpec.benchmarkSymbol;

    if (!overrides.isValid()) {
        fail(StrategyBacktestEntryServiceErrorCode::InvalidEntrySpec);
    }

    return overrides;
}

BacktestRequest StrategyBacktestEntryService::buildRequest(
    const domain::strategy::StrategyAggregate& aggregate,
    const StrategyBacktestEntrySpec& entrySpec) const
{
    bindOverlayFactors(aggregate, entrySpec);
    return requestFactory_.buildFromStrategy(aggregate, buildOverrides(aggregate, entrySpec));
}

BacktestResultDto StrategyBacktestEntryService::runInline(
    const domain::strategy::StrategyAggregate& aggregate,
    const StrategyBacktestEntrySpec& entrySpec) const
{
    return runtimeService().runInline(buildRequest(aggregate, entrySpec));
}

AsyncBacktestHandle StrategyBacktestEntryService::run(const domain::strategy::StrategyAggregate& aggregate,
                                                      const StrategyBacktestEntrySpec& entrySpec)
{
    return runtimeService().run(buildRequest(aggregate, entrySpec));
}

BacktestProgressSnapshot StrategyBacktestEntryService::progress(const AsyncBacktestHandle& handle) const
{
    return runtimeService().progress(handle);
}

std::optional<BacktestResultDto> StrategyBacktestEntryService::tryCollect(const AsyncBacktestHandle& handle)
{
    return runtimeService().tryCollect(handle);
}

void StrategyBacktestEntryService::cancel(const CancellationRequest& request)
{
    runtimeService().cancel(request);
}

void StrategyBacktestEntryService::bindOverlayFactors(
    const domain::strategy::StrategyAggregate& aggregate,
    const StrategyBacktestEntrySpec& entrySpec) const
{
    if (!overlayFactorBindingSink_) {
        return;
    }

    if (!requiresOverlayBindings(aggregate)) {
        overlayFactorBindingSink_->bindOverlayFactors(entrySpec.overlayBindingScopeId, {}, {});
        return;
    }

    overlayFactorBindingSink_->bindOverlayFactors(entrySpec.overlayBindingScopeId,
                                                  aggregate.spec.factorOverlay.selectedFactors,
                                                  entrySpec.resolvedOverlayFactorIds);
}

BacktestApplicationService& StrategyBacktestEntryService::runtimeService() const
{
    if (!applicationService_) {
        fail(StrategyBacktestEntryServiceErrorCode::MissingRuntimeService);
    }

    return *applicationService_;
}

} // namespace application::backtest
