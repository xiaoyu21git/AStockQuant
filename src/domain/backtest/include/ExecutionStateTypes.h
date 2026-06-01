#pragma once

#include <cstdint>

namespace astock::domain::backtest::execution_state {

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

    friend bool operator!=(InstrumentId left, InstrumentId right) noexcept
    {
        return !(left == right);
    }
};

struct QuantityLots final {
    static constexpr int32_t kMinValue = 0;

    int32_t value{kMinValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value >= kMinValue;
    }
};

enum class FillSide {
    Buy,
    Sell
};

struct ExecutionFill final {
    InstrumentId instrument{};
    FillSide side{FillSide::Buy};
    QuantityLots quantity{};

    [[nodiscard]] bool isValid() const noexcept
    {
        return instrument.isValid() && quantity.isValid();
    }
};

struct PositionState final {
    InstrumentId instrument{};
    QuantityLots longQuantity{};
    QuantityLots shortQuantity{};

    [[nodiscard]] bool isValid() const noexcept
    {
        return instrument.isValid() && longQuantity.isValid() && shortQuantity.isValid();
    }
};

} // namespace astock::domain::backtest::execution_state
