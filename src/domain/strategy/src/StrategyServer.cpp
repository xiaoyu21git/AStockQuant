#include "../include/IStrategyService.h"

#include <algorithm>

namespace domain::strategy {

namespace {
DefaultOrderBuilder kDefaultOrderBuilder;
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

StrategyService::StrategyService(IRuntimeFactorService& factorService,
                                 IRuleEvaluationService& ruleEvaluationService,
                                 IRuntimeOrderSink& orderSink)
    : factorService_(factorService)
    , ruleEvaluationService_(ruleEvaluationService)
    , orderSink_(&orderSink)
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
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidInput);
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
        return StrategyServiceFlowResult(StrategyServiceFlowCode::CapacityExceeded);
    }
    for (const MarketDataPoint& marketDataPoint : batch) {
        if (!marketDataPoint.isValid()) {
            return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidInput);
        }
    }

    // 批量更新因子可保持数据访问连续，并减少重复调度开销。
    const StrategyServiceFlowResult updateResult = factorService_.updateBatch(batch);
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
    outputOrders.assign(pendingOrderBuffer_.begin(), pendingOrderBuffer_.end());
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
    return flushPendingOrders();
}

StrategyServiceFlowResult StrategyService::evaluateAndCheckRulesLowLatency()
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
    return flushPendingOrders();
}

StrategyServiceFlowResult StrategyService::evaluateEntrySignals(
    const StrategyRuntimeEntry& entry,
    StrategyCount& generatedSignalCount)
{
    factorSnapshotBuffer_.clear();
    signalBuffer_.clear();
    factorService_.copySnapshots(factorSnapshotBuffer_);
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
            orderRequest.strategyInstanceId(),
            orderRequest.instrumentId(),
            static_cast<double>(orderRequest.quantity()),
            static_cast<double>(orderRequest.side() == RuntimeOrderSide::Buy ? 1 : -1)));
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

StrategyServiceFlowResult StrategyService::flushPendingOrders()
{
    if (pendingOrderBuffer_.empty()) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
    }
    // 未配置下单出口时进入仿真模式：仅在内存中保留订单供上层读取。
    if (!orderSink_) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
    }

    for (const OrderRequest& order : pendingOrderBuffer_) {
        const StrategyServiceFlowResult submitResult = orderSink_->submit(order);
        if (!submitResult.isOk()) {
            return StrategyServiceFlowResult(StrategyServiceFlowCode::OrderSubmitFailed);
        }

        publishDiagnostics(DiagnosticsEvent(
            DiagnosticsEventCode::OrderSubmitted,
            StrategyServiceFlowCode::Ok,
            order.strategyInstanceId(),
            order.instrumentId(),
            static_cast<double>(order.quantity()),
            static_cast<double>(order.side() == RuntimeOrderSide::Buy ? 1 : -1)));
    }
    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
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

} // namespace domain::strategy
