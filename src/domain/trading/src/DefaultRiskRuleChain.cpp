#include "../include/DefaultRiskRuleChain.h"

namespace domain::trading {

RiskDecision DefaultRiskRuleChain::evaluate(const TradeIntentBatch& batch,
                                            const TradingSnapshot&,
                                            const TradingRiskProfile& profile) const
{
    RiskDecision decision;

    if (!batch.isValid()) {
        decision.type = RiskDecisionType::Block;
        decision.reasonCode = strategy::ReasonCode(std::string{"invalid_trade_batch"});
        decision.message = "交易批次为空或缺少有效目标";
        return decision;
    }

    if (!profile.isValid()) {
        decision.type = RiskDecisionType::Block;
        decision.reasonCode = strategy::ReasonCode(std::string{"invalid_risk_profile"});
        decision.message = "风险画像无效";
        return decision;
    }

    for (const TradeIntent& intent : batch.intents) {
        if (!intent.isValid()) {
            decision.type = RiskDecisionType::Block;
            decision.reasonCode = strategy::ReasonCode(std::string{"invalid_trade_intent"});
            decision.message = "存在无效交易意图";
            return decision;
        }
    }

    for (const TargetPosition& targetPosition : batch.targetPositions) {
        if (!targetPosition.isValid()) {
            decision.type = RiskDecisionType::Block;
            decision.reasonCode = strategy::ReasonCode(std::string{"invalid_target_position"});
            decision.message = "存在无效目标仓位";
            return decision;
        }
    }

    decision.type = RiskDecisionType::Pass;
    decision.reasonCode = strategy::ReasonCode(std::string{"pass"});
    decision.message = "通过默认风险规则链";
    return decision;
}

} // namespace domain::trading