#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <memory>
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
                               const std::string& accountId,
                               const std::string& runtimeStrategyId,
                               const std::unordered_set<std::string>& boundStrategyIds);
    void enqueueWatchSymbol(const std::string& symbol);
    void processSubscriptionRequests(engine::EventBus* eventBus);
    bool subscribeSymbolBatch(const std::vector<std::string>& symbols, engine::EventBus* eventBus);
    bool subscribeSymbol(const std::string& symbol, engine::EventBus* eventBus);
    void publishSubscriptionStatus(engine::EventBus* eventBus, bool active);

    bool m_started = false;
    std::atomic<bool> m_stopRequested{false};
    std::string m_lastError;
    std::thread m_initialOrderSyncThread;
    std::thread m_marketSubscriptionThread;
    std::unique_ptr<thirdparty::JujinApi> m_api;
    std::mutex m_subscriptionMutex;
    std::unordered_set<std::string> m_subscribedSymbols;
    std::mutex m_pendingWatchMutex;
    std::condition_variable m_pendingWatchCv;
    std::deque<std::string> m_pendingWatchQueue;
    std::unordered_set<std::string> m_pendingWatchSymbols;
    size_t m_maxMarketSubscriptions = 512;
    size_t m_marketSubscriptionBatchSize = 4;
    foundation::utils::Uuid m_watchRequestSubscription;
    foundation::utils::Uuid m_tradingTickSubscription;
    foundation::utils::Uuid m_tradingBarSubscription;

    // 标的代码 → 中文名映射
    mutable std::mutex m_symbolNameMutex;
    std::unordered_map<std::string, std::string> m_symbolNames;
public:
    std::string symbolName(const std::string& gmSymbol) const;
};