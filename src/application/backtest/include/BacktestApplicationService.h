#pragma once

#include "BacktestBatchTypes.h"
#include "BacktestEngineGateway.h"

namespace application::backtest {

using domain::backtest::strategy_engine::IAsyncBacktestScheduler;

class BacktestApplicationService final {
public:
    BacktestApplicationService(const BacktestEngineGateway& engineGateway,
                               IAsyncBacktestScheduler& asyncScheduler);

    [[nodiscard]] BacktestResultDto runInline(const BacktestRequest& request) const;
    [[nodiscard]] AsyncBacktestHandle run(const BacktestRequest& request);
    [[nodiscard]] AsyncBacktestHandleList runBatch(const BacktestBatchRequest& batchRequest);
    [[nodiscard]] BacktestProgressSnapshot progress(const AsyncBacktestHandle& handle) const;
    [[nodiscard]] std::optional<BacktestResultDto> tryCollect(const AsyncBacktestHandle& handle);

    void cancel(const CancellationRequest& request);

private:
    const BacktestEngineGateway& engineGateway_;
    IAsyncBacktestScheduler& asyncScheduler_;
};

} // namespace application::backtest
