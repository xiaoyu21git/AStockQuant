#pragma once
// ─────────────────────────────────────────────────────────────────────
// PositionAccountEngine — 持仓账户引擎 (纯 C++, 零 Qt, 零字符串比较)
// ─────────────────────────────────────────────────────────────────────

#include "TradingTypes.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace domain::trading {

enum class PositionSide : int { Long = 0, Short = 1 };
enum class PositionType : int { Stock = 0, MarginBuy, MarginSell, Futures, Options };
enum class TradeAction : int { None = 0, MarginSell, CloseShort };
enum class PositionEffect : int { Unspecified = 0, Open, Close };
enum class ExchangeCode : int { Unknown = 0, SHSE, SZSE, BSE, CFFEX, SHFE, DCE, CZCE, INE, GFEX };

class Position final {
public:
    Position() = default;

    [[nodiscard]] const std::string& symbol() const noexcept { return m_symbol; }
    void setSymbol(std::string v) { m_symbol = std::move(v); }

    [[nodiscard]] ExchangeCode exchange() const noexcept { return m_exchange; }
    void setExchange(ExchangeCode v) noexcept { m_exchange = v; }

    [[nodiscard]] PositionSide side() const noexcept { return m_side; }
    void setSide(PositionSide v) noexcept { m_side = v; }

    [[nodiscard]] PositionType type() const noexcept { return m_type; }
    void setType(PositionType v) noexcept { m_type = v; }

    [[nodiscard]] std::int64_t quantity() const noexcept { return m_quantity; }
    void setQuantity(std::int64_t v) noexcept { m_quantity = v; }

    [[nodiscard]] std::int64_t availableQuantity() const noexcept { return m_availableQuantity; }
    void setAvailableQuantity(std::int64_t v) noexcept { m_availableQuantity = v; }

    [[nodiscard]] std::int64_t closeableQuantity() const noexcept { return m_closeableQuantity; }
    void setCloseableQuantity(std::int64_t v) noexcept { m_closeableQuantity = v; }

    [[nodiscard]] double costBasis() const noexcept { return m_costBasis; }
    void setCostBasis(double v) noexcept { m_costBasis = v; }

    [[nodiscard]] double lastPrice() const noexcept { return m_lastPrice; }
    void setLastPrice(double v) noexcept { m_lastPrice = v; }

    [[nodiscard]] double marketValue() const noexcept { return m_marketValue; }
    void setMarketValue(double v) noexcept { m_marketValue = v; }

    [[nodiscard]] double unrealizedPnl() const noexcept { return m_unrealizedPnl; }
    void setUnrealizedPnl(double v) noexcept { m_unrealizedPnl = v; }

    [[nodiscard]] PositionEffect positionEffect() const noexcept { return m_positionEffect; }
    void setPositionEffect(PositionEffect v) noexcept { m_positionEffect = v; }

    [[nodiscard]] const std::string& underlying() const noexcept { return m_underlying; }
    void setUnderlying(std::string v) { m_underlying = std::move(v); }

    [[nodiscard]] const std::string& optionType() const noexcept { return m_optionType; }
    void setOptionType(std::string v) { m_optionType = std::move(v); }

    [[nodiscard]] const std::string& expiry() const noexcept { return m_expiry; }
    void setExpiry(std::string v) { m_expiry = std::move(v); }

private:
    std::string m_symbol;
    ExchangeCode m_exchange{ExchangeCode::Unknown};
    PositionSide m_side{PositionSide::Long};
    PositionType m_type{PositionType::Stock};
    std::int64_t m_quantity{0};
    std::int64_t m_availableQuantity{0};
    std::int64_t m_closeableQuantity{0};
    double m_costBasis{0.0};
    double m_lastPrice{0.0};
    double m_marketValue{0.0};
    double m_unrealizedPnl{0.0};
    PositionEffect m_positionEffect{PositionEffect::Unspecified};
    std::string m_underlying;
    std::string m_optionType;
    std::string m_expiry;
};

