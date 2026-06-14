#pragma once

#include "ExecutionSchedulingEngine.h"
#include "OrderMapper.h"
#include "TradingEnums.h"

#include <functional>
#include <memory>
#include <string>

namespace domain::trading {

// 风控审批回调：由 facade 注入 RiskApprovalGateway
using RiskReviewFn = std::function<bool(const OrderSubmissionRequest&, std::string& rejectReason)>;

class OrderSubmissionPipeline {
public:
    OrderSubmissionPipeline(std::shared_ptr<ExecutionSchedulingEngine> schedulingEngine,
                            RiskReviewFn riskReviewFn);

    // 完整流水线：参数校验 -> 调度规则链 -> 风控审批
    OrderSubmissionResult submit(const OrderSubmissionRequest& request,
                                  const std::vector<TradeOrder>& pendingOrders);

private:
    static bool validateRequest(const OrderSubmissionRequest& req, std::string& error);

    std::shared_ptr<ExecutionSchedulingEngine> m_schedulingEngine;
    RiskReviewFn m_riskReviewFn;
};

} // namespace domain::trading