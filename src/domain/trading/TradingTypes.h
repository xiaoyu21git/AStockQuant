#pragma once

#include "foundation/Utils/Timestamp.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace domain {
namespace trading {

// ============================================================
// 强类型 ID 模板 (纯 C++, 零 Qt)
// 避免字符串堆砌, 获得编译期类型安全
// ============================================================

template <typename Tag>
class StrongId final {
public:
    StrongId() = default;
    explicit StrongId(std::string value) : m_value(std::move(value)) {}
    explicit StrongId(const char* value) : m_value(value ? value : "") {}

    [[nodiscard]] const std::string& text() const noexcept { return m_value; }
    [[nodiscard]] bool empty() const noexcept { return m_value.empty(); }
    [[nodiscard]] bool valid() const noexcept { return !m_value.empty(); }

    [[nodiscard]] friend bool operator==(const StrongId& lhs, const StrongId& rhs) noexcept
    {
        return lhs.m_value == rhs.m_value;
    }

    [[nodiscard]] friend bool operator!=(const StrongId& lhs, const StrongId& rhs) noexcept
    {
        return lhs.m_value != rhs.m_value;
    }

private:
    std::string m_value;
};

// ============================================================
// 类型标签
// ============================================================

struct StrategyIdTag;
struct SymbolCodeTag;
struct BrokerOrderIdTag;
struct CorrelationIdTag;
struct FillIdTag;
struct AlgoStrategyNameTag;
struct BrokerNameTag;
struct CapabilityIdTag;

// ============================================================
// 强类型别名 — 语义明确, 禁止混用
// ============================================================

using StrategyId = StrongId<StrategyIdTag>;             // 策略ID
using SymbolCode = StrongId<SymbolCodeTag>;              // 股票代码 e.g. "000001.SZ"
using BrokerOrderId = StrongId<BrokerOrderIdTag>;        // 券商订单号
using CorrelationId = StrongId<CorrelationIdTag>;        // 策略内部追踪ID
using FillId = StrongId<FillIdTag>;                      // 成交流水号
using AlgoStrategyName = StrongId<AlgoStrategyNameTag>;  // 算法名 e.g. "TWAP"
using BrokerName = StrongId<BrokerNameTag>;              // 券商名称 e.g. "jujin"
using CapabilityId = StrongId<CapabilityIdTag>;          // 能力ID e.g. "algo_twap"

// ============================================================
// 枚举
// ============================================================

enum class OrderSide { Buy, Sell };

enum class OrderType { Limit, Market };

enum class OrderStatusValue {
    Pending,
    Submitted,
    PartiallyFilled,
    Filled,
    Cancelled,
    Rejected,
    Expired
};

// ============================================================
// 订单请求
// ============================================================

class OrderRequest {
public:
    OrderRequest() = default;

    // 工厂方法
    static OrderRequest create(
        StrategyId strategyId,
        SymbolCode symbol,
        OrderSide side,
        OrderType type,
        double price,
        std::int64_t quantity);

    // -- 基础属性 --
    StrategyId strategyId() const;
    void setStrategyId(StrategyId value);

    SymbolCode symbol() const;
    void setSymbol(SymbolCode value);

    OrderSide side() const;
    void setSide(OrderSide value);

    OrderType orderType() const;
    void setOrderType(OrderType value);

    double price() const;
    void setPrice(double value);

    std::int64_t quantity() const;
    void setQuantity(std::int64_t value);

    CorrelationId correlationId() const;
    void setCorrelationId(CorrelationId value);

