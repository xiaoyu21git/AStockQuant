// SlimDispatchWorker.h
#pragma once

#include "EventProcessor.h"
#include "StatsCollector.h"
#include <memory>
#include <atomic>
#include <thread>
#include <condition_variable>

namespace engine {

class SlimDispatchWorker {
private:
    // 核心组件
    std::shared_ptr<EventProcessor> processor_;
    std::unique_ptr<StatsCollector> stats_collector_;
    
    // 线程控制
    std::thread worker_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_flag_{false};
    std::condition_variable cv_;
    std::mutex cv_mutex_;
    
    // 简单配置
    bool enable_batch_processing_{true};
    size_t batch_size_{100};
    std::chrono::milliseconds poll_interval_{50};
    
public:
    // 简洁的构造函数
    SlimDispatchWorker(
        std::shared_ptr<EventQueue> queue,
        std::shared_ptr<SubscriptionManager> subs_mgr,
        std::shared_ptr<IEventDispatcher> dispatcher)
        : processor_(std::make_shared<EventProcessor>(
            std::move(queue), std::move(dispatcher), std::move(subs_mgr)))
        , stats_collector_(std::make_unique<StatsCollector>()) {
    }
    
    ~SlimDispatchWorker() {
        stop();
    }
    
    // ===== 简洁的公共接口 =====
    
    // 启动工作线程
    void start() {
        if (running_.exchange(true)) {
            return;
        }
        
        stop_flag_ = false;
        worker_thread_ = std::thread([this]() {
            work_loop();
        });
    }
    
    // 停止工作线程
    void stop() {
        if (!running_.exchange(false)) {
            return;
        }
        
        stop_flag_ = true;
        cv_.notify_all();
        
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
    
    // 提交事件
    void submit(std::unique_ptr<Event> event) {
        processor_->submit(std::move(event));
        stats_collector_->record_queue_size(processor_->queue_size());
        
        // 通知工作线程
        cv_.notify_one();
    }
    
    void submit(const EventFormat& event) {
        processor_->submit(event);
        stats_collector_->record_queue_size(processor_->queue_size());
        cv_.notify_one();
    }
    
    // 同步处理一次
    size_t process_once() {
        if (processor_->process_one()) {
            stats_collector_->record_events_processed(1);
            return 1;
        }
        return 0;
    }
    
    // 批量处理
    size_t process_batch(size_t max_events = 100) {
        size_t processed = processor_->process_smart_batch(max_events);
        if (processed > 0) {
            stats_collector_->record_events_processed(processed, true);
        }
        return processed;
    }
    
    // 等待完成
    void wait_for_completion() {
        while (processor_->queue_size() > 0 && running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    // ===== 状态查询 =====
    bool is_running() const { return running_; }
    size_t queue_size() const { return processor_->queue_size(); }
    size_t processed_count() const { return stats_collector_->get_total_processed(); }
    size_t batch_count() const { return stats_collector_->get_total_batches(); }
    
    // ===== 配置 =====
    void set_batch_size(size_t size) { 
        if (size > 0) batch_size_ = size; 
    }
    void set_poll_interval(std::chrono::milliseconds interval) { 
        poll_interval_ = interval; 
    }
    void enable_batch(bool enable) { 
        enable_batch_processing_ = enable; 
    }
    
    // ===== 组件访问 =====
    std::shared_ptr<EventProcessor> get_processor() const { return processor_; }
    StatsCollector* get_stats_collector() const { return stats_collector_.get(); }
    
private:
    void work_loop() {
        while (!stop_flag_) {
            // 处理事件
            size_t processed = 0;
            if (enable_batch_processing_) {
                processed = process_batch(batch_size_);
            } else {
                processed = process_once();
            }
            
            // 如果处理了事件，继续处理
            if (processed > 0) {
                continue;
            }
            
            // 没有事件，等待
            std::unique_lock<std::mutex> lock(cv_mutex_);
            cv_.wait_for(lock, poll_interval_);
        }
    }
};

} // namespace engine