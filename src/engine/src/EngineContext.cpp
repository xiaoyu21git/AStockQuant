#include "EngineContext.h"
#include "EngineImpl.h"
#include "IClock.h"
#include "EventBus.h"
#include "IDataSource.h"
#include <foundation.h>

namespace engine {

class EngineContextImpl : public EngineContext {
public:
    explicit EngineContextImpl(Engine* engine) :engine_(engine) {
        if (!engine_) {
            throw std::invalid_argument("Engine cannot be null");
        }
        context_id_ = foundation::Uuid_create();
    }

    Engine* engine() override {
        return engine_;
    }

    const Engine* engine() const override {
        return engine_;
    }

    Timestamp current_time() const override {
        auto impl = dynamic_cast<EngineImpl*>(engine_);
        if (!impl || !impl->clock()) {
            return foundation::timestamp_now();
        }
        return impl->clock()->current_time();
    }

    Clock* clock() override {
        auto impl = dynamic_cast<EngineImpl*>(engine_);
        return impl ? impl->clock() : nullptr;
    }

    EventBus* event_bus() override {
        auto impl = dynamic_cast<EngineImpl*>(engine_);
        return impl ? impl->event_bus() : nullptr;
    }

    DataSource* find_data_source(const std::string& name) override {
        auto impl = dynamic_cast<EngineImpl*>(engine_);
        return impl ? impl->get_data_source(name) : nullptr;
    }

    std::vector<std::string> all_data_source_names() const override {
        auto impl = dynamic_cast<EngineImpl*>(engine_);
        if (!impl) return {};
        return impl->data_source_names();
    }

    Error publish_event(std::unique_ptr<Event> event) override {
        auto impl = dynamic_cast<EngineImpl*>(engine_);
        if (!impl) {
            return Error::fail(Error::Code::NOT_FOUND, "Engine implementation not available");
        }
        return impl->publish_event(std::move(event));
    }

    void set_user_data(const std::string& key, std::any value) override {
        std::lock_guard<std::mutex> lock(user_data_mutex_);
        user_data_[key] = std::move(value);
    }

    std::any get_user_data(const std::string& key) const override {
        std::lock_guard<std::mutex> lock(user_data_mutex_);
        auto it = user_data_.find(key);
        if (it != user_data_.end()) {
            return it->second;
        }
        return {};
    }

    bool has_user_data(const std::string& key) const override {
        std::lock_guard<std::mutex> lock(user_data_mutex_);
        return user_data_.find(key) != user_data_.end();
    }

    bool remove_user_data(const std::string& key) override {
        std::lock_guard<std::mutex> lock(user_data_mutex_);
        return user_data_.erase(key) > 0;
    }

    std::vector<std::string> all_user_data_keys() const override {
        std::lock_guard<std::mutex> lock(user_data_mutex_);
        std::vector<std::string> keys;
        keys.reserve(user_data_.size());
        for (const auto& pair : user_data_) {
            keys.push_back(pair.first);
        }
        return keys;
    }

    void set_engine_flag(const std::string& flag, bool value) override {
        std::lock_guard<std::mutex> lock(flags_mutex_);
        engine_flags_[flag] = value;
    }

    bool get_engine_flag(const std::string& flag) const override {
        std::lock_guard<std::mutex> lock(flags_mutex_);
        auto it = engine_flags_.find(flag);
        if (it != engine_flags_.end()) {
            return it->second;
        }
        return false;
    }

    std::string get_config_param(const std::string& key) const override {
        auto impl = dynamic_cast<EngineImpl*>(engine_);
        if (!impl) return "";
        
        const auto& config = impl->config();
        auto it = config.parameters.find(key);
        if (it != config.parameters.end()) {
            return it->second;
        }
        return "";
    }

    std::string get_runtime_stats() const override {
        auto impl = dynamic_cast<EngineImpl*>(engine_);
        if (!impl) return "{}";
        
        auto stats = impl->statistics();
        auto current_time = foundation::timestamp_now();
        auto uptime =current_time - stats.start_time;
        
        std::ostringstream ss;
        ss << "{"
           << "\"total_events_processed\":" << stats.total_events_processed << ","
           << "\"total_triggers_fired\":" << stats.total_triggers_fired << ","
           << "\"uptime_ms\":" << uptime.to_milliseconds()
           << "}";
        return ss.str();
    }

    bool is_engine_running() const override {
        auto impl = dynamic_cast<EngineImpl*>(engine_);
        if (!impl) return false;
        return impl->state() == EngineListener::State::Running;
    }

    bool is_backtest_mode() const override {
        auto impl = dynamic_cast<EngineImpl*>(engine_);
        if (!impl || !impl->clock()) return false;
        return impl->clock()->mode() == Clock::Mode::Backtest;
    }

    bool is_realtime_mode() const override {
        auto impl = dynamic_cast<EngineImpl*>(engine_);
        if (!impl || !impl->clock()) return false;
        return impl->clock()->mode() == Clock::Mode::Realtime;
    }

    foundation::Uuid context_id() const override {
        return context_id_;
    }

    Timestamp engine_start_time() const override {
        auto impl = dynamic_cast<EngineImpl*>(engine_);
        if (!impl) return Timestamp();
        
        auto stats = impl->statistics();
        return stats.start_time;
    }

    Duration engine_uptime() const override {
        auto impl = dynamic_cast<EngineImpl*>(engine_);
        if (!impl) return Duration::zero();
        
        auto stats = impl->statistics();
        auto current_time = foundation::timestamp_now();
        return current_time - stats.start_time;
    }

private:
    Engine* engine_;
    foundation::Uuid context_id_;
    
    mutable std::mutex user_data_mutex_;
    std::map<std::string, std::any> user_data_;
    
    mutable std::mutex flags_mutex_;
    std::map<std::string, bool> engine_flags_;
};

std::unique_ptr<EngineContext> EngineContext::create(Engine* engine) {
    return std::make_unique<EngineContextImpl>(engine);
}

} // namespace engine