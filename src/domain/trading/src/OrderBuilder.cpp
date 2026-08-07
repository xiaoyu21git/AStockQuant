// OrderBuilder.cpp — 统一订单构建器实现（复用 OrderRequest::Builder）
// v2: 实现 IOrderBuilder，完全无状态 — 线程安全
#include "OrderBuilder.h"
#include "foundation/market/AStockSymbol.h"
#include "foundation/log/logging.hpp"

#include <atomic>
#include <chrono>
#include <sstream>

namespace domain::trading {

std::string OrderBuilder::generateClOrdId() {
    static std::atomic<uint64_t> s_counter{0};
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    uint64_t seq = s_counter.fetch_add(1, std::memory_order_relaxed);
    std::ostringstream oss;
    oss << std::hex << now << "_" << seq;
    return oss.str();
}

void OrderBuilder::fillCommon(OrderRequest::Builder& b, const std::string& symbol,
                               const std::string& strategyId, const std::string& accountId) {
    b.clOrdId(generateClOrdId())
     .strategyId(strategyId)
     .accountId(accountId)
     .currency(m_currency);
    // exchange 仅展示用，gmsdk 不消费
    auto sym = foundation::market::AStockSymbol::fromString(symbol);
    if (sym.isValid() && !sym.suffix().empty()) {
        b.symbol(sym.fullSymbol()).exchange(sym.suffix().substr(1));
    }
}

// ── IOrderBuilder::build() 纯虚实现 ──

OrderRequest OrderBuilder::build(const OrderSpec& spec,
                                  const std::string& strategyId,
                                  const std::string& accountId) {
    OrderRequest::Builder b;
    b.symbol(spec.symbol).side(spec.side).price(spec.price)
     .quantity(static_cast<double>(spec.quantity))
     .orderType(spec.orderType).positionEffect(spec.positionEffect)
     .ext(ExtKey::kSignalScore, spec.signalScore)
     .ext(ExtKey::kTargetWeight, spec.targetWeight);
    // 透传扩展字段
    for (const auto& [key, val] : spec.extensions)
        b.ext(static_cast<uint64_t>(key), val);
    fillCommon(b, spec.symbol, strategyId, accountId);
    auto [req, err] = b.build();
    if (!err.empty())
        INTERNAL_ERROR_STREAM << "[OrderBuilder] build(OrderSpec) failed: " << err;
    return req;
}

// ── 旧兼容方法（追加 strategyId/accountId） ──

OrderRequest OrderBuilder::buildStopOrder(const std::string& symbol, double price,
                                          std::int64_t quantity,
                                          const std::string& strategyId,
                                          const std::string& accountId) {
    OrderRequest::Builder b;
    b.symbol(symbol).side(OrderSide::Sell).price(price)
     .quantity(static_cast<double>(quantity))
     .orderType(OrderType::Limit).positionEffect(PositionEffect::Close)
     .ext(ExtKey::kSignalScore, 0.8);
    fillCommon(b, symbol, strategyId, accountId);
    auto [req, err] = b.build();
    if (!err.empty())
        INTERNAL_ERROR_STREAM << "[OrderBuilder] buildStopOrder failed: " << err;
    return req;
}

OrderRequest OrderBuilder::buildSignalOrder(const std::string& symbol, OrderSide side,
                                            double price, std::int64_t quantity,
                                            double signalScore,
                                            const std::string& strategyId,
                                            const std::string& accountId) {
    OrderRequest::Builder b;
    b.symbol(symbol).side(side).price(price)
     .quantity(static_cast<double>(quantity))
     .orderType(OrderType::Market)
     .positionEffect(side == OrderSide::Buy ? PositionEffect::Open : PositionEffect::Close)
     .ext(ExtKey::kSignalScore, signalScore);
    fillCommon(b, symbol, strategyId, accountId);
    auto [req, err] = b.build();
    if (!err.empty())
        INTERNAL_ERROR_STREAM << "[OrderBuilder] buildSignalOrder failed: " << err;
    return req;
}

OrderRequest OrderBuilder::buildManualOrder(const std::string& symbol, OrderSide side,
                                            double price, std::int64_t quantity,
                                            PositionEffect pe,
                                            const std::string& strategyId,
                                            const std::string& accountId) {
    OrderRequest::Builder b;
    b.symbol(symbol).side(side).price(price)
     .quantity(static_cast<double>(quantity))
     .orderType(OrderType::Limit).positionEffect(pe);
    fillCommon(b, symbol, strategyId, accountId);
    auto [req, err] = b.build();
    if (!err.empty())
        INTERNAL_ERROR_STREAM << "[OrderBuilder] buildManualOrder failed: " << err;
    return req;
}

} // namespace domain::trading
