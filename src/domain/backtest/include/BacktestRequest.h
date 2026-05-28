#pragma once

#include "../../strategy/include/StrategySnapshotTypes.h"

namespace domain::backtest {

struct DateWindow final {
    QDate startDate;
    QDate endDate;

    [[nodiscard]] bool isValid() const
    {
        return startDate.isValid() && endDate.isValid() && startDate <= endDate;
    }
};

struct MarketEnvironmentSpec final {
    factor::MarketEnvironmentProfile profile{factor::MarketEnvironmentProfile::GENERIC_EQUITY};

    [[nodiscard]] bool isValid() const
    {
        return true;
    }
};

struct CostSpec final {
    strategy::Money initialCapital;
    strategy::Ratio commissionRate;
    strategy::Ratio slippageRate;
    strategy::Ratio taxRate;

    [[nodiscard]] bool isValid() const
    {
        return initialCapital.isPositive()
            && commissionRate.isValid()
            && slippageRate.isValid()
            && taxRate.isValid();
    }
};

struct RiskSpec final {
    strategy::Ratio maxPositionRatio;
    strategy::Ratio maxSinglePositionRatio;
    strategy::Ratio maxDrawdownLimit;
    strategy::Ratio stopLossRate;

    [[nodiscard]] bool isValid() const
    {
        return maxPositionRatio.isValid()
            && maxSinglePositionRatio.isValid()
            && maxDrawdownLimit.isValid()
            && stopLossRate.isValid();
    }
};

struct ExecutionSpec final {
    strategy::StrategyExecutionKind executionKind{strategy::StrategyExecutionKind::Standard};
    strategy::PositionSizingMethod positionSizingMethod{strategy::PositionSizingMethod::FixedFraction};
    bool enableShortSelling{false};
    int rebalanceFrequencyDays{1};
    bool useMarketOnClose{true};

    [[nodiscard]] bool isValid() const
    {
        return rebalanceFrequencyDays > 0;
    }
};

struct DataSourceSpec final {
    strategy::DataSourceMode mode{strategy::DataSourceMode::Raw};
    strategy::DatasetId datasetId;

    [[nodiscard]] bool isValid() const
    {
        return mode != strategy::DataSourceMode::CacheDataset || datasetId.isValid();
    }
};

struct RuntimeOptionSpec final {
    int maxThreads{1};
    bool enableCache{false};
    int cacheTtlSeconds{0};

    [[nodiscard]] bool isValid() const
    {
        return maxThreads > 0 && cacheTtlSeconds >= 0;
    }
};

struct BacktestRequest final {
    strategy::StrategyIdentity strategyIdentity;
    strategy::StrategySpec strategySpec;
    strategy::UniverseSpec universeSpec;
    MarketEnvironmentSpec marketEnvironmentSpec;
    CostSpec costSpec;
    RiskSpec riskSpec;
    ExecutionSpec executionSpec;
    strategy::FactorOverlaySpec factorOverlaySpec;
    DataSourceSpec dataSourceSpec;
    RuntimeOptionSpec runtimeOptions;
    DateWindow window;

    [[nodiscard]] bool isValid() const
    {
        return strategyIdentity.isValid()
            && strategySpec.isValid()
            && universeSpec.isValid()
            && marketEnvironmentSpec.isValid()
            && costSpec.isValid()
            && riskSpec.isValid()
            && executionSpec.isValid()
            && factorOverlaySpec.isValid()
            && dataSourceSpec.isValid()
            && runtimeOptions.isValid()
            && window.isValid();
    }
};

} // namespace domain::backtest