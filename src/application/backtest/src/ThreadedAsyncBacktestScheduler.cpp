#include "../include/ThreadedAsyncBacktestScheduler.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <limits>
#include <vector>

namespace application::backtest {

namespace {

using domain::backtest::strategy_engine::AsyncTaskState;
using domain::backtest::strategy_engine::BacktestExecutionCallbacks;
using domain::backtest::strategy_engine::BacktestExecutionProgress;
using domain::backtest::strategy_engine::BacktestRunState;
using domain::backtest::strategy_engine::CandidateCount;
using domain::backtest::strategy_engine::EngineFailure;
using domain::backtest::strategy_engine::IBacktestCancellationObserver;
using domain::backtest::strategy_engine::IBacktestProgressSink;
using domain::backtest::strategy_engine::Ratio;
using domain::backtest::strategy_engine::RunId;

CandidateCount toCandidateCount(const std::size_t value)
{
    const auto boundedValue = std::min<std::size_t>(value, std::numeric_limits<std::uint32_t>::max());
    return CandidateCount(static_cast<std::uint32_t>(boundedValue));
}

CandidateCount totalTradingDays(const BacktestRequest& request)
{
    const auto days = static_cast<std::size_t>(request.window.endDay.value() - request.window.startDay.value() + 1);
    return toCandidateCount(days);
}

double completionRatioValue(const CandidateCount completedTradingDays, const CandidateCount totalTradingDays)
{
    if (!totalTradingDays.isPositive()) {
        return 0.0;
    }

    return static_cast<double>(completedTradingDays.value()) / static_cast<double>(totalTradingDays.value());
}

} // namespace

struct ThreadedAsyncBacktestTaskEntry final {
    explicit ThreadedAsyncBacktestTaskEntry(BacktestRequest submittedRequest)
        : request(std::move(submittedRequest))
    {
    }

    mutable std::mutex mutex;
    BacktestRequest request;
    BacktestProgressSnapshot progressSnapshot;
    std::optional<BacktestResultDto> result;
    std::exception_ptr failure;
    bool cancelRequested{false};
    std::future<void> future;
};

class TaskProgressSink final : public IBacktestProgressSink {
public:
    explicit TaskProgressSink(std::shared_ptr<ThreadedAsyncBacktestTaskEntry> taskEntry)
        : taskEntry_(std::move(taskEntry))
    {
    }

    void publish(const BacktestExecutionProgress& progress) override
    {
        std::scoped_lock lock(taskEntry_->mutex);
        taskEntry_->progressSnapshot.currentTradingDay = progress.currentTradingDay;
        taskEntry_->progressSnapshot.completedTradingDays = progress.completedTradingDays;
        taskEntry_->progressSnapshot.totalTradingDays = progress.totalTradingDays;
        taskEntry_->progressSnapshot.completionRatio = progress.completionRatio;
        if (taskEntry_->progressSnapshot.state == AsyncTaskState::Queued) {
            taskEntry_->progressSnapshot.state = AsyncTaskState::Running;
        }
    }

private:
    std::shared_ptr<ThreadedAsyncBacktestTaskEntry> taskEntry_;
};

class TaskCancellationObserver final : public IBacktestCancellationObserver {
public:
    explicit TaskCancellationObserver(std::shared_ptr<ThreadedAsyncBacktestTaskEntry> taskEntry)
        : taskEntry_(std::move(taskEntry))
    {
    }

