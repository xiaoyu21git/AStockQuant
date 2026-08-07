#pragma once
// IOrderBuilder.h — 订单构建器抽象接口（无状态）
// 属于 domain::trading 交易域，策略域通过此接口依赖交易域（依赖倒置）

#include "OrderSpec.h"
#include "TradingTypes.h"

#include <cstdint>
#include <string>

namespace domain::trading {

/// @brief 订单构建器抽象接口 — 无状态，所有上下文由调用方显式传入
/// 策略域通过此接口依赖交易域（依赖倒置原则）
class IOrderBuilder {
public:
    virtual ~IOrderBuilder() = default;

    /// @brief 从 OrderSpec 构建 OrderRequest（纯虚，唯一需子类实现的方法）
    [[nodiscard]] virtual OrderRequest build(
        const OrderSpec& spec,
        const std::string& strategyId,
        const std::string& accountId) = 0;

    // ── 便捷方法：默认实现，内部构造 OrderSpec → 调用 build() ──

    /// @brief 市价买入开仓
    [[nodiscard]] OrderRequest buildMarketBuy(
        const std::string& symbol, std::int64_t qty,
        const std::string& strategyId, const std::string& accountId,
        double signalScore = 0.5)
    {
        OrderSpec spec;
        spec.symbol = symbol;
        spec.side = OrderSide::Buy;
        spec.orderType = OrderType::Market;
        spec.positionEffect = PositionEffect::Open;
        spec.quantity = qty;
        spec.signalScore = signalScore;
        return build(spec, strategyId, accountId);
    }

    /// @brief 市价卖出
    [[nodiscard]] OrderRequest buildMarketSell(
        const std::string& symbol, std::int64_t qty, PositionEffect pe,
        const std::string& strategyId, const std::string& accountId)
    {
        OrderSpec spec;
        spec.symbol = symbol;
        spec.side = OrderSide::Sell;
        spec.orderType = OrderType::Market;
        spec.positionEffect = pe;
        spec.quantity = qty;
        return build(spec, strategyId, accountId);
    }

    /// @brief 一键清仓/熔断强平（市价卖出平仓）
    [[nodiscard]] OrderRequest buildLiquidationExit(
        const std::string& symbol, std::int64_t qty,
        const std::string& strategyId, const std::string& accountId)
    {
        return buildMarketSell(symbol, qty, PositionEffect::Close,
                               strategyId, accountId);
    }

    /// @brief 规则闸门出场（isFullExit=true → 全清, false → 减半）
    [[nodiscard]] OrderRequest buildRuleExit(
        const std::string& symbol, std::int64_t held, bool isFullExit,
        const std::string& strategyId, const std::string& accountId,
        double /*currentPrice*/ = 0.0)
    {
        OrderSpec spec;
        spec.symbol = symbol;
        spec.side = OrderSide::Sell;
        spec.orderType = OrderType::Market;
        spec.positionEffect = PositionEffect::Close;
        spec.quantity = isFullExit ? held : held / 2;
        return build(spec, strategyId, accountId);
    }
};

} // namespace domain::trading
