#pragma once

#include "CommonExecutionTypes.h"

#include <cstdint>
#include <vector>

namespace astock::domain::trading::risk_approval {

using InstrumentId = execution::ExecutionInstrumentId;
using DeltaBps = execution::ExecutionDeltaBps;
using OrderAction = execution::ExecutionOrderAction;

struct OrderCandidate final {
    InstrumentId instrument{};
    OrderAction action{OrderAction::Buy};
    DeltaBps delta{};

    [[nodiscard]] bool isValid() const noexcept
    {
        return instrument.isValid() && delta.isValid();
    }
};

struct RiskLimitsSpec final {
    DeltaBps maxSingleOrderDelta{};
    DeltaBps maxTurnoverDelta{};
    int32_t maxOrderCount{0};

    [[nodiscard]] bool isValid() const noexcept
    {
        return maxSingleOrderDelta.isValid()
            && maxTurnoverDelta.isValid()
            && maxOrderCount > 0;
    }
};

struct RiskRuntimeContext final {
    DeltaBps consumedTurnover{};

    [[nodiscard]] bool isValid() const noexcept
    {
        return consumedTurnover.isValid();
    }
};

enum class RejectReason {
    SingleOrderLimitExceeded,
    TurnoverLimitExceeded,
    OrderCountLimitExceeded
};

struct RejectedOrder final {
    OrderCandidate order{};
    RejectReason reason{RejectReason::SingleOrderLimitExceeded};
};

struct ApprovedOrderSet final {
    std::vector<OrderCandidate> approved;
    std::vector<RejectedOrder> rejected;
    DeltaBps finalConsumedTurnover{};
};

} // namespace astock::domain::trading::risk_approval

