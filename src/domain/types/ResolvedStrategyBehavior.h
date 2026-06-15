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

struct ResolvedStrategyBehavior {
    StrategyBehaviorKind kind{StrategyBehaviorKind::TrendFollowing};
    bool valid{false};

    constexpr int index() const noexcept
    {
        return static_cast<int>(kind);
    }
};

enum class StrategyStoredType : int {
    Unknown = -1,
    DOUBLE_MOVING_AVERAGE = 0,
    TURTLE_BREAKOUT = 1,
    BOLLINGER_BAND_MEAN_REVERSION = 2,
    RSI_MEAN_REVERSION = 3,
    MULTI_FACTOR_SELECTION = 4,
    EARNINGS_SURPRISE = 5,
    STATISTICAL_PAIR_TRADING = 6,
    RISK_PARITY_ALLOCATION = 7,
    MACHINE_LEARNING_SELECTION = 8,
    ORDER_FLOW_IMBALANCE = 9,
    VOLATILITY_SPREAD = 10,
    Portfolio = 11,
    Custom = 12,
};

struct ResolvedStrategyIdentity {
    StrategyStoredType storedType{StrategyStoredType::Unknown};
    ResolvedStrategyBehavior behavior;
    bool validStoredType{false};

    constexpr int storedTypeIndex() const noexcept
    {
        return static_cast<int>(storedType);
    }
};

inline ResolvedStrategyIdentity resolveStrategyStoredType(const int storedTypeIndex)
{
    switch (static_cast<StrategyStoredType>(storedTypeIndex)) {
    case StrategyStoredType::DOUBLE_MOVING_AVERAGE:
    case StrategyStoredType::TURTLE_BREAKOUT:
    case StrategyStoredType::BOLLINGER_BAND_MEAN_REVERSION:
    case StrategyStoredType::RSI_MEAN_REVERSION:
    case StrategyStoredType::MULTI_FACTOR_SELECTION:
    case StrategyStoredType::EARNINGS_SURPRISE:
    case StrategyStoredType::STATISTICAL_PAIR_TRADING:
    case StrategyStoredType::RISK_PARITY_ALLOCATION:
    case StrategyStoredType::MACHINE_LEARNING_SELECTION:
    case StrategyStoredType::ORDER_FLOW_IMBALANCE:
    case StrategyStoredType::VOLATILITY_SPREAD:
    case StrategyStoredType::Portfolio:
    case StrategyStoredType::Custom:
        return ResolvedStrategyIdentity{static_cast<StrategyStoredType>(storedTypeIndex), {}, true};
    case StrategyStoredType::Unknown:
    default:
        return {};
    }
}

inline ResolvedStrategyBehavior resolveStrategyBehavior(const int behaviorIndex)
{
    if (!isValidStrategyBehaviorKind(behaviorIndex)) {
        return {};
    }

    return ResolvedStrategyBehavior{static_cast<StrategyBehaviorKind>(behaviorIndex), true};
}

} // namespace domain::backtest