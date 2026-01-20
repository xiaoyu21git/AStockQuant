// foundation/include/foundation/fs/FileWatcher.h
#pragma once

#include "foundation/core/exception.hpp"
#include "foundation/Utils/system.hpp"
#include <string>
#include <functional>
#include <unordered_map>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace foundation {
namespace fs {

/**
 * @brief 文件变化类型枚举
 */
enum class FileChangeType {
    CREATED,      // 文件创建
    MODIFIED,     // 文件修改
    DELETED,      // 文件删除
    RENAMED,      // 文件重命名
    ACCESSED      // 文件访问
};

/**
 * @brief 文件变化事件
 */
struct FileChangeEvent {
    std::string path;           // 文件路径
    FileChangeType type;        // 变化类型
    std::chrono::system_clock::time_point timestamp; // 时间戳
    uint64_t file_size = 0;     // 文件大小（修改时有效）
    
    std::string toString() const {
        std::string type_str;
        switch (type) {
            case FileChangeType::CREATED: type_str = "CREATED"; break;
            case FileChangeType::MODIFIED: type_str = "MODIFIED"; break;
            case FileChangeType::DELETED: type_str = "DELETED"; break;
            case FileChangeType::RENAMED: type_str = "RENAMED"; break;
            case FileChangeType::ACCESSED: type_str = "ACCESSED"; break;
        }
        return "FileChangeEvent{path=" + path + ", type=" + type_str + "}";
    }
};

/**
 * @brief 文件变化回调函数类型
 */
using FileChangeCallback = std::function<void(const FileChangeEvent& event)>;

/**
 * @brief 文件监控配置
 */
struct FileWatcherConfig {
    bool recursive = false;                     // 是否递归监控子目录
    std::chrono::milliseconds poll_interval{1000}; // 轮询间隔（非inotify模式）
    std::vector<std::string> extensions;        // 只监控指定扩展名
    bool watch_access = false;                  // 是否监控访问事件
    size_t max_events_per_second = 1000;        // 每秒最大事件数（防抖动）
    
    // 文件过滤
    using FileFilter = std::function<bool(const std::string& path)>;
    FileFilter filter = nullptr;
    
    // 事件过滤
    using EventFilter = std::function<bool(const FileChangeEvent& event)>;
    EventFilter event_filter = nullptr;
    
    static FileWatcherConfig defaultConfig() {
        return FileWatcherConfig{};
    }
};

/**
 * @brief 文件监控器异常
 */
class FileWatcherException : public foundation::Exception {
public:
    explicit FileWatcherException(const std::string& message)
        : foundation::Exception("FileWatcher: " + message) {}
    
    explicit FileWatcherException(const char* message)
        : foundation::Exception(std::string("FileWatcher: ") + message) {}
};

/**
 * @brief 文件监控器类
 * 
 * 跨平台文件系统监控，支持实时文件变化检测
 */
class FileWatcher {
public:
    /**
     * @brief 构造函数
     * @param config 监控配置
     */
    explicit FileWatcher(const FileWatcherConfig& config = FileWatcherConfig::defaultConfig());
    
    /**
     * @brief 析构函数
     */
    ~FileWatcher();
    
    // 禁止拷贝
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;
    
    // 允许移动
    FileWatcher(FileWatcher&& other) noexcept;
    FileWatcher& operator=(FileWatcher&& other) noexcept;
    
    /**
     * @brief 开始监控路径
     * @param path 要监控的文件或目录路径
     * @param callback 变化回调函数
     * @return 是否成功开始监控
     */
    bool watch(const std::string& path, FileChangeCallback callback);
    
    /**
     * @brief 停止监控路径
     * @param path 要停止监控的路径
     */
    void unwatch(const std::string& path);
    
    /**
     * @brief 开始所有监控
     * @return 是否成功启动
     */
    bool start();
    
    /**
     * @brief 停止所有监控
     */
    void stop();
    
    /**
     * @brief 检查是否正在运行
     */
    bool isRunning() const { return running_; }
    
    /**
     * @brief 获取监控的路径列表
     */
    std::vector<std::string> getWatchedPaths() const;
    
    /**
     * @brief 获取事件统计信息
     */
    struct Stats {
        size_t total_events = 0;
        size_t created_events = 0;
        size_t modified_events = 0;
        size_t deleted_events = 0;
        size_t renamed_events = 0;
        size_t filtered_events = 0;
        std::chrono::system_clock::time_point start_time;
        std::chrono::milliseconds total_runtime{0};
        
        std::string toString() const {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now() - start_time
            ).count();
            
            return "FileWatcherStats{"
                "total=" + std::to_string(total_events) + 
                ", created=" + std::to_string(created_events) +
                ", modified=" + std::to_string(modified_events) +
                ", deleted=" + std::to_string(deleted_events) +
                ", renamed=" + std::to_string(renamed_events) +
                ", filtered=" + std::to_string(filtered_events) +
                ", runtime=" + std::to_string(duration) + "s}";
        }
    };
    
    Stats getStats() const;
    
    /**
     * @brief 重置统计信息
     */
    void resetStats();
    
    /**
     * @brief 设置配置
     */
    void setConfig(const FileWatcherConfig& config);
    
private:
    // 平台相关实现
#ifdef _WIN32
    class WindowsImpl;
    std::unique_ptr<WindowsImpl> impl_;
#else
    class LinuxImpl;
    std::unique_ptr<LinuxImpl> impl_;
#endif
    
    // 监控项
    struct WatchItem {
        std::string path;
        FileChangeCallback callback;
        std::chrono::system_clock::time_point last_event_time;
        size_t event_count = 0;
        
        // 用于防抖动的临时状态
        std::unordered_map<std::string, FileChangeEvent> pending_events;
        std::mutex pending_mutex;
        
        // 检查是否超过速率限制
        bool isRateLimited() const {
            auto now = std::chrono::system_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - last_event_time
            ).count();
            
            if (elapsed == 0) {
                // 同一秒内的事件数检查
                return event_count > 1000; // 每秒最多1000个事件
            }
            return false;
        }
    };
    
    FileWatcherConfig config_;
    std::unordered_map<std::string, std::shared_ptr<WatchItem>> watch_items_;
    mutable std::mutex items_mutex_;
    
    std::atomic<bool> running_{false};
    std::thread monitor_thread_;
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    Stats stats_;
    
    // 内部方法
    void monitorLoop();
    void processEvent(const std::string& path, FileChangeType type, uint64_t size = 0);
    bool shouldFilter(const std::string& path) const;
    bool shouldFilterEvent(const FileChangeEvent& event) const;
    void updateStats(FileChangeType type);
    void debounceEvents();
    
    // 平台抽象接口
    class PlatformImpl {
    public:
        virtual ~PlatformImpl() = default;
        virtual bool addWatch(const std::string& path, bool recursive) = 0;
        virtual bool removeWatch(const std::string& path) = 0;
        virtual void run() = 0;
        virtual void stop() = 0;
        virtual bool isRunning() const = 0;
    };
};

} // namespace fs
} // namespace foundation