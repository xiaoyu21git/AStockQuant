#include "ExecutionStageAdapters.h"

#include "../../../domain/backtest/include/BacktestRequest.h"
#include "../../../domain/trading/include/BacktestExecutionVenue.h"
#include "../../../domain/trading/include/TradingTypes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace application::backtest {

namespace {

using RiskOrderCandidate = astock::domain::trading::risk_approval::OrderCandidate;
using SignalSnapshot = astock::domain::trading::signal_orders::SignalSnapshot;

const FirstSliceSignalValueProjection kDefaultSignalProjection{};
const DefaultRiskLimitsPolicy kDefaultRiskLimitsPolicy{};
const DefaultTranslationSpecPolicy kDefaultTranslationSpecPolicy{};
const ConfigurableFillOrderPlanPolicy kDefaultFillOrderPlanPolicy{};

[[nodiscard]] domain::DomainDate toDomainDate(int32_t yyyymmdd)
{
    domain::DomainDate date;
    date.value = yyyymmdd;
    return date;
}

[[nodiscard]] domain::trading::TradingExecutionContext buildExecutionContext(
    const domain::backtest::BacktestRequest& request)
{
    domain::trading::TradingExecutionContext context;
    context.mode = domain::trading::TradingMode::Backtest;
    context.marketProfile = request.marketEnvironmentSpec.profile;
    context.window.startDate = toDomainDate(request.window.startDate);
    context.window.endDate = toDomainDate(request.window.endDate);

    context.costProfile.initialCapital = request.costSpec.initialCapital;
    context.costProfile.commissionRate = request.costSpec.commissionRate;
    context.costProfile.slippageRate = request.costSpec.slippageRate;
    context.costProfile.taxRate = request.costSpec.taxRate;

    context.riskProfile.maxPositionRatio = request.riskSpec.maxPositionRatio;
    context.riskProfile.maxSinglePositionRatio = request.riskSpec.maxSinglePositionRatio;
    context.riskProfile.maxDrawdownLimit = request.riskSpec.maxDrawdownLimit;
    context.riskProfile.stopLossRate = request.riskSpec.stopLossRate;

    context.executionProfile.executionKind = request.executionSpec.executionKind;
    context.executionProfile.positionSizingMethod = request.executionSpec.positionSizingMethod;
    context.executionProfile.priceModel = request.executionSpec.useMarketOnClose
        ? domain::trading::ExecutionPriceModel::MarketOnClose
        : domain::trading::ExecutionPriceModel::NextSessionOpen;
    context.executionProfile.shortSellingMode = request.executionSpec.enableShortSelling
        ? domain::strategy::ShortSellingMode::Enabled
        : domain::strategy::ShortSellingMode::Disabled;
    context.executionProfile.rebalanceFrequencyDays.value = request.executionSpec.rebalanceFrequencyDays;

    context.runtimeOptions.maxThreads = request.runtimeOptions.maxThreads;
    context.runtimeOptions.enableCache = request.runtimeOptions.enableCache;
    context.runtimeOptions.cacheTtlSeconds = request.runtimeOptions.cacheTtlSeconds;
    return context;
}

[[nodiscard]] std::optional<domain::strategy::SymbolCode> pickSymbol(
    const domain::backtest::BacktestRequest& request,
    std::uint32_t orderIndex)
{
    const auto& resolvedSymbols = request.universeSpec.resolvedSymbols;
    if (!resolvedSymbols.empty()) {
        const int index = static_cast<int>(orderIndex % static_cast<std::uint32_t>(resolvedSymbols.size()));
        const domain::strategy::SymbolCode& symbol = resolvedSymbols[index];
        if (symbol.isValid()) {
            return symbol;
        }
    }

    const auto& explicitSymbols = request.universeSpec.explicitSymbols;
    if (!explicitSymbols.empty()) {
        const int index = static_cast<int>(orderIndex % static_cast<std::uint32_t>(explicitSymbols.size()));
        const domain::strategy::SymbolCode& symbol = explicitSymbols[index];
        if (symbol.isValid()) {
            return symbol;
        }
    }

    return std::nullopt;
}

[[nodiscard]] StageResult executeFillWithBacktestVenue(
    RunContext& context,
    const IFillOrderPlanPolicy& fillOrderPlanPolicy)
{
    StageResult stageResult;
    stageResult.stage = RunStage::ExecuteFill;
    stageResult.code = RunErrorCode::None;

    if (!context.spec.request || context.workingSet.generatedOrderCount == 0U) {
        stageResult.code = RunErrorCode::StageExecutionFailed;
        return stageResult;
    }

    const domain::backtest::BacktestRequest& request = *context.spec.request;
    FillPlanBuildInput fillPlanInput{request,
                                     context.spec.fillOrderSideMode,
                                     context.workingSet.approvedOrderCount,
                                     context.workingSet.generatedOrderCount};
    const std::optional<domain::trading::OrderPlan> orderPlan = fillOrderPlanPolicy.build(fillPlanInput);
    if (!orderPlan.has_value()) {
        stageResult.code = RunErrorCode::StageExecutionFailed;
        return stageResult;
    }

    const domain::trading::TradingExecutionContext executionContext = buildExecutionContext(request);
    if (!executionContext.isValid()) {
        stageResult.code = RunErrorCode::StageExecutionFailed;
        return stageResult;
    }

    domain::trading::BacktestExecutionVenue executionVenue;
    const domain::trading::ExecutionVenueResult venueResult = executionVenue.submit(*orderPlan, executionContext);
    if (venueResult.acceptedOrders.empty() || venueResult.fills.empty()) {
        stageResult.code = RunErrorCode::StageExecutionFailed;
        return stageResult;
    }

    context.workingSet.filledOrderCount = static_cast<std::uint32_t>(venueResult.fills.size());
    return stageResult;
}

} // namespace

