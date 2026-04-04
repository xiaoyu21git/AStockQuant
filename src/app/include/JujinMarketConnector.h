#pragma once

#include <atomic>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "foundation/Utils/Uuid.h"

namespace engine {
class EventBus;
struct EventFormat;
}

namespace thirdparty {
class JujinApi;
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
                               const std::string& accountId);
    bool subscribeSymbol(const std::string& symbol, engine::EventBus* eventBus);
    std::vector<std::string> watchlistFromEnvironment() const;
    std::string readEnvironment(const char* name, const char* fallback = "") const;

    bool m_started = false;
    std::atomic<bool> m_stopRequested{false};
    std::string m_lastError;
    std::thread m_initialOrderSyncThread;
    std::unique_ptr<thirdparty::JujinApi> m_api;
    std::mutex m_subscriptionMutex;
    std::unordered_set<std::string> m_subscribedSymbols;
    foundation::utils::Uuid m_watchRequestSubscription;
};