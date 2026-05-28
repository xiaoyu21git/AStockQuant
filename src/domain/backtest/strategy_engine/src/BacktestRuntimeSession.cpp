#include "BacktestRuntimeSession.h"

namespace domain::backtest::strategy_engine {

BacktestRuntimeSession::BacktestRuntimeSession(RunId runId,
                                               const BacktestRequest& request,
                                               PortfolioState initialPortfolioState)
    : request_(request)
    , state_{runId,
             BacktestRunState::Created,
             request.window.startDay,
             request.costSpec.initialCapital,
             initialPortfolioState,
             {},
             {}}
{
}

const BacktestRequest& BacktestRuntimeSession::request() const
{
    return request_;
}

const BacktestRuntimeSessionState& BacktestRuntimeSession::state() const
{
    return state_;
}

BacktestRuntimeSessionState& BacktestRuntimeSession::state()
{
    return state_;
}

TradingDayIndex BacktestRuntimeSession::currentTradingDay() const
{
    return state_.currentTradingDay;
}

bool BacktestRuntimeSession::isActive() const
{
    return state_.runState == BacktestRunState::Running;
}

TimestampNs BacktestRuntimeSession::startedAt() const
{
    return startedAt_;
}

TimestampNs BacktestRuntimeSession::finishedAt() const
{
    return finishedAt_;
}

DurationNs BacktestRuntimeSession::elapsed() const
{
    return elapsed_;
}

void BacktestRuntimeSession::markRunning(TimestampNs startedAt)
{
    startedAt_ = startedAt;
    state_.runState = BacktestRunState::Running;
}

void BacktestRuntimeSession::advanceToDay(TradingDayIndex tradingDay)
{
    state_.currentTradingDay = tradingDay;
}

void BacktestRuntimeSession::replacePortfolioState(const PortfolioState& portfolioState)
{
    state_.portfolioState = portfolioState;
}

void BacktestRuntimeSession::appendEquityCurvePoint(const EquityCurvePoint& equityCurvePoint)
{
    state_.equityCurve.add(equityCurvePoint);
}

void BacktestRuntimeSession::appendLayerState(const LayerExecutionState& layerState)
{
    state_.layerStates.add(layerState);
}

void BacktestRuntimeSession::markSucceeded(TimestampNs finishedAt, DurationNs elapsed)
{
    finishedAt_ = finishedAt;
    elapsed_ = elapsed;
    state_.runState = BacktestRunState::Succeeded;
}

void BacktestRuntimeSession::markFailed(TimestampNs finishedAt, DurationNs elapsed)
{
    finishedAt_ = finishedAt;
    elapsed_ = elapsed;
    state_.runState = BacktestRunState::Failed;
}

void BacktestRuntimeSession::markCancelled(TimestampNs finishedAt, DurationNs elapsed)
{
    finishedAt_ = finishedAt;
    elapsed_ = elapsed;
    state_.runState = BacktestRunState::Cancelled;
}

} // namespace domain::backtest::strategy_engine