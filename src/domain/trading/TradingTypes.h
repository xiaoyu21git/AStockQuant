#pragma once

#include "foundation/Utils/Timestamp.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
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
// 枚举 — uint8_t 底层, 缓存友好
// ============================================================

enum class OrderSide      : uint8_t { Buy = 0, Sell = 1 };
enum class OrderType      : uint8_t { Limit = 0, Market = 1, Stop = 2, StopLimit = 3 };
enum class PositionEffect : uint8_t { Open = 0, Close = 1, CloseToday = 2, CloseYesterday = 3 };
enum class TimeInForce    : uint8_t { ROD = 0, IOC = 1, FOK = 2, GTD = 3, GTX = 4 };
enum class OrderStatusValue : uint8_t {
    Pending = 0,
    New = 1,
    PartiallyFilled = 2,
    Filled = 3,
    PendingCancel = 4,
    Cancelled = 5,
    Rejected = 6,
    Expired = 7
};

// ============================================================
// ExtensionSlot — 无堆分配的扩展字段载体
// 固定 4 槽, std::variant 栈上存储, 覆盖 99% 品种特有参数
// ============================================================

struct ExtensionSlot {
    uint64_t key = 0;  // 预定义常量: 0x01=Strike, 0x02=Multiplier, ...
    std::variant<std::int64_t, double, uint64_t> value{0.0};

    ExtensionSlot() = default;
    ExtensionSlot(uint64_t k, double v)       : key(k), value(v) {}
    ExtensionSlot(uint64_t k, std::int64_t v) : key(k), value(v) {}
    ExtensionSlot(uint64_t k, uint64_t v)     : key(k), value(v) {}
};

// 扩展 Key 常量 (编译期哈希, 无字符串开销)
namespace ExtKey {
    constexpr uint64_t kStrike            = 0x01;  // 期权行权价
    constexpr uint64_t kMultiplier        = 0x02;  // 合约乘数
    constexpr uint64_t kSignalScore       = 0x10;  // 策略信号强度
    constexpr uint64_t kTargetWeight     = 0x11;  // 策略目标权重（资金量级分配）
    constexpr uint64_t kSignalIntent     = 0x12;  // 策略信号意图(SignalIntent枚举值)
    constexpr uint64_t kPostOnly          = 0x20;  // 仅 Maker
    constexpr uint64_t kReduceOnly        = 0x21;  // 仅减仓
    constexpr uint64_t kSlippageTolerance = 0x30;  // 滑点容忍
    constexpr uint64_t kBasketId          = 0x31;  // 篮子ID，同批次订单关联查询
    constexpr uint64_t kAlgoType          = 0x40;  // 算法单类型
    constexpr uint64_t kAlgoDuration      = 0x41;  // 算法单时长
    constexpr uint64_t kStopPrice         = 0x50;  // 止损价 (gmsdk)
    constexpr uint64_t kOrderBusiness     = 0x51;  // 业务类型 (gmsdk)
}

// ============================================================
// OrderRequest — 下单指令 (紧凑 POD 风格)
// 字符串字段依赖 SSO, 扩展用固定数组, clone() → trivial copy
// ============================================================

class OrderRequest {
public:
    class Builder;

    OrderRequest() noexcept = default;

    // ── 核心 getter/setter (noexcept) ──
    [[nodiscard]] const std::string& clOrdId()    const noexcept;
    void setClOrdId(std::string v);

    [[nodiscard]] const std::string& accountId()  const noexcept;
    void setAccountId(std::string v);

    [[nodiscard]] const std::string& symbol()     const noexcept;
    void setSymbol(std::string v);

    [[nodiscard]] const std::string& exchange()   const noexcept;
    void setExchange(std::string v);

    [[nodiscard]] const std::string& strategyId() const noexcept;
    void setStrategyId(std::string v);

    [[nodiscard]] OrderSide   side()      const noexcept;
    void setSide(OrderSide v)             noexcept;

    [[nodiscard]] OrderType   orderType() const noexcept;
    void setOrderType(OrderType v)        noexcept;

    [[nodiscard]] double price()          const noexcept;
    void setPrice(double v)               noexcept;

    [[nodiscard]] double quantity()       const noexcept;
    void setQuantity(double v)            noexcept;

