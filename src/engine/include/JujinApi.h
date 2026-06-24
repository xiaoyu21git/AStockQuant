#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <functional>

namespace engine {
class EventBus;
}

namespace thirdparty {

enum class PlatformType {
    JUJIN,      // 鎺橀噾
    SIMULATION, // 浠跨湡
    UNKNOWN
};

enum class OrderSide {
    BUY,
    SELL
};

enum class OrderType {
    MARKET,
    LIMIT
};

enum class MarketDataType {
    TICK,
    BAR_1M
};

struct ConfigParams {
    PlatformType platform = PlatformType::JUJIN;
    std::string token;
    std::string account_id;
    std::string server_url;
    std::map<std::string, std::string> extra_params;
};

struct Position {
    std::string symbol;
    std::string name;
    int64_t quantity = 0;
    double price = 0.0;
    double market_value = 0.0;
    double pnl = 0.0;
    double pnl_percent = 0.0;
    std::string direction; // "LONG", "SHORT"
    std::string entry_time;
    std::string update_time;
};

struct AccountInfo {
    double total_asset = 0.0;
    double cash = 0.0;
    double available = 0.0;
    double buying_power = 0.0;
    double frozen = 0.0;
    double market_value = 0.0;
    double pnl = 0.0;
    double pnl_percent = 0.0;
    std::string update_time;
};

struct OrderResult {
    std::string order_id;
    std::string symbol;
    std::string exchange;
    std::string side;
    std::string status;
    std::string message;
    int64_t quantity = 0;
    int64_t filled_quantity = 0;
    double price = 0.0;
    double avg_price = 0.0;
    double filled_notional = 0.0;
    std::string submit_time;
    std::string update_time;
};

class JujinApi {
public:
    JujinApi();
    ~JujinApi();

    // 绂佹鎷疯礉
    JujinApi(const JujinApi&) = delete;
    JujinApi& operator=(const JujinApi&) = delete;

    // 鍒濆鍖栧拰杩炴帴
    bool initialize(const ConfigParams& config);
    bool connect();
    bool disconnect();
    bool is_connected() const;
    bool is_initialized() const;

    // 浜ゆ槗鎺ュ彛
    std::string place_order(const std::string& symbol,
                           OrderSide side,
                           OrderType type,
                           double price,
                           double quantity,
                           const std::string& client_order_id = std::string(),
                           const std::map<std::string, std::string>& metadata = {});
    
    bool cancel_order(const std::string& order_id);
    bool subscribe_market_data(const std::vector<std::string>& symbols,
                               MarketDataType type,
                               const std::map<std::string, std::string>& options = {});
    
    // 鏌ヨ鎺ュ彛
    std::vector<Position> query_positions();
    AccountInfo query_account();
    
    // 璁㈠崟鏌ヨ
    OrderResult query_order(const std::string& order_id);
    std::vector<OrderResult> query_orders(const std::string& symbol = "",
                                         const std::string& status = "",
                                         int limit = 100);
    
    // 浜嬩欢闆嗘垚
    void set_event_bus(std::shared_ptr<engine::EventBus> bus);
    
    // 閿欒澶勭悊
    std::string last_error_message() const;
    void clear_error();
    
    // 閰嶇疆鑾峰彇
    ConfigParams get_config() const;
    
    // 状态检查
    bool check_connection() const;
    std::string get_connection_status() const;

    // 测试钩子：允许在不启动真实共享会话时伪造一个已连接 broker transport。
    void enable_test_transport(const ConfigParams& config,
                               bool connected = true,
                               const OrderResult& order_template = {});
    void disable_test_transport();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace thirdparty









