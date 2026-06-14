#pragma once

#include "ExecutionCheckpointManager.h"
#include "OrderConflictDetector.h"
#include "OrderMapper.h"

#include <memory>
#include <vector>

namespace domain::trading {

struct SchedulingRuleBlock {
    std::string ruleId;
    std::string reasonCode;
    std::string message;
};

class ExecutionSchedulingEngine {
public:
    explicit ExecutionSchedulingEngine(std::shared_ptr<OrderConflictDetector> conflictDetector,
                                       std::shared_ptr<ExecutionCheckpointManager> checkpointMgr);

    // 执行全部调度规则链，返回阻挡决策
    SchedulingRuleBlock evaluate(const OrderSubmissionRequest& request,
                                 const std::vector<TradeOrder>& pendingOrders);

private:
    std::shared_ptr<OrderConflictDetector> m_conflictDetector;
    std::shared_ptr<ExecutionCheckpointManager> m_checkpointMgr;
};

} // namespace domain::trading