    // -- metadata (券商特有参数, 惰性分配) --
    bool hasMetadata(const std::string& key) const;
    std::string metadataValue(const std::string& key, const std::string& defaultValue = "") const;
    void setMetadata(const std::string& key, std::string value);
    const std::unordered_map<std::string, std::string>* metadata() const;
    std::size_t metadataCount() const;

private:
    StrategyId m_strategyId;
    SymbolCode m_symbol;
    OrderSide m_side{OrderSide::Buy};
    OrderType m_orderType{OrderType::Limit};
    double m_price{0.0};
    std::int64_t m_quantity{0};
    CorrelationId m_correlationId;
    std::unique_ptr<std::unordered_map<std::string, std::string>> m_metadata;
};

// ============================================================
// 订单结果
// ============================================================

class OrderResult {
public:
    static OrderResult success(BrokerOrderId brokerOrderId);
    static OrderResult failure(std::string errorMessage);

    bool succeeded() const;
    BrokerOrderId brokerOrderId() const;
    const std::string& errorMessage() const;

private:
    OrderResult(bool ok, BrokerOrderId orderId, std::string error);
    bool m_success{false};
    BrokerOrderId m_brokerOrderId;
    std::string m_errorMessage;
};

// ============================================================
// 订单状态 (异步回调)
// ============================================================

class OrderStatus {
public:
    BrokerOrderId brokerOrderId() const;
    void setBrokerOrderId(BrokerOrderId value);

    CorrelationId correlationId() const;
    void setCorrelationId(CorrelationId value);

    OrderStatusValue statusValue() const;
    void setStatusValue(OrderStatusValue value);

    double filledPrice() const;
    void setFilledPrice(double value);

    std::int64_t filledQuantity() const;
    void setFilledQuantity(std::int64_t value);

    std::int64_t remainingQuantity() const;
    void setRemainingQuantity(std::int64_t value);

    foundation::utils::Timestamp updateTime() const;
    void setUpdateTime(foundation::utils::Timestamp value);

    const std::unordered_map<std::string, std::string>& attributes() const;
    void setAttribute(const std::string& key, std::string value);
    std::string attribute(const std::string& key, const std::string& defaultValue = "") const;

private:
    BrokerOrderId m_brokerOrderId;
    CorrelationId m_correlationId;
    OrderStatusValue m_status{OrderStatusValue::Pending};
    double m_filledPrice{0.0};
    std::int64_t m_filledQuantity{0};
    std::int64_t m_remainingQuantity{0};
    foundation::utils::Timestamp m_updateTime;
    std::unordered_map<std::string, std::string> m_attributes;
};

// ============================================================
// 成交回报
// ============================================================

class TradeFill {
public:
    BrokerOrderId brokerOrderId() const;
    void setBrokerOrderId(BrokerOrderId value);

    FillId fillId() const;
    void setFillId(FillId value);

    double price() const;
    void setPrice(double value);

    std::int64_t quantity() const;
    void setQuantity(std::int64_t value);

    double commission() const;
    void setCommission(double value);

    foundation::utils::Timestamp tradeTime() const;
    void setTradeTime(foundation::utils::Timestamp value);

private:
    BrokerOrderId m_brokerOrderId;
    FillId m_fillId;
    double m_price{0.0};
    std::int64_t m_quantity{0};
    double m_commission{0.0};
    foundation::utils::Timestamp m_tradeTime;
};

// ============================================================
// 持仓快照
// ============================================================

class PositionSnapshot {
public:
    SymbolCode symbol() const;
    void setSymbol(SymbolCode value);

    std::int64_t longQuantity() const;
    void setLongQuantity(std::int64_t value);

    std::int64_t shortQuantity() const;
    void setShortQuantity(std::int64_t value);

    double averageCost() const;
    void setAverageCost(double value);

    double marketValue() const;
    void setMarketValue(double value);

    double unrealizedPnl() const;
    void setUnrealizedPnl(double value);

private:
    SymbolCode m_symbol;
    std::int64_t m_longQuantity{0};
    std::int64_t m_shortQuantity{0};
    double m_averageCost{0.0};
    double m_marketValue{0.0};
    double m_unrealizedPnl{0.0};
};

// ============================================================
// 账户信息
// ============================================================

class AccountInfo {
public:
    double totalAssets() const;
    void setTotalAssets(double value);

