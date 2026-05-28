#include "StrategyBacktestEngine.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <exception>
#include <limits>
#include <vector>

namespace domain::backtest::strategy_engine {

namespace {

using SystemClock = std::chrono::system_clock;
using SteadyClock = std::chrono::steady_clock;
constexpr double kTradingDaysPerYear = 252.0;

struct OpenTradeState final {
    SymbolId symbolId;
    std::uint64_t quantity{0};
    double averageCost{0.0};
};

struct LayerTradeOutcome final {
    LayerId layerId;
    double realizedPnl{0.0};
    std::size_t closedTradeCount{0U};
    std::size_t winCount{0U};
};

void recordFailureDiagnostic(DiagnosticsRecorder& diagnosticsRecorder,
                            const EngineFailureCode code,
                            const TradingDayIndex tradingDay = TradingDayIndex(),
                            const LayerId layerId = LayerId(),
                            const SymbolId symbolId = SymbolId())
{
    const std::optional<DiagnosticRecord> diagnosticRecord = engineFailureDiagnosticRecord(code,
                                                                                           tradingDay,
                                                                                           layerId,
                                                                                           symbolId);
    if (!diagnosticRecord.has_value()) {
        return;
    }

    diagnosticsRecorder.recordDiagnostic(diagnosticRecord.value());
}

[[noreturn]] void failEngine(EngineFailureCode code,
                             Diagnostics diagnostics = Diagnostics())
{
    throw EngineFailure(code, std::move(diagnostics));
}

OpenTradeState& findOrCreateOpenTradeState(std::vector<OpenTradeState>& openTradeStates,
                                           const SymbolId symbolId)
{
    const auto it = std::find_if(openTradeStates.begin(),
                                 openTradeStates.end(),
                                 [symbolId](const OpenTradeState& openTradeState) {
                                     return openTradeState.symbolId == symbolId;
                                 });
    if (it != openTradeStates.end()) {
        return *it;
    }

    openTradeStates.push_back(OpenTradeState{symbolId, 0U, 0.0});
    return openTradeStates.back();
}

LayerTradeOutcome& findOrCreateLayerTradeOutcome(std::vector<LayerTradeOutcome>& layerTradeOutcomes,
                                                 const LayerId layerId)
{
    const auto it = std::find_if(layerTradeOutcomes.begin(),
                                 layerTradeOutcomes.end(),
                                 [layerId](const LayerTradeOutcome& layerTradeOutcome) {
                                     return layerTradeOutcome.layerId == layerId;
                                 });
    if (it != layerTradeOutcomes.end()) {
        return *it;
    }

    layerTradeOutcomes.push_back(LayerTradeOutcome{layerId, 0.0, 0U, 0U});
    return layerTradeOutcomes.back();
}

void accumulateOpenBuyFill(OpenTradeState& openTradeState,
                           const ExecutionFill& executionFill)
{
    const double executionValue = executionFill.fillPrice.value();
    const std::uint64_t quantity = executionFill.filledQuantity.value();
    const double aggregateCost = openTradeState.averageCost * static_cast<double>(openTradeState.quantity)
        + executionValue * static_cast<double>(quantity);
    openTradeState.quantity += quantity;
    openTradeState.averageCost = openTradeState.quantity == 0U
        ? 0.0
        : aggregateCost / static_cast<double>(openTradeState.quantity);
}

double realizeSellFill(OpenTradeState& openTradeState,
                       const ExecutionFill& executionFill)
{
    const double executionValue = executionFill.fillPrice.value();
    const std::uint64_t quantity = executionFill.filledQuantity.value();
    if (openTradeState.quantity < quantity) {
        failEngine(EngineFailureCode::InvalidOpenTradeState);
    }

    const double realizedPnl = (executionValue - openTradeState.averageCost)
        * static_cast<double>(quantity);
    openTradeState.quantity -= quantity;
    if (openTradeState.quantity == 0U) {
        openTradeState.averageCost = 0.0;
    }

    return realizedPnl;
}

TimestampNs currentTimestampNs()
{
    const auto now = std::chrono::time_point_cast<std::chrono::nanoseconds>(SystemClock::now());
    return TimestampNs(now.time_since_epoch().count());
}

DurationNs toDurationNs(const SteadyClock::duration duration)
{
    return DurationNs(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
}

CandidateCount toCandidateCount(const std::size_t value)
{
    const auto boundedValue = std::min<std::size_t>(value, std::numeric_limits<std::uint32_t>::max());
    return CandidateCount(static_cast<std::uint32_t>(boundedValue));
}

CandidateCount totalTradingDays(const BacktestRequest& request)
{
    const auto tradingDayCount = static_cast<std::size_t>(request.window.endDay.value() - request.window.startDay.value() + 1);
    return toCandidateCount(tradingDayCount);
}

SymbolIdList singleSymbolList(const SymbolId symbolId)
{
    SymbolIdList symbols;
    symbols.add(symbolId);
    return symbols;
}

RunId createRunId(const TimestampNs timestamp)
{
    const auto value = timestamp.value() > 0 ? static_cast<std::uint64_t>(timestamp.value()) : 1ULL;
    return RunId(value);
}

double computeTotalReturn(const CashAmount startingEquity, const CashAmount endingEquity)
{
    if (!startingEquity.isPositive()) {
        return 0.0;
    }

    return (endingEquity.value() - startingEquity.value()) / startingEquity.value();
}

double findClosePrice(const MarketDataSlice& marketData, const SymbolId symbolId)
{
    for (const MarketBar& marketBar : marketData.bars) {
        if (marketBar.symbolId == symbolId) {
            return marketBar.closePrice.value();
        }
    }

    failEngine(EngineFailureCode::MissingClosePrice);
}

const ExecutionOrder& findOrder(const ExecutionOrderList& executionOrders, const OrderId orderId)
{
    for (const ExecutionOrder& executionOrder : executionOrders) {
        if (executionOrder.orderId == orderId) {
            return executionOrder;
        }
    }

    failEngine(EngineFailureCode::MissingExecutionOrder);
}

Weight resolveTargetWeight(const TargetWeightList& targetWeights, const SymbolId symbolId, const Weight fallbackWeight)
{
    for (const TargetWeight& targetWeight : targetWeights) {
        if (targetWeight.symbolId == symbolId) {
            return targetWeight.weight;
        }
    }

    return fallbackWeight;
}

PortfolioState applyLayerExecution(const PortfolioState& currentPortfolioState,
                                   const LayerExecutionState& layerState,
                                   const MarketDataSlice& marketData)
{
    PortfolioState updatedPortfolioState = currentPortfolioState;
    auto& positions = updatedPortfolioState.positions.values();

    for (const ExecutionFill& executionFill : layerState.executionFills) {
        const ExecutionOrder& executionOrder = findOrder(layerState.executionOrders, executionFill.orderId);
        const double grossAmount = executionFill.fillPrice.value()
            * static_cast<double>(executionFill.filledQuantity.value());
        const double feeAmount = executionFill.fee.value();

        auto positionIterator = std::find_if(positions.begin(),
                                             positions.end(),
                                             [executionOrder](const PositionSnapshot& positionSnapshot) {
                                                 return positionSnapshot.symbolId == executionOrder.symbolId;
                                             });

        if (executionOrder.side == OrderSide::Buy) {
            const double cashRequired = grossAmount + feeAmount;
            if (updatedPortfolioState.availableCash.value() < cashRequired) {
                failEngine(EngineFailureCode::InsufficientCash);
            }

            updatedPortfolioState.availableCash =
                CashAmount(updatedPortfolioState.availableCash.value() - cashRequired);

            if (positionIterator == positions.end()) {
                PositionSnapshot positionSnapshot;
                positionSnapshot.symbolId = executionOrder.symbolId;
                positionSnapshot.quantity = executionFill.filledQuantity;
                positionSnapshot.marketValue = CashAmount(grossAmount);
                positionSnapshot.targetWeight = resolveTargetWeight(layerState.targetWeights,
                                                                   executionOrder.symbolId,
                                                                   Weight(0.0));
                positions.push_back(positionSnapshot);
            } else {
                positionIterator->quantity = ShareQuantity(positionIterator->quantity.value()
                    + executionFill.filledQuantity.value());
                positionIterator->targetWeight = resolveTargetWeight(layerState.targetWeights,
                                                                     executionOrder.symbolId,
                                                                     positionIterator->targetWeight);
            }
        } else {
            if (positionIterator == positions.end()
                || positionIterator->quantity.value() < executionFill.filledQuantity.value()) {
                failEngine(EngineFailureCode::MissingPositionForSell);
            }

            updatedPortfolioState.availableCash =
                CashAmount(updatedPortfolioState.availableCash.value() + grossAmount - feeAmount);

            const std::uint64_t remainingQuantity =
                positionIterator->quantity.value() - executionFill.filledQuantity.value();
            if (remainingQuantity == 0U) {
                positions.erase(positionIterator);
            } else {
                positionIterator->quantity = ShareQuantity(remainingQuantity);
                positionIterator->targetWeight = resolveTargetWeight(layerState.targetWeights,
                                                                     executionOrder.symbolId,
                                                                     positionIterator->targetWeight);
            }
        }
    }

    double positionsMarketValue = 0.0;
    for (PositionSnapshot& positionSnapshot : positions) {
        const double closePrice = findClosePrice(marketData, positionSnapshot.symbolId);
        const double marketValue = closePrice * static_cast<double>(positionSnapshot.quantity.value());
        positionSnapshot.marketValue = CashAmount(marketValue);
        positionSnapshot.targetWeight = resolveTargetWeight(layerState.targetWeights,
                                                            positionSnapshot.symbolId,
                                                            positionSnapshot.targetWeight);
        positionsMarketValue += marketValue;
    }

    updatedPortfolioState.totalEquity =
        CashAmount(updatedPortfolioState.availableCash.value() + positionsMarketValue);
    return updatedPortfolioState;
}

ReturnValue computePeriodReturn(const double previousEquity, const double currentEquity)
{
    if (previousEquity <= 0.0) {
        return ReturnValue(0.0);
    }

    return ReturnValue((currentEquity - previousEquity) / previousEquity);
}

double computeMaxDrawdownValue(const EquityCurve& equityCurve)
{
    double peakEquity = 0.0;
    double maxDrawdown = 0.0;
    for (const EquityCurvePoint& equityCurvePoint : equityCurve) {
        peakEquity = std::max(peakEquity, equityCurvePoint.equity.value());
        if (peakEquity <= 0.0) {
            continue;
        }

        const double drawdown = (peakEquity - equityCurvePoint.equity.value()) / peakEquity;
        maxDrawdown = std::max(maxDrawdown, drawdown);
    }

    return std::clamp(maxDrawdown, 0.0, 1.0);
}

double computeAnnualizedReturnValue(const CashAmount startingEquity,
                                    const CashAmount endingEquity,
                                    const std::size_t periodCount)
{
    if (!startingEquity.isPositive()) {
        return 0.0;
    }

    if (periodCount == 0U) {
        return computeTotalReturn(startingEquity, endingEquity);
    }

    const double equityRatio = endingEquity.value() / startingEquity.value();
    if (equityRatio <= 0.0) {
        return -1.0;
    }

    return std::pow(equityRatio, kTradingDaysPerYear / static_cast<double>(periodCount)) - 1.0;
}

double computeVolatilityValue(const EquityCurve& equityCurve)
{
    if (equityCurve.size() <= 1U) {
        return 0.0;
    }

    double returnSum = 0.0;
    std::size_t observationCount = 0U;
    for (const EquityCurvePoint& equityCurvePoint : equityCurve) {
        returnSum += equityCurvePoint.periodReturn.value();
        ++observationCount;
    }

    if (observationCount <= 1U) {
        return 0.0;
    }

    const double averageReturn = returnSum / static_cast<double>(observationCount);
    double squaredDeviationSum = 0.0;
    for (const EquityCurvePoint& equityCurvePoint : equityCurve) {
        const double deviation = equityCurvePoint.periodReturn.value() - averageReturn;
        squaredDeviationSum += deviation * deviation;
    }

    const double variance = squaredDeviationSum / static_cast<double>(observationCount - 1U);
    return std::sqrt(std::max(variance, 0.0)) * std::sqrt(kTradingDaysPerYear);
}

SymbolIdList resolveUniverseSymbols(const BacktestRequest& request)
{
    if (request.universeSpec.mode != UniverseSelectionMode::ExplicitSymbols) {
        failEngine(EngineFailureCode::UnsupportedUniverseMode);
    }

    return request.universeSpec.explicitSymbols;
}

} // namespace

StrategyBacktestEngine::StrategyBacktestEngine(const IMarketDataCache& marketDataCache,
                                               const ILayerSelectionStrategy& layerSelectionStrategy,
                                               const IRuleChecker& ruleChecker,
                                               const IExecutionSimulator& executionSimulator,
                                               const IPortfolioOptimizer& portfolioOptimizer,
                                               const IExecutionPolicyStrategy& executionPolicyStrategy,
                                               BacktestRequestValidator requestValidator,
                                               ResultAssembler resultAssembler)
    : marketDataCache_(marketDataCache)
    , layerSelectionStrategy_(layerSelectionStrategy)
    , ruleChecker_(ruleChecker)
    , executionSimulator_(executionSimulator)
    , portfolioOptimizer_(portfolioOptimizer)
    , executionPolicyStrategy_(executionPolicyStrategy)
    , requestValidator_(std::move(requestValidator))
    , resultAssembler_(std::move(resultAssembler))
{
}

BacktestResultDto StrategyBacktestEngine::execute(const BacktestRequest& request,
                                                  const BacktestExecutionCallbacks& callbacks) const
{
    DiagnosticsRecorder diagnosticsRecorder;
    const ValidationIssueList validationIssues = requestValidator_.validate(request);
    if (!validationIssues.empty()) {
        for (const ValidationIssue& validationIssue : validationIssues) {
            diagnosticsRecorder.recordValidationIssue(validationIssue);
        }
        failEngine(EngineFailureCode::InvalidRequest, diagnosticsRecorder.snapshot());
    }

    const auto startTick = SteadyClock::now();
    const TimestampNs startedAt = currentTimestampNs();

    BacktestRuntimeSession session = createSession(request);
    session.markRunning(startedAt);

    diagnosticsRecorder.recordAssumption(EngineAssumption{EngineAssumptionCode::UseClosingPrice, {}});
    diagnosticsRecorder.recordAssumption(EngineAssumption{EngineAssumptionCode::LongOnly, {}});
    diagnosticsRecorder.recordAssumption(EngineAssumption{EngineAssumptionCode::FullFillOrCancel, {}});

    SymbolIdList universeSymbols;
    try {
        universeSymbols = resolveUniverseSymbols(request);
    } catch (const EngineFailure& failure) {
        recordFailureDiagnostic(diagnosticsRecorder, failure.code());
        failEngine(failure.code(), diagnosticsRecorder.snapshot());
    }

    LayerExecutionPipeline pipeline(layerSelectionStrategy_,
                                    ruleChecker_,
                                    portfolioOptimizer_,
                                    executionPolicyStrategy_,
                                    executionSimulator_);

    const auto buildCurrentResult = [&](BacktestRunState runState,
                                        const TimestampNs finishedAt,
                                        const DurationNs elapsed) {
        if (runState == BacktestRunState::Cancelled) {
            session.markCancelled(finishedAt, elapsed);
        } else if (runState == BacktestRunState::Failed) {
            session.markFailed(finishedAt, elapsed);
        } else {
            session.markSucceeded(finishedAt, elapsed);
        }

        diagnosticsRecorder.markElapsed(elapsed);
        try {
            const TradeRecordList tradeRecords = buildTradeRecords(session.state());
            const PerformanceSummary performanceSummary = buildPerformanceSummary(session.state());
            return resultAssembler_.assemble(request,
                                             session.state(),
                                             buildRunMetadata(session),
                                             performanceSummary,
                                             buildTradeStatistics(tradeRecords),
                                             buildRiskMetrics(session.state()),
                                             buildTimeSeries(session.state()),
                                             tradeRecords,
                                             buildUniverseResolution(request),
                                             buildRuleSummary(session.state()),
                                             buildLayerAttribution(session.state()),
                                             buildBenchmarkComparison(request, performanceSummary),
                                             diagnosticsRecorder.snapshot());
        } catch (const EngineFailure& failure) {
            recordFailureDiagnostic(diagnosticsRecorder,
                                    failure.code(),
                                    session.currentTradingDay());
            diagnosticsRecorder.markElapsed(elapsed);
            failEngine(failure.code(), diagnosticsRecorder.snapshot());
        }
    };

    UniverseId activeUniverseId = request.universeSpec.universeId;
    double previousEquity = session.state().startingEquity.value();
    const CandidateCount tradingDayCount = totalTradingDays(request);
    for (std::int32_t tradingDay = request.window.startDay.value();
         tradingDay <= request.window.endDay.value();
         ++tradingDay) {
        if (callbacks.hasCancellationObserver()
            && callbacks.cancellationObserver->get().isCancellationRequested()) {
            return buildCurrentResult(BacktestRunState::Cancelled,
                                      currentTimestampNs(),
                                      toDurationNs(SteadyClock::now() - startTick));
        }

        const TradingDayIndex currentDay(tradingDay);
        session.advanceToDay(currentDay);

        for (const DecisionLayer& decisionLayer : request.spec.layers) {
            if (callbacks.hasCancellationObserver()
                && callbacks.cancellationObserver->get().isCancellationRequested()) {
                return buildCurrentResult(BacktestRunState::Cancelled,
                                          currentTimestampNs(),
                                          toDurationNs(SteadyClock::now() - startTick));
            }

            StrategyContext context;
            context.identity = request.identity;
            context.tradingDay = currentDay;
            context.activeLayerId = decisionLayer.id;
            context.activeUniverseId = activeUniverseId;
            context.riskSpec = request.riskSpec;
            context.executionSpec = request.executionSpec;
            context.portfolioState = session.state().portfolioState;

            const MarketDataSlice marketData = marketDataCache_.sliceForDay(request.overlayBindingScopeId,
                                                                            currentDay,
                                                                            universeSymbols,
                                                                            decisionLayer.overlay.factorIds);
            if (!marketData.isValid()) {
                recordFailureDiagnostic(diagnosticsRecorder,
                                        EngineFailureCode::InvalidMarketDataSlice,
                                        currentDay,
                                        decisionLayer.id);
                failEngine(EngineFailureCode::InvalidMarketDataSlice,
                           diagnosticsRecorder.snapshot());
            }

            try {
                const LayerExecutionState layerState = pipeline.execute(decisionLayer, context, marketData);
                session.appendLayerState(layerState);
                session.replacePortfolioState(applyLayerExecution(session.state().portfolioState,
                                                                  layerState,
                                                                  marketData));
                activeUniverseId = layerState.outputUniverseId;
            } catch (const EngineFailure& failure) {
                recordFailureDiagnostic(diagnosticsRecorder,
                                        failure.code(),
                                        currentDay,
                                        decisionLayer.id);
                failEngine(failure.code(), diagnosticsRecorder.snapshot());
            }
        }

        const double currentEquity = session.state().portfolioState.totalEquity.value();
        session.appendEquityCurvePoint(EquityCurvePoint{currentDay,
                                                        session.state().portfolioState.totalEquity,
                                                        computePeriodReturn(previousEquity, currentEquity)});
        previousEquity = currentEquity;

        if (callbacks.hasProgressSink()) {
            const CandidateCount completedTradingDays(
                static_cast<std::uint32_t>(tradingDay - request.window.startDay.value() + 1));
            callbacks.progressSink->get().publish(BacktestExecutionProgress{
                currentDay,
                completedTradingDays,
                tradingDayCount,
                Ratio(tradingDayCount.isPositive()
                    ? static_cast<double>(completedTradingDays.value()) / static_cast<double>(tradingDayCount.value())
                    : 0.0)});
        }
    }

    return buildCurrentResult(BacktestRunState::Succeeded,
                              currentTimestampNs(),
                              toDurationNs(SteadyClock::now() - startTick));
}

BacktestRuntimeSession StrategyBacktestEngine::createSession(const BacktestRequest& request) const
{
    return BacktestRuntimeSession(createRunId(currentTimestampNs()),
                                  request,
                                  createInitialPortfolioState(request));
}

PortfolioState StrategyBacktestEngine::createInitialPortfolioState(const BacktestRequest& request) const
{
    return PortfolioState{request.costSpec.initialCapital, request.costSpec.initialCapital, {}};
}

RunMetadata StrategyBacktestEngine::buildRunMetadata(const BacktestRuntimeSession& session) const
{
    RunMetadata metadata;
    metadata.runId = session.state().runId;
    metadata.startedAt = session.startedAt();
    metadata.finishedAt = session.finishedAt();
    metadata.elapsed = session.elapsed();
    metadata.state = session.state().runState;
    return metadata;
}

UniverseResolutionSummary StrategyBacktestEngine::buildUniverseResolution(const BacktestRequest& request) const
{
    const CandidateCount requestedCount = toCandidateCount(request.universeSpec.explicitSymbols.size());
    return UniverseResolutionSummary{request.universeSpec.universeId, requestedCount, requestedCount};
}

RuleTemplateSummary StrategyBacktestEngine::buildRuleSummary(const BacktestRuntimeSessionState& runtimeState) const
{
    RuleTemplateSummary summary;
    summary.boundTemplateCount = toCandidateCount(runtimeState.layerStates.size());

    std::size_t matchedCount = 0;
    std::size_t blockedCount = 0;
    std::size_t forcedExitCount = 0;
    for (const LayerExecutionState& layerState : runtimeState.layerStates) {
        for (const RuleDecision& decision : layerState.ruleDecisions) {
            summary.recentDecisions.add(decision);
            ++matchedCount;
            if (decision.code == RuleDecisionCode::ForcedExit) {
                ++forcedExitCount;
            }
            if (decision.code == RuleDecisionCode::EntryBlocked
                || decision.code == RuleDecisionCode::RiskRejected
                || decision.code == RuleDecisionCode::DomainMismatch
                || decision.code == RuleDecisionCode::TimeAlignmentFailure
                || decision.code == RuleDecisionCode::DataUnavailable) {
                ++blockedCount;
            }
        }
    }

    summary.matchedTemplateCount = toCandidateCount(matchedCount);
    summary.blockedTemplateCount = toCandidateCount(blockedCount);
    summary.forcedExitTemplateCount = toCandidateCount(forcedExitCount);
    return summary;
}

LayerAttribution StrategyBacktestEngine::buildLayerAttribution(const BacktestRuntimeSessionState& runtimeState) const
{
    LayerAttribution attribution;
    std::vector<OpenTradeState> openTradeStates;
    std::vector<LayerTradeOutcome> layerTradeOutcomes;
    openTradeStates.reserve(runtimeState.layerStates.size());
    layerTradeOutcomes.reserve(runtimeState.layerStates.size());

    for (const LayerExecutionState& layerState : runtimeState.layerStates) {
        LayerTradeOutcome& layerTradeOutcome = findOrCreateLayerTradeOutcome(layerTradeOutcomes,
                                                                             layerState.layerId);
        for (const ExecutionFill& executionFill : layerState.executionFills) {
            const ExecutionOrder& executionOrder = findOrder(layerState.executionOrders,
                                                             executionFill.orderId);
            OpenTradeState& openTradeState = findOrCreateOpenTradeState(openTradeStates,
                                                                        executionOrder.symbolId);
            if (executionOrder.side == OrderSide::Buy) {
                accumulateOpenBuyFill(openTradeState, executionFill);
                continue;
            }

            const double realizedPnl = realizeSellFill(openTradeState, executionFill);
            layerTradeOutcome.realizedPnl += realizedPnl;
            ++layerTradeOutcome.closedTradeCount;
            if (realizedPnl > 0.0) {
                ++layerTradeOutcome.winCount;
            }
        }
    }

    const double startingEquity = runtimeState.startingEquity.value();
    for (const LayerTradeOutcome& layerTradeOutcome : layerTradeOutcomes) {
        const double contributionReturn = startingEquity > 0.0
            ? layerTradeOutcome.realizedPnl / startingEquity
            : 0.0;
        const double hitRate = layerTradeOutcome.closedTradeCount == 0U
            ? 0.0
            : static_cast<double>(layerTradeOutcome.winCount)
                / static_cast<double>(layerTradeOutcome.closedTradeCount);
        attribution.contributions.add(LayerContribution{layerTradeOutcome.layerId,
                                                        ReturnValue(contributionReturn),
                                                        Ratio(hitRate)});
    }

    return attribution;
}

BenchmarkComparison StrategyBacktestEngine::buildBenchmarkComparison(const BacktestRequest& request,
                                                                    const PerformanceSummary& performanceSummary) const
{
    if (!request.benchmarkSymbol.has_value()) {
        return BenchmarkComparison{};
    }

    const SymbolId benchmarkSymbol = *request.benchmarkSymbol;
    const MarketDataSlice startSlice = marketDataCache_.sliceForDay(request.overlayBindingScopeId,
                                                                    request.window.startDay,
                                                                    singleSymbolList(benchmarkSymbol),
                                                                    FactorIdList{});
    const MarketDataSlice endSlice = marketDataCache_.sliceForDay(request.overlayBindingScopeId,
                                                                  request.window.endDay,
                                                                  singleSymbolList(benchmarkSymbol),
                                                                  FactorIdList{});
    if (!startSlice.isValid() || !endSlice.isValid()) {
        failEngine(EngineFailureCode::InvalidBenchmarkSlice);
    }

    const double startPrice = findClosePrice(startSlice, benchmarkSymbol);
    const double endPrice = findClosePrice(endSlice, benchmarkSymbol);
    if (startPrice <= 0.0) {
        failEngine(EngineFailureCode::InvalidBenchmarkStartPrice);
    }

    const ReturnValue benchmarkReturn((endPrice - startPrice) / startPrice);
    return BenchmarkComparison{true,
                               benchmarkSymbol,
                               benchmarkReturn,
                               ReturnValue(performanceSummary.totalReturn.value() - benchmarkReturn.value())};
}

PerformanceSummary StrategyBacktestEngine::buildPerformanceSummary(const BacktestRuntimeSessionState& runtimeState) const
{
    const CashAmount startingEquity = runtimeState.startingEquity;
    const CashAmount endingEquity = runtimeState.portfolioState.totalEquity;
    const ReturnValue totalReturn(computeTotalReturn(startingEquity, endingEquity));
    return PerformanceSummary{startingEquity,
                              endingEquity,
                              totalReturn,
                              ReturnValue(computeAnnualizedReturnValue(startingEquity,
                                                                       endingEquity,
                                                                       runtimeState.equityCurve.size())),
                              Ratio(computeMaxDrawdownValue(runtimeState.equityCurve))};
}

TradeStatistics StrategyBacktestEngine::buildTradeStatistics(const TradeRecordList& tradeRecords) const
{
    std::vector<OpenTradeState> openTradeStates;
    openTradeStates.reserve(tradeRecords.size());
    std::size_t closedTradeCount = 0U;
    std::size_t winCount = 0U;

    for (const TradeRecord& tradeRecord : tradeRecords) {
        OpenTradeState& openTradeState = findOrCreateOpenTradeState(openTradeStates,
                                                                    tradeRecord.symbolId);
        const double executionValue = tradeRecord.executionPrice.value();
        const std::uint64_t quantity = tradeRecord.quantity.value();

        if (tradeRecord.side == OrderSide::Buy) {
            const double aggregateCost = openTradeState.averageCost * static_cast<double>(openTradeState.quantity)
                + executionValue * static_cast<double>(quantity);
            openTradeState.quantity += quantity;
            openTradeState.averageCost = openTradeState.quantity == 0U
                ? 0.0
                : aggregateCost / static_cast<double>(openTradeState.quantity);
            continue;
        }

        if (openTradeState.quantity < quantity) {
            failEngine(EngineFailureCode::InvalidOpenTradeState);
        }

        ++closedTradeCount;
        if (executionValue > openTradeState.averageCost) {
            ++winCount;
        }

        openTradeState.quantity -= quantity;
        if (openTradeState.quantity == 0U) {
            openTradeState.averageCost = 0.0;
        }
    }

    const CandidateCount tradeCount = toCandidateCount(closedTradeCount);
    const CandidateCount positiveTradeCount = toCandidateCount(winCount);
    return TradeStatistics{tradeCount,
                           positiveTradeCount,
                           Ratio(closedTradeCount == 0U
                               ? 0.0
                               : static_cast<double>(winCount) / static_cast<double>(closedTradeCount))};
}

RiskMetrics StrategyBacktestEngine::buildRiskMetrics(const BacktestRuntimeSessionState& runtimeState) const
{
    double grossExposure = 0.0;
    for (const PositionSnapshot& position : runtimeState.portfolioState.positions) {
        grossExposure += position.marketValue.value();
    }

    const double averageExposure = runtimeState.portfolioState.totalEquity.isPositive()
        ? grossExposure / runtimeState.portfolioState.totalEquity.value()
        : 0.0;
    return RiskMetrics{Ratio(computeMaxDrawdownValue(runtimeState.equityCurve)),
                       Ratio(std::clamp(averageExposure, 0.0, 1.0)),
                       ReturnValue(computeVolatilityValue(runtimeState.equityCurve))};
}

TimeSeries StrategyBacktestEngine::buildTimeSeries(const BacktestRuntimeSessionState& runtimeState) const
{
    TimeSeries series;
    for (const EquityCurvePoint& equityCurvePoint : runtimeState.equityCurve) {
        series.add(TimeSeriesPoint{equityCurvePoint.tradingDay,
                                   equityCurvePoint.equity,
                                   equityCurvePoint.periodReturn});
    }

    return series;
}

TradeRecordList StrategyBacktestEngine::buildTradeRecords(const BacktestRuntimeSessionState& runtimeState) const
{
    TradeRecordList tradeRecords;
    for (const LayerExecutionState& layerState : runtimeState.layerStates) {
        for (const ExecutionFill& executionFill : layerState.executionFills) {
            const ExecutionOrder& executionOrder = findOrder(layerState.executionOrders, executionFill.orderId);
            tradeRecords.add(TradeRecord{executionOrder.orderId,
                                         executionOrder.symbolId,
                                         executionOrder.side,
                                         executionFill.filledQuantity,
                                         executionFill.fillPrice,
                                         executionFill.tradingDay});
        }
    }

    return tradeRecords;
}

} // namespace domain::backtest::strategy_engine