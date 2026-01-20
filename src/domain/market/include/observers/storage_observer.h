// storage_observer.h
#pragma once

#include "data_observer_base.h"
#include <map>
#include <queue>
#include <thread>

namespace astock {
namespace market {
namespace observer {

/**
 * @brief 数据存储观察者 - 将数据保存到文件或数据库
 */
class StorageObserver : public AsyncDataObserver {
public:
    /**
     * @brief 存储类型
     */
    enum class StorageType {
        CSV_FILE,
        BINARY_FILE,
        DATABASE,
        INFLUXDB,
        ELASTICSEARCH
    };
    
    /**
     * @brief 存储配置
     */
    struct StorageConfig {
        StorageType type = StorageType::CSV_FILE;
        std::string storage_path = "./data";
        std::string file_prefix = "market_data";
        
        // 文件存储配置
        size_t max_file_size_mb = 100;
        size_t max_files = 100;
        bool rotate_daily = true;
        bool compress_old_files = false;
        
        // 数据库存储配置
        std::string db_connection_string;
        std::string db_table_prefix = "market_";
        size_t batch_insert_size = 1000;
        
        // 性能配置
        bool enable_buffer = true;
        size_t buffer_size = 10000;
        std::chrono::seconds flush_interval = std::chrono::seconds(5);
        bool sync_on_write = false;
        
        // 数据过滤
        bool store_klines = true;
        bool store_ticks = true;
        std::vector<std::string> store_symbols;  // 空表示存储所有
        std::vector<uint16_t> store_periods = {60, 300, 900, 3600};
    };
    
    StorageObserver(const std::string& name,
                   const StorageConfig& config,
                   const std::string& description = "");
    ~StorageObserver() override;
    
    /**
     * @brief 更新存储配置
     */
    void update_config(const StorageConfig& config);
    
    /**
     * @brief 获取存储统计
     */
    struct StorageStats {
        size_t total_klines_stored = 0;
        size_t total_ticks_stored = 0;
        size_t total_files_created = 0;
        size_t current_buffer_size = 0;
        size_t total_flush_count = 0;
        std::chrono::nanoseconds total_storage_time{0};
        
        double storage_rate_klines_per_sec() const;
        double storage_rate_ticks_per_sec() const;
        std::string to_string() const;
    };
    
    StorageStats get_stats() const;
    
    /**
     * @brief 手动刷新缓冲区
     */
    void flush_buffer();
    
    /**
     * @brief 清理旧数据
     */
    void cleanup_old_data(uint64_t before_timestamp);
    
    /**
     * @brief 导出数据到文件
     */
    bool export_data(const std::string& output_path,
                    const std::string& symbol = "",
                    uint64_t start_time = 0,
                    uint64_t end_time = 0,
                    bool include_ticks = true);
    
protected:
    void process_events_batch(const std::vector<DataEvent>& events) override;
    void process_event(const DataEvent& event) override;
    
private:
    // 数据处理
    void process_kline_for_storage(const DataEvent& event);
    void process_tick_for_storage(const DataEvent& event);
    
    // 存储实现
    void store_to_csv(const foundation::KLine& kline);
    void store_to_csv(const foundation::TickData& tick);
    void store_to_binary(const foundation::KLine& kline);
    void store_to_binary(const foundation::TickData& tick);
    void store_to_database(const foundation::KLine& kline);
    void store_to_database(const foundation::TickData& tick);
    
    // 文件管理
    std::string get_current_filename(const std::string& symbol, 
                                    const std::string& data_type) const;
    void rotate_file_if_needed(const std::string& symbol,
                              const std::string& data_type);
    void compress_old_files();
    
    // 缓冲区管理
    void flush_buffer_impl();
    void flush_worker_thread();
    
    // 数据缓冲区
    struct BufferEntry {
        DataEventType type;
        std::string symbol;
        uint64_t timestamp;
        std::vector<char> data;
    };
    
    StorageConfig config_;
    std::queue<BufferEntry> storage_buffer_;
    mutable std::mutex buffer_mutex_;
    std::condition_variable buffer_cv_;
    
    std::atomic<bool> flush_running_{false};
    std::thread flush_thread_;
    
    // 文件句柄管理
    struct FileHandle {
        std::string filename;
        uint64_t file_size = 0;
        uint64_t creation_time = 0;
    };
    
    std::map<std::string, FileHandle> current_files_;  // symbol_data_type -> handle
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    StorageStats stats_;
    
    // Foundation 工具
    foundation::fs::File& fs_;
    foundation::utils::Time& time_;
};

} // namespace observer
} // namespace market
} // namespace astock