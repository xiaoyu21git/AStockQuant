#pragma once

#include "../../strategy/include/StrategySnapshotTypes.h"
#include "foundation/Utils/Timestamp.h"

#include <cstdint>
#include <vector>

namespace domain::backtest {

struct DateWindow final {
    foundation::utils::Timestamp startDate;
    foundation::utils::Timestamp endDate;

    [[nodiscard]] bool isValid() const
    {
        return startDate <= endDate;
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

enum class BacktestLayerViolationCode {
    None,
    InvalidRequest,
    OverlayEnableFlagMismatch,
    RebalanceDaysMustComeFromStrategyDefinition,
    StrategyExecutionPolicyRebalanceDaysMismatch,
    ExecutionPositionSizingMethodMismatch,
    ExecutionShortSellingModeMismatch,
    OverlayTargetPositionCountMismatch,
    OverlayMinimumCompositeScoreMismatch,
    OverlaySelectedFactorsMismatch,
    OverlayAllocationsMismatch,
    RiskStopLossMustAlignRuleProfile,
    RiskMaxPositionMustAlignRuleProfile
};

struct BacktestLayerGuardResult final {
    std::vector<BacktestLayerViolationCode> violations;

    [[nodiscard]] bool ok() const
    {
        return violations.empty();
    }
};

class IBacktestLayerGuard {
public:
    virtual ~IBacktestLayerGuard() = default;

    virtual BacktestLayerGuardResult validate(const BacktestRequest& request) const = 0;
};

class StrictBacktestLayerGuard final : public IBacktestLayerGuard {
public:
    static constexpr double kMinimumCompositeScoreTolerance = 1e-9;
    static constexpr double kAllocationWeightTolerance = 1e-9;
    static constexpr double kRatioTolerance = 1e-9;

    BacktestLayerGuardResult validate(const BacktestRequest& request) const override;

private:
    [[nodiscard]] static bool nearEqual(double left, double right, double tolerance);
};

} // namespace domain::backtest