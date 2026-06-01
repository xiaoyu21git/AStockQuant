#pragma once

#include <cstdint>
#include <vector>

namespace astock::domain::backtest::signal_orders {

struct InstrumentId final {
    static constexpr uint32_t kInvalidValue = 0U;

    uint32_t value{kInvalidValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value != kInvalidValue;
    }

    friend bool operator==(InstrumentId left, InstrumentId right) noexcept
    {
        return left.value == right.value;
    }

    friend bool operator<(InstrumentId left, InstrumentId right) noexcept
    {
        return left.value < right.value;
    }
};

struct SignalBps final {
    static constexpr int32_t kMinValue = -10000;
    static constexpr int32_t kMaxValue = 10000;

    int32_t value{0};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value >= kMinValue && value <= kMaxValue;
    }
};

struct WeightDeltaBps final {
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

struct SignalSnapshot final {
    InstrumentId instrument{};
    SignalBps signal{};

    [[nodiscard]] bool isValid() const noexcept
    {
        return instrument.isValid() && signal.isValid();
    }
};

struct OrderInstruction final {
    InstrumentId instrument{};
    OrderAction action{OrderAction::Buy};
    WeightDeltaBps delta{};

    [[nodiscard]] bool isValid() const noexcept
    {
        return instrument.isValid() && delta.isValid();
    }
};

struct OrderInstructionSet final {
    std::vector<OrderInstruction> items;
};

} // namespace astock::domain::backtest::signal_orders
