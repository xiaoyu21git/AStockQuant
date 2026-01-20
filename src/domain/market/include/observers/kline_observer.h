// kline_observer.h
#pragma once

#include "data_observer_base.h"
#include "foundation/data_types.h"
#include <deque>
#include <map>

namespace astock {
namespace market {
namespace observer {

/**
 * @brief K线数据观察者 - 专门处理K线数据
 */
class KLineObserver : public AsyncDataObserver {
public:
    struct KLineBuffer {
        std::deque<foundation::KLine> buffer;
        uint64_t last_timestamp = 0;
        size_t max_size = 1000;
        
        void add_kline(const foundation::KLine& kline);
        foundation::KLine get_latest() const;
        std::vector<foundation::KLine> get_recent(size_t count) const;
        void clear();
    };
    
    /**
     * @brief K线聚合配置
     */
    struct AggregationConfig {
        uint16_t source_period = 60;      // 源数据周期（秒）
        uint16_t target_period = 300;     // 目标周期（秒）
        bool auto_adjust = true;          // 自动调整周期
        bool fill_gaps = true;            // 填充数据缺口
        bool validate_output = true;      // 验证输出数据
    };
    
    /**
     * @brief 技术指标配置
     */
    struct IndicatorConfig {
        bool calculate_ma = true;         // 计算移动平均线
        std::vector<int> ma_periods = {5, 10, 20, 60};  // MA周期
        bool calculate_boll = false;      // 计算布林带
        int boll_period = 20;             // 布林带周期
        double boll_std_dev = 2.0;        // 标准差倍数
        bool calculate_macd = false;      // 计算MACD
        int macd_fast = 12;               // MACD快线
        int macd_slow = 26;               // MACD慢线
        int macd_signal = 9;              // MACD信号线
        bool calculate_rsi = false;       // 计算RSI
        int rsi_period = 14;              // RSI周期
        bool calculate_kdj = false;       // 计算KDJ
        int kdj_period = 9;               // KDJ周期
    };
    
    KLineObserver(const std::string& name, 
                  const std::string& description = "");
    ~KLineObserver() override;
    
    /**
     * @brief 设置K线聚合配置
     */
    void set_aggregation_config(const AggregationConfig& config);
    
    /**
     * @brief 设置技术指标配置
     */
    void set_indicator_config(const IndicatorConfig& config);
    
    /**
     * @brief 获取指定交易对的K线缓冲
     */
    KLineBuffer* get_kline_buffer(const std::string& symbol, uint16_t period = 0);
    
    /**
     * @brief 获取最新K线数据
     */
    foundation::KLine get_latest_kline(const std::string& symbol, uint16_t period = 0) const;
    
    /**
     * @brief 获取历史K线数据
     */
    std::vector<foundation::KLine> get_history_klines(
        const std::string& symbol, 
        uint16_t period = 0,
        size_t limit = 100) const;
    
    /**
     * @brief 添加自定义指标计算器
     */
    using IndicatorCalculator = std::function<void(const std::vector<foundation::KLine>&, 
                                                   foundation::KLine&)>;
    void add_indicator_calculator(const std::string& name, IndicatorCalculator calculator);
    
protected:
    void process_events_batch(const std::vector<DataEvent>& events) override;
    
private:
    // K线数据处理
    void process_kline_update(const DataEvent& event);
    void process_kline_complete(const DataEvent& event);
    
    // K线聚合
    foundation::KLine aggregate_klines(
        const std::vector<foundation::KLine>& source_klines,
        uint16_t target_period) const;
    
    // 技术指标计算
    void calculate_indicators(const std::string& symbol, uint16_t period);
    void calculate_ma(const std::vector<foundation::KLine>& klines, 
                     foundation::KLine& current_kline) const;
    void calculate_boll(const std::vector<foundation::KLine>& klines,
                       foundation::KLine& current_kline) const;
    void calculate_macd(const std::vector<foundation::KLine>& klines,
                       foundation::KLine& current_kline) const;
    void calculate_rsi(const std::vector<foundation::KLine>& klines,
                      foundation::KLine& current_kline) const;
    
    // 数据存储
    std::map<std::string, std::map<uint16_t, KLineBuffer>> kline_buffers_;
    mutable std::shared_mutex buffers_mutex_;
    
    // 配置
    AggregationConfig agg_config_;
    IndicatorConfig indicator_config_;
    
    // 自定义指标计算器
    std::map<std::string, IndicatorCalculator> indicator_calculators_;
    
    // Foundation 工具
    foundation::utils::Time& time_;
};

} // namespace observer
} // namespace market
} // namespace astock