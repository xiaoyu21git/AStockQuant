#include "../include/DefaultTradingPlanner.h"

#include <unordered_set>

namespace domain::trading {
namespace {

strategy::OrderId generatedOrderId()
{
    return strategy::OrderId(foundation::utils::Uuid::generate_v4().to_string());
}

strategy::BatchId generatedBatchId()
{
    return strategy::BatchId(foundation::utils::Uuid::generate_v4().to_string());
}

strategy::ExecutionScopeId generatedExecutionScopeId()
{
    return strategy::ExecutionScopeId(foundation::utils::Uuid::generate_v4().to_string());
}

strategy::Quantity currentPositionQuantity(const TradingSnapshot& snapshot,
                                          const strategy::SymbolCode& symbol)
{
    for (const TradingPositionSnapshot& position : snapshot.positions) {
        if (position.symbol == symbol) {
            return position.quantity;
        }
    }
    return {};
}

void appendPlannedOrder(OrderPlan* plan,
                        const strategy::SymbolCode& symbol,
                        OrderSide side,
                        const strategy::Quantity& quantity,
                        const strategy::Money& price,
                        const DiagnosticMap& metadata = {})
{
    if (!plan || !symbol.isValid() || !quantity.isPositive() || !price.isFinite()) {
        return;
    }

    OrderPlanItem item;
    item.plannedOrderId = generatedOrderId();
    item.symbol = symbol;
    item.side = side;
    item.orderType = OrderType::Limit;
    item.quantity = quantity;
    item.limitPrice = price;
    item.batchId = generatedBatchId();
    item.executionScopeId = generatedExecutionScopeId();
    item.metadata = metadata;
    plan->items.push_back(item);
}

} // namespace

OrderPlan DefaultTradingPlanner::buildOrderPlan(const TradeIntentBatch& batch,
                                                const TradingSnapshot& snapshot,
                                                const TradingExecutionContext&) const
{
    OrderPlan plan;

    for (const TradeIntent& intent : batch.intents) {
        if (!intent.isValid()) {
            continue;
        }

        OrderPlanItem item;
        item.plannedOrderId = generatedOrderId();
        item.symbol = intent.symbol;
        item.side = intent.side;
        item.orderType = intent.orderType;
        item.quantity = intent.quantity;
        item.limitPrice = intent.referencePrice;
        item.batchId = generatedBatchId();
        item.executionScopeId = generatedExecutionScopeId();
        plan.items.push_back(item);
    }

    std::unordered_set<std::string> targetedSymbols;
    for (const TargetPosition& target : batch.targetPositions) {
        if (!target.isValid() || !target.targetQuantity.isPositive()) {
            continue;
        }

        targetedSymbols.insert(target.symbol.text());
        const strategy::Quantity currentQuantity = currentPositionQuantity(snapshot, target.symbol);
        const int64_t delta = target.targetQuantity.value - currentQuantity.value;
        if (delta > 0) {
            DiagnosticMap metadata;
            diagnosticSet(metadata, "targetMode", "rebalance_delta");
            diagnosticSet(metadata, "targetQuantity", std::to_string(target.targetQuantity.value));
            diagnosticSet(metadata, "currentQuantity", std::to_string(currentQuantity.value));
            appendPlannedOrder(&plan,
                               target.symbol,
                               OrderSide::Buy,
                               strategy::Quantity{delta},
                               target.referencePrice,
                               metadata);
        } else if (delta < 0) {
            DiagnosticMap metadata;
            diagnosticSet(metadata, "targetMode", "rebalance_delta");
            diagnosticSet(metadata, "targetQuantity", std::to_string(target.targetQuantity.value));
            diagnosticSet(metadata, "currentQuantity", std::to_string(currentQuantity.value));
            appendPlannedOrder(&plan,
                               target.symbol,
                               OrderSide::Sell,
                               strategy::Quantity{-delta},
                               target.referencePrice,
                               metadata);
        }
    }

    if (!batch.targetPositions.empty()) {
        for (const TradingPositionSnapshot& position : snapshot.positions) {
            if (!position.symbol.isValid() || !position.quantity.isPositive()) {
                continue;
            }
            if (targetedSymbols.count(position.symbol.text()) > 0) {
                continue;
            }

            DiagnosticMap metadata;
            diagnosticSet(metadata, "targetMode", "rebalance_close");
            diagnosticSet(metadata, "targetQuantity", "0");
            diagnosticSet(metadata, "currentQuantity", std::to_string(position.quantity.value));
            appendPlannedOrder(&plan,
                               position.symbol,
                               OrderSide::Sell,
                               position.quantity,
                               position.lastPrice,
                               metadata);
        }
    }

    if (plan.items.empty()) {
        diagnosticSet(plan.diagnostics, "reason", "no_order_plan_items_generated");
    }

    return plan;
}

} // namespace domain::trading