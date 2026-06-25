#pragma once
// ─────────────────────────────────────────────────────────────────────
// TradingSystem — 订单风控交易底层单例 (纯 C++，零 Qt)
// 统一管理 TradeExecutionEngine / PositionAccountEngine / RiskEvaluator
// 实现 IOrderListener，接收策略引擎订单并驱动执行 + 通知
// ─────────────────────────────────────────────────────────────────────

#include "../../domain/trading/TradeExecutionEngine.h"
#include "../../domain/trading/PositionAccountEngine.h"
#include "../../domain/strategy/include/RiskEvaluator.h"
#include "../../domain/strategy/include/IOrderListener.h"
#include "../../foundation/include/foundation/Utils/Uuid.h"

#include <memory>
#include <mutex>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace app::system {

class TradingSystem final : public domain::strategy::IOrderListener {
public:
    static TradingSystem& instance();

    // ── IOrderListener ──
    void onOrders(const std::vector<domain::strategy::OrderRequest>& orders) override;

    // ── 初始化 ──
    void initialize();
    void setBrokerGateway(std::unique_ptr<domain::trading::IBrokerGatewayEx> gw);

    /// @brief 带券商网关的完整初始化（由桥接层传入 token/accountId）
    void initializeWithBroker(const std::string& token, const std::string& accountId);

    bool initialized() const noexcept { return m_initialized; }

    // ── 交易引擎 ──
    domain::trading::TradeExecutionEngine* tradeEngine() noexcept { return m_tradeEngine.get(); }
    domain::trading::SubmitResult submitOrder(const domain::trading::TradeOrder& order);

    // ── 持仓账户 ──
    domain::trading::PositionAccountEngine* positionEngine() noexcept { return m_positionEngine.get(); }
    const domain::trading::AccountSnapshot& accountSnapshot() const;
    const std::unordered_map<std::string, domain::trading::Position>& positions() const;

    /// @brief 从 SDK 刷新持仓和账户快照（供桥接层定时调用）
    void refreshPositionsFromBroker();

    // ── 风控配置 ──
    const domain::strategy::RiskConfig& riskConfig() const noexcept { return m_riskConfig; }
    void setRiskConfig(const domain::strategy::RiskConfig& config);

    // ── 行情推送 ──
    /// @brief 推送实时行情到所有运行中的策略引擎（数据源无关）
    void pushMarketData(const std::string& symbol, double price,
                        double volume, std::int32_t tradingDay);

    /// @brief 获取标的最新行情价（供策略下单时参考定价）
    double latestPrice(const std::string& symbol) const;

    // ── 风控审批 ──
    domain::strategy::RiskResult evaluateOrderRisk(const domain::strategy::RiskInput& input);

    // ── 策略订单通知回调（IOrderListener → 桥接层 → QML）──
    using OrderGeneratedHandler = std::function<void(const domain::trading::TradeOrder&)>;
    void setOnOrderGenerated(OrderGeneratedHandler h) { m_onOrderGenerated = std::move(h); }

    using OrderSubmitResultHandler = std::function<void(
        const domain::trading::TradeOrder&, const domain::trading::SubmitResult&)>;
    void setOnOrderSubmitResult(OrderSubmitResultHandler h) { m_onOrderSubmitResult = std::move(h); }

    // ── 引擎回调（桥接层注册，用于转发成交/状态到 QML） ──
    using OrderUpdateHandler = std::function<void(const domain::trading::TradeOrder&)>;
    using TradeFillHandler = std::function<void(const domain::trading::TradeFill&)>;
    void setOnOrderAccepted(OrderUpdateHandler handler);
    void setOnOrderUpdate(OrderUpdateHandler handler);
    void setOnTradeFill(TradeFillHandler handler);

    // ── 事件回调 ──
    using DataChangedCallback = std::function<void()>;
    void setOnDataChanged(DataChangedCallback cb);
    void notifyDataChanged();

private:
    /// @brief 从当前状态填充完整的 RiskInput
    domain::strategy::RiskInput buildRiskInput(const domain::trading::TradeOrder& order) const;

    /// @brief 查找指定标的的持仓信息
    const domain::trading::Position* findPosition(const std::string& symbol) const;

    TradingSystem() = default;
    ~TradingSystem();
    TradingSystem(const TradingSystem&) = delete;
    TradingSystem& operator=(const TradingSystem&) = delete;

    bool m_initialized{false};
    std::unique_ptr<domain::trading::TradeExecutionEngine> m_tradeEngine;
    std::unique_ptr<domain::trading::PositionAccountEngine> m_positionEngine;
    std::unique_ptr<domain::trading::IBrokerGatewayEx> m_brokerGateway;
    domain::strategy::RiskConfig m_riskConfig{domain::strategy::RiskConfig::defaults()};
    mutable double m_peakTotalAsset{0.0};
    mutable std::mutex m_mutex;
    mutable std::mutex m_priceMutex;
    std::unordered_map<std::string, double> m_latestPrices;
    DataChangedCallback m_onDataChanged;
    OrderGeneratedHandler m_onOrderGenerated;
    OrderSubmitResultHandler m_onOrderSubmitResult;
    foundation::utils::Uuid m_accountSub;
    foundation::utils::Uuid m_positionSub;
};

} // namespace app::system