#include "TradingSystem.h"
#include "../../app/adapters/SimulatedBrokerGateway.h"
#include "../../domain/strategy/include/StrategyManager.h"
#include "../../domain/strategy/include/MarketDataAdapter.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace app::system {

TradingSystem& TradingSystem::instance() {
    static TradingSystem sys;
    return sys;
}

void TradingSystem::initialize() {
    if (m_initialized) return;

    std::cout << "[TradingSystem] Initializing...\n";

    m_tradeEngine = std::make_unique<domain::trading::TradeExecutionEngine>();
    m_positionEngine = std::make_unique<domain::trading::PositionAccountEngine>();

    // 注入模拟券商网关
    auto simGateway = std::make_unique<app::adapters::SimulatedBrokerGateway>();
    simGateway->connect("{}");
    m_tradeEngine->setGateway(std::move(simGateway));

    // 连接持仓变更回调：同步更新峰值资产
    m_positionEngine->setOnDataChanged([this]() {
        const auto& acc = accountSnapshot();
        if (acc.totalAsset() > m_peakTotalAsset) {
            m_peakTotalAsset = acc.totalAsset();
        }
        notifyDataChanged();
    });

    m_initialized = true;
    std::cout << "[TradingSystem] Initialized\n";
}

void TradingSystem::setRiskConfig(const domain::strategy::RiskConfig& config) {
    m_riskConfig = config;
}

void TradingSystem::pushMarketData(const std::string& symbol, double price,
                                    double volume, std::int32_t tradingDay) {
    domain::strategy::MarketDataAdapter adapter;
    adapter.pushTick(symbol, price, volume, tradingDay);
}

// ── 构建完整的 RiskInput（从订单 + 当前状态 + 配置） ──
domain::strategy::RiskInput TradingSystem::buildRiskInput(
    const domain::trading::TradeOrder& order) const {

    domain::strategy::RiskInput input;

    // ── 订单字段 ──
    input.setStrategyId(order.strategyId());
    input.setSymbol(order.symbol());
    input.setBuyOrder(order.side() == domain::strategy::OrderDirection::Buy);
    input.setPrice(order.price());
    input.setQuantity(order.quantity());
    input.setCashAmount(order.cashAmount());
    input.setSignalStrength(order.signalStrength() > 0.0 ? order.signalStrength() : 0.5);
    input.setRequestedNotional(order.price() * static_cast<double>(std::max<std::int64_t>(1, order.quantity())));

    // ── 策略上下文 ──
    const bool hasStrategy = !order.strategyId().empty();
    if (hasStrategy) {
        auto* engine = domain::strategy::StrategyManager::instance().get(order.strategyId());
        input.setStrategyBound(engine != nullptr);
        input.setStrategyActive(engine != nullptr);
        input.setAutoStrategySignal(false); // 手动提交的订单不是自动信号
    } else {
        // 无策略 ID 的订单（手动交易），跳过策略绑定检查
        input.setStrategyBound(true);
        input.setStrategyActive(true);
        input.setAutoStrategySignal(false);
    }

    // ── 账户字段 ──
    const auto& acc = accountSnapshot();
    input.setCurrentTotalAsset(acc.totalAsset());
    input.setCurrentMarketValue(acc.marketValue());
    input.setCurrentDailyTurnoverNotional(acc.dailyTurnoverNotional());

    // ── 持仓字段 ──
    const auto* pos = findPosition(order.symbol());
    if (pos) {
        input.setPositionSnapshotReady(true);
        input.setCloseableQuantity(pos->closeableQuantity());
        input.setSymbolMarketValue(pos->marketValue());
        // 持仓盈亏百分比
        if (pos->costBasis() > 0.0 && pos->lastPrice() > 0.0) {
            const double returnPct = ((pos->lastPrice() - pos->costBasis()) / pos->costBasis()) * 100.0;
            input.setSymbolPositionReturnPercent(
                pos->side() == domain::trading::PositionSide::Short ? -returnPct : returnPct);
        }
    } else {
        // 无持仓时：买入不需要快照（新开仓），卖出必须检查可卖量
        input.setPositionSnapshotReady(input.isBuyOrder());
    }

    // ── 市场字段 ──
    if (pos && pos->lastPrice() > 0.0) {
        input.setReferencePrice(pos->lastPrice());
    }
    input.setTradingSessionOpen(true); // MVP: 默认开启，实际应由日历服务提供

    // ── 回撤字段 ──
    if (m_peakTotalAsset > 0.0 && acc.totalAsset() < m_peakTotalAsset) {
        input.setCurrentDrawdownPercent(
            ((acc.totalAsset() - m_peakTotalAsset) / m_peakTotalAsset) * 100.0);
    }

    // ── 风控配置字段 ──
    domain::strategy::RiskEvaluator::applyConfig(input, m_riskConfig);

    return input;
}

