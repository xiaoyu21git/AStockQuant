#pragma once

#include "../../../domain/backtest/strategy_engine/include/StrategyBacktestEngineInterfaces.h"

namespace application::backtest {

using domain::backtest::strategy_engine::AsyncBacktestHandle;
using domain::backtest::strategy_engine::BacktestProgressSnapshot;
using domain::backtest::strategy_engine::BacktestRequest;
using domain::backtest::strategy_engine::BacktestResultDto;
using domain::backtest::strategy_engine::CancellationRequest;
using domain::backtest::strategy_engine::ObjectList;

struct BacktestBatchRequest final {
    ObjectList<BacktestRequest> requests;

    [[nodiscard]] bool isValid() const
    {
        if (requests.empty()) {
            return false;
        }

        for (const BacktestRequest& request : requests) {
            if (!request.isValid()) {
                return false;
            }
        }

        return true;
    }
};

using AsyncBacktestHandleList = ObjectList<AsyncBacktestHandle>;

} // namespace application::backtest