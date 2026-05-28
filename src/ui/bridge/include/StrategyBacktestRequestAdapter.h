#pragma once

#include "StrategyStructureResolvers.h"

#include "../../../application/backtest/include/BacktestRequestFactory.h"

namespace application::backtest {
class StrategyBacktestEntryService;
}

namespace domain::backtest::strategy_engine {
struct BacktestResultDto;
}

namespace bridge::config {

enum class StrategyBacktestRequestAdapterErrorCode : std::uint8_t {
    None = 0,
    InvalidResolutionContext = 1,
    MissingDataSourceMode = 2,
    UnsupportedDataSourceMode = 3,
    MissingRuntimeThreads = 4,
    InvalidCacheTtl = 5,
    MissingCacheDatasetId = 6,
    MissingBenchmarkResolution = 7,
    MissingRunContextField = 8,
    InvalidRunContextField = 9,
    UnsupportedLayerType = 10,
    UnsupportedExecutionMode = 11,
    InvalidProgressSnapshot = 12,
    MissingRuntimeService = 13,
    InvalidBacktestHandle = 14,
    InvalidBacktestResult = 15,
    InvalidUniverseResolution = 16,
    InvalidFactorOverlayResolution = 17,
    InvalidBacktestRequest = 18,
};

struct StrategyBacktestRequestAdapterError final {
    StrategyBacktestRequestAdapterErrorCode code{StrategyBacktestRequestAdapterErrorCode::None};

    [[nodiscard]] bool isValid() const
    {
        return code != StrategyBacktestRequestAdapterErrorCode::None;
    }
};

struct StrategyBacktestRunContext final {
    application::backtest::StrategyId strategyId;
    application::backtest::OverlayBindingScopeId overlayBindingScopeId;
    application::backtest::UniverseId universeId;
    application::backtest::LayerId layerId;
    application::backtest::LayerType layerType{application::backtest::LayerType::Tactical};
    application::backtest::SymbolIdList resolvedExplicitSymbols;
    application::backtest::FactorIdList resolvedOverlayFactorIds;
    application::backtest::CandidateCount targetPositionCount;
    application::backtest::DateRange window;
    application::backtest::DatasetId universeDatasetId;
    application::backtest::DatasetId dataSourceDatasetId;
    application::backtest::ExecutionMode executionMode{application::backtest::ExecutionMode::EndOfDay};
    std::optional<application::backtest::SymbolId> benchmarkSymbol;

    [[nodiscard]] bool isValid() const
    {
        return strategyId.isValid()
            && overlayBindingScopeId.isValid()
            && universeId.isValid()
            && layerId.isValid()
            && targetPositionCount.isPositive()
            && window.isValid();
    }
};

[[nodiscard]] StrategyBacktestRunContext buildStrategyBacktestRunContext(
    const QVariantMap& runContext);

[[nodiscard]] domain::backtest::strategy_engine::BacktestRequest buildStrategyBacktestRequest(
    const QVariantMap& strategy,
    const StrategyBacktestRunContext& runContext,
    const QVariantMap& appliedRiskConfig = QVariantMap());

[[nodiscard]] domain::backtest::strategy_engine::AsyncBacktestHandle launchStrategyBacktest(
    const QVariantMap& strategy,
    const StrategyBacktestRunContext& runContext,
    application::backtest::StrategyBacktestEntryService* entryService,
    const QVariantMap& appliedRiskConfig = QVariantMap());

[[nodiscard]] QVariantMap buildStrategyBacktestHandleMap(
    const domain::backtest::strategy_engine::AsyncBacktestHandle& handle);

[[nodiscard]] QVariantMap buildStrategyBacktestResultMap(
    const domain::backtest::strategy_engine::BacktestResultDto& result);

[[nodiscard]] QVariantMap buildStrategyBacktestProgressMap(
    const domain::backtest::strategy_engine::BacktestProgressSnapshot& snapshot);

[[nodiscard]] QVariantMap tryCollectStrategyBacktestResult(
    qulonglong handleRunId,
    application::backtest::StrategyBacktestEntryService* entryService);

[[nodiscard]] QVariantMap pollStrategyBacktestProgress(
    qulonglong handleRunId,
    application::backtest::StrategyBacktestEntryService* entryService);

void cancelStrategyBacktest(
    qulonglong handleRunId,
    application::backtest::StrategyBacktestEntryService* entryService);

} // namespace bridge::config