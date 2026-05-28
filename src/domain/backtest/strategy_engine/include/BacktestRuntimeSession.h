#pragma once

#include "StrategyBacktestEngineInterfaces.h"

namespace domain::backtest::strategy_engine {

class BacktestRuntimeSession final {
public:
    BacktestRuntimeSession(RunId runId,
                           const BacktestRequest& request,
                           PortfolioState initialPortfolioState);

    [[nodiscard]] const BacktestRequest& request() const;
    [[nodiscard]] const BacktestRuntimeSessionState& state() const;
    [[nodiscard]] BacktestRuntimeSessionState& state();
    [[nodiscard]] TradingDayIndex currentTradingDay() const;
    [[nodiscard]] bool isActive() const;
    [[nodiscard]] TimestampNs startedAt() const;
    [[nodiscard]] TimestampNs finishedAt() const;
    [[nodiscard]] DurationNs elapsed() const;

    void markRunning(TimestampNs startedAt);
    void advanceToDay(TradingDayIndex tradingDay);
    void replacePortfolioState(const PortfolioState& portfolioState);
    void appendEquityCurvePoint(const EquityCurvePoint& equityCurvePoint);
    void appendLayerState(const LayerExecutionState& layerState);
    void markSucceeded(TimestampNs finishedAt, DurationNs elapsed);
    void markFailed(TimestampNs finishedAt, DurationNs elapsed);
    void markCancelled(TimestampNs finishedAt, DurationNs elapsed);

private:
    BacktestRequest request_;
    BacktestRuntimeSessionState state_;
    TimestampNs startedAt_;
    TimestampNs finishedAt_;
    DurationNs elapsed_;
};

} // namespace domain::backtest::strategy_engine