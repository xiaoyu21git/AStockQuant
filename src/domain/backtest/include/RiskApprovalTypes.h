#pragma once

#include <cstdint>
#include <vector>

namespace astock::domain::backtest::risk_approval {

struct InstrumentId final {
    static constexpr uint32_t kInvalidValue = 0U;

    uint32_t value{kInvalidValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value != kInvalidValue;
    }
};

struct DeltaBps final {
    static constexpr int32_t kMinValue = 0;
    static constexpr int32_t kMaxValue = 10000;

    int32_t value{kMinValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value >= kMinValue && value <= kMaxValue;
    }
};

enum class OrderAction {
    Buy,
    Sell
};

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

} // namespace astock::domain::backtest::risk_approval
