#include "../include/IStrategyService.h"

#include "foundation/log/logging.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace domain::strategy {

namespace {
DefaultOrderBuilder kDefaultOrderBuilder;

using SigKey = std::pair<std::uint32_t, std::uint8_t>;
std::map<StrategyInstanceId, std::set<SigKey>> s_lastKeys;
}

StrategyService::StrategyService(IRuntimeFactorService& factorService,
                                 IRuleEvaluationService& ruleEvaluationService)
    : factorService_(factorService)
    , ruleEvaluationService_(ruleEvaluationService)
    , orderBuilder_(&kDefaultOrderBuilder)
    , plan_(defaultExecutionPlan())
{
    reserveWorkingBuffers();
}

StrategyServiceFlowResult StrategyService::configureExecutionPlan(
    const StrategyServiceExecutionPlan& plan)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!plan.isValid()) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidInput);
    }
    plan_ = plan;
    reserveWorkingBuffers();
    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
}

StrategyServiceExecutionPlan StrategyService::executionPlan() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return plan_;
}

void StrategyService::setDiagnosticsSink(IDiagnosticsSink* diagnosticsSink)
{
    std::lock_guard<std::mutex> lock(mutex_);
    diagnosticsSink_ = diagnosticsSink;
}

void StrategyService::setOrderBuilder(const IOrderBuilder* orderBuilder)
{
    std::lock_guard<std::mutex> lock(mutex_);
    orderBuilder_ = orderBuilder ? orderBuilder : &kDefaultOrderBuilder;
}

StrategyServiceFlowResult StrategyService::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == StrategyServiceState::Running) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);  // 幂等：已在运行中
    }
    state_ = StrategyServiceState::Running;
    publishDiagnostics(DiagnosticsEvent(
        DiagnosticsEventCode::StateChanged,
        StrategyServiceFlowCode::Ok,
        0,
        InstrumentId(),
        1.0,
        0.0));
    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
}

StrategyServiceFlowResult StrategyService::pause()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != StrategyServiceState::Running) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidState);
    }
    state_ = StrategyServiceState::Paused;
    publishDiagnostics(DiagnosticsEvent(
        DiagnosticsEventCode::StateChanged,
        StrategyServiceFlowCode::Ok,
        0,
        InstrumentId(),
        2.0,
        0.0));
    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
}

StrategyServiceFlowResult StrategyService::resume()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != StrategyServiceState::Paused) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidState);
    }
    state_ = StrategyServiceState::Running;
    publishDiagnostics(DiagnosticsEvent(
        DiagnosticsEventCode::StateChanged,
        StrategyServiceFlowCode::Ok,
        0,
        InstrumentId(),
        1.0,
        0.0));
    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
}

StrategyServiceFlowResult StrategyService::stop()
{
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = StrategyServiceState::Stopped;
    signalBuffer_.clear();
    ruleResultBuffer_.clear();
    pendingOrderBuffer_.clear();
    resetStats();
    publishDiagnostics(DiagnosticsEvent(
        DiagnosticsEventCode::StateChanged,
        StrategyServiceFlowCode::Ok,
        0,
        InstrumentId(),
        0.0,
        0.0));
    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
}

StrategyServiceState StrategyService::state() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

StrategyServiceFlowResult StrategyService::registerStrategy(
    std::shared_ptr<IRuntimeStrategy> strategy,
    const RuntimeStrategyContext& context)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!strategy) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidInput);
    }
    if (!context.isValid()) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidInput);
    }
    if (context.strategyInstanceId() != strategy->instanceId()) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidInput);
    }
    if (strategyEntries_.size() >= plan_.maxStrategyCount()) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::CapacityExceeded);
    }

    const StrategyInstanceId instanceId = strategy->instanceId();
    const auto exists = std::find_if(
        strategyEntries_.begin(),
        strategyEntries_.end(),
        [instanceId](const StrategyRuntimeEntry& item) {
            return item.strategy && item.strategy->instanceId() == instanceId;
        });
    if (exists != strategyEntries_.end()) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::DuplicateStrategy);
    }

    StrategyRuntimeEntry entry;
    entry.strategy = std::move(strategy);
    entry.context = context;
    strategyEntries_.push_back(std::move(entry));
    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
}

StrategyServiceFlowResult StrategyService::unregisterStrategy(
    StrategyInstanceId strategyInstanceId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto beforeSize = strategyEntries_.size();
    strategyEntries_.erase(
        std::remove_if(
            strategyEntries_.begin(),
            strategyEntries_.end(),
            [strategyInstanceId](const StrategyRuntimeEntry& item) {
                return item.strategy && item.strategy->instanceId() == strategyInstanceId;
            }),
        strategyEntries_.end());

    if (strategyEntries_.size() == beforeSize) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::StrategyNotFound);
    }
    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
}

