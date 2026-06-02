#pragma once

#include "CommonExecutionTypes.h"

#include <vector>

namespace astock::domain::trading::order_routing {

using InstrumentId = execution::ExecutionInstrumentId;
using DeltaBps = execution::ExecutionDeltaBps;
using OrderAction = execution::ExecutionOrderAction;

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

} // namespace astock::domain::trading::order_routing

