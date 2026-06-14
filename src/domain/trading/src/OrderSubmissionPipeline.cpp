#include "OrderSubmissionPipeline.h"

namespace domain::trading {

OrderSubmissionPipeline::OrderSubmissionPipeline(
    std::shared_ptr<ExecutionSchedulingEngine> schedulingEngine,
    RiskReviewFn riskReviewFn)
    : m_schedulingEngine(std::move(schedulingEngine))
    , m_riskReviewFn(std::move(riskReviewFn)) {}

bool OrderSubmissionPipeline::validateRequest(const OrderSubmissionRequest& req, std::string& error) {
    if (req.symbol.empty()) { error = "symbol_empty"; return false; }
    if (req.quantity <= 0) { error = "quantity_invalid"; return false; }
    if (req.price < 0.0) { error = "price_invalid"; return false; }
    return true;
}

OrderSubmissionResult OrderSubmissionPipeline::submit(
    const OrderSubmissionRequest& request,
    const std::vector<TradeOrder>& pendingOrders) {

    OrderSubmissionResult result;

    std::string validationError;
    if (!validateRequest(request, validationError)) {
        result.decision = ExecutionDecision::Block;
        result.message = validationError;
        return result;
    }

    if (m_schedulingEngine) {
        auto block = m_schedulingEngine->evaluate(request, pendingOrders);
        if (!block.ruleId.empty()) {
            result.decision = ExecutionDecision::Block;
            result.message = block.reasonCode;
            return result;
        }
    }

    if (m_riskReviewFn) {
        std::string riskReason;
        if (!m_riskReviewFn(request, riskReason)) {
            result.decision = ExecutionDecision::Block;
            result.message = riskReason;
            return result;
        }
    }

    result.accepted = true;
    result.decision = ExecutionDecision::Allow;
    return result;
}

} // namespace domain::trading