#include "../include/StrategyManager.h"

namespace domain::strategy {

StrategyEngine* StrategyManager::createEngine(const std::string& strategyId) {
    auto engine = StrategyEngine::fromDb(strategyId);
    if (!engine) return nullptr;

    const std::lock_guard<std::mutex> lock(m_mutex);
    auto* ptr = engine.get();
    m_engines[strategyId] = std::move(engine);
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
        if (engine) engine->stop();
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

std::size_t StrategyManager::count() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_engines.size();
}

bool StrategyManager::empty() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_engines.empty();
}

} // namespace domain::strategy