    [[nodiscard]] bool isCancellationRequested() const override
    {
        std::scoped_lock lock(taskEntry_->mutex);
        return taskEntry_->cancelRequested;
    }

private:
    std::shared_ptr<ThreadedAsyncBacktestTaskEntry> taskEntry_;
};

ThreadedAsyncBacktestScheduler::ThreadedAsyncBacktestScheduler(const BacktestEngineGateway& engineGateway)
    : engineGateway_(engineGateway)
{
}

ThreadedAsyncBacktestScheduler::~ThreadedAsyncBacktestScheduler()
{
    std::vector<std::shared_ptr<ThreadedAsyncBacktestTaskEntry>> taskEntries;
    {
        std::scoped_lock lock(mutex_);
        for (const auto& [taskId, taskEntry] : tasks_) {
            (void)taskId;
            taskEntries.push_back(taskEntry);
        }
    }

    for (const std::shared_ptr<ThreadedAsyncBacktestTaskEntry>& taskEntry : taskEntries) {
        finalizeTask(taskEntry);
    }
}

AsyncBacktestHandle ThreadedAsyncBacktestScheduler::submit(const BacktestRequest& request)
{
    const AsyncBacktestHandle handle{RunId(nextRunId_.fetch_add(1, std::memory_order_relaxed))};
    auto taskEntry = std::make_shared<ThreadedAsyncBacktestTaskEntry>(request);
    taskEntry->progressSnapshot.handle = handle;
    taskEntry->progressSnapshot.state = AsyncTaskState::Queued;
    taskEntry->progressSnapshot.currentTradingDay = request.window.startDay;
    taskEntry->progressSnapshot.completedTradingDays = CandidateCount(0);
    taskEntry->progressSnapshot.totalTradingDays = totalTradingDays(request);
    taskEntry->progressSnapshot.completionRatio = Ratio(0.0);
    taskEntry->progressSnapshot.failureCode.reset();
    taskEntry->progressSnapshot.failureDiagnostics.reset();

    {
        std::scoped_lock lock(mutex_);
        tasks_.emplace(handle.runId.value(), taskEntry);
    }

    taskEntry->future = std::async(std::launch::async, [this, taskEntry]() {
        runTask(taskEntry);
    });
    return handle;
}

BacktestProgressSnapshot ThreadedAsyncBacktestScheduler::progress(const AsyncBacktestHandle& handle) const
{
    const std::shared_ptr<ThreadedAsyncBacktestTaskEntry> taskEntry = findTask(handle);
    std::scoped_lock lock(taskEntry->mutex);
    return taskEntry->progressSnapshot;
}

std::optional<BacktestResultDto> ThreadedAsyncBacktestScheduler::tryCollect(const AsyncBacktestHandle& handle)
{
    const std::shared_ptr<ThreadedAsyncBacktestTaskEntry> taskEntry = findTask(handle);
    finalizeTask(taskEntry);

    std::scoped_lock lock(taskEntry->mutex);
    if (taskEntry->progressSnapshot.state == AsyncTaskState::Failed && taskEntry->failure != nullptr) {
        std::rethrow_exception(taskEntry->failure);
    }

    if (taskEntry->progressSnapshot.state != AsyncTaskState::Succeeded
        && taskEntry->progressSnapshot.state != AsyncTaskState::Cancelled) {
        return std::nullopt;
    }

    return taskEntry->result;
}

void ThreadedAsyncBacktestScheduler::requestCancel(const CancellationRequest& request)
{
    const std::shared_ptr<ThreadedAsyncBacktestTaskEntry> taskEntry = findTask(request.handle);
    std::scoped_lock lock(taskEntry->mutex);
    taskEntry->cancelRequested = true;
    if (taskEntry->progressSnapshot.state == AsyncTaskState::Queued) {
        taskEntry->progressSnapshot.state = AsyncTaskState::Cancelled;
    } else if (taskEntry->progressSnapshot.state == AsyncTaskState::Running) {
        taskEntry->progressSnapshot.state = AsyncTaskState::CancelRequested;
    }
}

std::shared_ptr<ThreadedAsyncBacktestTaskEntry>
ThreadedAsyncBacktestScheduler::findTask(const AsyncBacktestHandle& handle) const
{
    std::scoped_lock lock(mutex_);
    const auto iterator = tasks_.find(handle.runId.value());
    if (iterator == tasks_.end()) {
        throw std::exception();
    }

    return iterator->second;
}

void ThreadedAsyncBacktestScheduler::runTask(const std::shared_ptr<ThreadedAsyncBacktestTaskEntry>& taskEntry)
{
    {
        std::scoped_lock lock(taskEntry->mutex);
        if (taskEntry->cancelRequested) {
            taskEntry->progressSnapshot.state = AsyncTaskState::Cancelled;
            return;
        }

        taskEntry->progressSnapshot.state = AsyncTaskState::Running;
    }

    try {
        TaskProgressSink progressSink(taskEntry);
        TaskCancellationObserver cancellationObserver(taskEntry);
        BacktestExecutionCallbacks callbacks;
        callbacks.progressSink = progressSink;
        callbacks.cancellationObserver = cancellationObserver;
        BacktestResultDto result = engineGateway_.execute(taskEntry->request, callbacks);

        std::scoped_lock lock(taskEntry->mutex);
        if (result.runMetadata.state == BacktestRunState::Cancelled) {
            taskEntry->progressSnapshot.state = AsyncTaskState::Cancelled;
            taskEntry->progressSnapshot.currentTradingDay = result.runMetadata.state == BacktestRunState::Cancelled
                ? taskEntry->progressSnapshot.currentTradingDay
                : taskEntry->request.window.endDay;
            taskEntry->progressSnapshot.failureCode.reset();
            taskEntry->progressSnapshot.failureDiagnostics.reset();
            taskEntry->result = std::move(result);
            return;
        }

        taskEntry->progressSnapshot.currentTradingDay = taskEntry->request.window.endDay;
        taskEntry->progressSnapshot.completedTradingDays = taskEntry->progressSnapshot.totalTradingDays;
        taskEntry->progressSnapshot.completionRatio = Ratio(1.0);
        taskEntry->progressSnapshot.failureCode.reset();
        taskEntry->progressSnapshot.failureDiagnostics.reset();
        if (taskEntry->cancelRequested) {
            taskEntry->progressSnapshot.state = AsyncTaskState::Cancelled;
            taskEntry->result.reset();
            return;
        }

        taskEntry->result = std::move(result);
        taskEntry->progressSnapshot.state = AsyncTaskState::Succeeded;
    } catch (const EngineFailure& failure) {
        std::scoped_lock lock(taskEntry->mutex);
        taskEntry->progressSnapshot.currentTradingDay = taskEntry->request.window.endDay;
        taskEntry->progressSnapshot.completedTradingDays = taskEntry->progressSnapshot.totalTradingDays;
        taskEntry->progressSnapshot.completionRatio = Ratio(completionRatioValue(taskEntry->progressSnapshot.completedTradingDays,
                                                                                 taskEntry->progressSnapshot.totalTradingDays));
        taskEntry->result.reset();
        taskEntry->failure = std::current_exception();
        taskEntry->progressSnapshot.failureCode = failure.code();
        taskEntry->progressSnapshot.failureDiagnostics = failure.diagnostics();
        taskEntry->progressSnapshot.state = AsyncTaskState::Failed;
    } catch (...) {
        std::scoped_lock lock(taskEntry->mutex);
        taskEntry->progressSnapshot.currentTradingDay = taskEntry->request.window.endDay;
        taskEntry->progressSnapshot.completedTradingDays = taskEntry->progressSnapshot.totalTradingDays;
        taskEntry->progressSnapshot.completionRatio = Ratio(completionRatioValue(taskEntry->progressSnapshot.completedTradingDays,
                                                                                 taskEntry->progressSnapshot.totalTradingDays));
        taskEntry->result.reset();
        taskEntry->failure = std::current_exception();
        taskEntry->progressSnapshot.failureCode.reset();
        taskEntry->progressSnapshot.failureDiagnostics.reset();
        taskEntry->progressSnapshot.state = AsyncTaskState::Failed;
    }
}

void ThreadedAsyncBacktestScheduler::finalizeTask(const std::shared_ptr<ThreadedAsyncBacktestTaskEntry>& taskEntry)
{
    if (!taskEntry->future.valid()) {
        return;
    }

    const auto status = taskEntry->future.wait_for(std::chrono::seconds(0));
    if (status == std::future_status::ready) {
        taskEntry->future.get();
    }
}

} // namespace application::backtest