#pragma once

#include "../../strategy/include/StrategySnapshotTypes.h"
#include "foundation/Utils/Timestamp.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace domain::backtest {

/// 策略参数覆写 — 调优时仅覆写可变字段，不动 rule_profile / factor_overlay 等
/// 字段为 nullopt 表示不覆写（保持策略 DB 中的原始值）
struct StrategyParamOverlay final {
    std::optional<int> topN;
    std::optional<int> maxPositions;
    std::optional<double> maxWeightPerStock;
    std::optional<double> minWeightPerStock;
    std::optional<int> weightSchemeIndex;        // cast to WeightScheme
    std::optional<int> rebalanceFrequencyIndex;  // cast to RebalanceFrequency
    std::optional<bool> allowShort;
    std::optional<bool> industryNeutral;
    std::optional<double> stopLossPercent;
    std::optional<double> takeProfitPercent;
    std::optional<int> minHoldDays;

    // MultiFactor 专用
    std::optional<double> minCompositeScore;
    std::optional<double> sellThreshold;
    std::optional<double> sellRankMultiplier;

    // 技术指标策略专用
    std::optional<int> fastPeriod;
    std::optional<int> slowPeriod;
    std::optional<int> signalPeriod;
    std::optional<int> macdFast;
    std::optional<int> macdSlow;
    std::optional<int> macdSignal;
    std::optional<int> bbPeriod;
    std::optional<double> bbStdDev;

    // 枚举选项
    std::optional<int> priceFieldIndex;

    [[nodiscard]] bool isEmpty() const noexcept
    {
        return !topN && !maxPositions && !maxWeightPerStock && !minWeightPerStock
            && !weightSchemeIndex && !rebalanceFrequencyIndex && !allowShort
            && !industryNeutral && !stopLossPercent && !takeProfitPercent
            && !minHoldDays && !minCompositeScore && !sellThreshold
            && !sellRankMultiplier && !fastPeriod && !slowPeriod
            && !signalPeriod && !macdFast && !macdSlow && !macdSignal
            && !bbPeriod && !bbStdDev && !priceFieldIndex;
    }
};

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
    strategy::Ratio takeProfitRate;

    [[nodiscard]] bool isValid() const
    {
        return maxPositionRatio.isValid()
            && maxSinglePositionRatio.isValid()
            && maxDrawdownLimit.isValid()
            && stopLossRate.isValid()
            && takeProfitRate.isValid();
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
    std::string benchmarkIndex{"000300.SH"};  // 基准指数代码
    StrategyParamOverlay strategyParamOverlay; // 参数调优覆写 (空=不覆写)

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