StrategyServiceFlowResult StrategyService::clearStrategies()
{
    std::lock_guard<std::mutex> lock(mutex_);
    strategyEntries_.clear();
    signalBuffer_.clear();
    ruleResultBuffer_.clear();
    pendingOrderBuffer_.clear();
    resetStats();
    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
}

StrategyServiceFlowResult StrategyService::onMarketDataPoint(
    const MarketDataPoint& marketDataPoint)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != StrategyServiceState::Running) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidState);
    }
    if (!marketDataPoint.isValid()) {
        publishDiagnostics(DiagnosticsEvent(
            DiagnosticsEventCode::MarketDataRejected,
            StrategyServiceFlowCode::InvalidInput,
            0,
            marketDataPoint.instrumentId(),
            0.0,
            0.0));
        // 实盘单 tick 路径：无效行情仅跳过当次评估，不中断策略运行
        return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
    }

    // 因子服务是运行时因子快照的唯一写入路径。
    const StrategyServiceFlowResult updateResult =
        factorService_.updateIncremental(marketDataPoint);
    if (!updateResult.isOk()) {
        return updateResult;
    }

    publishDiagnostics(DiagnosticsEvent(
        DiagnosticsEventCode::MarketDataAccepted,
        StrategyServiceFlowCode::Ok,
        0,
        marketDataPoint.instrumentId(),
        marketDataPoint.lastPrice(),
        marketDataPoint.volume()));

    // 单笔低延迟路径：策略评估后对每条信号执行 check(signal)。
    return evaluateAndCheckRulesLowLatency();
}

StrategyServiceFlowResult StrategyService::onMarketDataBatch(
    const std::vector<MarketDataPoint>& batch)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != StrategyServiceState::Running) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidState);
    }
    if (batch.empty()) {
        resetStats();
        return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
    }
    if (batch.size() > plan_.maxMarketDataPerBatch()) {
        INTERNAL_WARN_STREAM << "[StrategyService] 行情批量超限被拒: batch=" << batch.size()
                             << " max=" << plan_.maxMarketDataPerBatch();
        return StrategyServiceFlowResult(StrategyServiceFlowCode::CapacityExceeded);
    }

    // 过滤无效 MarketDataPoint（如 InstrumentId=0），避免因个别无效条目拒绝整批
    std::vector<MarketDataPoint> validBatch;
    validBatch.reserve(batch.size());
    for (const auto& mdp : batch) {
        if (mdp.isValid()) validBatch.push_back(mdp);
    }
    if (validBatch.empty()) {
        resetStats();
        return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
    }

    // 批量更新因子可保持数据访问连续，并减少重复调度开销。
    const StrategyServiceFlowResult updateResult = factorService_.updateBatch(validBatch);
    if (!updateResult.isOk()) {
        return updateResult;
    }

    return evaluateAndCheckRulesBatch();
}

StrategyExecutionStats StrategyService::lastExecutionStats() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

StrategyCount StrategyService::pendingOrderCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return pendingOrderBuffer_.size();
}

void StrategyService::copyPendingOrders(std::vector<OrderRequest>& outputOrders) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    outputOrders = std::move(pendingOrderBuffer_);
    pendingOrderBuffer_.clear();
}

StrategyServiceFlowResult StrategyService::evaluateAndCheckRulesBatch()
{
    // 复用预分配缓冲区，避免逐 tick 重分配。
    ruleResultBuffer_.clear();
    pendingOrderBuffer_.clear();

    if (!ruleEvaluationService_.isReady()) {
        publishDiagnostics(DiagnosticsEvent(
            DiagnosticsEventCode::RuleRejected,
            StrategyServiceFlowCode::InvalidState,
            0,
            InstrumentId(),
            0.0,
            0.0));
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidState);
    }

    StrategyCount generatedSignalCount = 0;
    StrategyCount passedCount = 0;
    StrategyCount rejectedCount = 0;
    for (const StrategyRuntimeEntry& entry : strategyEntries_) {
        if (!entry.strategy || !entry.strategy->isEnabled()) {
            continue;
        }
        const StrategyServiceFlowResult evaluateResult =
            evaluateEntrySignals(entry, generatedSignalCount);
        if (!evaluateResult.isOk()) {
            return evaluateResult;
        }
        if (signalBuffer_.empty()) {
            continue;
        }

        ruleResultBuffer_.clear();
        const rules::RuleEvaluationContext evaluationContext(
            rules::RuleEvaluationPhase::Batch,
            entry.context.strategyInstanceId(),
            static_cast<StrategyCount>(signalBuffer_.size()));
        const StrategyServiceFlowResult evaluateBatchResult =
            ruleEvaluationService_.evaluateBatch(
                signalBuffer_,
                entry.strategy->ruleSetId(),
                evaluationContext,
                ruleResultBuffer_);
        if (!evaluateBatchResult.isOk()) {
            publishDiagnostics(DiagnosticsEvent(
                DiagnosticsEventCode::RuleRejected,
                evaluateBatchResult.code(),
                entry.context.strategyInstanceId(),
                InstrumentId(),
                static_cast<double>(signalBuffer_.size()),
                static_cast<double>(entry.strategy->ruleSetId())));
            return evaluateBatchResult;
        }
        if (ruleResultBuffer_.size() > plan_.maxRuleResultPerBatch()) {
            return StrategyServiceFlowResult(StrategyServiceFlowCode::CapacityExceeded);
        }

        for (const RuleEvaluationResult& result : ruleResultBuffer_) {
            const StrategyServiceFlowResult handleResult =
                handleRuleEvaluationResult(result, passedCount, rejectedCount);
            if (!handleResult.isOk()) {
                publishDiagnostics(DiagnosticsEvent(
                    DiagnosticsEventCode::RuleRejected,
                    handleResult.code(),
                    result.signal().strategyInstanceId(),
                    result.signal().instrumentId(),
                    result.signal().score(),
                    result.signal().targetWeight()));
                return handleResult;
            }
        }
    }

    stats_.setStrategyCount(strategyEntries_.size());
    stats_.setGeneratedSignalCount(generatedSignalCount);
    stats_.setPassedRuleCount(passedCount);
    stats_.setRejectedRuleCount(rejectedCount);
    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
}

