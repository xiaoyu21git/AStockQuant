#pragma once
// ═════════════════════════════════════════════════════════════════════════
// OrderBuilder — 统一订单构建器 (纯 C++, 零 Qt)
//
// 所有订单必须通过此类构建，调用方只传必要可变字段（标的/方向/价格/数量），
// 固定字段（clOrdId/accountId/currency/exchange）由内部统一填充，
// 消除散落各处的字段缺失导致的订单被拒问题。
// ═════════════════════════════════════════════════════════════════════════

#include "TradingTypes.h"
#include <string>

namespace domain::trading {

class OrderBuilder {
public:
    // ── 一次性配置 ──
    void setStrategyId(std::string id)  { m_strategyId = std::move(id); }
    void setAccountId(std::string id)   { m_accountId = std::move(id); }

    // ── 构建方法 ──

    /// @brief 止损/止盈退出单（A 股做多场景，写死 Sell+Close）
    OrderRequest buildStopOrder(const std::string& symbol, double price, int64_t quantity);

    /// @brief 策略信号单（开仓/加仓/减仓，市价）
    OrderRequest buildSignalOrder(const std::string& symbol, OrderSide side,
                                  double price, int64_t quantity, double signalScore);

    /// @brief 手动下单（限价）
    OrderRequest buildManualOrder(const std::string& symbol, OrderSide side,
                                  double price, int64_t quantity, PositionEffect pe);

private:
    std::string m_strategyId;
    std::string m_accountId;
    std::string m_currency = "CNY";

    /// @brief 填充所有通用字段: clOrdId / strategyId / accountId / currency / exchange
    void fillCommon(OrderRequest::Builder& b, const std::string& symbol);
    static std::string generateClOrdId();
};

} // namespace domain::trading
