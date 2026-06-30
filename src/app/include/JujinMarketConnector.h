#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "foundation/Utils/Uuid.h"
#include "JujinTypes.h"

namespace engine {
class EventBus;
struct EventFormat;
}

class JujinMarketConnector {
public:
    JujinMarketConnector();
    ~JujinMarketConnector();

    bool isEnabledByEnvironment() const;
    bool start();
    void stop();
    const std::string& lastError() const;

private:
    void publishExistingOrders(engine::EventBus* eventBus,
                               const std::string& token,
                               const std::string& accountId,
                               const std::string& runtimeStrategyId,
                               const std::unordered_set<std::string>& boundStrategyIds);

    bool m_started = false;
    std::atomic<bool> m_stopRequested{false};
    std::string m_lastError;
    std::thread m_initialOrderSyncThread;

    // 标的代码 → 中文名映射
    mutable std::mutex m_symbolNameMutex;
    std::unordered_map<std::string, std::string> m_symbolNames;
public:
    std::string symbolName(const std::string& gmSymbol) const;
};
