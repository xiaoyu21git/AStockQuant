#pragma once

#include <optional>

#include "PositionStateTypes.h"

namespace astock::domain::trading::execution_state {

enum class PositionTransitionError {
    None,
    InvalidInput,
    InsufficientLongPosition,
    InsufficientShortPosition
};

struct PositionTransitionResult final {
    PositionTransitionError error{PositionTransitionError::None};
    std::optional<PositionState> value;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == PositionTransitionError::None && value.has_value();
    }
};

class IPositionStateMachine {
public:
    virtual ~IPositionStateMachine() = default;

    virtual PositionTransitionResult apply(PositionState current, ExecutionFill fill) const = 0;
};

class NetPositionStateMachine final : public IPositionStateMachine {
public:
    PositionTransitionResult apply(PositionState current, ExecutionFill fill) const override;

private:
    static bool checkedAddInt32(int32_t left, int32_t right, int32_t* out);
};

class LongOnlyPositionStateMachine final : public IPositionStateMachine {
public:
    PositionTransitionResult apply(PositionState current, ExecutionFill fill) const override;

private:
    static bool checkedAddInt32(int32_t left, int32_t right, int32_t* out);
};

} // namespace astock::domain::trading::execution_state


