// ─────────────────────────────────────────────────────────────────────
// TradingTypes.cpp — definitions for factory methods and non-inline
// getters/setters declared in TradingTypes.h
// ─────────────────────────────────────────────────────────────────────
#include "TradingTypes.h"
#include "foundation/Utils/Timestamp.h"

namespace domain::trading {

// ══════════════════════════════════════════════════════════
// StrongId template
// ══════════════════════════════════════════════════════════
// (fully defined in header — nothing needed here)

// ══════════════════════════════════════════════════════════
// OrderRequest
// ══════════════════════════════════════════════════════════
OrderRequest OrderRequest::create(StrategyId strategyId, SymbolCode symbol,
                                   OrderSide side, OrderType type,
                                   double price, std::int64_t quantity) {
    OrderRequest r;
    r.m_strategyId = std::move(strategyId);
    r.m_symbol = std::move(symbol);
    r.m_side = side;
    r.m_orderType = type;
    r.m_price = price;
    r.m_quantity = quantity;
    return r;
}

StrategyId OrderRequest::strategyId() const { return m_strategyId; }
void OrderRequest::setStrategyId(StrategyId value) { m_strategyId = std::move(value); }
SymbolCode OrderRequest::symbol() const { return m_symbol; }
void OrderRequest::setSymbol(SymbolCode value) { m_symbol = std::move(value); }
OrderSide OrderRequest::side() const { return m_side; }
void OrderRequest::setSide(OrderSide value) { m_side = value; }
OrderType OrderRequest::orderType() const { return m_orderType; }
void OrderRequest::setOrderType(OrderType value) { m_orderType = value; }
double OrderRequest::price() const { return m_price; }
void OrderRequest::setPrice(double value) { m_price = value; }
std::int64_t OrderRequest::quantity() const { return m_quantity; }
void OrderRequest::setQuantity(std::int64_t value) { m_quantity = value; }
CorrelationId OrderRequest::correlationId() const { return m_correlationId; }
void OrderRequest::setCorrelationId(CorrelationId value) { m_correlationId = std::move(value); }

bool OrderRequest::hasMetadata(const std::string& key) const {
    return m_metadata && m_metadata->count(key) > 0;
}
std::string OrderRequest::metadataValue(const std::string& key, const std::string& defaultValue) const {
    if (!m_metadata) return defaultValue;
    auto it = m_metadata->find(key);
    return it != m_metadata->end() ? it->second : defaultValue;
}
void OrderRequest::setMetadata(const std::string& key, std::string value) {
    if (!m_metadata) m_metadata = std::make_unique<std::unordered_map<std::string, std::string>>();
    (*m_metadata)[key] = std::move(value);
}
const std::unordered_map<std::string, std::string>* OrderRequest::metadata() const {
    return m_metadata.get();
}
std::size_t OrderRequest::metadataCount() const {
    return m_metadata ? m_metadata->size() : 0;
}

// ══════════════════════════════════════════════════════════
// OrderResult
// ══════════════════════════════════════════════════════════
OrderResult::OrderResult(bool ok, BrokerOrderId orderId, std::string error)
    : m_success(ok), m_brokerOrderId(std::move(orderId)), m_errorMessage(std::move(error)) {}

OrderResult OrderResult::success(BrokerOrderId brokerOrderId) {
    return OrderResult(true, std::move(brokerOrderId), "");
}
OrderResult OrderResult::failure(std::string errorMessage) {
    return OrderResult(false, BrokerOrderId{}, std::move(errorMessage));
}
bool OrderResult::succeeded() const { return m_success; }
BrokerOrderId OrderResult::brokerOrderId() const { return m_brokerOrderId; }
const std::string& OrderResult::errorMessage() const { return m_errorMessage; }

// ══════════════════════════════════════════════════════════
// OrderStatus
// ══════════════════════════════════════════════════════════
BrokerOrderId OrderStatus::brokerOrderId() const { return m_brokerOrderId; }
void OrderStatus::setBrokerOrderId(BrokerOrderId value) { m_brokerOrderId = std::move(value); }
CorrelationId OrderStatus::correlationId() const { return m_correlationId; }
void OrderStatus::setCorrelationId(CorrelationId value) { m_correlationId = std::move(value); }
OrderStatusValue OrderStatus::statusValue() const { return m_status; }
void OrderStatus::setStatusValue(OrderStatusValue value) { m_status = value; }
double OrderStatus::filledPrice() const { return m_filledPrice; }
void OrderStatus::setFilledPrice(double value) { m_filledPrice = value; }
std::int64_t OrderStatus::filledQuantity() const { return m_filledQuantity; }
void OrderStatus::setFilledQuantity(std::int64_t value) { m_filledQuantity = value; }
std::int64_t OrderStatus::remainingQuantity() const { return m_remainingQuantity; }
void OrderStatus::setRemainingQuantity(std::int64_t value) { m_remainingQuantity = value; }
foundation::utils::Timestamp OrderStatus::updateTime() const { return m_updateTime; }
void OrderStatus::setUpdateTime(foundation::utils::Timestamp value) { m_updateTime = value; }
const std::unordered_map<std::string, std::string>& OrderStatus::attributes() const { return m_attributes; }
void OrderStatus::setAttribute(const std::string& key, std::string value) { m_attributes[key] = std::move(value); }
std::string OrderStatus::attribute(const std::string& key, const std::string& defaultValue) const {
    auto it = m_attributes.find(key);
    return it != m_attributes.end() ? it->second : defaultValue;
}

// ══════════════════════════════════════════════════════════
// TradeFill
// ══════════════════════════════════════════════════════════
BrokerOrderId TradeFill::brokerOrderId() const { return m_brokerOrderId; }
void TradeFill::setBrokerOrderId(BrokerOrderId value) { m_brokerOrderId = std::move(value); }
FillId TradeFill::fillId() const { return m_fillId; }
void TradeFill::setFillId(FillId value) { m_fillId = std::move(value); }
double TradeFill::price() const { return m_price; }
void TradeFill::setPrice(double value) { m_price = value; }
std::int64_t TradeFill::quantity() const { return m_quantity; }
void TradeFill::setQuantity(std::int64_t value) { m_quantity = value; }
double TradeFill::commission() const { return m_commission; }
void TradeFill::setCommission(double value) { m_commission = value; }
foundation::utils::Timestamp TradeFill::tradeTime() const { return m_tradeTime; }
void TradeFill::setTradeTime(foundation::utils::Timestamp value) { m_tradeTime = value; }

// ══════════════════════════════════════════════════════════
// PositionSnapshot
// ══════════════════════════════════════════════════════════
SymbolCode PositionSnapshot::symbol() const { return m_symbol; }
void PositionSnapshot::setSymbol(SymbolCode value) { m_symbol = std::move(value); }
std::int64_t PositionSnapshot::longQuantity() const { return m_longQuantity; }
void PositionSnapshot::setLongQuantity(std::int64_t value) { m_longQuantity = value; }
std::int64_t PositionSnapshot::shortQuantity() const { return m_shortQuantity; }
void PositionSnapshot::setShortQuantity(std::int64_t value) { m_shortQuantity = value; }
double PositionSnapshot::averageCost() const { return m_averageCost; }
void PositionSnapshot::setAverageCost(double value) { m_averageCost = value; }
double PositionSnapshot::marketValue() const { return m_marketValue; }
void PositionSnapshot::setMarketValue(double value) { m_marketValue = value; }
double PositionSnapshot::unrealizedPnl() const { return m_unrealizedPnl; }
void PositionSnapshot::setUnrealizedPnl(double value) { m_unrealizedPnl = value; }

// ══════════════════════════════════════════════════════════
// AccountInfo
// ══════════════════════════════════════════════════════════
double AccountInfo::totalAssets() const { return m_totalAssets; }
void AccountInfo::setTotalAssets(double value) { m_totalAssets = value; }
double AccountInfo::availableCash() const { return m_availableCash; }
void AccountInfo::setAvailableCash(double value) { m_availableCash = value; }
double AccountInfo::frozenCash() const { return m_frozenCash; }
void AccountInfo::setFrozenCash(double value) { m_frozenCash = value; }
double AccountInfo::marketValue() const { return m_marketValue; }
void AccountInfo::setMarketValue(double value) { m_marketValue = value; }
double AccountInfo::totalPnl() const { return m_totalPnl; }
void AccountInfo::setTotalPnl(double value) { m_totalPnl = value; }

// ══════════════════════════════════════════════════════════
// BrokerCapability
// ══════════════════════════════════════════════════════════
BrokerName BrokerCapability::brokerName() const { return m_brokerName; }
void BrokerCapability::setBrokerName(BrokerName value) { m_brokerName = std::move(value); }
bool BrokerCapability::supportsAlgo() const { return m_supportsAlgo; }
void BrokerCapability::setSupportsAlgo(bool value) { m_supportsAlgo = value; }
bool BrokerCapability::supportsBasket() const { return m_supportsBasket; }
void BrokerCapability::setSupportsBasket(bool value) { m_supportsBasket = value; }
bool BrokerCapability::supportsConditional() const { return m_supportsConditional; }
void BrokerCapability::setSupportsConditional(bool value) { m_supportsConditional = value; }
bool BrokerCapability::supportsShortSelling() const { return m_supportsShortSelling; }
void BrokerCapability::setSupportsShortSelling(bool value) { m_supportsShortSelling = value; }
bool BrokerCapability::supportsFutures() const { return m_supportsFutures; }
void BrokerCapability::setSupportsFutures(bool value) { m_supportsFutures = value; }
bool BrokerCapability::supportsOptions() const { return m_supportsOptions; }
void BrokerCapability::setSupportsOptions(bool value) { m_supportsOptions = value; }
bool BrokerCapability::hasCapability(CapabilityId id) const {
    auto it = m_extendedCapabilities.find(id.text());
    return it != m_extendedCapabilities.end() && it->second;
}
void BrokerCapability::setCapability(CapabilityId id, bool supported) {
    m_extendedCapabilities[id.text()] = supported;
}

// ══════════════════════════════════════════════════════════
// AlgoOrderRequest
// ══════════════════════════════════════════════════════════
AlgoStrategyName AlgoOrderRequest::algoStrategy() const { return m_algoStrategy; }
void AlgoOrderRequest::setAlgoStrategy(AlgoStrategyName value) { m_algoStrategy = std::move(value); }
const OrderRequest& AlgoOrderRequest::baseOrder() const { return m_baseOrder; }
OrderRequest& AlgoOrderRequest::mutableBaseOrder() { return m_baseOrder; }
void AlgoOrderRequest::setBaseOrder(OrderRequest value) { m_baseOrder = std::move(value); }
bool AlgoOrderRequest::hasParameter(const std::string& key) const {
    return m_parameters.count(key) > 0;
}
std::string AlgoOrderRequest::parameter(const std::string& key, const std::string& defaultValue) const {
    auto it = m_parameters.find(key);
    return it != m_parameters.end() ? it->second : defaultValue;
}
void AlgoOrderRequest::setParameter(const std::string& key, std::string value) {
    m_parameters[key] = std::move(value);
}

// ══════════════════════════════════════════════════════════
// AlgoOrderResult
// ══════════════════════════════════════════════════════════
AlgoOrderResult::AlgoOrderResult(bool ok, BrokerOrderId orderId, AlgoStrategyName algo, std::string error)
    : m_success(ok), m_brokerOrderId(std::move(orderId)), m_algoId(std::move(algo)), m_errorMessage(std::move(error)) {}
AlgoOrderResult AlgoOrderResult::success(BrokerOrderId brokerOrderId, AlgoStrategyName algoId) {
    return AlgoOrderResult(true, std::move(brokerOrderId), std::move(algoId), "");
}
AlgoOrderResult AlgoOrderResult::failure(std::string errorMessage) {
    return AlgoOrderResult(false, BrokerOrderId{}, AlgoStrategyName{}, std::move(errorMessage));
}
bool AlgoOrderResult::succeeded() const { return m_success; }
BrokerOrderId AlgoOrderResult::brokerOrderId() const { return m_brokerOrderId; }
AlgoStrategyName AlgoOrderResult::algoId() const { return m_algoId; }
const std::string& AlgoOrderResult::errorMessage() const { return m_errorMessage; }

} // namespace domain::trading