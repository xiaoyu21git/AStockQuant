#pragma once

/**
 * @file JujinApi.h
 * @brief 掘金API实现
 * 
 * 基于掘金C++ SDK封装，提供统一的第三方API接口。
 */

#include "ThirdPartyApi.h"
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <queue>

// 掘金SDK头文件
extern "C" {
#include <strategy.h>
#include <gmapi.h>
#include <gmdef.h>
#include <error.h>
}

// EventBus头文件
#include "engine/Event/EventBus.hpp"
#include "engine/Event/EventFormat.hpp"

namespace thirdparty {

/**
 * @brief 掘金API实现类
 */
class JujinApi : public IThirdPartyApi {
public:
    JujinApi();
    virtual ~JujinApi();
    
    // IThirdPartyApi接口实现
    bool initialize(const ConfigParams& config) override;
    bool connect() override;
    void disconnect() override;
    
    std::vector<MarketData> get_market_data(
        const std::string& symbol,
        MarketDataType data_type,
        std::chrono::system_clock::time_point start_time,
        std::chrono::system_clock::time_point end_time) override;
    
    std::vector<MarketData> get_realtime_quotes(
        const std::vector<std::string>& symbols) override;
    
    bool subscribe_market_data(
        const std::vector<std::string>& symbols,
        MarketDataType data_type,
        MarketDataCallback callback) override;
    
    bool unsubscribe_market_data(
        const std::vector<std::string>& symbols,
        MarketDataType data_type) override;
    
    AccountInfo get_account_info() override;
    std::vector<PositionInfo> get_positions() override;
    
    std::vector<OrderInfo> get_orders(
        const std::string& symbol = "",
        std::chrono::system_clock::time_point start_time = std::chrono::system_clock::time_point(),
        std::chrono::system_clock::time_point end_time = std::chrono::system_clock::time_point()) override;
    
    std::string place_order(
        const std::string& symbol,
        OrderSide side,
        OrderType type,
        double price,
        double volume) override;
    
    bool cancel_order(const std::string& order_id) override;
    
    void set_order_callback(OrderCallback callback) override;
    void set_position_callback(PositionCallback callback) override;
    void set_account_callback(AccountCallback callback) override;
    void set_error_callback(ErrorCallback callback) override;
    
    PlatformType get_platform_type() const override;
    std::string get_platform_name() const override;
    bool is_connected() const override;
    
    // EventBus集成接口
    void set_event_bus(std::shared_ptr<engine::EventBus> bus);
    std::shared_ptr<engine::EventBus> get_event_bus() const;
    
    // 便捷方法：直接订阅掘金事件
    foundation::Uuid subscribe_jujin_event(
        const std::string& event_type,
        engine::EventFormatHandler handler);
    
private:
    // 内部辅助函数
    std::string convert_to_gm_symbol(const std::string& symbol) const;
    std::string convert_from_gm_symbol(const std::string& gm_symbol) const;
    std::string convert_market_data_type(MarketDataType data_type) const;
    MarketDataType convert_from_gm_frequency(const std::string& frequency) const;
    
    // 掘金回调处理
    static void on_tick_callback(Tick* tick, void* user_data);
    static void on_bar_callback(Bar* bar, void* user_data);
    static void on_order_status_callback(Order* order, void* user_data);
    static void on_position_callback(Position* position, void* user_data);
    static void on_cash_callback(Cash* cash, void* user_data);
    static void on_error_callback(int error_code, const char* error_msg, void* user_data);
    
    // 事件处理
    void process_tick(Tick* tick);
    void process_bar(Bar* bar);
    void process_order_status(Order* order);
    void process_position(Position* position);
    void process_cash(Cash* cash);
    void process_error(int error_code, const std::string& error_msg);
    
    // 线程处理
    void event_loop();
    
private:
    ConfigParams config_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    
    // 掘金策略实例
    Strategy* strategy_{nullptr};
    
    // EventBus实例
    std::shared_ptr<engine::EventBus> event_bus_{nullptr};
    
    // 回调函数
    MarketDataCallback market_data_callback_;
    OrderCallback order_callback_;
    PositionCallback position_callback_;
    AccountCallback account_callback_;
    ErrorCallback error_callback_;
    
    // 线程安全
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread event_thread_;
    
    // 事件队列
    struct Event {
        enum Type {
            TICK,
            BAR,
            ORDER,
            POSITION,
            CASH,
            ERROR
        };
        
        Type type;
        union {
            Tick* tick;
            Bar* bar;
            Order* order;
            Position* position;
            Cash* cash;
            struct {
                int code;
                std::string msg;
            } error;
        } data;
        
        Event() : type(TICK), data{} {}
        ~Event() {
            // 注意：这里不释放内存，由掘金SDK管理
        }
    };
    
    std::queue<Event> event_queue_;
    
    // 订阅管理
    std::map<std::string, std::vector<MarketDataType>> subscriptions_;
};

} // namespace thirdparty