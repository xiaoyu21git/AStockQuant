#pragma once

#include "StrategyBehaviorKind.h"

namespace domain::backtest {

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
    TrendFollowing = 0,
    MeanReversion = 1,
    Alpha = 2,
    Arbitrage = 3,
    HighFrequency = 4,
    Portfolio = 5,
    Custom = 6,
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

inline StrategyStoredType strategyStoredTypeForBehavior(StrategyBehaviorKind behaviorKind)
{
    switch (behaviorKind) {
    case StrategyBehaviorKind::TrendFollowing:
        return StrategyStoredType::TrendFollowing;
    case StrategyBehaviorKind::MeanReversion:
        return StrategyStoredType::MeanReversion;
    case StrategyBehaviorKind::Momentum:
    case StrategyBehaviorKind::MultiFactor:
    case StrategyBehaviorKind::MachineLearning:
        return StrategyStoredType::Alpha;
    case StrategyBehaviorKind::Arbitrage:
        return StrategyStoredType::Arbitrage;
    case StrategyBehaviorKind::HighFrequency:
        return StrategyStoredType::HighFrequency;
    case StrategyBehaviorKind::EventDriven:
    case StrategyBehaviorKind::Custom:
    default:
        return StrategyStoredType::Custom;
    }
}

inline ResolvedStrategyIdentity resolveStrategyStoredType(const int storedTypeIndex)
{
    switch (static_cast<StrategyStoredType>(storedTypeIndex)) {
    case StrategyStoredType::TrendFollowing:
    case StrategyStoredType::MeanReversion:
    case StrategyStoredType::Alpha:
    case StrategyStoredType::Arbitrage:
    case StrategyStoredType::HighFrequency:
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