std::optional<domain::trading::OrderPlan> ConfigurableFillOrderPlanPolicy::build(
    const FillPlanBuildInput& input) const
{
    if (input.generatedOrderCount == 0U || input.approvedOrderCount == 0U) {
        return std::nullopt;
    }

    const std::uint32_t acceptedOrderCount = (std::min)(
        input.generatedOrderCount,
        input.approvedOrderCount);
    if (acceptedOrderCount == 0U) {
        return std::nullopt;
    }

    domain::trading::OrderPlan orderPlan;
    orderPlan.items.reserve(static_cast<int>(acceptedOrderCount));

    for (std::uint32_t index = 0U; index < acceptedOrderCount; ++index) {
        const std::optional<domain::strategy::SymbolCode> symbol = pickSymbol(input.request, index);
        if (!symbol.has_value()) {
            return std::nullopt;
        }

        const std::optional<domain::trading::OrderSide> side =
            resolveOrderSide(index, input.fillOrderSideMode, input.request.executionSpec.enableShortSelling);
        if (!side.has_value()) {
            return std::nullopt;
        }

        domain::trading::OrderPlanItem item;
        item.plannedOrderId =
            domain::strategy::OrderId(std::to_string(kDefaultOrderIdBase + static_cast<int32_t>(index)));
        item.symbol = *symbol;
        item.side = *side;
        item.orderType = input.request.executionSpec.useMarketOnClose
            ? domain::trading::OrderType::MarketOnClose
            : domain::trading::OrderType::NextSessionOpen;
        item.quantity.value = kMinimumQuantityValue;
        item.limitPrice.value = (std::max)(
            input.request.costSpec.initialCapital.value,
            kMinimumLimitPriceValue);

        if (!item.isValid()) {
            return std::nullopt;
        }
        orderPlan.items.push_back(item);
    }

    return orderPlan;
}

BacktestVenueFillEngineAdapter::BacktestVenueFillEngineAdapter()
    : fillOrderPlanPolicy_(kDefaultFillOrderPlanPolicy)
{
}

StageResult BacktestVenueFillEngineAdapter::executeFill(RunContext& context) const
{
    return executeFillWithBacktestVenue(context, fillOrderPlanPolicy_);
}

LiveVenueFillEngineAdapter::LiveVenueFillEngineAdapter()
    : fillOrderPlanPolicy_(kDefaultFillOrderPlanPolicy)
{
}

StageResult LiveVenueFillEngineAdapter::executeFill(RunContext& context) const
{
    return executeFillWithBacktestVenue(context, fillOrderPlanPolicy_);
}

double FirstSliceSignalValueProjection::project(
    const factor::compute::SignalSet& signalSet,
    std::size_t instrumentIndex) const
{
    if (!signalSet.values.empty() && instrumentIndex < signalSet.values.size()) {
        return signalSet.values[instrumentIndex];
    }
    return 0.0;
}

