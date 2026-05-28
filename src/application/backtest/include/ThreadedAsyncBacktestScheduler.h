#pragma once

#include "BacktestBatchTypes.h"
#include "BacktestEngineGateway.h"

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace application::backtest {

struct ThreadedAsyncBacktestTaskEntry;

using domain::backtest::strategy_engine::AsyncBacktestHandle;
using domain::backtest::strategy_engine::BacktestProgressSnapshot;
using domain::backtest::strategy_engine::CancellationRequest;

class ThreadedAsyncBacktestScheduler final : public domain::backtest::strategy_engine::IAsyncBacktestScheduler {
public:
    explicit ThreadedAsyncBacktestScheduler(const BacktestEngineGateway& engineGateway);
    ~ThreadedAsyncBacktestScheduler() override;

    [[nodiscard]] AsyncBacktestHandle submit(const BacktestRequest& request) override;
    [[nodiscard]] BacktestProgressSnapshot progress(const AsyncBacktestHandle& handle) const override;
    [[nodiscard]] std::optional<BacktestResultDto> tryCollect(const AsyncBacktestHandle& handle) override;
    void requestCancel(const CancellationRequest& request) override;

private:
    [[nodiscard]] std::shared_ptr<ThreadedAsyncBacktestTaskEntry> findTask(const AsyncBacktestHandle& handle) const;
    void runTask(const std::shared_ptr<ThreadedAsyncBacktestTaskEntry>& taskEntry);
    void finalizeTask(const std::shared_ptr<ThreadedAsyncBacktestTaskEntry>& taskEntry);

    const BacktestEngineGateway& engineGateway_;
    std::atomic<std::uint64_t> nextRunId_{1};
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<ThreadedAsyncBacktestTaskEntry>> tasks_;
};

} // namespace application::backtest