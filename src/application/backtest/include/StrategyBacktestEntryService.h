#pragma once

#include "BacktestApplicationService.h"
#include "BacktestRequestFactory.h"

namespace application::backtest {

using domain::backtest::strategy_engine::DataSourceMode;
using domain::backtest::strategy_engine::DurationNs;
using domain::backtest::strategy_engine::OverlayBindingScopeId;

class IOverlayFactorBindingSink {
public:
    virtual ~IOverlayFactorBindingSink() = default;

    virtual void bindOverlayFactors(OverlayBindingScopeId overlayBindingScopeId,
                                    const QVector<domain::strategy::FactorId>& configuredFactorIds,
                                    const FactorIdList& resolvedFactorIds) = 0;
};

enum class StrategyBacktestEntryServiceErrorCode : std::uint8_t {
    None = 0,
    InvalidEntrySpec = 1,
    UnsupportedMarketEnvironment = 2,
    MissingRuntimeService = 3,
};

struct StrategyBacktestEntryServiceError final {
    StrategyBacktestEntryServiceErrorCode code{StrategyBacktestEntryServiceErrorCode::None};

    [[nodiscard]] bool isValid() const
    {
        return code != StrategyBacktestEntryServiceErrorCode::None;
    }
};

struct StrategyBacktestEntrySpec final {
    StrategyId strategyId;
    OverlayBindingScopeId overlayBindingScopeId;
    UniverseId universeId;
    LayerId layerId;
    LayerType layerType{LayerType::Tactical};
    SymbolIdList resolvedExplicitSymbols;
    FactorIdList resolvedOverlayFactorIds;
    CandidateCount targetPositionCount;
    DateRange window;
    DataSourceMode dataSourceMode{DataSourceMode::Raw};
    DatasetId universeDatasetId;
    DatasetId dataSourceDatasetId;
    CandidateCount maxThreads;
    bool enableCache{false};
    DurationNs cacheTtl;
    ExecutionMode executionMode{ExecutionMode::EndOfDay};
    std::optional<SymbolId> benchmarkSymbol;

    [[nodiscard]] bool isValid() const
    {
        if (!strategyId.isValid()
            || !overlayBindingScopeId.isValid()
            || !universeId.isValid()
            || !layerId.isValid()
            || !targetPositionCount.isPositive()
            || !window.isValid()) {
            return false;
        }

        const DataSourceSpec dataSourceSpec{dataSourceMode, dataSourceDatasetId};
        const RuntimeOptions runtimeOptions{maxThreads, enableCache, cacheTtl};
        return dataSourceSpec.isValid() && runtimeOptions.isValid();
    }
};

class StrategyBacktestEntryService final {
public:
    explicit StrategyBacktestEntryService(const BacktestRequestFactory& requestFactory,
                                          IOverlayFactorBindingSink* overlayFactorBindingSink = nullptr);
    StrategyBacktestEntryService(const BacktestRequestFactory& requestFactory,
                                 BacktestApplicationService& applicationService,
                                 IOverlayFactorBindingSink* overlayFactorBindingSink = nullptr);

    [[nodiscard]] bool hasRuntime() const;
    [[nodiscard]] BacktestRunOverrides buildOverrides(const domain::strategy::StrategyAggregate& aggregate,
                                                      const StrategyBacktestEntrySpec& entrySpec) const;
    [[nodiscard]] BacktestRequest buildRequest(const domain::strategy::StrategyAggregate& aggregate,
                                               const StrategyBacktestEntrySpec& entrySpec) const;
    [[nodiscard]] BacktestResultDto runInline(const domain::strategy::StrategyAggregate& aggregate,
                                              const StrategyBacktestEntrySpec& entrySpec) const;
    [[nodiscard]] AsyncBacktestHandle run(const domain::strategy::StrategyAggregate& aggregate,
                                          const StrategyBacktestEntrySpec& entrySpec);
    [[nodiscard]] BacktestProgressSnapshot progress(const AsyncBacktestHandle& handle) const;
    [[nodiscard]] std::optional<BacktestResultDto> tryCollect(const AsyncBacktestHandle& handle);
    void cancel(const CancellationRequest& request);

private:
    void bindOverlayFactors(const domain::strategy::StrategyAggregate& aggregate,
                            const StrategyBacktestEntrySpec& entrySpec) const;
    [[nodiscard]] BacktestApplicationService& runtimeService() const;

    const BacktestRequestFactory& requestFactory_;
    BacktestApplicationService* applicationService_{nullptr};
    IOverlayFactorBindingSink* overlayFactorBindingSink_{nullptr};
};

} // namespace application::backtest