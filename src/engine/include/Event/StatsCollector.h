// StatsCollector.h
#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <sstream>

namespace engine {

class StatsCollector {
private:
    std::atomic<size_t> total_events_processed_{0};
    std::atomic<size_t> total_batches_processed_{0};
    std::atomic<size_t> max_queue_size_{0};
    
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point last_activity_time_;
    std::atomic<size_t> peak_processing_rate_{0};
    
public:
    StatsCollector() {
        reset();
    }
    
    // 记录事件处理
    void record_events_processed(size_t count, bool is_batch = false) {
        total_events_processed_ += count;
        if (is_batch) {
            total_batches_processed_++;
        }
        last_activity_time_ = std::chrono::steady_clock::now();
        
        // 更新峰值处理速率
        update_peak_rate(count);
    }
    
    // 记录队列大小
    void record_queue_size(size_t size) {
        size_t old_max = max_queue_size_;
        while (size > old_max && 
               !max_queue_size_.compare_exchange_weak(old_max, size)) {
            // 循环直到更新成功
        }
    }
    
    // 获取统计信息
    struct Statistics {
        size_t current_size = 0;
        size_t max_size = 0;
        size_t total_enqueued = 0;
        size_t total_dequeued = 0;
        size_t estimated_wait_ms = 0;
        size_t total_events_processed = 0;
        size_t total_batches_processed = 0;
        std::chrono::milliseconds idle_time_ms{0};
        std::chrono::seconds uptime{0};
        double processing_rate = 0.0;  // 事件/秒
        
        Statistics(size_t current_size_, size_t max_size_, size_t total_enqueued_,
                  size_t total_dequeued_, size_t estimated_wait_ms_,
                  size_t total_events_processed_, size_t total_batches_processed_,
                  std::chrono::milliseconds idle_time_ms_, std::chrono::seconds uptime_,
                  double processing_rate_)
            : current_size(current_size_), max_size(max_size_), 
              total_enqueued(total_enqueued_), total_dequeued(total_dequeued_),
              estimated_wait_ms(estimated_wait_ms_),
              total_events_processed(total_events_processed_),
              total_batches_processed(total_batches_processed_),
              idle_time_ms(idle_time_ms_), uptime(uptime_),
              processing_rate(processing_rate_) {}
        
        std::string to_string() const {
            std::ostringstream oss;
            oss << "Stats{"
                << "size=" << current_size
                << ", max=" << max_size
                << ", processed=" << total_events_processed
                << ", batches=" << total_batches_processed
                << ", idle_ms=" << idle_time_ms.count()
                << ", uptime=" << uptime.count() << "s"
                << ", rate=" << processing_rate << "/s"
                << "}";
            return oss.str();
        }
    };
    
    Statistics get_statistics(size_t current_queue_size = 0) const {
        auto now = std::chrono::steady_clock::now();
        auto idle_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_activity_time_);
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            now - start_time_);
        
        double processing_rate = 0.0;
        if (uptime.count() > 0) {
            processing_rate = static_cast<double>(total_events_processed_) / uptime.count();
        }
        
        return Statistics(
            current_queue_size,
            max_queue_size_.load(),
            0,  // total_enqueued (需要从队列获取)
            total_events_processed_.load(),
            0,  // estimated_wait_ms
            total_events_processed_.load(),
            total_batches_processed_.load(),
            idle_time,
            uptime,
            processing_rate
        );
    }
    
    // 重置统计
    void reset() {
        total_events_processed_ = 0;
        total_batches_processed_ = 0;
        max_queue_size_ = 0;
        start_time_ = std::chrono::steady_clock::now();
        last_activity_time_ = start_time_;
        peak_processing_rate_ = 0;
    }
    
    // 获取特定统计
    size_t get_total_processed() const { return total_events_processed_.load(); }
    size_t get_total_batches() const { return total_batches_processed_.load(); }
    size_t get_max_queue_size() const { return max_queue_size_.load(); }
    
private:
    void update_peak_rate(size_t count) {
        // 简单实现：记录单次处理的最大数量
        size_t old_peak = peak_processing_rate_;
        while (count > old_peak && 
               !peak_processing_rate_.compare_exchange_weak(old_peak, count)) {
            // 循环直到更新成功
        }
    }
};

} // namespace engine