#include "../include/StrategyManager.h"
#include "../include/RuntimeFactorSvc.h"
#include "../../factor/include/FactorInstanceManager.h"
#include "foundation/log/logging.hpp"

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
    INTERNAL_INFO_STREAM << "[SM] createEngine: id=" << strategyId << " factorSvc=" << static_cast<void*>(factorSvc.get());
    auto engine = StrategyEngine::fromDb(strategyId, std::move(factorSvc));
    INTERNAL_INFO_STREAM << "[SM] createEngine: fromDb returned engine=" << static_cast<void*>(engine.get());
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
    INTERNAL_INFO_STREAM << "[SM] createEngine: stored, count=" << m_engines.size();
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
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%06u", id);
        return buf;
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

    auto result = engine->start();
    if (!result.isOk()) {
        throw std::runtime_error("引擎启动失败: " + strategyId);
    }

    // 加载历史行情数据并注入引擎
    if (!engine->prepareMarketData()) {
        INTERNAL_WARN_STREAM << "[SM] 历史数据加载失败或为空: " << strategyId;
        // 不阻塞启动 — 允许无历史数据运行（仅依赖实时 tick）
    }

    // 注入订单监听器
    if (m_defaultOrderListener) {
        engine->setOrderListener(m_defaultOrderListener);
    }

    // 注入实盘数据持久化路径（lastEvalDay JSON 等）
    if (!m_liveDataPath.empty()) {
        engine->setLiveDataPath(m_liveDataPath);
    }

    // 启动实盘循环
    engine->startLiveLoop();

    INTERNAL_INFO_STREAM << "[SM] startStrategy OK: " << strategyId;
}

void StrategyManager::stopStrategy(const std::string& strategyId)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_engines.find(strategyId);
    if (it == m_engines.end() || !it->second) return;

    it->second->stopLiveLoop();
    it->second->stop();
    m_engines.erase(it);

    INTERNAL_INFO_STREAM << "[SM] stopStrategy OK: " << strategyId << " count=" << m_engines.size();
}

} // namespace domain::strategy