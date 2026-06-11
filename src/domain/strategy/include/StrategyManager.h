#pragma once

#include "IStrategyService.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace domain::strategy {

class StrategyManager final {
public:
    StrategyManager(const StrategyManager&) = delete;
    StrategyManager& operator=(const StrategyManager&) = delete;

    static StrategyManager& instance();

    [[nodiscard]] StrategyEngine* createEngine(const std::string& strategyId);
    [[nodiscard]] StrategyEngine* get(const std::string& id) const;
    void remove(const std::string& id);

    void startAll();
    void pauseAll();
    void resumeAll();
    void stopAll();

    [[nodiscard]] std::vector<OrderRequest> stepAll(const MarketDataPoint& mdp);

    [[nodiscard]] std::size_t count() const;
    [[nodiscard]] bool empty() const;

private:
    StrategyManager() = default;

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::unique_ptr<StrategyEngine>> m_engines;
};

} // namespace domain::strategy
