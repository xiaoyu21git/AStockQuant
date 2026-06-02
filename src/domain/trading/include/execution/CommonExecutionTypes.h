#pragma once

#include <stdint.h>

namespace astock::domain::trading::execution {

struct ExecutionInstrumentId final {
    static constexpr uint32_t kInvalidValue = 0U;

    uint32_t value{kInvalidValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value != kInvalidValue;
    }

    friend bool operator==(ExecutionInstrumentId left, ExecutionInstrumentId right) noexcept
    {
        return left.value == right.value;
    }

    friend bool operator!=(ExecutionInstrumentId left, ExecutionInstrumentId right) noexcept
    {
        return !(left == right);
    }

    friend bool operator<(ExecutionInstrumentId left, ExecutionInstrumentId right) noexcept
    {
        return left.value < right.value;
    }
};

struct ExecutionDeltaBps final {
    static constexpr int32_t kMinValue = 0;
    static constexpr int32_t kMaxValue = 10000;

    int32_t value{kMinValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value >= kMinValue && value <= kMaxValue;
    }
};

enum class ExecutionOrderAction {
    Buy,
    Sell
};

using InstrumentId = ExecutionInstrumentId;
using DeltaBps = ExecutionDeltaBps;
using OrderAction = ExecutionOrderAction;

} // namespace astock::domain::trading::execution
