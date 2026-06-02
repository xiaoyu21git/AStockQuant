#pragma once

#include "CommonExecutionTypes.h"

#include <cstdint>
#include <vector>

namespace astock::domain::trading::signal_orders {

using InstrumentId = execution::ExecutionInstrumentId;

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

using OrderAction = execution::ExecutionOrderAction;

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

} // namespace astock::domain::trading::signal_orders

