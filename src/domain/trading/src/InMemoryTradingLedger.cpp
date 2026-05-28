#include "../include/InMemoryTradingLedger.h"

#include <QString>

namespace domain::trading {
namespace {

QString orderIdText(const OrderRef& orderRef)
{
    return orderRef.orderId.text();
}

int findPositionIndex(const QVector<TradingPositionSnapshot>& positions,
                      const strategy::SymbolCode& symbol)
{
    for (int index = 0; index < positions.size(); ++index) {
        if (positions.at(index).symbol == symbol) {
            return index;
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

void removeOpenOrder(QVariantList* openOrders, const OrderRef& orderRef)
{
    if (!openOrders) {
        return;
    }

    const QString targetOrderId = orderIdText(orderRef);
    if (targetOrderId.isEmpty()) {
        return;
    }

    for (int index = openOrders->size() - 1; index >= 0; --index) {
        const QVariantMap orderRecord = openOrders->at(index).toMap();
        if (orderRecord.value(QStringLiteral("orderId")).toString().trimmed() == targetOrderId) {
            openOrders->removeAt(index);
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
    QVariantMap orderRecord;
    orderRecord.insert(QStringLiteral("orderId"), order.orderRef.orderId.text());
    orderRecord.insert(QStringLiteral("batchId"), order.orderRef.batchId.text());
    orderRecord.insert(QStringLiteral("executionScopeId"), order.orderRef.executionScopeId.text());
    orderRecord.insert(QStringLiteral("symbol"), order.symbol.text());
    orderRecord.insert(QStringLiteral("quantity"), QVariant::fromValue(order.quantity.value));
    orderRecord.insert(QStringLiteral("price"), order.acceptedPrice.value);
    orderRecord.insert(QStringLiteral("metadata"), order.metadata);
    snapshot_.openOrders.append(orderRecord);
}

void InMemoryTradingLedger::applyFill(const FillEvent& fill)
{
    if (!fill.isValid()) {
        return;
    }

    const double notional = fill.fillPrice.value * static_cast<double>(fill.fillQuantity.value);
    const double commissionAmount = fill.metadata.value(QStringLiteral("commissionAmount")).toDouble();
    const double taxAmount = fill.metadata.value(QStringLiteral("taxAmount")).toDouble();
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
        snapshot_.positions.append(position);
        positionIndex = snapshot_.positions.size() - 1;
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