astock::domain::trading::risk_approval::RiskLimitsSpec DefaultRiskLimitsPolicy::build(
    const domain::backtest::BacktestRequest& request,
    std::size_t /*candidateCount*/) const
{
    astock::domain::trading::risk_approval::RiskLimitsSpec spec;
    spec.maxSingleOrderDelta = astock::domain::trading::risk_approval::DeltaBps{
        static_cast<std::int32_t>(request.riskSpec.maxSinglePositionRatio.value * kBasisPointScale)};
    spec.maxTurnoverDelta = astock::domain::trading::risk_approval::DeltaBps{
        static_cast<std::int32_t>(request.riskSpec.maxPositionRatio.value * kBasisPointScale)};
    spec.maxOrderCount = kMinimumRiskOrderCount;
    return spec;
}

astock::domain::trading::signal_orders::TranslationSpec DefaultTranslationSpecPolicy::build() const
{
    astock::domain::trading::signal_orders::TranslationSpec spec;
    spec.maxBuyDelta = astock::domain::trading::signal_orders::WeightDeltaBps{kMaximumBuyDeltaBps};
    spec.maxSellDelta = astock::domain::trading::signal_orders::WeightDeltaBps{kMaximumSellDeltaBps};
    return spec;
}

SignalDrivenRiskApprovalStageAdapter::SignalDrivenRiskApprovalStageAdapter(
    const astock::domain::trading::risk_approval::IRiskApprovalEngine& riskApprovalEngine)
    : riskApprovalEngine_(riskApprovalEngine)
    , signalProjection_(kDefaultSignalProjection)
    , riskLimitsPolicy_(kDefaultRiskLimitsPolicy)
{
}

SignalDrivenRiskApprovalStageAdapter::SignalDrivenRiskApprovalStageAdapter(
    const astock::domain::trading::risk_approval::IRiskApprovalEngine& riskApprovalEngine,
    const ISignalValueProjection& signalProjection,
    const IRiskLimitsPolicy& riskLimitsPolicy)
    : riskApprovalEngine_(riskApprovalEngine)
    , signalProjection_(signalProjection)
    , riskLimitsPolicy_(riskLimitsPolicy)
{
}

SignalDrivenOrderGenerationAdapter::SignalDrivenOrderGenerationAdapter(
    const astock::domain::trading::signal_orders::ISignalOrderTranslator& signalOrderTranslator)
    : signalOrderTranslator_(signalOrderTranslator)
    , signalProjection_(kDefaultSignalProjection)
    , translationSpecPolicy_(kDefaultTranslationSpecPolicy)
{
}

SignalDrivenOrderGenerationAdapter::SignalDrivenOrderGenerationAdapter(
    const astock::domain::trading::signal_orders::ISignalOrderTranslator& signalOrderTranslator,
    const ISignalValueProjection& signalProjection,
    const ITranslationSpecPolicy& translationSpecPolicy)
    : signalOrderTranslator_(signalOrderTranslator)
    , signalProjection_(signalProjection)
    , translationSpecPolicy_(translationSpecPolicy)
{
}

StageResult SignalDrivenRiskApprovalStageAdapter::approve(RunContext& context) const
{
    StageResult result;
    result.stage = RunStage::RiskApprove;
    result.code = RunErrorCode::None;
    context.workingSet.approvedOrderCount = context.workingSet.generatedOrderCount;
    return result;
}

StageResult SignalDrivenOrderGenerationAdapter::generateOrders(RunContext& context) const
{
    StageResult result;
    result.stage = RunStage::GenerateOrders;
    result.code = RunErrorCode::None;
    context.workingSet.generatedOrderCount = 1U;
    return result;
}

std::optional<domain::trading::OrderSide> ConfigurableFillOrderPlanPolicy::resolveOrderSide(
    std::uint32_t orderIndex,
    FillOrderSideMode fillOrderSideMode,
    bool enableShortSelling) const
{
    if (fillOrderSideMode != FillOrderSideMode::AlternatingLongShort || !enableShortSelling) {
        return domain::trading::OrderSide::Buy;
    }

    return (orderIndex % 2U == 0U)
        ? domain::trading::OrderSide::Buy
        : domain::trading::OrderSide::Sell;
}

} // namespace application::backtest
