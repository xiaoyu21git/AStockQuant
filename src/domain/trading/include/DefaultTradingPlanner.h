#pragma once

#include "TradingPlanner.h"

namespace domain::trading {

class DefaultTradingPlanner final : public TradingPlanner {
public:
    [[nodiscard]] OrderPlan buildOrderPlan(const TradeIntentBatch& batch,
                                           const TradingSnapshot& snapshot,
                                           const TradingExecutionContext& context) const override;
};

} // namespace domain::trading