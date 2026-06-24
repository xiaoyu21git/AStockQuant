#include "PositionAccountEngine.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace domain::trading {

AccountSnapshot AccountSnapshot::createDefault(const std::string& accountId) {
    AccountSnapshot s;
    s.m_accountId = accountId.empty() ? "SIM_ACCOUNT" : accountId;
    s.m_availableCash = 1000000.0;
    return s;
}

void AccountSnapshot::recalculateTotals(double netMarketValue) {
    m_totalAsset = m_availableCash + netMarketValue;
}

PositionAccountEngine::PositionAccountEngine()
    : m_account(AccountSnapshot::createDefault("SIM_ACCOUNT")) {}

void PositionAccountEngine::setOnDataChanged(DataChangedCallback cb) noexcept {
    m_onDataChanged = std::move(cb);
}

const std::unordered_map<std::string, Position>&
PositionAccountEngine::positions() const noexcept { return m_positions; }

const AccountSnapshot& PositionAccountEngine::account() const noexcept { return m_account; }

void PositionAccountEngine::applyTradeFill(const TradeFillSummary& fill) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_positions.find(fill.symbol);
    Position pos = it != m_positions.end() ? it->second : Position{};
    if (pos.symbol().empty()) {
        pos.setSymbol(fill.symbol);
        pos.setExchange(fill.exchange);
    }

    const bool isBuy = (fill.side == OrderSide::Buy);
    const PositionEffect pe = fill.positionEffect;
    const TradeAction act = fill.action;

    PositionType resolvedType = pos.type();
    if (resolvedType == PositionType::Stock) {
        resolvedType = resolvePositionType(
            PositionType::Stock, fill.exchange,
            fill.optionType, fill.underlying, pos.side());
    }

    const bool shortOpen = isMarginShortOpenFill(resolvedType, fill.side, pe, act);
    const bool shortCover = isMarginShortCoverFill(resolvedType, fill.side, pe, act);

    const std::int64_t signedDelta = shortOpen
        ? fill.fillQuantity
        : shortCover
            ? -fill.fillQuantity
            : isBuy ? fill.fillQuantity : -fill.fillQuantity;

    const std::int64_t prevQty = pos.quantity();
    const std::int64_t newQty = std::max<std::int64_t>(0, prevQty + signedDelta);

    double newCostBasis = pos.costBasis();
    const bool openingExposureFill = shortOpen || (isBuy && !shortCover);
    if (openingExposureFill) {
        const double prevNotional = pos.costBasis() * static_cast<double>(prevQty);
        newCostBasis = newQty > 0
            ? (prevNotional + fill.fillNotional) / static_cast<double>(newQty) : 0.0;
    } else if (newQty <= 0) {
        newCostBasis = 0.0;
    }

    pos.setQuantity(newQty);
    pos.setAvailableQuantity(newQty);
    pos.setCloseableQuantity(newQty);
    pos.setCostBasis(newCostBasis);
    pos.setLastPrice(fill.fillPrice);
    pos.setMarketValue(static_cast<double>(newQty) * fill.fillPrice);
    pos.setType(resolvedType);

    if (resolvedType == PositionType::MarginSell && (shortOpen || pos.side() == PositionSide::Short)) {
        pos.setSide(PositionSide::Short);
    }

    pos.setUnrealizedPnl(unrealizedPnlForPosition(pos));

    if (newQty > 0) {
        m_positions[fill.symbol] = std::move(pos);
    } else {
        m_positions.erase(fill.symbol);
    }

    if (isBuy) {
        m_account.setAvailableCash(m_account.availableCash() - fill.fillNotional);
    } else {
        m_account.setAvailableCash(m_account.availableCash() + fill.fillNotional);
    }
    m_account.addDailyTurnoverNotional(fill.fillNotional);

    double netMarketValue = 0.0;
    double totalMarketValue = 0.0;
    double totalUnrealizedPnl = 0.0;
    for (const auto& [sym, p] : m_positions) {
        const double absMv = std::abs(p.marketValue());
        totalMarketValue += absMv;
        netMarketValue += (p.side() == PositionSide::Short) ? -absMv : absMv;
        totalUnrealizedPnl += p.unrealizedPnl();
    }
    m_account.setMarketValue(totalMarketValue);
    m_account.setUnrealizedPnl(totalUnrealizedPnl);
    m_account.recalculateTotals(netMarketValue);
}

void PositionAccountEngine::applyPositionEvent(const std::string& symbol, const Position& pos) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_positions[symbol] = pos;
    }
    if (m_onDataChanged) m_onDataChanged();
}

void PositionAccountEngine::applyAccountEvent(const AccountSnapshot& acc) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!acc.accountId().empty()) m_account.setAccountId(acc.accountId());
        m_account.setAvailableCash(acc.availableCash());
        m_account.setMarketValue(acc.marketValue());
        m_account.setRealizedPnl(acc.realizedPnl());
        m_account.setUnrealizedPnl(acc.unrealizedPnl());
        m_account.setTotalAsset(acc.totalAsset());
    }
    if (m_onDataChanged) m_onDataChanged();
}

void PositionAccountEngine::applyBrokerSnapshot(
    const std::vector<Position>& positions, const AccountSnapshot& acc) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_positions.clear();
    for (const auto& p : positions) m_positions[p.symbol()] = p;
    m_account = acc;
}

void PositionAccountEngine::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_positions.clear();
    m_account = AccountSnapshot::createDefault("SIM_ACCOUNT");
}

bool PositionAccountEngine::isFuturesExchange(ExchangeCode code) noexcept {
    switch (code) {
    case ExchangeCode::CFFEX:
    case ExchangeCode::SHFE:
    case ExchangeCode::DCE:
    case ExchangeCode::CZCE:
    case ExchangeCode::INE:
    case ExchangeCode::GFEX:
        return true;
    default:
        return false;
    }
}

PositionType PositionAccountEngine::resolvePositionType(
    PositionType rawType, ExchangeCode exchange,
    const std::string& optionType, const std::string& underlying,
    PositionSide /*existingSide*/) {

    if (rawType != PositionType::Stock) return rawType;
    if (!optionType.empty() || !underlying.empty()) return PositionType::Options;
    if (isFuturesExchange(exchange)) return PositionType::Futures;
    return PositionType::Stock;
}

bool PositionAccountEngine::isMarginShortOpenFill(
    PositionType resolvedType, OrderSide fillSide,
    PositionEffect positionEffect, TradeAction action) noexcept {

    return resolvedType == PositionType::MarginSell
        && fillSide == OrderSide::Sell
        && (positionEffect == PositionEffect::Open || action == TradeAction::MarginSell);
}

bool PositionAccountEngine::isMarginShortCoverFill(
    PositionType resolvedType, OrderSide fillSide,
    PositionEffect positionEffect, TradeAction action) noexcept {

    return resolvedType == PositionType::MarginSell
        && fillSide == OrderSide::Buy
        && (positionEffect == PositionEffect::Close || action == TradeAction::CloseShort);
}

double PositionAccountEngine::unrealizedPnlForPosition(const Position& pos) noexcept {
    if (pos.quantity() <= 0 || !std::isfinite(pos.costBasis()) || !std::isfinite(pos.lastPrice()))
        return 0.0;
    if (pos.side() == PositionSide::Short)
        return (pos.costBasis() - pos.lastPrice()) * static_cast<double>(pos.quantity());
    return (pos.lastPrice() - pos.costBasis()) * static_cast<double>(pos.quantity());
}

} // namespace domain::trading