StrategyServiceFlowResult StrategyService::evaluateAndCheckRulesLowLatency()
{
    // 复用预分配缓冲区，避免逐 tick 重分配。
    ruleResultBuffer_.clear();
    pendingOrderBuffer_.clear();

    static int s_evalRound = 0;
    static int s_totalSignals = 0;
    static int s_totalOrders = 0;

    if (!ruleEvaluationService_.isReady()) {
        publishDiagnostics(DiagnosticsEvent(
            DiagnosticsEventCode::RuleRejected,
            StrategyServiceFlowCode::InvalidState,
            0,
            InstrumentId(),
            0.0,
            0.0));
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidState);
    }

    StrategyCount generatedSignalCount = 0;
    StrategyCount passedCount = 0;
    StrategyCount rejectedCount = 0;

    for (const StrategyRuntimeEntry& entry : strategyEntries_) {
        if (!entry.strategy || !entry.strategy->isEnabled()) {
            continue;
        }
        const StrategyServiceFlowResult evaluateResult =
            evaluateEntrySignals(entry, generatedSignalCount);
        if (!evaluateResult.isOk()) {
            return evaluateResult;
        }

        if (!signalBuffer_.empty()) {
            std::set<SigKey> cur;
            for (const auto& s : signalBuffer_)
                cur.emplace(s.instrumentId().value, static_cast<std::uint8_t>(s.side()));
            if (cur == s_lastKeys[entry.strategy->instanceId()]) {
                signalBuffer_.clear();
                continue;
            }
            s_lastKeys[entry.strategy->instanceId()] = std::move(cur);
        }

        for (const StrategySignal& signal : signalBuffer_) {
            const rules::RuleEvaluationContext evaluationContext(
                rules::RuleEvaluationPhase::LowLatency,
                signal.strategyInstanceId(),
                1);
            const RuleEvaluationResult result =
                ruleEvaluationService_.evaluate(
                    signal,
                    entry.strategy->ruleSetId(),
                    evaluationContext);
            const StrategyServiceFlowResult handleResult =
                handleRuleEvaluationResult(result, passedCount, rejectedCount);
            if (!handleResult.isOk()) {
                publishDiagnostics(DiagnosticsEvent(
                    DiagnosticsEventCode::RuleRejected,
                    handleResult.code(),
                    result.signal().strategyInstanceId(),
                    result.signal().instrumentId(),
                    result.signal().score(),
                    result.signal().targetWeight()));
                return handleResult;
            }
        }
    }

    stats_.setStrategyCount(strategyEntries_.size());
    stats_.setGeneratedSignalCount(generatedSignalCount);
    stats_.setPassedRuleCount(passedCount);
    stats_.setRejectedRuleCount(rejectedCount);

    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
}

StrategyServiceFlowResult StrategyService::evaluateEntrySignals(
    const StrategyRuntimeEntry& entry,
    StrategyCount& generatedSignalCount)
{
    factorSnapshotBuffer_.clear();
    signalBuffer_.clear();
    // 因子快照仅多因子选股策略消费; 非因子策略(含混合模式)跳过,
    // 避免每日为 overlay 因子做全市场符号匹配的无效开销
    if (entry.strategy->usesFactors()) {
        factorService_.copySnapshots(factorSnapshotBuffer_);
    }
    // 策略只消费当前因子结果快照，不接触因子服务更新职责。
    entry.strategy->evaluate(factorSnapshotBuffer_, entry.context, signalBuffer_);
    generatedSignalCount += signalBuffer_.size();
    publishDiagnostics(DiagnosticsEvent(
        DiagnosticsEventCode::StrategyEvaluated,
        StrategyServiceFlowCode::Ok,
        entry.context.strategyInstanceId(),
        InstrumentId(),
        static_cast<double>(signalBuffer_.size()),
        static_cast<double>(entry.context.snapshotVersion())));
    if (signalBuffer_.size() > plan_.maxSignalPerBatch()) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::CapacityExceeded);
    }
    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
}

