#pragma once

#include "TradingTypes.h"

namespace domain::trading {

class TradingPlanner {
public:
    virtual ~TradingPlanner() = default;

    [[nodiscard]] virtual OrderPlan buildOrderPlan(const TradeIntentBatch& batch,
                                                   const TradingSnapshot& snapshot,
                                                   const TradingExecutionContext& context) const = 0;
};

} // namespace domain::trading