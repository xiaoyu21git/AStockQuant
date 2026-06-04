#include "../include/BacktestExecutionVenue.h"

namespace domain::trading {

namespace {

double adjustedFillPrice(const OrderPlanItem& item,
                         const TradingExecutionContext& context)
{
    const double price = item.limitPrice.value;
    const double slippageRate = context.costProfile.slippageRate.value;
    if (!std::isfinite(price) || price <= 0.0 || !std::isfinite(slippageRate) || slippageRate <= 0.0) {
        return price;
    }

    const bool isBuy = item.side == OrderSide::Buy || item.side == OrderSide::BuyToCover;
    return isBuy ? price * (1.0 + slippageRate) : price * (1.0 - slippageRate);
}

double commissionAmount(double notional,
                        const TradingExecutionContext& context)
{
    const double commissionRate = context.costProfile.commissionRate.value;
    if (!std::isfinite(notional) || notional <= 0.0 || !std::isfinite(commissionRate) || commissionRate <= 0.0) {
        return 0.0;
    }
    return notional * commissionRate;
}

double taxAmount(const OrderPlanItem& item,
                 double notional,
                 const TradingExecutionContext& context)
{
    const bool isSell = item.side == OrderSide::Sell || item.side == OrderSide::SellShort;
    const double taxRate = context.costProfile.taxRate.value;
    if (!isSell || !std::isfinite(notional) || notional <= 0.0 || !std::isfinite(taxRate) || taxRate <= 0.0) {
        return 0.0;
    }
    return notional * taxRate;
}

}

ExecutionVenueResult BacktestExecutionVenue::submit(const OrderPlan& plan,
                                                     const TradingExecutionContext& context)
{
    ExecutionVenueResult result;
    const DomainDate fillDate = context.window.startDate.isValid()
        ? context.window.startDate
        : DomainDate{};

    for (const OrderPlanItem& item : plan.items) {
        if (!item.isValid()) {
            continue;
        }

        const double fillPrice = adjustedFillPrice(item, context);
        const double notional = fillPrice * static_cast<double>(item.quantity.value);
        const double commission = commissionAmount(notional, context);
        const double tax = taxAmount(item, notional, context);
        DiagnosticMap executionMetadata = item.metadata;
        diagnosticSet(executionMetadata, "commissionAmount", std::to_string(commission));
        diagnosticSet(executionMetadata, "taxAmount", std::to_string(tax));
        diagnosticSet(executionMetadata, "grossNotional", std::to_string(notional));

        AcceptedOrder acceptedOrder;
        acceptedOrder.orderRef.orderId = item.plannedOrderId;
        acceptedOrder.orderRef.batchId = item.batchId;
        acceptedOrder.orderRef.executionScopeId = item.executionScopeId;
        acceptedOrder.symbol = item.symbol;
        acceptedOrder.side = item.side;
        acceptedOrder.quantity = item.quantity;
        acceptedOrder.acceptedPrice.value = fillPrice;
        acceptedOrder.metadata = executionMetadata;
        result.acceptedOrders.push_back(acceptedOrder);

        FillEvent fillEvent;
        fillEvent.orderRef = acceptedOrder.orderRef;
        fillEvent.symbol = item.symbol;
        fillEvent.side = item.side;
        fillEvent.fillQuantity = item.quantity;
        fillEvent.fillPrice.value = fillPrice;
        fillEvent.fillDate = fillDate;
        fillEvent.metadata = executionMetadata;
        result.fills.push_back(fillEvent);
    }

    if (result.acceptedOrders.empty()) {
        diagnosticSet(result.diagnostics, "reason", "no_orders_submitted_to_backtest_venue");
    }

    return result;
}

CancelResult BacktestExecutionVenue::cancel(const OrderRef& orderRef)
{
    CancelResult result;
    result.accepted = orderRef.isValid();
    result.message = result.accepted
        ? "回测撮合环境已接受撤单"
        : "撤单引用无效";
    return result;
}

std::vector<FillEvent> BacktestExecutionVenue::poll()
{
    return {};
}

} // namespace domain::trading