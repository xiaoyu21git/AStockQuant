#pragma once

#include "StrategySnapshotTypes.h"

namespace domain::strategy {

struct StrategyAggregate final {
    StrategyIdentity identity;
    StrategyMetadata metadata;
    StrategyLifecycle lifecycle;
    StrategyRuntimeProfile runtime;
    StrategySpec spec;
    StrategyPerformanceSummary performanceSummary;
    BacktestSnapshot latestBacktestSnapshot;
    QVector<BacktestHistoryEntry> backtestHistory;

    [[nodiscard]] bool isValid() const
    {
        return identity.isValid() && lifecycle.isValid() && spec.isValid();
    }
};

} // namespace domain::strategy