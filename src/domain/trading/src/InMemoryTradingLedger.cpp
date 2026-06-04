#include "../include/InMemoryTradingLedger.h"

namespace domain::trading {
namespace {

std::string orderIdText(const OrderRef& orderRef)
{
    return orderRef.orderId.text();
}

int findPositionIndex(const std::vector<TradingPositionSnapshot>& positions,
                      const strategy::SymbolCode& symbol)
{
    for (size_t index = 0; index < positions.size(); ++index) {
        if (positions[index].symbol == symbol) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void recalculateAccountSnapshot(TradingSnapshot* snapshot)
{
    if (!snapshot) {
        return;
    }

    double marketValue = 0.0;
    for (const TradingPositionSnapshot& position : snapshot->positions) {
        marketValue += position.marketValue.value;
    }
    snapshot->account.marketValue.value = marketValue;
    snapshot->account.totalAsset.value = snapshot->account.availableCash.value + marketValue;
}

void removeOpenOrder(std::vector<DiagnosticRecord>* openOrders, const OrderRef& orderRef)
{
    if (!openOrders) {
        return;
    }

    const std::string targetOrderId = orderIdText(orderRef);
    if (targetOrderId.empty()) {
        return;
    }

    for (auto it = openOrders->begin(); it != openOrders->end(); ) {
        if (it->key == "orderId" && it->value == targetOrderId) {
            it = openOrders->erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace

InMemoryTradingLedger::InMemoryTradingLedger(const TradingSnapshot& initialSnapshot)
    : snapshot_(initialSnapshot)
{
}

void InMemoryTradingLedger::applyOrderAccepted(const AcceptedOrder& order)
{
    DiagnosticRecord rec;
    rec.key = "orderId";
    rec.value = order.orderRef.orderId.text();
    snapshot_.openOrders.push_back(rec);

    DiagnosticRecord recBatch;
    recBatch.key = "batchId";
    recBatch.value = order.orderRef.batchId.text();
    snapshot_.openOrders.push_back(recBatch);

    DiagnosticRecord recScope;
    recScope.key = "executionScopeId";
    recScope.value = order.orderRef.executionScopeId.text();
    snapshot_.openOrders.push_back(recScope);

    DiagnosticRecord recSymbol;
    recSymbol.key = "symbol";
    recSymbol.value = order.symbol.text();
    snapshot_.openOrders.push_back(recSymbol);

    DiagnosticRecord recQty;
    recQty.key = "quantity";
    recQty.value = std::to_string(order.quantity.value);
    snapshot_.openOrders.push_back(recQty);

    DiagnosticRecord recPrice;
    recPrice.key = "price";
    recPrice.value = std::to_string(order.acceptedPrice.value);
    snapshot_.openOrders.push_back(recPrice);
}

void InMemoryTradingLedger::applyFill(const FillEvent& fill)
{
    if (!fill.isValid()) {
        return;
    }

    const double notional = fill.fillPrice.value * static_cast<double>(fill.fillQuantity.value);
    const double commissionAmount = std::stod(diagnosticGet(fill.metadata, "commissionAmount", "0"));
    const double taxAmount = std::stod(diagnosticGet(fill.metadata, "taxAmount", "0"));
    const double totalFees = commissionAmount + taxAmount;
    const bool isBuy = fill.side == OrderSide::Buy || fill.side == OrderSide::BuyToCover;
    snapshot_.account.availableCash.value += isBuy ? -(notional + totalFees) : (notional - totalFees);
    if (fill.fillDate.isValid()) {
        snapshot_.account.tradingDate = fill.fillDate;
    }

    int positionIndex = findPositionIndex(snapshot_.positions, fill.symbol);
    if (positionIndex < 0) {
        TradingPositionSnapshot position;
        position.symbol = fill.symbol;
        snapshot_.positions.push_back(position);
        positionIndex = static_cast<int>(snapshot_.positions.size()) - 1;
    }

    TradingPositionSnapshot& position = snapshot_.positions[positionIndex];
    if (isBuy) {
        position.quantity.value += fill.fillQuantity.value;
    } else {
        position.quantity.value -= fill.fillQuantity.value;
        if (position.quantity.value < 0) {
            position.quantity.value = 0;
        }
    }
    position.lastPrice = fill.fillPrice;
    position.marketValue.value = fill.fillPrice.value * static_cast<double>(position.quantity.value);

    removeOpenOrder(&snapshot_.openOrders, fill.orderRef);
    recalculateAccountSnapshot(&snapshot_);
}

void InMemoryTradingLedger::applyCancel(const CancelEvent& cancel)
{
    removeOpenOrder(&snapshot_.openOrders, cancel.orderRef);
}

void InMemoryTradingLedger::applyPriceMark(const MarketPriceMark& mark)
{
    if (!mark.isValid()) {
        return;
    }

    const int positionIndex = findPositionIndex(snapshot_.positions, mark.symbol);
    if (positionIndex < 0) {
        if (mark.tradingDate.isValid()) {
            snapshot_.account.tradingDate = mark.tradingDate;
        }
        return;
    }

    TradingPositionSnapshot& position = snapshot_.positions[positionIndex];
    position.lastPrice = mark.price;
    position.marketValue.value = mark.price.value * static_cast<double>(position.quantity.value);
    if (mark.tradingDate.isValid()) {
        snapshot_.account.tradingDate = mark.tradingDate;
    }
    recalculateAccountSnapshot(&snapshot_);
}

TradingSnapshot InMemoryTradingLedger::snapshot() const
{
    return snapshot_;
}

} // namespace domain::trading