    double availableCash() const;
    void setAvailableCash(double value);

    double frozenCash() const;
    void setFrozenCash(double value);

    double marketValue() const;
    void setMarketValue(double value);

    double totalPnl() const;
    void setTotalPnl(double value);

private:
    double m_totalAssets{0.0};
    double m_availableCash{0.0};
    double m_frozenCash{0.0};
    double m_marketValue{0.0};
    double m_totalPnl{0.0};
};

// ============================================================
// 券商能力描述
// ============================================================

class BrokerCapability {
public:
    BrokerName brokerName() const;
    void setBrokerName(BrokerName value);

    bool supportsAlgo() const;
    void setSupportsAlgo(bool value);

    bool supportsBasket() const;
    void setSupportsBasket(bool value);

    bool supportsConditional() const;
    void setSupportsConditional(bool value);

    bool supportsShortSelling() const;
    void setSupportsShortSelling(bool value);

    bool supportsFutures() const;
    void setSupportsFutures(bool value);

    bool supportsOptions() const;
    void setSupportsOptions(bool value);

    // 扩展能力查询 (避免结构体膨胀)
    bool hasCapability(CapabilityId id) const;
    void setCapability(CapabilityId id, bool supported);

private:
    BrokerName m_brokerName;
    bool m_supportsAlgo{false};
    bool m_supportsBasket{false};
    bool m_supportsConditional{false};
    bool m_supportsShortSelling{false};
    bool m_supportsFutures{false};
    bool m_supportsOptions{false};
    std::unordered_map<std::string, bool> m_extendedCapabilities;
};

// ============================================================
// 算法订单请求 / 结果
// ============================================================

class AlgoOrderRequest {
public:
    AlgoStrategyName algoStrategy() const;
    void setAlgoStrategy(AlgoStrategyName value);

    const OrderRequest& baseOrder() const;
    OrderRequest& mutableBaseOrder();
    void setBaseOrder(OrderRequest value);

    bool hasParameter(const std::string& key) const;
    std::string parameter(const std::string& key, const std::string& defaultValue = "") const;
    void setParameter(const std::string& key, std::string value);

private:
    AlgoStrategyName m_algoStrategy;
    OrderRequest m_baseOrder;
    std::unordered_map<std::string, std::string> m_parameters;
};

class AlgoOrderResult {
public:
    static AlgoOrderResult success(BrokerOrderId brokerOrderId, AlgoStrategyName algoId);
    static AlgoOrderResult failure(std::string errorMessage);