const domain::trading::Position* TradingSystem::findPosition(const std::string& symbol) const {
    if (!m_positionEngine) return nullptr;
    const auto& posMap = m_positionEngine->positions();
    auto it = posMap.find(symbol);
    return (it != posMap.end()) ? &it->second : nullptr;
}

domain::trading::SubmitResult TradingSystem::submitOrder(const domain::trading::TradeOrder& order) {
    if (!m_tradeEngine) {
        return domain::trading::SubmitResult::rejected("trading engine not initialized");
    }

    domain::strategy::RiskInput riskInput = buildRiskInput(order);
    auto result = m_tradeEngine->submitOrder(order, riskInput);

    // 模拟成交场景：委托受理后直接更新持仓（实盘需券商回调）
    if (result.succeeded() && m_positionEngine) {
        const auto& posMap = m_positionEngine->positions();
        auto it = posMap.find(order.symbol());
        std::int64_t existingQty = (it != posMap.end()) ? it->second.quantity() : 0LL;

        domain::trading::Position pos;
        pos.setSymbol(order.symbol());
        pos.setLastPrice(order.price());
        pos.setSide(order.side() == domain::strategy::OrderDirection::Buy
            ? domain::trading::PositionSide::Long : domain::trading::PositionSide::Short);
        pos.setQuantity(order.side() == domain::strategy::OrderDirection::Buy
            ? existingQty + order.quantity() : std::max(0LL, existingQty - order.quantity()));
        m_positionEngine->applyPositionEvent(order.symbol(), pos);

        double cashDelta = order.side() == domain::strategy::OrderDirection::Buy
            ? -(order.price() * static_cast<double>(order.quantity()))
            : order.price() * static_cast<double>(order.quantity());
        auto acc = m_positionEngine->account();
        acc.setAvailableCash(acc.availableCash() + cashDelta);
        acc.setTotalAsset(acc.totalAsset() + cashDelta);
        m_positionEngine->applyAccountEvent(acc);
        notifyDataChanged();
    }
    return result;
}

const domain::trading::AccountSnapshot& TradingSystem::accountSnapshot() const {
    static domain::trading::AccountSnapshot emptySnapshot;
    if (!m_positionEngine) return emptySnapshot;
    return m_positionEngine->account();
}

const std::unordered_map<std::string, domain::trading::Position>& TradingSystem::positions() const {
    static std::unordered_map<std::string, domain::trading::Position> empty;
    if (!m_positionEngine) return empty;
    return m_positionEngine->positions();
}

domain::strategy::RiskResult TradingSystem::evaluateOrderRisk(const domain::strategy::RiskInput& input) {
    return domain::strategy::RiskEvaluator::evaluateOrder(input);
}

void TradingSystem::setOnOrderUpdate(OrderUpdateHandler handler) {
    if (m_tradeEngine) {
        m_tradeEngine->setOnOrderUpdate(std::move(handler));
    }
}

void TradingSystem::setOnTradeFill(TradeFillHandler handler) {
    if (m_tradeEngine) {
        m_tradeEngine->setOnTradeFill(std::move(handler));
    }
}

void TradingSystem::setOnDataChanged(DataChangedCallback cb) {
    if (m_positionEngine) {
        m_positionEngine->setOnDataChanged(std::move(cb));
    }
    m_onDataChanged = std::move(cb);
}

void TradingSystem::notifyDataChanged() {
    if (m_onDataChanged) m_onDataChanged();
}

} // namespace app::system