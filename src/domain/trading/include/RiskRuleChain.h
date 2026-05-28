#pragma once

#include "TradingTypes.h"

namespace domain::trading {

class RiskRuleChain {
public:
    virtual ~RiskRuleChain() = default;

    [[nodiscard]] virtual RiskDecision evaluate(const TradeIntentBatch& batch,
                                                const TradingSnapshot& snapshot,
                                                const TradingRiskProfile& profile) const = 0;
};

} // namespace domain::trading