#pragma once

#include <cstdint>
#include <vector>

namespace astock::domain::backtest::order_routing {

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

enum class ExecutionVenue {
    Primary,
    Secondary
};

enum class LiquidityIntent {
    Aggressive,
    Passive
};

struct ExecutionIntent final {
    InstrumentId instrument{};
    OrderAction action{OrderAction::Buy};
    DeltaBps delta{};
    DeltaBps urgency{};

    [[nodiscard]] bool isValid() const noexcept
    {
        return instrument.isValid() && delta.isValid() && urgency.isValid();
    }
};

struct RoutingSpec final {
    DeltaBps maxOrderDelta{};
    DeltaBps aggressiveUrgencyThreshold{};
    DeltaBps passiveSecondaryMaxDelta{};

    [[nodiscard]] bool isValid() const noexcept
    {
        return maxOrderDelta.isValid()
            && aggressiveUrgencyThreshold.isValid()
            && passiveSecondaryMaxDelta.isValid()
            && maxOrderDelta.value > DeltaBps::kMinValue
            && aggressiveUrgencyThreshold.value > DeltaBps::kMinValue;
    }
};

struct RoutedOrder final {
    InstrumentId instrument{};
    OrderAction action{OrderAction::Buy};
    DeltaBps delta{};
    ExecutionVenue venue{ExecutionVenue::Primary};
    LiquidityIntent intent{LiquidityIntent::Passive};
};

struct RoutedOrderSet final {
    std::vector<RoutedOrder> items;
};

} // namespace astock::domain::backtest::order_routing
