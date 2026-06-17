#include "TradingRuntimeManager.h"

#include "Event/EventBus.hpp"

namespace {

std::string build_session_id(const thirdparty::ConfigParams& config)
{
    const auto runtimeIt = config.extra_params.find("runtime_strategy_id");
    const auto strategyIt = config.extra_params.find("strategy_id");
    const std::string strategy_id = runtimeIt != config.extra_params.end() && !runtimeIt->second.empty()
        ? runtimeIt->second
        : (strategyIt == config.extra_params.end() ? std::string("default") : strategyIt->second);
    return config.account_id + ":" + strategy_id;
}

} // namespace

namespace thirdparty {

TradingRuntimeManager& TradingRuntimeManager::instance()
{
    static TradingRuntimeManager manager;
    return manager;
}

void TradingRuntimeManager::set_event_bus(std::shared_ptr<engine::EventBus> event_bus)
{
    std::lock_guard<std::mutex> lock(mutex_);
    event_bus_ = std::move(event_bus);
    for (auto& entry : sessions_) {
        entry.second->set_event_bus(event_bus_);
    }
}

std::shared_ptr<GmStrategySession> TradingRuntimeManager::create_session(const ConfigParams& config)
{
    if (config.account_id.empty()) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto session = std::make_shared<GmStrategySession>(build_session_id(config));
    session->set_event_bus(event_bus_);
    if (!session->initialize(config)) {
        return nullptr;
    }

    sessions_[config.account_id] = session;
    return session;
}

std::shared_ptr<GmStrategySession> TradingRuntimeManager::get_session(const std::string& account_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = sessions_.find(account_id);
    return it == sessions_.end() ? nullptr : it->second;
}

bool TradingRuntimeManager::start_session(const std::string& account_id)
{
    const std::shared_ptr<GmStrategySession> session = get_session(account_id);
    return session ? session->start() : false;
}

bool TradingRuntimeManager::stop_session(const std::string& account_id)
{
    const std::shared_ptr<GmStrategySession> session = get_session(account_id);
    return session ? session->stop() : false;
}

void TradingRuntimeManager::stop_all_sessions()
{
    std::vector<std::shared_ptr<GmStrategySession>> sessions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions.reserve(sessions_.size());
        for (const auto& entry : sessions_) {
            sessions.push_back(entry.second);
        }
    }

    for (const std::shared_ptr<GmStrategySession>& session : sessions) {
        session->stop();
    }
}

std::vector<TradingSessionSnapshot> TradingRuntimeManager::session_snapshots() const
{
    std::vector<TradingSessionSnapshot> snapshots;
    std::lock_guard<std::mutex> lock(mutex_);
    snapshots.reserve(sessions_.size());
    for (const auto& entry : sessions_) {
        snapshots.push_back(entry.second->snapshot());
    }
    return snapshots;
}

size_t TradingRuntimeManager::active_session_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

} // namespace thirdparty