    bool succeeded() const;
    BrokerOrderId brokerOrderId() const;
    AlgoStrategyName algoId() const;
    const std::string& errorMessage() const;

private:
    AlgoOrderResult(bool ok, BrokerOrderId orderId, AlgoStrategyName algo, std::string error);
    bool m_success{false};
    BrokerOrderId m_brokerOrderId;
    AlgoStrategyName m_algoId;
    std::string m_errorMessage;
};

// ============================================================
// 回调类型别名
// ============================================================

using OrderCallback = std::function<void(const OrderStatus&)>;
using TradeCallback = std::function<void(const TradeFill&)>;
using ErrorCallback = std::function<void(const std::string& errorMessage)>;

using OrderQueryCallback = std::function<void(std::optional<OrderStatus>)>;
using PositionsQueryCallback = std::function<void(std::vector<PositionSnapshot>)>;
using AccountQueryCallback = std::function<void(std::optional<AccountInfo>)>;

// ============================================================
// 预设 CapabilityId 常量
// ============================================================

inline constexpr const char* CAP_ALGO_TWAP = "algo_twap";
inline constexpr const char* CAP_ALGO_VWAP = "algo_vwap";
inline constexpr const char* CAP_SHORT_SELLING = "short_selling";


// ── Moved from PositionAccountEngine.h (types only, engine class removed) ──

enum class PositionSide : int { Long = 0, Short = 1 };
enum class PositionType : int { Stock = 0, MarginBuy, MarginSell, Futures, Options };
enum class TradeAction : int { None = 0, MarginSell, CloseShort };
enum class ExchangeCode : int { Unknown = 0, SHSE, SZSE, BSE, CFFEX, SHFE, DCE, CZCE, INE, GFEX };

class Position final {
public:
    Position() = default;
    const std::string& symbol() const noexcept { return m_symbol; }
    void setSymbol(std::string v) { m_symbol = std::move(v); }
    ExchangeCode exchange() const noexcept { return m_exchange; }
    void setExchange(ExchangeCode v) noexcept { m_exchange = v; }
    PositionSide side() const noexcept { return m_side; }
    void setSide(PositionSide v) noexcept { m_side = v; }
    PositionType type() const noexcept { return m_type; }
    void setType(PositionType v) noexcept { m_type = v; }
    std::int64_t quantity() const noexcept { return m_quantity; }
    void setQuantity(std::int64_t v) noexcept { m_quantity = v; }
    std::int64_t availableQuantity() const noexcept { return m_availableQuantity; }
    void setAvailableQuantity(std::int64_t v) noexcept { m_availableQuantity = v; }
    std::int64_t closeableQuantity() const noexcept { return m_closeableQuantity; }
    void setCloseableQuantity(std::int64_t v) noexcept { m_closeableQuantity = v; }
    double costBasis() const noexcept { return m_costBasis; }
    void setCostBasis(double v) noexcept { m_costBasis = v; }
    double lastPrice() const noexcept { return m_lastPrice; }
    void setLastPrice(double v) noexcept { m_lastPrice = v; }
    double marketValue() const noexcept { return m_marketValue; }
    void setMarketValue(double v) noexcept { m_marketValue = v; }
    double unrealizedPnl() const noexcept { return m_unrealizedPnl; }
    void setUnrealizedPnl(double v) noexcept { m_unrealizedPnl = v; }
private:
    std::string m_symbol; ExchangeCode m_exchange{ExchangeCode::Unknown};
    PositionSide m_side{PositionSide::Long}; PositionType m_type{PositionType::Stock};
    std::int64_t m_quantity{0}, m_availableQuantity{0}, m_closeableQuantity{0};
    double m_costBasis{0}, m_lastPrice{0}, m_marketValue{0}, m_unrealizedPnl{0};
};

class AccountSnapshot final {
public:
    AccountSnapshot() = default;
    const std::string& accountId() const noexcept { return m_accountId; }
    void setAccountId(std::string v) { m_accountId = std::move(v); }
    double availableCash() const noexcept { return m_availableCash; }
    void setAvailableCash(double v) noexcept { m_availableCash = v; }
    double marketValue() const noexcept { return m_marketValue; }
    void setMarketValue(double v) noexcept { m_marketValue = v; }
    double totalAsset() const noexcept { return m_totalAsset; }
    void setTotalAsset(double v) noexcept { m_totalAsset = v; }
    double dailyTurnoverNotional() const noexcept { return m_dailyTurnoverNotional; }
    void setDailyTurnoverNotional(double v) noexcept { m_dailyTurnoverNotional = v; }
    double realizedPnl() const noexcept { return m_realizedPnl; }
    void setRealizedPnl(double v) noexcept { m_realizedPnl = v; }
    double unrealizedPnl() const noexcept { return m_unrealizedPnl; }
    void setUnrealizedPnl(double v) noexcept { m_unrealizedPnl = v; }
private:
    std::string m_accountId; double m_availableCash{1000000}, m_marketValue{0};
    double m_realizedPnl{0}, m_unrealizedPnl{0}, m_totalAsset{1000000}, m_dailyTurnoverNotional{0};
};
} // namespace trading
} // namespace domain