#include "../include/StrategyManager.h"
#include "../include/RuntimeFactorSvc.h"
#include "../../factor/include/FactorInstanceManager.h"
#include "foundation/config/ConfigManager.hpp"
#include "foundation/log/logging.hpp"
#include "foundation/market/AStockSymbol.h"

#include <cstdio>
#include <sstream>
#include <stdexcept>

namespace domain::strategy {

StrategyManager& StrategyManager::instance() {
    static StrategyManager s_instance;
    return s_instance;
}

StrategyEngine* StrategyManager::createEngine(const std::string& strategyId,
                                               std::unique_ptr<IRuntimeFactorService> factorSvc) {
    INTERNAL_INFO_STREAM << "[SM] 创建引擎: id=" << strategyId << " factorSvc=" << static_cast<void*>(factorSvc.get());
    auto engine = StrategyEngine::fromDb(strategyId, std::move(factorSvc));
    INTERNAL_INFO_STREAM << "[SM] 创建引擎: fromDb 返回 engine=" << static_cast<void*>(engine.get());
    if (!engine) return nullptr;

    const std::lock_guard<std::mutex> lock(m_mutex);
    auto* ptr = engine.get();
    // 移除旧引擎（参数可能已变更），用新引擎替换
    auto old = m_engines.find(strategyId);
    if (old != m_engines.end() && old->second) {
        old->second->stopLiveLoop();
        m_engines.erase(old);
    }
    m_engines[strategyId] = std::move(engine);
    INTERNAL_INFO_STREAM << "[SM] 创建引擎: 已存储, 数量=" << m_engines.size();
    return ptr;
}

StrategyEngine* StrategyManager::get(const std::string& id) const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_engines.find(id);
    return it != m_engines.end() ? it->second.get() : nullptr;
}

void StrategyManager::remove(const std::string& id) {
    if (id.empty()) return;
    const std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_engines.find(id);
    if (it != m_engines.end() && it->second) {
        it->second->stopLiveLoop();  // 先停后台线程再销毁
    }
    m_engines.erase(id);
}

void StrategyManager::startAll() {
    const std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, engine] : m_engines) {
        if (engine) engine->start();
    }
}

void StrategyManager::pauseAll() {
    const std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, engine] : m_engines) {
        if (engine) engine->pause();
    }
}

void StrategyManager::resumeAll() {
    const std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, engine] : m_engines) {
        if (engine) engine->resume();
    }
}

void StrategyManager::stopAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, engine] : m_engines) {
        if (engine) {
            engine->stopLiveLoop();  // 先停后台 drainQueue 线程
            engine->stop();          // 再停策略服务状态
        }
    }
    m_engines.clear();
}

std::vector<OrderRequest> StrategyManager::stepAll(const MarketDataPoint& mdp) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<OrderRequest> orders;
    for (auto& [id, engine] : m_engines) {
        if (!engine) continue;
        auto result = engine->step(mdp);
        if (result.has_value()) {
            orders.insert(orders.end(),
                          std::make_move_iterator(result->begin()),
                          std::make_move_iterator(result->end()));
        }
    }
    return orders;
}

void StrategyManager::setOrderListener(IOrderListener* listener)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, engine] : m_engines) {
        if (engine) {
            engine->setOrderListener(listener);
        }
    }
}

void StrategyManager::setBasketInterceptor(IBasketInterceptor* interceptor)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, engine] : m_engines) {
        if (engine) {
            engine->setBasketInterceptor(interceptor);
        }
    }
}

void StrategyManager::setExecutionMode(EngineExecutionMode mode)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, engine] : m_engines) {
        if (engine) {
            engine->setExecutionMode(mode);
        }
    }
}

std::size_t StrategyManager::count() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_engines.size();
}

bool StrategyManager::empty() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_engines.empty();
}

// ── 因子服务工厂 ──

std::unique_ptr<IRuntimeFactorService> StrategyManager::createFactorService()
{
    if (!m_factorInstanceManager) return nullptr;

    auto symbolResolver = [](std::uint32_t id) -> std::string {
        return foundation::market::AStockSymbol::fromInstrumentId(id);
    };
    auto factorNameResolver = [](std::uint64_t fid) -> std::string {
        return std::to_string(fid);
    };
    return std::make_unique<RuntimeFactorSvc>(
        *m_factorInstanceManager,
        std::move(symbolResolver),
        std::move(factorNameResolver));
}

// ── 统一生命周期 ──

StrategyEngine* StrategyManager::getOrCreateEngine(const std::string& strategyId)
{
    // 先查缓存
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_engines.find(strategyId);
        if (it != m_engines.end()) return it->second.get();
    }

    // 创建因子服务（如果 FactorInstanceManager 已注入）
    std::unique_ptr<IRuntimeFactorService> factorSvc = createFactorService();

    return createEngine(strategyId, std::move(factorSvc));
}

void StrategyManager::startStrategy(const std::string& strategyId)
{
    auto* engine = getOrCreateEngine(strategyId);
    if (!engine) {
        throw std::runtime_error("引擎创建失败: " + strategyId);
    }

    // ── 注入交易账户 ID (从 TradingConnection 配置读取, Phase 1.4) ──
    {
        auto cfg = foundation::config::ConfigManager::instance()
            .loadConfigFile(foundation::config::ConfigFile::TradingConnection);
        if (cfg && !cfg->isNull() && cfg->has("accountId")) {
            std::string accountId = cfg->get("accountId").asString();
            if (!accountId.empty()) {
                engine->setAccountId(accountId);
                INTERNAL_INFO_STREAM << "[SM] 已注入 accountId: " << strategyId
                                     << " -> " << accountId;
            } else {
                INTERNAL_ERROR_STREAM << "[SM] accountId 为空, 策略将无法下单: " << strategyId;
            }
        } else {
            INTERNAL_ERROR_STREAM << "[SM] 配置中未找到 accountId, 策略将无法下单: " << strategyId;
        }
    }

    auto result = engine->start();
    if (!result.isOk()) {
        throw std::runtime_error("引擎启动失败: " + strategyId);
    }

    // 加载历史行情数据并注入引擎 — 失败则拒绝启动
    if (!engine->prepareMarketData()) {
        INTERNAL_ERROR_STREAM << "[SM] 历史数据加载失败或为空, 策略启动被拒绝: " << strategyId;
        engine->stop();
        throw std::runtime_error("历史数据不可用, 策略启动失败: " + strategyId);
    }

    // 注入订单监听器
    if (m_defaultOrderListener) {
        engine->setOrderListener(m_defaultOrderListener);
    }

    // 注入篮子拦截器 + 切换到半自动模式 (v0.16.0)
    if (m_defaultBasketInterceptor) {
        engine->setBasketInterceptor(m_defaultBasketInterceptor);
        engine->setExecutionMode(EngineExecutionMode::SemiAuto);
    }

    // 注入实盘数据持久化路径（lastEvalDay JSON 等）
    if (!m_liveDataPath.empty()) {
        engine->setLiveDataPath(m_liveDataPath);
    }

    // 启动实盘循环
    engine->startLiveLoop();

    INTERNAL_INFO_STREAM << "[SM] 启动策略成功: " << strategyId;
}

void StrategyManager::stopStrategy(const std::string& strategyId)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_engines.find(strategyId);
    if (it == m_engines.end() || !it->second) return;

    it->second->stopLiveLoop();
    it->second->stop();
    m_engines.erase(it);

    INTERNAL_INFO_STREAM << "[SM] 停止策略成功: " << strategyId << " count=" << m_engines.size();
}

} // namespace domain::strategy