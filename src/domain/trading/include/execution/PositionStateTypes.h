#pragma once

#include "CommonExecutionTypes.h"

#include <cstdint>

namespace astock::domain::trading::execution_state {

using InstrumentId = execution::ExecutionInstrumentId;

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

} // namespace astock::domain::trading::execution_state

