#pragma once

#include "RiskRuleChain.h"

namespace domain::trading {

class DefaultRiskRuleChain final : public RiskRuleChain {
public:
    [[nodiscard]] RiskDecision evaluate(const TradeIntentBatch& batch,
                                        const TradingSnapshot& snapshot,
                                        const TradingRiskProfile& profile) const override;
};

} // namespace domain::trading