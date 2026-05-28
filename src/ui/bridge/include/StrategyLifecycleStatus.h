#pragma once

#include <QVariant>

namespace strategy_view {

enum class StrategyLifecycleStatus : int {
    Unknown = -1,
    Draft = 0,
    Active = 1,
    Inactive = 2,
    Testing = 3,
    Archived = 4,
    Running = 5,
    Paused = 6,
    Stopped = 7,
};

inline StrategyLifecycleStatus strategyLifecycleStatusFromIndex(int statusIndex)
{
    switch (static_cast<StrategyLifecycleStatus>(statusIndex)) {
    case StrategyLifecycleStatus::Draft:
    case StrategyLifecycleStatus::Active:
    case StrategyLifecycleStatus::Inactive:
    case StrategyLifecycleStatus::Testing:
    case StrategyLifecycleStatus::Archived:
    case StrategyLifecycleStatus::Running:
    case StrategyLifecycleStatus::Paused:
    case StrategyLifecycleStatus::Stopped:
        return static_cast<StrategyLifecycleStatus>(statusIndex);
    case StrategyLifecycleStatus::Unknown:
    default:
        return StrategyLifecycleStatus::Unknown;
    }
}

inline int strategyLifecycleStatusIndex(StrategyLifecycleStatus status)
{
    return static_cast<int>(status);
}

inline StrategyLifecycleStatus resolveStrategyLifecycleStatus(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return StrategyLifecycleStatus::Unknown;
    }

    bool ok = false;
    const int statusIndex = value.toInt(&ok);
    if (ok) {
        return strategyLifecycleStatusFromIndex(statusIndex);
    }
    return StrategyLifecycleStatus::Unknown;
}

inline bool isKnownStrategyLifecycleStatus(StrategyLifecycleStatus status)
{
    return status != StrategyLifecycleStatus::Unknown;
}

} // namespace strategy_view