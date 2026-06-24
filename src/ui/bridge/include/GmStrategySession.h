#pragma once

#include "../../../engine/include/JujinApi.h"
#include "TradingCommandQueue.h"

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace engine {
class EventBus;
}

namespace thirdparty {

class RuntimeStrategy;

enum class TradingSessionState {
    Created,
    Initialized,
    Starting,
    Running,
    Stopping,
    Stopped,
    Error
};

struct TradingSessionSnapshot {
    std::string session_id;
    std::string account_id;
    std::string strategy_id;
    TradingSessionState state = TradingSessionState::Created;
    bool initialized = false;
    bool connected = false;
    std::string last_error;
    std::vector<std::string> subscriptions;
};

class GmStrategySession {
public:
    explicit GmStrategySession(std::string session_id);
    ~GmStrategySession();

    bool initialize(const ConfigParams& config);
    bool start();
    bool stop();

    bool enqueue_command(const TradingCommand& command);
    size_t drain_pending_commands(size_t max_count = 0);

    void set_event_bus(std::shared_ptr<engine::EventBus> event_bus);

    bool is_initialized() const;
    bool is_connected() const;
    bool is_running() const;

    std::string last_error_message() const;
    ConfigParams config() const;
    std::string session_id() const;
    TradingSessionSnapshot snapshot() const;
    std::shared_ptr<TradingCommandQueue> command_queue() const;
    std::vector<Position> snapshot_positions() const;
    AccountInfo snapshot_account() const;
    OrderResult snapshot_order(const std::string& order_id) const;
    std::vector<OrderResult> snapshot_orders() const;

private:
    struct PendingOrderReconciliation {
        std::string broker_order_id;
        std::string correlation_id;
        int attempts = 0;
        int next_due_tick = 0;
    };

    struct ExecutionFillProgress {
        int64_t cumulative_quantity = 0;
        double cumulative_notional = 0.0;
        std::set<std::string> exec_ids;
    };

    friend class RuntimeStrategy;

    void apply_command_locked(const TradingCommand& command);
    void publish_event(const std::string& event_type, const TradingCommand* command = nullptr) const;
    void set_error_locked(const std::string& message);
    void mark_runtime_started_locked();
    void mark_runtime_stopped_locked(const std::string& error_message);
    void sync_initial_state_locked();
    void schedule_order_reconciliation_locked(const std::string& order_id,
                                              const std::string& broker_order_id,
                                              const std::string& correlation_id);
    void reconcile_pending_orders_locked();
    void cache_order_locked(const std::string& order_id, const OrderResult& order);
    void cache_position_locked(const Position& position);
    void cache_account_locked(const AccountInfo& account);

    mutable std::mutex mutex_;
    std::string session_id_;
    ConfigParams config_;
    TradingSessionState state_ = TradingSessionState::Created;
    bool initialized_ = false;
    bool connected_ = false;
    std::string last_error_;
    std::vector<std::string> subscriptions_;
    std::shared_ptr<engine::EventBus> event_bus_;
    std::shared_ptr<TradingCommandQueue> command_queue_;
    std::unique_ptr<RuntimeStrategy> strategy_;
    std::thread runtime_thread_;
    std::thread::id runtime_thread_id_;
    int command_timer_id_ = 0;
    bool stop_requested_ = false;
    bool has_account_snapshot_ = false;
    AccountInfo account_snapshot_;
    std::map<std::string, Position> positions_;
    std::map<std::string, OrderResult> orders_;
    std::map<std::string, std::map<std::string, std::string>> order_contexts_;
    std::map<std::string, std::string> order_aliases_;
    std::map<std::string, ExecutionFillProgress> order_fill_progress_;
    std::map<std::string, PendingOrderReconciliation> pending_order_reconciliations_;
    int reconciliation_tick_ = 0;
};

} // namespace thirdparty


