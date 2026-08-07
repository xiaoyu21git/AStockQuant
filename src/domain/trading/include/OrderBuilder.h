#pragma once
// ═════════════════════════════════════════════════════════════════════════
// OrderBuilder — 统一订单构建器 (纯 C++, 零 Qt)
//
// 所有订单必须通过此类构建，调用方只传必要可变字段（标的/方向/价格/数量），
// 固定字段（clOrdId/accountId/currency/exchange）由内部统一填充，
// 消除散落各处的字段缺失导致的订单被拒问题。
//
// v2: 实现 IOrderBuilder 接口，完全无状态 — 线程安全
// ═════════════════════════════════════════════════════════════════════════

#include "IOrderBuilder.h"
#include "TradingTypes.h"
#include <string>

namespace domain::trading {

class OrderBuilder final : public IOrderBuilder {
public:
    // ── IOrderBuilder 纯虚方法 ──
    [[nodiscard]] OrderRequest build(
        const OrderSpec& spec,
        const std::string& strategyId,
        const std::string& accountId) override;

    // ── 旧兼容方法（追加 strategyId/accountId 参数，无状态） ──

    /// @brief 止损/止盈退出单（A 股做多场景，写死 Sell+Close）
    [[nodiscard]] OrderRequest buildStopOrder(
        const std::string& symbol, double price, std::int64_t quantity,
        const std::string& strategyId, const std::string& accountId);

    /// @brief 策略信号单（开仓/加仓/减仓，市价）
    [[nodiscard]] OrderRequest buildSignalOrder(
        const std::string& symbol, OrderSide side,
        double price, std::int64_t quantity, double signalScore,
        const std::string& strategyId, const std::string& accountId);

    /// @brief 手动下单（限价）
    [[nodiscard]] OrderRequest buildManualOrder(
        const std::string& symbol, OrderSide side,
        double price, std::int64_t quantity, PositionEffect pe,
        const std::string& strategyId, const std::string& accountId);

private:
    std::string m_currency = "CNY";

    /// @brief 填充所有通用字段: clOrdId / strategyId / accountId / currency / exchange
    void fillCommon(OrderRequest::Builder& b, const std::string& symbol,
                    const std::string& strategyId, const std::string& accountId);
    static std::string generateClOrdId();
};

} // namespace domain::trading