StrategyServiceFlowResult StrategyService::handleRuleEvaluationResult(
    const RuleEvaluationResult& result,
    StrategyCount& passedCount,
    StrategyCount& rejectedCount)
{
    if (result.passed()) {
        ++passedCount;
        const RuntimeStrategyContext* context = findContext(result.signal().strategyInstanceId());
        if (!context) {
            return StrategyServiceFlowResult(StrategyServiceFlowCode::StrategyNotFound);
        }

        const OrderRequest orderRequest = buildOrderRequest(result.signal(), *context);
        if (!orderRequest.isValid()) {
            return StrategyServiceFlowResult(StrategyServiceFlowCode::OrderBuildFailed);
        }
        pendingOrderBuffer_.push_back(orderRequest);
        publishDiagnostics(DiagnosticsEvent(
            DiagnosticsEventCode::OrderBuilt,
            StrategyServiceFlowCode::Ok,
            static_cast<std::uint64_t>(std::stoull(orderRequest.strategyId())),
            InstrumentId(static_cast<std::uint32_t>(std::stoul(orderRequest.symbol()))),
            static_cast<double>(orderRequest.quantity()),
            static_cast<double>(orderRequest.side() == OrderSide::Buy ? 1 : -1)));
        return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
    }

    ++rejectedCount;
    publishDiagnostics(DiagnosticsEvent(
        DiagnosticsEventCode::RuleRejected,
        StrategyServiceFlowCode::RuleCheckFailed,
        result.signal().strategyInstanceId(),
        result.signal().instrumentId(),
        result.signal().score(),
        result.signal().targetWeight()));
    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
}

OrderRequest StrategyService::buildOrderRequest(
    const StrategySignal& signal,
    const RuntimeStrategyContext& context) const
{
    OrderRequest outputOrder;
    const IOrderBuilder* builder = orderBuilder_ ? orderBuilder_ : &kDefaultOrderBuilder;
    const StrategyServiceFlowResult buildResult = builder->buildOrder(signal, context, outputOrder);
    if (!buildResult.isOk()) {
        return OrderRequest();
    }
    return outputOrder;
}

const RuntimeStrategyContext* StrategyService::findContext(StrategyInstanceId strategyInstanceId) const
{
    const auto found = std::find_if(
        strategyEntries_.begin(),
        strategyEntries_.end(),
        [strategyInstanceId](const StrategyRuntimeEntry& entry) {
            return entry.strategy && entry.strategy->instanceId() == strategyInstanceId;
        });
    if (found == strategyEntries_.end()) {
        return nullptr;
    }
    return &found->context;
}

void StrategyService::publishDiagnostics(const DiagnosticsEvent& event)
{
    if (diagnosticsSink_) {
        diagnosticsSink_->publish(event);
    }
}

void StrategyService::reserveWorkingBuffers()
{
    factorSnapshotBuffer_.clear();
    signalBuffer_.reserve(plan_.maxSignalPerBatch());
    ruleResultBuffer_.reserve(plan_.maxRuleResultPerBatch());
    pendingOrderBuffer_.reserve(plan_.maxRuleResultPerBatch());
    strategyEntries_.reserve(plan_.maxStrategyCount());
}

void StrategyService::resetStats()
{
    stats_.setStrategyCount(0);
    stats_.setGeneratedSignalCount(0);
    stats_.setPassedRuleCount(0);
    stats_.setRejectedRuleCount(0);
    factorSnapshotBuffer_.clear();
    pendingOrderBuffer_.clear();
}

void StrategyService::setContextHistoricalView(const void* view)
{
    for (auto& entry : strategyEntries_) {
        entry.context.setHistoricalView(view);
    }
}

void StrategyService::setContextEvaluationRow(int row)
{
    for (auto& entry : strategyEntries_) {
        entry.context.setCurrentEvaluationRow(row);
    }
}

void StrategyService::updateCurrentWeights(
    const std::unordered_map<std::string, double>& weights)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : strategyEntries_) {
        entry.context.setCurrentWeights(weights);
    }
}

void StrategyService::updateCandidatePool(
    const std::unordered_set<std::string>& pool)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : strategyEntries_) {
        entry.context.setCandidatePool(pool);
    }
}

} // namespace domain::strategy
