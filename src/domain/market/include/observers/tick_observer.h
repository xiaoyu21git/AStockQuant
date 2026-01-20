// tick_observer.h
#pragma once

#include "data_observer_base.h"
#include "foundation/data_types.h"
#include <deque>
#include <map>

namespace astock {
namespace market {
namespace observer {

/**
 * @brief Tick数据观察者 - 专门处理Tick数据
 */
class TickObserver : public AsyncDataObserver {
public:
    struct TickBuffer {
        std::deque<foundation::TickData> buffer;
        uint64_t last_timestamp = 0;
        size_t max_size = 5000;  // Tick数据频率高，需要更大的缓冲区
        
        void add_tick(const foundation::TickData& tick);
        foundation::TickData get_latest() const;
        std::vector<foundation::TickData> get_recent(size_t count) const;
        void clear();
    };
    
    /**
     * @brief Tick统计信息
     */
    struct TickStats {
        uint64_t total_ticks = 0;
        uint64_t ticks_per_second = 0;
        double avg_price = 0.0;
        double total_volume = 0.0;
        double total_amount = 0.0;
        foundation::TickData last_tick;
        foundation::TickData max_tick;
        foundation::TickData min_tick;
        
        void update(const foundation::TickData& tick);
        void reset();
        std::string to_string() const;
    };
    
    /**
     * @brief 订单簿快照
     */
    struct OrderBookSnapshot {
        uint64_t timestamp;
        std::string symbol;
        std::vector<std::pair<double, double>> bids;  // price, volume
        std::vector<std::pair<double, double>> asks;  // price, volume
        
        double get_bid_price(int level = 0) const;
        double get_ask_price(int level = 0) const;
        double get_mid_price() const;
        double get_spread() const;
        double get_weighted_average_price() const;
    };
    
    TickObserver(const std::string& name,
                 const std::string& description = "");
    ~TickObserver() override;
    
    /**
     * @brief 设置统计窗口大小
     */
    void set_stats_window_size(size_t window_size);
    
    /**
     * @brief 设置订单簿深度
     */
    void set_order_book_depth(size_t depth);
    
    /**
     * @brief 获取Tick统计信息
     */
    TickStats get_tick_stats(const std::string& symbol) const;
    
    /**
     * @brief 获取最新订单簿快照
     */
    OrderBookSnapshot get_order_book_snapshot(const std::string& symbol) const;
    
    /**
     * @brief 获取Tick数据流
     */
    std::vector<foundation::TickData> get_tick_stream(
        const std::string& symbol,
        uint64_t start_time = 0,
        uint64_t end_time = 0,
        size_t limit = 0) const;
    
    /**
     * @brief 计算成交价格水平
     */
    std::map<double, double> calculate_trade_levels(
        const std::string& symbol,
        double price_interval = 0.01,
        size_t lookback_ticks = 1000) const;
    
    /**
     * @brief 计算买卖压力
     */
    struct BuySellPressure {
        double buy_pressure = 0.0;
        double sell_pressure = 0.0;
        double pressure_ratio = 0.0;
        uint64_t buy_volume = 0;
        uint64_t sell_volume = 0;
    };
    
    BuySellPressure calculate_pressure(const std::string& symbol, 
                                      size_t lookback_ticks = 100) const;
    
protected:
    void process_events_batch(const std::vector<DataEvent>& events) override;
    
private:
    // Tick数据处理
    void process_tick_update(const DataEvent& event);
    
    // 统计计算
    void update_tick_stats(const foundation::TickData& tick);
    
    // 订单簿管理
    void update_order_book(const foundation::TickData& tick);
    
    // 数据存储
    std::map<std::string, TickBuffer> tick_buffers_;
    std::map<std::string, TickStats> tick_stats_;
    std::map<std::string, OrderBookSnapshot> order_books_;
    mutable std::shared_mutex data_mutex_;
    
    // 配置
    size_t stats_window_size_ = 1000;
    size_t order_book_depth_ = 10;
    
    // 时间窗口统计
    struct TimeWindowStats {
        std::deque<foundation::TickData> window;
        size_t max_size;
        
        void add_tick(const foundation::TickData& tick);
        void clear();
    };
    
    std::map<std::string, TimeWindowStats> time_windows_;
};

} // namespace observer
} // namespace market
} // namespace astock