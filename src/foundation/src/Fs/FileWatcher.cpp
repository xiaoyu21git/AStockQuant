// foundation/src/fs/FileWatcher.cpp
#include "foundation/fs/FileWatcher.h"
#include "foundation/log/logging.hpp"
#include <algorithm>

namespace foundation {
namespace fs {

// 构造函数和析构函数
FileWatcher::~FileWatcher() {
    stop();
}

FileWatcher::FileWatcher(FileWatcher&& other) noexcept
    : config_(std::move(other.config_))
    , impl_(std::move(other.impl_))
    , watch_items_(std::move(other.watch_items_))
    , running_(other.running_.load())
    , stats_(std::move(other.stats_)) {
    other.running_ = false;
}

FileWatcher& FileWatcher::operator=(FileWatcher&& other) noexcept {
    if (this != &other) {
        stop();
        config_ = std::move(other.config_);
        impl_ = std::move(other.impl_);
        watch_items_ = std::move(other.watch_items_);
        running_ = other.running_.load();
        stats_ = std::move(other.stats_);
        other.running_ = false;
    }
    return *this;
}

// 公共方法实现
bool FileWatcher::watch(const std::string& path, FileChangeCallback callback) {
    if (path.empty()) {
        INTERNAL_ERROR("Cannot watch empty path");
        return false;
    }
    
    std::lock_guard<std::mutex> lock(items_mutex_);
    
    // 检查是否已经在监控
    if (watch_items_.find(path) != watch_items_.end()) {
        INTERNAL_WARN("Path already being watched: " + path);
        return false;
    }
    
    // 创建监控项
    auto item = std::make_shared<WatchItem>();
    item->path = path;
    item->callback = std::move(callback);
    item->last_event_time = std::chrono::system_clock::now();
    
    watch_items_[path] = item;
    
    INTERNAL_INFO("Added watch for path: " + path);
    
    // 如果已经在运行，立即开始监控
    if (running_) {
        
        if (!impl_->addWatch(path, config_.recursive)) {
            INTERNAL_ERROR("Failed to start platform watch for: " + path);
            watch_items_.erase(path);
            return false;
        }
    }
    
    return true;
}

void FileWatcher::unwatch(const std::string& path) {
    std::lock_guard<std::mutex> lock(items_mutex_);
    
    auto it = watch_items_.find(path);
    if (it != watch_items_.end()) {
        if (running_) {
            impl_->removeWatch(path);
        }
        watch_items_.erase(it);
        INTERNAL_INFO("Removed watch for path: " + path);
    }
}

bool FileWatcher::start() {
    if (running_) {
        INTERNAL_WARN("FileWatcher is already running");
        return true;
    }
    
    INTERNAL_INFO("Starting FileWatcher");
    
    // 添加所有监控路径
    std::lock_guard<std::mutex> lock(items_mutex_);
    bool success = true;
    
    for (const auto& [path, item] : watch_items_) {
        if (!impl_->addWatch(path, config_.recursive)) {
            INTERNAL_ERROR("Failed to add watch for: " + path);
            success = false;
        }
    }
    
    if (!success) {
        INTERNAL_ERROR("Failed to start all watches");
        return false;
    }
    
    // 开始监控线程
    impl_->run();
    running_ = true;
    
    INTERNAL_INFO("FileWatcher started successfully");
    return true;
}

void FileWatcher::stop() {
    if (!running_) return;
    
    INTERNAL_INFO("Stopping FileWatcher");
    
    impl_->stop();
    running_ = false;
    
    // 清空挂起的事件
    std::lock_guard<std::mutex> lock(items_mutex_);
    for (auto& [path, item] : watch_items_) {
        std::lock_guard<std::mutex> item_lock(item->pending_mutex);
        item->pending_events.clear();
    }
    
    INTERNAL_INFO("FileWatcher stopped");
}

std::vector<std::string> FileWatcher::getWatchedPaths() const {
    std::lock_guard<std::mutex> lock(items_mutex_);
    std::vector<std::string> paths;
    paths.reserve(watch_items_.size());
    
    for (const auto& [path, _] : watch_items_) {
        paths.push_back(path);
    }
    
    return paths;
}

FileWatcher::Stats FileWatcher::getStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    auto copy = stats_;
    copy.total_runtime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - copy.start_time
    );
    return copy;
}

