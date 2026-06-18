#include "../include/StrategyManager.h"
#include "../include/RuntimeFactorSvc.h"

#include <cstdio>

namespace domain::strategy {

StrategyManager& StrategyManager::instance() {
    static StrategyManager s_instance;
    return s_instance;
}

StrategyEngine* StrategyManager::createEngine(const std::string& strategyId,
                                               std::unique_ptr<IRuntimeFactorService> factorSvc) {
    fprintf(stderr, "[SM] createEngine: id=%s factorSvc=%p\n",
            strategyId.c_str(), static_cast<void*>(factorSvc.get()));
    fflush(stderr);
    auto engine = StrategyEngine::fromDb(strategyId, std::move(factorSvc));
    fprintf(stderr, "[SM] createEngine: fromDb returned engine=%p\n", static_cast<void*>(engine.get()));
    fflush(stderr);
    if (!engine) return nullptr;

    const std::lock_guard<std::mutex> lock(m_mutex);
    auto* ptr = engine.get();
    m_engines[strategyId] = std::move(engine);
    fprintf(stderr, "[SM] createEngine: stored, count=%zu\n", m_engines.size());
    fflush(stderr);
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

void StrategyManager::pushMarketData(const MarketDataPoint& mdp)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    fprintf(stderr, "[SM] pushMarketData: engines=%zu valid=%d day=%d\n",
            m_engines.size(), mdp.isValid() ? 1 : 0, mdp.tradingDay());
    fflush(stderr);
    for (auto& [id, engine] : m_engines) {
        if (engine) {
            engine->enqueueMarketData(mdp);
        }
    }
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

} // namespace domain::strategy