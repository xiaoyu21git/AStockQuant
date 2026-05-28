#pragma once

#include <optional>

#include "StrategyBacktestEngineFailure.h"
#include "StrategyBacktestEngineTypes.h"

namespace domain::backtest::strategy_engine {

enum class AsyncTaskState : std::uint8_t {
    Queued = 0,
    Running = 1,
    Succeeded = 2,
    Failed = 3,
    CancelRequested = 4,
    Cancelled = 5,
};

struct AsyncBacktestHandle final {
    RunId runId;

    [[nodiscard]] bool isValid() const
    {
        return runId.isValid();
    }
};

struct BacktestProgressSnapshot final {
    AsyncBacktestHandle handle;
    AsyncTaskState state{AsyncTaskState::Queued};
    TradingDayIndex currentTradingDay;
    CandidateCount completedTradingDays;
    CandidateCount totalTradingDays;
    Ratio completionRatio;
    std::optional<EngineFailureCode> failureCode;
    std::optional<Diagnostics> failureDiagnostics;

    [[nodiscard]] bool isValid() const
    {
        if (failureCode.has_value() != failureDiagnostics.has_value()) {
            return false;
        }

        if (failureDiagnostics.has_value() && !failureDiagnostics->isValid()) {
            return false;
        }

        return handle.isValid()
            && currentTradingDay.isValid()
            && completionRatio.isValid()
            && completedTradingDays.value() <= totalTradingDays.value();
    }
};

struct BacktestExecutionProgress final {
    TradingDayIndex currentTradingDay;
    CandidateCount completedTradingDays;
    CandidateCount totalTradingDays;
    Ratio completionRatio;

    [[nodiscard]] bool isValid() const
    {
        return currentTradingDay.isValid()
            && completionRatio.isValid()
            && completedTradingDays.value() <= totalTradingDays.value();
    }
};

struct CancellationRequest final {
    AsyncBacktestHandle handle;

    [[nodiscard]] bool isValid() const
    {
        return handle.isValid();
    }
};

} // namespace domain::backtest::strategy_engine