void FileWatcher::resetStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = Stats{};
    stats_.start_time = std::chrono::system_clock::now();
}

void FileWatcher::setConfig(const FileWatcherConfig& config) {
    if (running_) {
        INTERNAL_WARN("Cannot change config while FileWatcher is running");
        return;
    }
    config_ = config;
}

// 私有方法实现
void FileWatcher::processEvent(const std::string& path, 
                              FileChangeType type, 
                              uint64_t file_size) {
    // 检查文件过滤
    if (shouldFilter(path)) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.filtered_events++;
        return;
    }
    
    // 创建事件
    FileChangeEvent event;
    event.path = path;
    event.type = type;
    event.timestamp = std::chrono::system_clock::now();
    event.file_size = file_size;
    
    // 检查事件过滤
    if (shouldFilterEvent(event)) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.filtered_events++;
        return;
    }
    
    // 找到对应的监控项
    std::shared_ptr<WatchItem> item;
    {
        std::lock_guard<std::mutex> lock(items_mutex_);
        
        // 查找最长的匹配路径
        std::string matched_path;
        for (const auto& [watch_path, watch_item] : watch_items_) {
            if (path.find(watch_path) == 0 && 
                watch_path.length() > matched_path.length()) {
                matched_path = watch_path;
                item = watch_item;
            }
        }
        
        if (!item) {
            INTERNAL_WARN("No watch item found for path: " + path);
            return;
        }
    }
    
    // 检查速率限制
    if (item->isRateLimited()) {
        INTERNAL_WARN("Rate limit exceeded for path: " + path);
        return;
    }
    
    // 更新统计
    updateStats(type);
    
    // 防抖动处理：将事件添加到挂起队列
    {
        std::lock_guard<std::mutex> lock(item->pending_mutex);
        item->pending_events[path] = event;
        item->event_count++;
        item->last_event_time = event.timestamp;
    }
    
    // 如果是高优先级事件，立即处理
    if (type == FileChangeType::DELETED || type == FileChangeType::RENAMED) {
        debounceEvents();
    }
}

bool FileWatcher::shouldFilter(const std::string& path) const {
    // 扩展名过滤
    if (!config_.extensions.empty()) {
        size_t dot_pos = path.find_last_of('.');
        if (dot_pos == std::string::npos) {
            return true; // 没有扩展名，过滤掉
        }
        
        std::string ext = path.substr(dot_pos + 1);
        std::string lower_ext;
        std::transform(ext.begin(), ext.end(), std::back_inserter(lower_ext), ::tolower);
        
        bool found = false;
        for (const auto& allowed_ext : config_.extensions) {
            if (allowed_ext == lower_ext) {
                found = true;
                break;
            }
        }
        
        if (!found) return true;
    }
    
    // 自定义过滤
    if (config_.filter) {
        return !config_.filter(path);
    }
    
    return false;
}

bool FileWatcher::shouldFilterEvent(const FileChangeEvent& event) const {
    if (config_.event_filter) {
        return !config_.event_filter(event);
    }
    return false;
}

void FileWatcher::updateStats(FileChangeType type) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.total_events++;
    
    switch (type) {
        case FileChangeType::CREATED:
            stats_.created_events++;
            break;
        case FileChangeType::MODIFIED:
            stats_.modified_events++;
            break;
        case FileChangeType::DELETED:
            stats_.deleted_events++;
            break;
        case FileChangeType::RENAMED:
            stats_.renamed_events++;
            break;
        case FileChangeType::ACCESSED:
            // 访问事件不单独统计
            break;
    }
}

void FileWatcher::debounceEvents() {
    std::lock_guard<std::mutex> lock(items_mutex_);
    
    for (auto& [path, item] : watch_items_) {
        std::lock_guard<std::mutex> item_lock(item->pending_mutex);
        
        if (item->pending_events.empty()) continue;
        
        // 处理所有挂起的事件
        for (auto& [event_path, event] : item->pending_events) {
            try {
                if (item->callback) {
                    item->callback(event);
                }
            } catch (const std::exception& e) {
                INTERNAL_ERROR("Error in file change callback for " + 
                         event_path + ": " + e.what());
            }
        }
        
        item->pending_events.clear();
    }
}

} // namespace fs
} // namespace foundation