class AccountSnapshot final {
public:
    AccountSnapshot() = default;
    static AccountSnapshot createDefault(const std::string& accountId);

    [[nodiscard]] const std::string& accountId() const noexcept { return m_accountId; }
    void setAccountId(std::string v) { m_accountId = std::move(v); }

    [[nodiscard]] double availableCash() const noexcept { return m_availableCash; }
    void setAvailableCash(double v) noexcept { m_availableCash = v; }

    [[nodiscard]] double marketValue() const noexcept { return m_marketValue; }
    void setMarketValue(double v) noexcept { m_marketValue = v; }

    [[nodiscard]] double realizedPnl() const noexcept { return m_realizedPnl; }
    void setRealizedPnl(double v) noexcept { m_realizedPnl = v; }

    [[nodiscard]] double unrealizedPnl() const noexcept { return m_unrealizedPnl; }
    void setUnrealizedPnl(double v) noexcept { m_unrealizedPnl = v; }

    [[nodiscard]] double totalAsset() const noexcept { return m_totalAsset; }
    void setTotalAsset(double v) noexcept { m_totalAsset = v; }

    [[nodiscard]] double dailyTurnoverNotional() const noexcept { return m_dailyTurnoverNotional; }
    void setDailyTurnoverNotional(double v) noexcept { m_dailyTurnoverNotional = v; }
    void addDailyTurnoverNotional(double v) noexcept { m_dailyTurnoverNotional += v; }

    void recalculateTotals(double netMarketValue);

private:
    std::string m_accountId;
    double m_availableCash{1000000.0};
    double m_marketValue{0.0};
    double m_realizedPnl{0.0};
    double m_unrealizedPnl{0.0};
    double m_totalAsset{1000000.0};
    double m_dailyTurnoverNotional{0.0};
};

class TradeFillSummary final {
public:
    std::string symbol;
    OrderSide side{OrderSide::Buy};
    double fillPrice{0.0};
    std::int64_t fillQuantity{0};
    double fillNotional{0.0};
    PositionEffect positionEffect{PositionEffect::Unspecified};
    TradeAction action{TradeAction::None};
    ExchangeCode exchange{ExchangeCode::Unknown};
    std::string optionType;
    std::string underlying;
    std::string expiry;
};

class PositionAccountEngine final {
public:
    PositionAccountEngine();

    [[nodiscard]] const std::unordered_map<std::string, Position>& positions() const noexcept;
    [[nodiscard]] const AccountSnapshot& account() const noexcept;

    void applyTradeFill(const TradeFillSummary& fill);
    void applyPositionEvent(const std::string& symbol, const Position& pos);
    void applyAccountEvent(const AccountSnapshot& acc);
    void applyBrokerSnapshot(const std::vector<Position>& positions, const AccountSnapshot& acc);
    void reset();

    // ── Callback (for AppBootstrap to wire to bridge) ──
    using DataChangedCallback = std::function<void()>;
    void setOnDataChanged(DataChangedCallback cb) noexcept;

    [[nodiscard]] static PositionType resolvePositionType(
        PositionType rawType, ExchangeCode exchange,
        const std::string& optionType, const std::string& underlying,
        PositionSide existingSide);

    [[nodiscard]] static bool isMarginShortOpenFill(
        PositionType resolvedType, OrderSide fillSide,
        PositionEffect positionEffect, TradeAction action) noexcept;
    [[nodiscard]] static bool isMarginShortCoverFill(
        PositionType resolvedType, OrderSide fillSide,
        PositionEffect positionEffect, TradeAction action) noexcept;
    [[nodiscard]] static bool isFuturesExchange(ExchangeCode code) noexcept;

private:
    static double unrealizedPnlForPosition(const Position& pos) noexcept;

    std::unordered_map<std::string, Position> m_positions;
    AccountSnapshot m_account;
    mutable std::mutex m_mutex;
    DataChangedCallback m_onDataChanged;
};

} // namespace domain::trading