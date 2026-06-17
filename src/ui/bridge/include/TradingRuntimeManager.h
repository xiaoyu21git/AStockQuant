#pragma once

#include "GmStrategySession.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {
class EventBus;
}

namespace thirdparty {

class TradingRuntimeManager {
public:
    static TradingRuntimeManager& instance();

    void set_event_bus(std::shared_ptr<engine::EventBus> event_bus);

    std::shared_ptr<GmStrategySession> create_session(const ConfigParams& config);
    std::shared_ptr<GmStrategySession> get_session(const std::string& account_id) const;

    bool start_session(const std::string& account_id);
    bool stop_session(const std::string& account_id);
    void stop_all_sessions();

    std::vector<TradingSessionSnapshot> session_snapshots() const;
    size_t active_session_count() const;

private:
    TradingRuntimeManager() = default;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<GmStrategySession>> sessions_;
    std::shared_ptr<engine::EventBus> event_bus_;
};

} // namespace thirdparty
