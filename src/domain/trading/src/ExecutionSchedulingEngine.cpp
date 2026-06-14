#include "ExecutionSchedulingEngine.h"

namespace domain::trading {

ExecutionSchedulingEngine::ExecutionSchedulingEngine(
    std::shared_ptr<OrderConflictDetector> conflictDetector,
    std::shared_ptr<ExecutionCheckpointManager> checkpointMgr)
    : m_conflictDetector(std::move(conflictDetector))
    , m_checkpointMgr(std::move(checkpointMgr)) {}

SchedulingRuleBlock ExecutionSchedulingEngine::evaluate(
    const OrderSubmissionRequest& request,
    const std::vector<TradeOrder>& pendingOrders) {

    SchedulingRuleBlock block;
    if (!m_conflictDetector || !m_checkpointMgr) {
        return block;
    }

    // 规则 1: 冲突检测
    if (auto conflict = m_conflictDetector->detect(request.symbol, request.side, pendingOrders);
        conflict.hasConflict) {
        block.ruleId = "conflict";
        block.reasonCode = conflict.message;
        block.message = conflict.message;
        return block;
    }

    // 规则 2: 检查点暂停
    ExecutionScopeId scopeId{request.executionScopeId};
    if (!scopeId.empty() && m_checkpointMgr->isPaused(scopeId)) {
        block.ruleId = "checkpoint_paused";
        block.reasonCode = "scope_paused";
        block.message = "scope_paused";
        return block;
    }

    // 规则 3: 检查点审批
    BatchId batchId{request.batchId};
    if (!scopeId.empty() && !batchId.empty()
        && !m_checkpointMgr->isCheckpointApproved(scopeId, batchId)) {
        block.ruleId = "checkpoint_required";
        block.reasonCode = "manual_approval";
        block.message = "需手动审批";
        return block;
    }

    return block;
}

} // namespace domain::trading