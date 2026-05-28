#pragma once

#include "../../../domain/strategy/include/StrategyAggregate.h"
#include "../../../domain/backtest/strategy_engine/include/StrategyBacktestEngineInterfaces.h"

#include <optional>

namespace application::backtest {

using domain::backtest::strategy_engine::BacktestRequest;
using domain::backtest::strategy_engine::CandidateCount;
using domain::backtest::strategy_engine::DataSourceSpec;
using domain::backtest::strategy_engine::DatasetId;
using domain::backtest::strategy_engine::DateRange;
using domain::backtest::strategy_engine::ExecutionMode;
using domain::backtest::strategy_engine::FactorIdList;
using domain::backtest::strategy_engine::LayerId;
using domain::backtest::strategy_engine::LayerType;
using domain::backtest::strategy_engine::MarketEnvironmentSpec;
using domain::backtest::strategy_engine::OverlayBindingScopeId;
using domain::backtest::strategy_engine::RuntimeOptions;
using domain::backtest::strategy_engine::StrategyId;
using domain::backtest::strategy_engine::SymbolId;
using domain::backtest::strategy_engine::SymbolIdList;
using domain::backtest::strategy_engine::UniverseId;

enum class BacktestRequestFactoryErrorCode : std::uint8_t {
    None = 0,
    InvalidAggregate = 1,
    InvalidOverrides = 2,
    InvalidUniverseResolution = 3,
    InvalidFactorOverlayResolution = 4,
    UnsupportedUniverseMode = 5,
    UnsupportedPositionSizingMethod = 6,
    UnsupportedBehaviorKind = 7,
    InvalidRequest = 8,
};

struct BacktestRequestFactoryError final {
    BacktestRequestFactoryErrorCode code{BacktestRequestFactoryErrorCode::None};

    [[nodiscard]] bool isValid() const
    {
        return code != BacktestRequestFactoryErrorCode::None;
    }
};

struct BacktestRunOverrides final {
    StrategyId strategyId;
    OverlayBindingScopeId overlayBindingScopeId;
    UniverseId universeId;
    LayerId layerId;
    LayerType layerType{LayerType::Tactical};
    SymbolIdList resolvedExplicitSymbols;
    FactorIdList resolvedOverlayFactorIds;
    CandidateCount targetPositionCount;
    DateRange window;
    MarketEnvironmentSpec marketEnvironmentSpec;
    DataSourceSpec dataSourceSpec;
    RuntimeOptions runtimeOptions;
    DatasetId universeDatasetId;
    ExecutionMode executionMode{ExecutionMode::EndOfDay};
    std::optional<SymbolId> benchmarkSymbol;

    [[nodiscard]] bool isValid() const
    {
        return strategyId.isValid()
            && overlayBindingScopeId.isValid()
            && universeId.isValid()
            && layerId.isValid()
            && targetPositionCount.isPositive()
            && window.isValid()
            && marketEnvironmentSpec.isValid()
            && dataSourceSpec.isValid()
            && runtimeOptions.isValid();
    }
};

class BacktestRequestFactory {
public:
    virtual ~BacktestRequestFactory() = default;

    [[nodiscard]] virtual BacktestRequest buildFromStrategy(
        const domain::strategy::StrategyAggregate& aggregate,
        const BacktestRunOverrides& overrides) const = 0;
};

class CanonicalBacktestRequestFactory final : public BacktestRequestFactory {
public:
    [[nodiscard]] BacktestRequest buildFromStrategy(
        const domain::strategy::StrategyAggregate& aggregate,
        const BacktestRunOverrides& overrides) const override;
};

} // namespace application::backtest
