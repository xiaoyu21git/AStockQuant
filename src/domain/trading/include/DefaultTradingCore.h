#pragma once

#include "TradingCore.h"

#include <memory>

namespace domain::trading {

class TradingPlanner;
class TradingLedger;
class RiskRuleChain;
class ExecutionVenue;

class DefaultTradingCore final : public TradingCore {
public:
    DefaultTradingCore(std::shared_ptr<TradingPlanner> planner = {},
                       std::shared_ptr<TradingLedger> ledger = {},
                       std::shared_ptr<RiskRuleChain> riskRuleChain = {},
                       std::shared_ptr<ExecutionVenue> executionVenue = {});

    [[nodiscard]] ExecutionResult execute(const TradeIntentBatch& batch,
                                          const TradingExecutionContext& context) override;

    void applyFill(const FillEvent& fill) override;
    void markToMarket(const MarketPriceMark& mark) override;

    [[nodiscard]] TradingSnapshot snapshot() const override;

private:
    std::shared_ptr<TradingPlanner> planner_;
    std::shared_ptr<TradingLedger> ledger_;
    std::shared_ptr<RiskRuleChain> riskRuleChain_;
    std::shared_ptr<ExecutionVenue> executionVenue_;
};

} // namespace domain::trading