    [[nodiscard]] PositionEffect positionEffect() const noexcept;
    void setPositionEffect(PositionEffect v)      noexcept;

    [[nodiscard]] TimeInForce timeInForce() const noexcept;
    void setTimeInForce(TimeInForce v)      noexcept;

    [[nodiscard]] const std::string& expireTime() const noexcept;
    void setExpireTime(std::string v);

    [[nodiscard]] const std::string& currency() const noexcept;
    void setCurrency(std::string v);

    [[nodiscard]] double displayQty()    const noexcept;
    void setDisplayQty(double v)         noexcept;

    [[nodiscard]] double minQty()        const noexcept;
    void setMinQty(double v)             noexcept;

    // ── 扩展槽 (固定 4 槽, 无堆分配) ──
    [[nodiscard]] const std::array<ExtensionSlot, 4>& extensions() const noexcept;
    [[nodiscard]] ExtensionSlot* findExtension(uint64_t key) noexcept;
    [[nodiscard]] const ExtensionSlot* findExtension(uint64_t key) const noexcept;
    void setExtension(uint64_t key, double v)       noexcept;
    void setExtension(uint64_t key, std::int64_t v) noexcept;
    void setExtension(uint64_t key, uint64_t v)     noexcept;

    /// @brief 按 key 取值, 类型不匹配返回 defaultVal
    template <typename T>
    [[nodiscard]] T extensionAs(uint64_t key, T defaultVal) const noexcept {
        const auto* slot = findExtension(key);
        if (!slot) return defaultVal;
        if (auto* p = std::get_if<T>(&slot->value)) return *p;
        return defaultVal;
    }

    // ── 核心方法 (全部 noexcept) ──
    [[nodiscard]] static std::pair<bool, std::string> validate(const OrderRequest& req);

    /// @brief 批量校验 — SIMD 友好的连续内存布局
    [[nodiscard]] static size_t validateBatch(const OrderRequest* reqs, size_t count,
                                              std::string* outErrors);

    [[nodiscard]] bool isValid() const noexcept;

    [[nodiscard]] std::string toCanonicalString() const;

    /// @brief trivial copy (SSO 内字符串仅复制栈上缓冲区)
    [[nodiscard]] OrderRequest clone() const noexcept { return *this; }

private:
    [[nodiscard]] static bool checkPriceSemantics(const OrderRequest& req,
                                                  std::string& errDetail) noexcept;
    [[nodiscard]] static bool checkQuantityPrecision(const OrderRequest& req,
                                                     std::string& errDetail) noexcept;

    // ── 数据成员 (按访问频率排列, 热字段在前) ──
    std::string m_symbol;            // SSO: ≤15 字符零堆分配
    std::string m_strategyId;        // SSO
    std::string m_clOrdId;           // SSO
    std::string m_accountId;         // SSO
    std::string m_exchange;          // SSO
    std::string m_currency;          // SSO (典型 "CNY"/"USD" 远小于 SSO)
    std::string m_expireTime;        // SSO (ISO8601 格式)
    double      m_price{0};
    double      m_quantity{0};
    double      m_displayQty{0};
    double      m_minQty{0};
    OrderSide   m_side{OrderSide::Buy};
    OrderType   m_orderType{OrderType::Limit};
    PositionEffect m_positionEffect{PositionEffect::Open};
    TimeInForce    m_timeInForce{TimeInForce::ROD};
    std::array<ExtensionSlot, 4> m_extensions{};  // 4×24字节=96字节, 无堆
};

// ============================================================
// OrderRequest::Builder (链式构造)
// ============================================================

class OrderRequest::Builder final {
public:
    Builder() = default;

