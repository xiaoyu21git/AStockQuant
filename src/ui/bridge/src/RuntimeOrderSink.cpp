#include "../include/RuntimeOrderSink.h"
#include "../../domain/trading/TradeExecutionEngine.h"

namespace bridge {

RuntimeOrderSink::RuntimeOrderSink(domain::trading::TradeExecutionEngine& engine)
    : engine_(engine)
{}

domain::strategy::StrategyServiceFlowResult RuntimeOrderSink::submit(
    const domain::strategy::OrderRequest& order)
{
    if (!order.isValid()) {
        return domain::strategy::StrategyServiceFlowResult(
            domain::strategy::StrategyServiceFlowCode::InvalidInput);
    }

    auto tradeOrder = engine_.buildTradeOrder(order);
    auto risk = engine_.buildRiskInput(tradeOrder);
    auto result = engine_.submitOrder(tradeOrder, risk);

    if (result.succeeded()) {
        return domain::strategy::StrategyServiceFlowResult(
            domain::strategy::StrategyServiceFlowCode::Ok);
    }
    return domain::strategy::StrategyServiceFlowResult(
        domain::strategy::StrategyServiceFlowCode::OrderSubmitFailed);
}

} // namespace bridge
