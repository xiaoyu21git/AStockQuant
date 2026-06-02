#pragma once

#include <optional>

#include "RiskApprovalTypes.h"

namespace astock::domain::trading::risk_approval {

enum class RiskApprovalError {
    None,
    InvalidInput,
    InvalidOrder
};

struct RiskApprovalResult final {
    RiskApprovalError error{RiskApprovalError::None};
    std::optional<ApprovedOrderSet> value;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == RiskApprovalError::None && value.has_value();
    }
};

class IRiskApprovalEngine {
public:
    virtual ~IRiskApprovalEngine() = default;

    virtual RiskApprovalResult evaluate(RiskLimitsSpec limits,
                                        RiskRuntimeContext context,
                                        std::vector<OrderCandidate> orders) const = 0;
};

class SequentialRiskApprovalEngine final : public IRiskApprovalEngine {
public:
    RiskApprovalResult evaluate(RiskLimitsSpec limits,
                                RiskRuntimeContext context,
                                std::vector<OrderCandidate> orders) const override;
};

} // namespace astock::domain::trading::risk_approval


