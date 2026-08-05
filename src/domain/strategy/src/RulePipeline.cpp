#include "../include/RulePipeline.h"

namespace domain::strategy {

std::vector<domain::trading::OrderRequest> RulePipeline::filterBuySignals(
    const std::vector<domain::trading::OrderRequest>& buyOrders,
    const std::function<void(rules::RuleCandidateContext& ctx,
                              const std::string& symbol)>& buildContext,
    rules::IRuleVariableProvider& provider) const
{
    if (!m_gate.enabled()) return buyOrders;  // 无规则绑定 → 全部通过

    std::vector<domain::trading::OrderRequest> result;
    result.reserve(buyOrders.size());
    for (const auto& order : buyOrders) {
        if (order.side() != domain::trading::OrderSide::Buy) {
            result.push_back(order);  // 卖单原样保留
            continue;
        }
        rules::RuleCandidateContext ctx;
        buildContext(ctx, order.symbol());
        if (m_gate.allowSignal(provider)) {
            result.push_back(order);
        }
    }
    return result;
}

std::vector<domain::trading::OrderRequest> RulePipeline::collectPositionExits(
    const std::vector<PositionExitInput>& positions,
    const std::function<void(rules::RuleCandidateContext& ctx,
                              const PositionExitInput& pos)>& buildContext,
    rules::IRuleVariableProvider& provider) const
{
    std::vector<domain::trading::OrderRequest> exits;
    if (!m_gate.enabled()) return exits;

    for (const auto& pos : positions) {
        if (pos.quantity <= 0) continue;
        rules::RuleCandidateContext ctx;
        buildContext(ctx, pos);
        const rules::RuleAction action = m_gate.positionAction(provider);
        if (action == rules::RuleAction::Exit || action == rules::RuleAction::Reduce) {
            domain::trading::OrderRequest exitOrder;
            exitOrder.setSymbol(pos.symbol);
            exitOrder.setSide(domain::trading::OrderSide::Sell);
            exitOrder.setQuantity(action == rules::RuleAction::Exit
                ? pos.quantity
                : (std::max)(static_cast<std::int64_t>(1), pos.quantity / 2));
            exitOrder.setOrderType(domain::trading::OrderType::Market);
            exitOrder.setPrice(pos.currentPrice);
            exits.push_back(std::move(exitOrder));
        }
    }
    return exits;
}

} // namespace domain::strategy
