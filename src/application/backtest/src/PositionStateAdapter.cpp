#include "PositionStateAdapter.h"

namespace application::backtest {

NetPositionStateEngineAdapter::NetPositionStateEngineAdapter() = default;

StageResult NetPositionStateEngineAdapter::updatePositionState(RunContext& context) const
{
    StageResult result;
    result.stage = RunStage::UpdatePositionState;
    result.code = RunErrorCode::None;

    if (context.workingSet.filledOrderCount == 0U) {
        result.code = RunErrorCode::StageExecutionFailed;
        return result;
    }

    astock::domain::trading::execution_state::PositionState state;
    state.instrument.value = kDefaultInstrumentId;
    state.longQuantity.value = 0;
    state.shortQuantity.value = 0;

    astock::domain::trading::execution_state::ExecutionFill fill;
    fill.instrument.value = kDefaultInstrumentId;
    fill.side = astock::domain::trading::execution_state::FillSide::Buy;
    fill.quantity.value = kMinimumFillLots;

    for (std::uint32_t idx = 0U; idx < context.workingSet.filledOrderCount; ++idx) {
        const auto transition = machine_.apply(state, fill);
        if (!transition.ok()) {
            result.code = RunErrorCode::StageExecutionFailed;
            return result;
        }
        state = *transition.value;
    }

    context.workingSet.positionSnapshotCount =
        static_cast<std::uint32_t>(state.longQuantity.value + state.shortQuantity.value);
    if (context.workingSet.positionSnapshotCount == 0U) {
        result.code = RunErrorCode::StageExecutionFailed;
    }

    return result;
}

} // namespace application::backtest