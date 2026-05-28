#include "../include/DefaultTradingPlanner.h"

#include <QString>
#include <QSet>

namespace domain::trading {
namespace {

strategy::OrderId generatedOrderId()
{
    return strategy::OrderId(QString::fromStdString(foundation::utils::Uuid::generate_v4().to_string()));
}

strategy::BatchId generatedBatchId()
{
    return strategy::BatchId(QString::fromStdString(foundation::utils::Uuid::generate_v4().to_string()));
}

strategy::ExecutionScopeId generatedExecutionScopeId()
{
    return strategy::ExecutionScopeId(QString::fromStdString(foundation::utils::Uuid::generate_v4().to_string()));
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
                        const QVariantMap& metadata = {})
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
    plan->items.append(item);
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
        plan.items.append(item);
    }

    QSet<QString> targetedSymbols;
    for (const TargetPosition& target : batch.targetPositions) {
        if (!target.isValid() || !target.targetQuantity.isPositive()) {
            continue;
        }

        targetedSymbols.insert(target.symbol.text());
        const strategy::Quantity currentQuantity = currentPositionQuantity(snapshot, target.symbol);
        const qint64 delta = target.targetQuantity.value - currentQuantity.value;
        if (delta > 0) {
            QVariantMap metadata;
            metadata.insert(QStringLiteral("targetMode"), QStringLiteral("rebalance_delta"));
            metadata.insert(QStringLiteral("targetQuantity"), QVariant::fromValue(target.targetQuantity.value));
            metadata.insert(QStringLiteral("currentQuantity"), QVariant::fromValue(currentQuantity.value));
            appendPlannedOrder(&plan,
                               target.symbol,
                               OrderSide::Buy,
                               strategy::Quantity{delta},
                               target.referencePrice,
                               metadata);
        } else if (delta < 0) {
            QVariantMap metadata;
            metadata.insert(QStringLiteral("targetMode"), QStringLiteral("rebalance_delta"));
            metadata.insert(QStringLiteral("targetQuantity"), QVariant::fromValue(target.targetQuantity.value));
            metadata.insert(QStringLiteral("currentQuantity"), QVariant::fromValue(currentQuantity.value));
            appendPlannedOrder(&plan,
                               target.symbol,
                               OrderSide::Sell,
                               strategy::Quantity{-delta},
                               target.referencePrice,
                               metadata);
        }
    }

    if (!batch.targetPositions.isEmpty()) {
        for (const TradingPositionSnapshot& position : snapshot.positions) {
            if (!position.symbol.isValid() || !position.quantity.isPositive()) {
                continue;
            }
            if (targetedSymbols.contains(position.symbol.text())) {
                continue;
            }

            QVariantMap metadata;
            metadata.insert(QStringLiteral("targetMode"), QStringLiteral("rebalance_close"));
            metadata.insert(QStringLiteral("targetQuantity"), 0);
            metadata.insert(QStringLiteral("currentQuantity"), QVariant::fromValue(position.quantity.value));
            appendPlannedOrder(&plan,
                               position.symbol,
                               OrderSide::Sell,
                               position.quantity,
                               position.lastPrice,
                               metadata);
        }
    }

    if (plan.items.isEmpty()) {
        plan.diagnostics.insert(QStringLiteral("reason"), QStringLiteral("no_order_plan_items_generated"));
    }

    return plan;
}

} // namespace domain::trading