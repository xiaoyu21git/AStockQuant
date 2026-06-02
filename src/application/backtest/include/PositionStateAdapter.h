#pragma once

#include "BacktestInterfaces.hpp"

#include "../../../domain/trading/include/execution/PositionStateMachine.h"

namespace application::backtest {

class NetPositionStateEngineAdapter final : public IPositionStateEngine {
public:
    NetPositionStateEngineAdapter();

    [[nodiscard]] StageResult updatePositionState(RunContext& context) const override;

private:
    static constexpr int32_t kDefaultInstrumentId = 1;
    static constexpr int32_t kMinimumFillLots = 1;

    astock::domain::trading::execution_state::NetPositionStateMachine machine_;
};

} // namespace application::backtest