    Builder& clOrdId(std::string v)      { m_req.setClOrdId(std::move(v)); return *this; }
    Builder& accountId(std::string v)    { m_req.setAccountId(std::move(v)); return *this; }
    Builder& symbol(std::string v)       { m_req.setSymbol(std::move(v));    return *this; }
    Builder& exchange(std::string v)     { m_req.setExchange(std::move(v));  return *this; }
    Builder& strategyId(std::string v)   { m_req.setStrategyId(std::move(v)); return *this; }
    Builder& side(OrderSide v)           { m_req.setSide(v);  return *this; }
    Builder& orderType(OrderType v)      { m_req.setOrderType(v); return *this; }
    Builder& price(double v)             { m_req.setPrice(v); return *this; }
    Builder& quantity(double v)          { m_req.setQuantity(v); return *this; }
    Builder& positionEffect(PositionEffect v) { m_req.setPositionEffect(v); return *this; }
    Builder& timeInForce(TimeInForce v)  { m_req.setTimeInForce(v); return *this; }
    Builder& expireTime(std::string v)   { m_req.setExpireTime(std::move(v)); return *this; }
    Builder& currency(std::string v)     { m_req.setCurrency(std::move(v)); return *this; }
    Builder& displayQty(double v)        { m_req.setDisplayQty(v); return *this; }
    Builder& minQty(double v)            { m_req.setMinQty(v); return *this; }
    Builder& ext(uint64_t k, double v)   { m_req.setExtension(k, v); return *this; }

    [[nodiscard]] std::pair<OrderRequest, std::string> build() const;

private:
    OrderRequest m_req;
};

// ============================================================
// Order — 运行时订单实体 (无锁状态机 + 缓存行对齐)
// 不持有引擎/网关指针, 不加锁, 不调外部API.
// ============================================================

class alignas(64) Order {
public:
    /// @brief 构造 — 传入柜台ID + 客户端ID + 原始请求
    Order(std::string orderId, std::string clOrdId, const OrderRequest& origin);

    // ── 不可变 ID (读多写少, 热缓存行) ──
    [[nodiscard]] const std::string& orderId()  const noexcept;
    [[nodiscard]] const std::string& clOrdId()  const noexcept;

    // ── 原始请求回显 ──
    [[nodiscard]] const OrderRequest& originRequest() const noexcept;
    [[nodiscard]] const std::string& symbol()   const noexcept;
    [[nodiscard]] OrderSide side()              const noexcept;
    [[nodiscard]] double price()                const noexcept;
    [[nodiscard]] double quantity()             const noexcept;

    // ── 运行时状态 (原子读, 无锁) ──
    [[nodiscard]] OrderStatusValue status()     const noexcept;
    [[nodiscard]] double cumQty()               const noexcept;
    [[nodiscard]] double leavesQty()            const noexcept;
    [[nodiscard]] double avgPx()                const noexcept;
    [[nodiscard]] double fee()                  const noexcept;
    [[nodiscard]] bool   isFrozen()             const noexcept;
    [[nodiscard]] bool   isTerminal()           const noexcept;

    // ── 时间戳 (纳秒, 单调时钟) ──
    [[nodiscard]] foundation::utils::Timestamp createTime()     const noexcept;
    [[nodiscard]] foundation::utils::Timestamp lastUpdateTime() const noexcept;

    // ── 状态管理 (无锁 CAS + 位图转换表) ──

    /// @brief 应用成交. 纯算术, 无锁. 自动推导终态.
    /// @return false = 数据异常
    bool applyFill(double fillQty, double fillPx, double tradeFee = 0.0) noexcept;

    /// @brief CAS 撤单. 仅 New/PartiallyFilled → PendingCancel.
    bool tryCancel() noexcept;

    void confirmCancelled() noexcept;
    void confirmRejected(const std::string& reason) noexcept;
    void markExpired() noexcept;
    void freeze() noexcept;

    /// @brief 编译期位图状态转换表 (8×8=64bits)
    [[nodiscard]] static constexpr bool canTransition(uint8_t from, uint8_t to) noexcept;

private:
    void transitionTo(OrderStatusValue target) noexcept;

    // ═══ 缓存行 1: 只读/少写 (构造后基本不变) ═══
    char m_pad1[8];                              // 对齐填充
    std::string m_orderId;                       // 柜台委托编号
    std::string m_clOrdId;                       // 客户端ID
    OrderRequest m_originRequest;                // 原始指令快照
    foundation::utils::Timestamp m_createTime;    // 构造时刻
    // ═══ 缓存行 2: 高频写入 (applyFill 热路径) ═══
    alignas(64) std::atomic<uint8_t> m_status{static_cast<uint8_t>(OrderStatusValue::Pending)};
    double m_cumQty{0};                          // 累计成交
    double m_avgPx{0};                           // 成交均价
    double m_fee{0};                             // 累计手续费
    bool   m_frozen{false};                      // 冻结标志
    foundation::utils::Timestamp m_lastUpdateTime; // 最后状态变更
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