#include "OrderPipeline.h"
#include "../include/OrderSubmissionPipeline.h"
#include "../include/ExecutionSchedulingEngine.h"
#include "../include/ExecutionCheckpointManager.h"
#include "../include/OrderConflictDetector.h"
#include "../../strategy/include/RiskEvaluator.h"

#include <cmath>

namespace domain::trading {

OrderPipeline::OrderPipeline(
    OrderSubmissionPipeline& submissionPipeline,
    ExecutionSchedulingEngine& schedulingEngine,
    OrderConflictDetector& conflictDetector,
    ExecutionCheckpointManager& checkpointManager)
    : m_submissionPipeline(submissionPipeline)
    , m_schedulingEngine(schedulingEngine)
    , m_conflictDetector(conflictDetector)
    , m_checkpointManager(checkpointManager)
{}

void OrderPipeline::beginBatch() {
    m_pendingInBatch.clear();
    m_pendingResults.clear();
}

PipelineResult OrderPipeline::submit(const TradeOrder& order, double closingPrice) {
    // ── Stage 1: validate ──
    auto validation = TradeExecutionEngine::validateOrder(order);
    if (!validation.valid()) {
        return PipelineResult::rejected(validation.message());
    }

    // 盘后固定价格单额外校验
    if (order.orderType() == OrderType::AfterHoursFixed) {
        if (closingPrice <= 0.0) {
            return PipelineResult::rejected("After-hours: closing price not available");
        }
        if (order.side() == strategy::OrderDirection::Buy && order.price() < closingPrice) {
            return PipelineResult::rejected("After-hours buy: price must be >= closing price");
        }
        if (order.side() == strategy::OrderDirection::Sell && order.price() > closingPrice) {
            return PipelineResult::rejected("After-hours sell: price must be <= closing price");
        }
    }

    // ── Stage 3: risk evaluation（纯静态函数，无副作用）──
    strategy::RiskInput riskInput;
    riskInput.symbol        = order.symbol();
    riskInput.side          = static_cast<strategy::OrderSide>(order.side());
    riskInput.price         = order.price();
    riskInput.quantity      = static_cast<double>(order.quantity());
    riskInput.positionEffect = order.positionEffect();
    strategy::RiskResult riskResult = strategy::RiskEvaluator::evaluateOrder(riskInput);
    if (!riskResult.approved()) {
        return PipelineResult::riskReject(riskResult.code(), riskResult.description());
    }

    // 暂存，Stage 2（调度）在 endBatch 批量执行
    m_pendingInBatch.push_back(order);
    PipelineResult result = PipelineResult::ok();
    result.riskScore = riskResult.score();
    m_pendingResults.push_back(result);
    return result;
}

void OrderPipeline::endBatch(std::vector<PipelineResult>& results) {
    if (m_pendingInBatch.empty()) return;

    // ── Stage 2: batch scheduling（全量冲突检测）──
    for (size_t i = 0; i < m_pendingInBatch.size(); ++i) {
        const auto& order = m_pendingInBatch[i];

        if (m_conflictDetector.hasConflict(order, m_pendingInBatch)) {
            if (i < results.size()) {
                results[i] = PipelineResult::scheduleBlock(
                    ScheduleConflictCode::PendingConflictingOrder,
                    "conflicting order in batch");
            }
            continue;
        }

        if (order.requiresManualCheckpoint()) {
            if (!m_checkpointManager.isApproved(order.executionScopeId(), order.batchId())) {
                if (i < results.size()) {
                    results[i] = PipelineResult::scheduleBlock(
                        ScheduleConflictCode::ManualCheckpointRequired,
                        "manual checkpoint required");
                }
                continue;
            }
        }
    }
}

} // namespace domain::trading
