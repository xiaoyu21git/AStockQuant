#include "PositionStateMachine.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace astock::domain::trading::execution_state {

bool NetPositionStateMachine::checkedAddInt32(int32_t left, int32_t right, int32_t* out)
{
    if (out == nullptr) {
        return false;
    }

    if (right > 0 && left > std::numeric_limits<int32_t>::max() - right) {
        return false;
    }

    *out = left + right;
    return true;
}

bool LongOnlyPositionStateMachine::checkedAddInt32(int32_t left, int32_t right, int32_t* out)
{
    if (out == nullptr) {
        return false;
    }

    if (right > 0 && left > std::numeric_limits<int32_t>::max() - right) {
        return false;
    }

    *out = left + right;
    return true;
}

PositionTransitionResult NetPositionStateMachine::apply(PositionState current, ExecutionFill fill) const
{
    if (!current.isValid() || !fill.isValid() || current.instrument != fill.instrument) {
        return PositionTransitionResult{PositionTransitionError::InvalidInput, std::nullopt};
    }

    int32_t longLots = current.longQuantity.value;
    int32_t shortLots = current.shortQuantity.value;
    const int32_t qty = fill.quantity.value;

    if (fill.side == FillSide::Buy) {
        const int32_t cover = std::min(shortLots, qty);
        shortLots -= cover;
        if (!NetPositionStateMachine::checkedAddInt32(longLots, qty - cover, &longLots)) {
            return PositionTransitionResult{PositionTransitionError::InvalidInput, std::nullopt};
        }
    } else {
        const int32_t close = std::min(longLots, qty);
        longLots -= close;
        if (!NetPositionStateMachine::checkedAddInt32(shortLots, qty - close, &shortLots)) {
            return PositionTransitionResult{PositionTransitionError::InvalidInput, std::nullopt};
        }
    }

    PositionState next;
    next.instrument = current.instrument;
    next.longQuantity = QuantityLots{longLots};
    next.shortQuantity = QuantityLots{shortLots};

    return PositionTransitionResult{PositionTransitionError::None, std::move(next)};
}

PositionTransitionResult LongOnlyPositionStateMachine::apply(PositionState current, ExecutionFill fill) const
{
    if (!current.isValid() || !fill.isValid() || current.instrument != fill.instrument) {
        return PositionTransitionResult{PositionTransitionError::InvalidInput, std::nullopt};
    }
    if (current.shortQuantity.value != 0) {
        return PositionTransitionResult{PositionTransitionError::InvalidInput, std::nullopt};
    }

    int32_t longLots = current.longQuantity.value;
    if (fill.side == FillSide::Buy) {
        if (!LongOnlyPositionStateMachine::checkedAddInt32(longLots, fill.quantity.value, &longLots)) {
            return PositionTransitionResult{PositionTransitionError::InvalidInput, std::nullopt};
        }
    } else {
        if (fill.quantity.value > longLots) {
            return PositionTransitionResult{PositionTransitionError::InsufficientLongPosition, std::nullopt};
        }
        longLots -= fill.quantity.value;
    }

    PositionState next;
    next.instrument = current.instrument;
    next.longQuantity = QuantityLots{longLots};
    next.shortQuantity = QuantityLots{0};

    return PositionTransitionResult{PositionTransitionError::None, std::move(next)};
}

} // namespace astock::domain::trading::execution_state



