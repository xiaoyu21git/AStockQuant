// OrderBuilder.cpp — 统一订单构建器实现（复用 OrderRequest::Builder）
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

void OrderBuilder::fillCommon(OrderRequest::Builder& b, const std::string& symbol) {
    b.clOrdId(generateClOrdId())
     .strategyId(m_strategyId)
     .accountId(m_accountId)
     .currency(m_currency);
    // exchange 仅展示用，gmsdk 不消费
    auto dot = symbol.find('.');
    if (dot != std::string::npos) {
        b.exchange(symbol.substr(dot + 1));
    } else {
        auto so = foundation::market::AStockSymbol::fromCode(symbol);
        if (so.isValid() && !so.suffix().empty()) {
            b.symbol(so.fullSymbol()).exchange(so.suffix().substr(1));
        }
    }
}

OrderRequest OrderBuilder::buildStopOrder(const std::string& symbol, double price, int64_t quantity) {
    OrderRequest::Builder b;
    b.symbol(symbol).side(OrderSide::Sell).price(price)
     .quantity(static_cast<double>(quantity))
     .orderType(OrderType::Limit).positionEffect(PositionEffect::Close)
     .ext(ExtKey::kSignalScore, 0.8);
    fillCommon(b, symbol);
    auto [req, err] = b.build();
    if (!err.empty())
        INTERNAL_ERROR_STREAM << "[OrderBuilder] buildStopOrder failed: " << err;
    return req;
}

OrderRequest OrderBuilder::buildSignalOrder(const std::string& symbol, OrderSide side,
                                            double price, int64_t quantity, double signalScore) {
    OrderRequest::Builder b;
    b.symbol(symbol).side(side).price(price)
     .quantity(static_cast<double>(quantity))
     .orderType(OrderType::Market)
     .positionEffect(side == OrderSide::Buy ? PositionEffect::Open : PositionEffect::Close)
     .ext(ExtKey::kSignalScore, signalScore);
    fillCommon(b, symbol);
    auto [req, err] = b.build();
    if (!err.empty())
        INTERNAL_ERROR_STREAM << "[OrderBuilder] buildSignalOrder failed: " << err;
    return req;
}

OrderRequest OrderBuilder::buildManualOrder(const std::string& symbol, OrderSide side,
                                            double price, int64_t quantity, PositionEffect pe) {
    OrderRequest::Builder b;
    b.symbol(symbol).side(side).price(price)
     .quantity(static_cast<double>(quantity))
     .orderType(price > 0 ? OrderType::Limit : OrderType::Market).positionEffect(pe);
    fillCommon(b, symbol);
    auto [req, err] = b.build();
    if (!err.empty())
        INTERNAL_ERROR_STREAM << "[OrderBuilder] buildManualOrder failed: " << err;
    return req;
}

} // namespace domain::trading
