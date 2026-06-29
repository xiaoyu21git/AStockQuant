// ─────────────────────────────────────────────────────────────────────
// TradingTypes.cpp — OrderRequest + Order high-performance impl
// ─────────────────────────────────────────────────────────────────────
#include "TradingTypes.h"
#include "foundation/Utils/Timestamp.h"

#include <chrono>
#include <sstream>

namespace domain::trading {

// ══════════════════════════════════════════════════════════
// StrongId template
// ══════════════════════════════════════════════════════════
// (fully defined in header — nothing needed here)

// ══════════════════════════════════════════════════════════
// OrderRequest — getter/setter
// ══════════════════════════════════════════════════════════

const std::string& OrderRequest::clOrdId()    const noexcept { return m_clOrdId; }
void OrderRequest::setClOrdId(std::string v)            { m_clOrdId = std::move(v); }

const std::string& OrderRequest::accountId()  const noexcept { return m_accountId; }
void OrderRequest::setAccountId(std::string v)          { m_accountId = std::move(v); }

const std::string& OrderRequest::symbol()     const noexcept { return m_symbol; }
void OrderRequest::setSymbol(std::string v)             { m_symbol = std::move(v); }

const std::string& OrderRequest::exchange()   const noexcept { return m_exchange; }
void OrderRequest::setExchange(std::string v)           { m_exchange = std::move(v); }

const std::string& OrderRequest::strategyId() const noexcept { return m_strategyId; }
void OrderRequest::setStrategyId(std::string v)         { m_strategyId = std::move(v); }

OrderSide   OrderRequest::side()      const noexcept { return m_side; }
void OrderRequest::setSide(OrderSide v)       noexcept { m_side = v; }

OrderType   OrderRequest::orderType() const noexcept { return m_orderType; }
void OrderRequest::setOrderType(OrderType v)  noexcept { m_orderType = v; }

double OrderRequest::price()    const noexcept { return m_price; }
void OrderRequest::setPrice(double v)  noexcept { m_price = v; }

double OrderRequest::quantity() const noexcept { return m_quantity; }
void OrderRequest::setQuantity(double v) noexcept { m_quantity = v; }

PositionEffect OrderRequest::positionEffect() const noexcept { return m_positionEffect; }
void OrderRequest::setPositionEffect(PositionEffect v) noexcept { m_positionEffect = v; }

TimeInForce OrderRequest::timeInForce() const noexcept { return m_timeInForce; }
void OrderRequest::setTimeInForce(TimeInForce v) noexcept { m_timeInForce = v; }

const std::string& OrderRequest::expireTime() const noexcept { return m_expireTime; }
void OrderRequest::setExpireTime(std::string v) { m_expireTime = std::move(v); }

const std::string& OrderRequest::currency() const noexcept { return m_currency; }
void OrderRequest::setCurrency(std::string v)   { m_currency = std::move(v); }

double OrderRequest::displayQty() const noexcept { return m_displayQty; }
void OrderRequest::setDisplayQty(double v) noexcept { m_displayQty = v; }

double OrderRequest::minQty() const noexcept { return m_minQty; }
void OrderRequest::setMinQty(double v) noexcept { m_minQty = v; }

// ── 扩展槽 (O(4) 线性扫描, 无堆分配) ──
const std::array<ExtensionSlot, 4>& OrderRequest::extensions() const noexcept {
    return m_extensions;
}

ExtensionSlot* OrderRequest::findExtension(uint64_t key) noexcept {
    for (auto& slot : m_extensions) {
        if (slot.key == key) return &slot;
    }
    return nullptr;
}

const ExtensionSlot* OrderRequest::findExtension(uint64_t key) const noexcept {
    for (const auto& slot : m_extensions) {
        if (slot.key == key) return &slot;
    }
    return nullptr;
}

void OrderRequest::setExtension(uint64_t key, double v) noexcept {
    for (auto& slot : m_extensions) {
        if (slot.key == 0 || slot.key == key) {
            slot.key = key; slot.value = v; return;
        }
    }
}

void OrderRequest::setExtension(uint64_t key, std::int64_t v) noexcept {
    for (auto& slot : m_extensions) {
        if (slot.key == 0 || slot.key == key) {
            slot.key = key; slot.value = v; return;
        }
    }
}

void OrderRequest::setExtension(uint64_t key, uint64_t v) noexcept {
    for (auto& slot : m_extensions) {
        if (slot.key == 0 || slot.key == key) {
            slot.key = key; slot.value = v; return;
        }
    }
}

// ══════════════════════════════════════════════════════════
// OrderRequest — 核心方法
// ══════════════════════════════════════════════════════════

bool OrderRequest::isValid() const noexcept {
    return !m_symbol.empty() && m_quantity > 0;
}

std::string OrderRequest::toCanonicalString() const {
    std::ostringstream oss;
    oss << m_symbol << '|' << static_cast<int>(m_side) << '|'
        << static_cast<int>(m_orderType) << '|' << m_price << '|'
        << m_quantity << '|' << (m_currency.empty() ? "CNY" : m_currency);
    return oss.str();
}

bool OrderRequest::checkPriceSemantics(const OrderRequest& req,
                                        std::string& errDetail) noexcept {
    if (req.m_orderType == OrderType::Market || req.m_orderType == OrderType::Stop) {
        if (req.m_price != 0.0) {
            errDetail = "Market/Stop order must have price=0";
            return false;
        }
    } else {
        if (req.m_price <= 0.0) {
            errDetail = "Limit/StopLimit order must have price>0";
            return false;
        }
    }
    return true;
}

bool OrderRequest::checkQuantityPrecision(const OrderRequest& req,
                                           std::string& errDetail) noexcept {
    if (req.m_quantity <= 0) {
        errDetail = "quantity must be > 0";
        return false;
    }
    // 精度检查预留 — 若接入 TickSize/LotSize 配置表则在此查表
    (void)req;
    return true;
}

std::pair<bool, std::string> OrderRequest::validate(const OrderRequest& req) {
    if (req.m_symbol.empty())
        return {false, "symbol is empty"};
    if (req.m_quantity <= 0)
        return {false, "quantity must be > 0"};

    std::string err;
    if (!checkPriceSemantics(req, err)) return {false, err};
    if (!checkQuantityPrecision(req, err)) return {false, err};

    if (req.m_timeInForce == TimeInForce::GTD && req.m_expireTime.empty())
        return {false, "GTD order requires expireTime"};
    if (!req.m_clOrdId.empty() && req.m_clOrdId.size() < 8)
        return {false, "cl_ord_id too short (min 8 chars)"};

    return {true, ""};
}

size_t OrderRequest::validateBatch(const OrderRequest* reqs, size_t count,
                                    std::string* outErrors) {
    size_t fails = 0;
    for (size_t i = 0; i < count; ++i) {
        auto [ok, err] = validate(reqs[i]);
        if (!ok) {
            if (outErrors) outErrors[i] = std::move(err);
            ++fails;
        }
    }
    return fails;
}

// ══════════════════════════════════════════════════════════
// OrderRequest::Builder
// ══════════════════════════════════════════════════════════

std::pair<OrderRequest, std::string> OrderRequest::Builder::build() const {
    auto req = m_req;
    if (req.clOrdId().empty()) {
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        std::ostringstream oss;
        oss << std::hex << now;
        req.setClOrdId(oss.str());
    }
    auto [ok, err] = OrderRequest::validate(req);
    if (!ok) return {OrderRequest{}, err};
    return {std::move(req), ""};
}

// ══════════════════════════════════════════════════════════
// Order — 无锁状态机 + 编译期位图转换表
// ══════════════════════════════════════════════════════════

// compile-time transition bitmap: 8 states × 8 targets = 64 bits
constexpr bool Order::canTransition(uint8_t from, uint8_t to) noexcept {
    // clang-format off
    constexpr uint64_t T = 0
        | (1ULL << (0*8 + 1))                     // Pending(0)       → New(1)
        | (1ULL << (0*8 + 6))                     // Pending(0)       → Rejected(6)
        | (1ULL << (1*8 + 2)) | (1ULL << (1*8 + 3)) // New(1)         → Part(2), Filled(3)
        | (1ULL << (1*8 + 4)) | (1ULL << (1*8 + 6)) // New(1)         → PendingCancel(4), Rejected(6)
        | (1ULL << (1*8 + 7))                     // New(1)           → Expired(7)
        | (1ULL << (2*8 + 2)) | (1ULL << (2*8 + 3)) // Part(2)        → Part(2), Filled(3)
        | (1ULL << (2*8 + 4)) | (1ULL << (2*8 + 6)) // Part(2)        → PendingCancel(4), Rejected(6)
        | (1ULL << (2*8 + 7))                     // Part(2)          → Expired(7)
        | (1ULL << (4*8 + 5)) | (1ULL << (4*8 + 2)) // PendingCancel   → Cancelled(5), Part(2)
        | (1ULL << (4*8 + 3)) | (1ULL << (4*8 + 6)) // PendingCancel   → Filled(3), Rejected(6)
        ;
    // clang-format on
    return (T >> (from * 8 + to)) & 1ULL;
}

Order::Order(std::string orderId, std::string clOrdId, const OrderRequest& origin)
    : m_orderId(std::move(orderId))
    , m_clOrdId(std::move(clOrdId))
    , m_originRequest(origin)
    , m_createTime(foundation::utils::Timestamp::now())
    , m_lastUpdateTime(m_createTime)
{
    m_status.store(static_cast<uint8_t>(OrderStatusValue::Pending),
                   std::memory_order_release);
}

const std::string& Order::orderId()  const noexcept { return m_orderId; }
const std::string& Order::clOrdId()  const noexcept { return m_clOrdId; }
const OrderRequest& Order::originRequest() const noexcept { return m_originRequest; }
const std::string& Order::symbol()   const noexcept { return m_originRequest.symbol(); }
OrderSide Order::side()              const noexcept { return m_originRequest.side(); }
double Order::price()                const noexcept { return m_originRequest.price(); }
double Order::quantity()             const noexcept { return m_originRequest.quantity(); }

OrderStatusValue Order::status()  const noexcept {
    return static_cast<OrderStatusValue>(m_status.load(std::memory_order_acquire));
}
double Order::cumQty()    const noexcept { return m_cumQty; }
double Order::leavesQty() const noexcept { return m_originRequest.quantity() - m_cumQty; }
double Order::avgPx()     const noexcept { return m_avgPx; }
double Order::fee()       const noexcept { return m_fee; }
bool   Order::isFrozen()  const noexcept { return m_frozen; }

foundation::utils::Timestamp Order::createTime()     const noexcept { return m_createTime; }
foundation::utils::Timestamp Order::lastUpdateTime() const noexcept { return m_lastUpdateTime; }

bool Order::isTerminal() const noexcept {
    auto s = m_status.load(std::memory_order_acquire);
    return s >= static_cast<uint8_t>(OrderStatusValue::Filled);
}

void Order::transitionTo(OrderStatusValue target) noexcept {
    m_status.store(static_cast<uint8_t>(target), std::memory_order_release);
    m_lastUpdateTime = foundation::utils::Timestamp::now();
}

// ── applyFill: 纯算术, 零锁, noexcept ──

bool Order::applyFill(double fillQty, double fillPx, double tradeFee) noexcept {
    if (m_frozen) return false;
    if (fillQty <= 0.0 || fillPx <= 0.0) return false;

    auto cur = m_status.load(std::memory_order_acquire);
    if (cur >= static_cast<uint8_t>(OrderStatusValue::Filled)) return false;

    double newCum = m_cumQty + fillQty;
    if (newCum > m_originRequest.quantity() + 1e-12) return false;

    double totalVal = m_cumQty * m_avgPx + fillQty * fillPx;
    m_cumQty = newCum;
    m_avgPx  = (newCum > 0) ? (totalVal / newCum) : 0.0;
    m_fee   += tradeFee;

    double remaining = m_originRequest.quantity() - m_cumQty;
    transitionTo((remaining < 1e-12)
        ? OrderStatusValue::Filled
        : OrderStatusValue::PartiallyFilled);
    return true;
}

// ── tryCancel: CAS 无锁 ──

bool Order::tryCancel() noexcept {
    if (m_frozen) return false;
    uint8_t cur = m_status.load(std::memory_order_acquire);
    uint8_t n = static_cast<uint8_t>(OrderStatusValue::New);
    uint8_t p = static_cast<uint8_t>(OrderStatusValue::PartiallyFilled);
    if (cur != n && cur != p) return false;

    uint8_t target = static_cast<uint8_t>(OrderStatusValue::PendingCancel);
    return m_status.compare_exchange_strong(cur, target,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire);
}

void Order::confirmCancelled() noexcept {
    if (!m_frozen) transitionTo(OrderStatusValue::Cancelled);
}

void Order::confirmRejected(const std::string& /*reason*/) noexcept {
    if (!m_frozen) transitionTo(OrderStatusValue::Rejected);
}

void Order::markExpired() noexcept {
    if (m_frozen) return;
    auto cur = m_status.load(std::memory_order_acquire);
    if (canTransition(cur, static_cast<uint8_t>(OrderStatusValue::Expired)))
        transitionTo(OrderStatusValue::Expired);
}

void Order::freeze() noexcept {
    m_frozen = true;
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
