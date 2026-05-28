#pragma once

namespace domain::backtest {

enum class StrategyBehaviorKind : int {
    TrendFollowing = 0,
    MeanReversion = 1,
    Momentum = 2,
    Arbitrage = 3,
    MultiFactor = 4,
    MachineLearning = 5,
    EventDriven = 6,
    HighFrequency = 7,
    Custom = 8
};

inline bool isValidStrategyBehaviorKind(int index)
{
    return index >= static_cast<int>(StrategyBehaviorKind::TrendFollowing)
    && index <= static_cast<int>(StrategyBehaviorKind::Custom);
}

} // namespace domain::backtest