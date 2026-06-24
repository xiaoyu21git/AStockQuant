#include "TradingSystem.h"
#include "../../domain/strategy/include/StrategyManager.h"
#include "../../domain/strategy/include/MarketDataAdapter.h"
#include "../adapters/JujinBrokerGateway.h"

#include "../../engine/include/JujinApi.h"
#include "../../engine/include/GlobalEventBusRegistry.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace app::system {

TradingSystem& TradingSystem::instance() {
    static TradingSystem sys;
    return sys;
}

void TradingSystem::setBrokerGateway(std::unique_ptr<domain::trading::IBrokerGatewayEx> gw) {
    m_brokerGateway = std::move(gw);
}

void TradingSystem::initialize() {
    if (m_initialized) return;

    std::cout << "[TradingSystem] Initializing...\n";

    m_tradeEngine = std::make_unique<domain::trading::TradeExecutionEngine>();
    m_positionEngine = std::make_unique<domain::trading::PositionAccountEngine>();

    if (m_brokerGateway) {
        m_tradeEngine->setGateway(std::move(m_brokerGateway));
    }

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

void TradingSystem::initializeWithBroker(const std::string& token, const std::string& accountId) {
    if (m_initialized) return;

    if (!token.empty()) {
        auto gw = std::make_unique<app::adapters::JujinBrokerGateway>();
        const std::string configJson = R"({"token":")" + token + R"(","accountId":")" + accountId + R"("})";
        if (gw->connect(configJson)) {
            const bool live = gw->isConnected();
            setBrokerGateway(std::move(gw));
            std::cout << "[TradingSystem] Broker gateway connected"
                      << (live ? " (live)" : " (lazy, awaiting JMC)")
                      << "\n";
        } else {
            std::cerr << "[TradingSystem] WARNING: Broker gateway connect failed\n";
        }
    } else {
        std::cerr << "[TradingSystem] WARNING: No token configured, broker gateway not created\n";
    }

    initialize();

    // ── 从 SDK 同步账户/持仓 ──
    if (auto* api = engine::get_shared_jujin_api()) {
        if (m_positionEngine) {
            auto acc = api->query_account();
            domain::trading::AccountSnapshot snap;
            snap.setAccountId(accountId);
            snap.setAvailableCash(acc.available);
            snap.setTotalAsset(acc.total_asset);
            snap.setMarketValue(acc.market_value);
            m_positionEngine->applyAccountEvent(snap);
            for (auto& p : api->query_positions()) {
                domain::trading::Position pos;
                pos.setSymbol(p.symbol);
                pos.setLastPrice(p.price);
                pos.setQuantity(p.quantity);
                pos.setSide(p.quantity >= 0 ? domain::trading::PositionSide::Long
                                            : domain::trading::PositionSide::Short);
                m_positionEngine->applyPositionEvent(p.symbol, pos);
            }
        }
    }
}

void TradingSystem::refreshPositionsFromBroker() {
    if (!m_positionEngine) return;
    auto* api = engine::get_shared_jujin_api();
    if (!api) return;

    auto acc = api->query_account();
    domain::trading::AccountSnapshot snap;
    snap.setAvailableCash(acc.available);
    snap.setTotalAsset(acc.total_asset);
    snap.setMarketValue(acc.market_value);
    m_positionEngine->applyAccountEvent(snap);

    for (auto& p : api->query_positions()) {
        domain::trading::Position pos;
        pos.setSymbol(p.symbol);
        pos.setLastPrice(p.price);
        pos.setQuantity(p.quantity);
        pos.setSide(p.quantity >= 0 ? domain::trading::PositionSide::Long
                                    : domain::trading::PositionSide::Short);
        m_positionEngine->applyPositionEvent(p.symbol, pos);
    }
}

void TradingSystem::setRiskConfig(const domain::strategy::RiskConfig& config) {
    m_riskConfig = config;
}

void TradingSystem::pushMarketData(const std::string& symbol, double price,
                                    double volume, std::int32_t tradingDay) {
    {
        std::lock_guard<std::mutex> lock(m_priceMutex);
        m_latestPrices[symbol] = price;
    }
    domain::strategy::MarketDataAdapter adapter;
    adapter.pushTick(symbol, price, volume, tradingDay);
}

double TradingSystem::latestPrice(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(m_priceMutex);
    auto it = m_latestPrices.find(symbol);
    return it != m_latestPrices.end() ? it->second : 0.0;
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
    const bool isAutoSignal = order.signalStrength() > 0.0 && hasStrategy;
    if (isAutoSignal) {
        auto* engine = domain::strategy::StrategyManager::instance().get(order.strategyId());
        input.setStrategyBound(engine != nullptr);
        input.setStrategyActive(engine != nullptr);
        input.setAutoStrategySignal(true);
    } else {
        // 手动提交的订单，跳过策略绑定检查
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
    return m_tradeEngine->submitOrder(order, riskInput);
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

void TradingSystem::setOnOrderAccepted(OrderUpdateHandler handler) {
    if (m_tradeEngine) {
        m_tradeEngine->setOnOrderAccepted(std::move(handler));
    }
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