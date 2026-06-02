#include "OrderRouter.h"

#include <unordered_set>
#include <utility>

namespace astock::domain::trading::order_routing {

OrderRoutingResult SimpleOrderRouter::route(RoutingSpec spec,
                                            std::vector<ExecutionIntent> intents) const
{
    if (!spec.isValid()) {
        return OrderRoutingResult{OrderRoutingError::InvalidInput, std::nullopt};
    }

    RoutedOrderSet out;
    out.items.reserve(intents.size());
    std::unordered_set<uint32_t> seenInstruments;
    seenInstruments.reserve(intents.size());

    for (const ExecutionIntent& intent : intents) {
        if (!intent.isValid()) {
            return OrderRoutingResult{OrderRoutingError::InvalidIntent, std::nullopt};
        }
        if (!seenInstruments.insert(intent.instrument.value).second) {
            return OrderRoutingResult{OrderRoutingError::InvalidIntent, std::nullopt};
        }
        if (intent.delta.value > spec.maxOrderDelta.value) {
            return OrderRoutingResult{OrderRoutingError::OrderDeltaExceeded, std::nullopt};
        }

        RoutedOrder routed;
        routed.instrument = intent.instrument;
        routed.action = intent.action;
        routed.delta = intent.delta;

        if (intent.urgency.value >= spec.aggressiveUrgencyThreshold.value) {
            routed.venue = ExecutionVenue::Primary;
            routed.intent = LiquidityIntent::Aggressive;
        } else if (intent.delta.value <= spec.passiveSecondaryMaxDelta.value) {
            routed.venue = ExecutionVenue::Secondary;
            routed.intent = LiquidityIntent::Passive;
        } else {
            routed.venue = ExecutionVenue::Primary;
            routed.intent = LiquidityIntent::Passive;
        }

        out.items.push_back(routed);
    }

    return OrderRoutingResult{OrderRoutingError::None, std::move(out)};
}

} // namespace astock::domain::trading::order_routing



