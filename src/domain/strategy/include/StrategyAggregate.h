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

    [[nodiscard]] bool isValid() const
    {
        return identity.isValid() && lifecycle.isValid() && spec.isValid();
    }
};

} // namespace domain::strategy