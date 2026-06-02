#pragma once

#include "BacktestInterfaces.hpp"

#include "../../../domain/factor/include/factor_compute/FactorSignalTypes.h"
#include "../../../domain/trading/include/execution/RiskApprovalEngine.h"
#include "../../../domain/trading/include/execution/SignalOrderTranslator.h"

#include <cstddef>
#include <cstdint>

namespace domain::backtest {
struct BacktestRequest;
}

namespace domain::trading {
enum class OrderSide : int;
struct OrderPlan;
}

namespace application::backtest {

class ISignalValueProjection {
public:
    virtual ~ISignalValueProjection() = default;

    [[nodiscard]] virtual double project(
        const factor::compute::SignalSet& signalSet,
        std::size_t instrumentIndex) const = 0;
};

class FirstSliceSignalValueProjection final : public ISignalValueProjection {
public:
    [[nodiscard]] double project(
        const factor::compute::SignalSet& signalSet,
        std::size_t instrumentIndex) const override;
};

class IRiskLimitsPolicy {
public:
    virtual ~IRiskLimitsPolicy() = default;

    [[nodiscard]] virtual astock::domain::trading::risk_approval::RiskLimitsSpec build(
        const domain::backtest::BacktestRequest& request,
        std::size_t candidateCount) const = 0;
};

class DefaultRiskLimitsPolicy final : public IRiskLimitsPolicy {
public:
    [[nodiscard]] astock::domain::trading::risk_approval::RiskLimitsSpec build(
        const domain::backtest::BacktestRequest& request,
        std::size_t candidateCount) const override;

private:
    static constexpr int32_t kMinimumRiskOrderCount = 1;
    static constexpr double kBasisPointScale = 10000.0;
};

class ITranslationSpecPolicy {
public:
    virtual ~ITranslationSpecPolicy() = default;

    [[nodiscard]] virtual astock::domain::trading::signal_orders::TranslationSpec build() const = 0;
};

class DefaultTranslationSpecPolicy final : public ITranslationSpecPolicy {
public:
    [[nodiscard]] astock::domain::trading::signal_orders::TranslationSpec build() const override;

private:
    static constexpr int32_t kMaximumBuyDeltaBps = 300;
    static constexpr int32_t kMaximumSellDeltaBps = 300;
};

struct FillPlanBuildInput final {
    const domain::backtest::BacktestRequest& request;
    FillOrderSideMode fillOrderSideMode{kDefaultFillOrderSideMode};
    std::uint32_t approvedOrderCount{0U};
    std::uint32_t generatedOrderCount{0U};
};

class IFillOrderPlanPolicy {
public:
    virtual ~IFillOrderPlanPolicy() = default;

    [[nodiscard]] virtual std::optional<domain::trading::OrderPlan> build(
        const FillPlanBuildInput& input) const = 0;
};

class ConfigurableFillOrderPlanPolicy final : public IFillOrderPlanPolicy {
public:
    [[nodiscard]] std::optional<domain::trading::OrderPlan> build(
        const FillPlanBuildInput& input) const override;

private:
    [[nodiscard]] std::optional<domain::trading::OrderSide> resolveOrderSide(
        std::uint32_t orderIndex,
        FillOrderSideMode fillOrderSideMode,
        bool enableShortSelling) const;

private:
    static constexpr std::uint32_t kAlternationPeriod = 2U;
    static constexpr std::uint32_t kEvenBucket = 0U;
    static constexpr std::uint32_t kOddBucket = 1U;
    static constexpr int32_t kMinimumQuantityValue = 1;
    static constexpr double kMinimumLimitPriceValue = 1.0;
    static constexpr int32_t kDefaultOrderIdBase = 1;
};

class SignalDrivenRiskApprovalStageAdapter final : public IRiskApprovalStageEngine {
public:
    explicit SignalDrivenRiskApprovalStageAdapter(
        const astock::domain::trading::risk_approval::IRiskApprovalEngine& riskApprovalEngine);

    SignalDrivenRiskApprovalStageAdapter(
        const astock::domain::trading::risk_approval::IRiskApprovalEngine& riskApprovalEngine,
        const ISignalValueProjection& signalProjection,
        const IRiskLimitsPolicy& riskLimitsPolicy);

    [[nodiscard]] StageResult approve(RunContext& context) const override;

private:
    [[nodiscard]] static astock::domain::trading::risk_approval::OrderAction
    resolveOrderAction(double signalValue) noexcept;

private:
    static constexpr int32_t kDefaultOrderDeltaBps = 100;
    static constexpr double kSignalNonNegativeThreshold = 0.0;

    const astock::domain::trading::risk_approval::IRiskApprovalEngine& riskApprovalEngine_;
    const ISignalValueProjection& signalProjection_;
    const IRiskLimitsPolicy& riskLimitsPolicy_;
};

class SignalDrivenOrderGenerationAdapter final : public IOrderGenerationEngine {
public:
    explicit SignalDrivenOrderGenerationAdapter(
        const astock::domain::trading::signal_orders::ISignalOrderTranslator& signalOrderTranslator);

    SignalDrivenOrderGenerationAdapter(
        const astock::domain::trading::signal_orders::ISignalOrderTranslator& signalOrderTranslator,
        const ISignalValueProjection& signalProjection,
        const ITranslationSpecPolicy& translationSpecPolicy);

    [[nodiscard]] StageResult generateOrders(RunContext& context) const override;

private:
    [[nodiscard]] static astock::domain::trading::signal_orders::SignalBps
    toSignalBps(double signalValue) noexcept;

private:
    static constexpr int32_t kBasisPointScale = 10000;
    static constexpr int32_t kMinimumSignalBps = -10000;
    static constexpr int32_t kMaximumSignalBps = 10000;
    const astock::domain::trading::signal_orders::ISignalOrderTranslator& signalOrderTranslator_;
    const ISignalValueProjection& signalProjection_;
    const ITranslationSpecPolicy& translationSpecPolicy_;
};

class BacktestVenueFillEngineAdapter final : public IFillEngine {
public:
    BacktestVenueFillEngineAdapter();

    [[nodiscard]] StageResult executeFill(RunContext& context) const override;

private:
    const IFillOrderPlanPolicy& fillOrderPlanPolicy_;
};

class LiveVenueFillEngineAdapter final : public IFillEngine {
public:
    LiveVenueFillEngineAdapter();

    [[nodiscard]] StageResult executeFill(RunContext& context) const override;

private:
    const IFillOrderPlanPolicy& fillOrderPlanPolicy_;
};

} // namespace application::backtest