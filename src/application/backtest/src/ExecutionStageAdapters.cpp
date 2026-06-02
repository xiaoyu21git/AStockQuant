#include "ExecutionStageAdapters.h"

#include "../../../domain/backtest/include/BacktestRequest.h"
#include "../../../domain/trading/include/BacktestExecutionVenue.h"
#include "../../../domain/trading/include/TradingTypes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace application::backtest {

namespace {

using RiskOrderCandidate = astock::domain::trading::risk_approval::OrderCandidate;
using SignalSnapshot = astock::domain::trading::signal_orders::SignalSnapshot;

const FirstSliceSignalValueProjection kDefaultSignalProjection{};
const DefaultRiskLimitsPolicy kDefaultRiskLimitsPolicy{};
const DefaultTranslationSpecPolicy kDefaultTranslationSpecPolicy{};
const ConfigurableFillOrderPlanPolicy kDefaultFillOrderPlanPolicy{};

[[nodiscard]] QDate toQDate(int32_t yyyymmdd)
{
    const int year = yyyymmdd / 10000;
    const int month = (yyyymmdd / 100) % 100;
    const int day = yyyymmdd % 100;
    return QDate(year, month, day);
}

[[nodiscard]] domain::trading::TradingExecutionContext buildExecutionContext(
    const domain::backtest::BacktestRequest& request)
{
    domain::trading::TradingExecutionContext context;
    context.mode = domain::trading::TradingMode::Backtest;
    context.marketProfile = request.marketEnvironmentSpec.profile;
    context.window.startDate = toQDate(request.window.startDate);
    context.window.endDate = toQDate(request.window.endDate);

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
    if (!resolvedSymbols.isEmpty()) {
        const int index = static_cast<int>(orderIndex % static_cast<std::uint32_t>(resolvedSymbols.size()));
        const domain::strategy::SymbolCode& symbol = resolvedSymbols[index];
        if (symbol.isValid()) {
            return symbol;
        }
    }

    const auto& explicitSymbols = request.universeSpec.explicitSymbols;
    if (!explicitSymbols.isEmpty()) {
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
    if (venueResult.acceptedOrders.isEmpty() || venueResult.fills.isEmpty()) {
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
            domain::strategy::OrderId(QString::number(kDefaultOrderIdBase + static_cast<int32_t>(index)));
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

    return orderPlan.isValid() ? std::optional<domain::trading::OrderPlan>(std::move(orderPlan)) : std::nullopt;
}

std::optional<domain::trading::OrderSide> ConfigurableFillOrderPlanPolicy::resolveOrderSide(
    std::uint32_t orderIndex,
    FillOrderSideMode fillOrderSideMode,
    bool enableShortSelling) const
{
    if (fillOrderSideMode == FillOrderSideMode::LongOnlyBuy) {
        return domain::trading::OrderSide::Buy;
    }

    if (!enableShortSelling) {
        return std::nullopt;
    }

    const std::uint32_t bucket = orderIndex % kAlternationPeriod;
    if (bucket == kEvenBucket) {
        return domain::trading::OrderSide::Buy;
    }
    if (bucket == kOddBucket) {
        return domain::trading::OrderSide::SellShort;
    }
    return std::nullopt;
}

double FirstSliceSignalValueProjection::project(
    const factor::compute::SignalSet& signalSet,
    std::size_t instrumentIndex) const
{
    const std::size_t kFirstTimeIndex = 0U;
    const std::size_t kFirstFactorIndex = 0U;
    const std::size_t offset = static_cast<std::size_t>(signalSet.index.timeStride) * kFirstTimeIndex
        + static_cast<std::size_t>(signalSet.index.instrumentStride) * instrumentIndex
        + static_cast<std::size_t>(signalSet.index.factorStride) * kFirstFactorIndex;

    if (offset >= signalSet.values.size()) {
        return 0.0;
    }

    const double value = signalSet.values[offset];
    return std::isfinite(value) ? value : 0.0;
}

astock::domain::trading::risk_approval::RiskLimitsSpec DefaultRiskLimitsPolicy::build(
    const domain::backtest::BacktestRequest& request,
    std::size_t candidateCount) const
{
    astock::domain::trading::risk_approval::RiskLimitsSpec riskLimits;

    const auto toDeltaBps = [](double ratio) {
        astock::domain::trading::risk_approval::DeltaBps bps;
        const double scaled = std::round(ratio * kBasisPointScale);
        const int32_t raw = std::isfinite(scaled) ? static_cast<int32_t>(scaled) : 0;
        bps.value = std::clamp(
            raw,
            astock::domain::trading::risk_approval::DeltaBps::kMinValue,
            astock::domain::trading::risk_approval::DeltaBps::kMaxValue);
        return bps;
    };

    riskLimits.maxSingleOrderDelta = toDeltaBps(request.riskSpec.maxSinglePositionRatio.value);
    riskLimits.maxTurnoverDelta = toDeltaBps(request.riskSpec.maxPositionRatio.value);
    riskLimits.maxOrderCount = std::max(
        kMinimumRiskOrderCount,
        static_cast<int32_t>(candidateCount));
    return riskLimits;
}

astock::domain::trading::signal_orders::TranslationSpec DefaultTranslationSpecPolicy::build() const
{
    astock::domain::trading::signal_orders::TranslationSpec spec;
    spec.maxBuyDelta.value = kMaximumBuyDeltaBps;
    spec.maxSellDelta.value = kMaximumSellDeltaBps;
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

StageResult SignalDrivenRiskApprovalStageAdapter::approve(RunContext& context) const
{
    StageResult stageResult;
    stageResult.stage = RunStage::RiskApprove;
    stageResult.code = RunErrorCode::None;

    if (!context.workingSet.signalBatch.factorSignalSet
        || !context.workingSet.signalBatch.factorSignalSet->isValid()) {
        stageResult.code = RunErrorCode::StageExecutionFailed;
        return stageResult;
    }

    const factor::compute::SignalSet& signalSet = *context.workingSet.signalBatch.factorSignalSet;
    std::vector<RiskOrderCandidate> candidates;
    candidates.reserve(signalSet.instruments.size());

    for (std::size_t idx = 0U; idx < signalSet.instruments.size(); ++idx) {
        const factor::compute::InstrumentId instrument = signalSet.instruments[idx];
        const double value = signalProjection_.project(signalSet, idx);

        RiskOrderCandidate candidate;
        candidate.instrument.value = instrument.value;
        candidate.action = resolveOrderAction(value);
        candidate.delta.value = kDefaultOrderDeltaBps;
        if (candidate.isValid()) {
            candidates.push_back(candidate);
        }
    }

    if (candidates.empty()) {
        stageResult.code = RunErrorCode::StageExecutionFailed;
        return stageResult;
    }

    const astock::domain::trading::risk_approval::RiskLimitsSpec riskLimits =
        riskLimitsPolicy_.build(*context.spec.request, candidates.size());

    astock::domain::trading::risk_approval::RiskRuntimeContext runtimeContext;
    runtimeContext.consumedTurnover.value = 0;

    const auto approvalResult = riskApprovalEngine_.evaluate(riskLimits, runtimeContext, std::move(candidates));
    if (!approvalResult.ok()) {
        stageResult.code = RunErrorCode::StageExecutionFailed;
        return stageResult;
    }

    context.workingSet.approvedOrderCount = static_cast<std::uint32_t>(approvalResult.value->approved.size());
    return stageResult;
}

astock::domain::trading::risk_approval::OrderAction
SignalDrivenRiskApprovalStageAdapter::resolveOrderAction(double signalValue) noexcept
{
    if (signalValue < kSignalNonNegativeThreshold) {
        return astock::domain::trading::risk_approval::OrderAction::Sell;
    }
    return astock::domain::trading::risk_approval::OrderAction::Buy;
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

StageResult SignalDrivenOrderGenerationAdapter::generateOrders(RunContext& context) const
{
    StageResult stageResult;
    stageResult.stage = RunStage::GenerateOrders;
    stageResult.code = RunErrorCode::None;

    if (!context.workingSet.signalBatch.factorSignalSet
        || !context.workingSet.signalBatch.factorSignalSet->isValid()) {
        stageResult.code = RunErrorCode::StageExecutionFailed;
        return stageResult;
    }

    const factor::compute::SignalSet& signalSet = *context.workingSet.signalBatch.factorSignalSet;

    std::vector<SignalSnapshot> snapshots;
    snapshots.reserve(signalSet.instruments.size());
    for (std::size_t idx = 0U; idx < signalSet.instruments.size(); ++idx) {
        SignalSnapshot snapshot;
        snapshot.instrument.value = signalSet.instruments[idx].value;
        snapshot.signal = toSignalBps(signalProjection_.project(signalSet, idx));
        if (snapshot.isValid()) {
            snapshots.push_back(snapshot);
        }
    }

    if (snapshots.empty()) {
        stageResult.code = RunErrorCode::StageExecutionFailed;
        return stageResult;
    }

    const astock::domain::trading::signal_orders::TranslationSpec translationSpec =
        translationSpecPolicy_.build();

    const auto translationResult = signalOrderTranslator_.translate(translationSpec, std::move(snapshots));
    if (!translationResult.ok()) {
        stageResult.code = RunErrorCode::StageExecutionFailed;
        return stageResult;
    }

    context.workingSet.generatedOrderCount =
        static_cast<std::uint32_t>(translationResult.value->items.size());
    return stageResult;
}

astock::domain::trading::signal_orders::SignalBps
SignalDrivenOrderGenerationAdapter::toSignalBps(double signalValue) noexcept
{
    astock::domain::trading::signal_orders::SignalBps result;
    const double scaled = std::round(signalValue * static_cast<double>(kBasisPointScale));
    const int32_t raw = std::isfinite(scaled) ? static_cast<int32_t>(scaled) : 0;
    result.value = std::clamp(raw, kMinimumSignalBps, kMaximumSignalBps);
    return result;
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

} // namespace application::backtest