#include "RiskApprovalEngine.h"

#include <unordered_set>
#include <utility>

namespace astock::domain::trading::risk_approval {

RiskApprovalResult SequentialRiskApprovalEngine::evaluate(RiskLimitsSpec limits,
                                                          RiskRuntimeContext context,
                                                          std::vector<OrderCandidate> orders) const
{
    if (!limits.isValid() || !context.isValid()) {
        return RiskApprovalResult{RiskApprovalError::InvalidInput, std::nullopt};
    }

    ApprovedOrderSet out;
    out.approved.reserve(orders.size());
    out.rejected.reserve(orders.size());

    int32_t consumedTurnover = context.consumedTurnover.value;
    std::unordered_set<uint32_t> seenInstruments;
    seenInstruments.reserve(orders.size());

    for (const OrderCandidate& order : orders) {
        if (!order.isValid()) {
            return RiskApprovalResult{RiskApprovalError::InvalidOrder, std::nullopt};
        }
        if (!seenInstruments.insert(order.instrument.value).second) {
            return RiskApprovalResult{RiskApprovalError::InvalidOrder, std::nullopt};
        }

        if (order.delta.value > limits.maxSingleOrderDelta.value) {
            out.rejected.push_back(RejectedOrder{order, RejectReason::SingleOrderLimitExceeded});
            continue;
        }

        if (static_cast<int32_t>(out.approved.size()) >= limits.maxOrderCount) {
            out.rejected.push_back(RejectedOrder{order, RejectReason::OrderCountLimitExceeded});
            continue;
        }

        const int32_t afterConsume = consumedTurnover + order.delta.value;
        if (afterConsume > limits.maxTurnoverDelta.value) {
            out.rejected.push_back(RejectedOrder{order, RejectReason::TurnoverLimitExceeded});
            continue;
        }

        out.approved.push_back(order);
        consumedTurnover = afterConsume;
    }

    out.finalConsumedTurnover = DeltaBps{consumedTurnover};
    return RiskApprovalResult{RiskApprovalError::None, std::move(out)};
}

} // namespace astock::domain::trading::risk_approval



