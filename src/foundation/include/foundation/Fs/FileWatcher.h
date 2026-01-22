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
    class WindowsImpl {
    public:
        WindowsImpl() = default;
        ~WindowsImpl() { stop(); }
        
        bool addWatch(const std::string& path, bool recursive){
            std::lock_guard<std::mutex> lock(mutex_);
        
            // 检查路径是否已经监控
            if (watched_directories_.find(path) != watched_directories_.end()) {
                return true; // 已经监控
            }
        
            // 转换路径为宽字符串（Windows API 需要）
            std::wstring wpath;
            int size_needed = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), (int)path.size(), NULL, 0);
            if (size_needed > 0) {
                wpath.resize(size_needed);
                MultiByteToWideChar(CP_UTF8, 0, path.c_str(), (int)path.size(), &wpath[0], size_needed);
            }
        
        // 打开目录句柄
        HANDLE hDir = CreateFileW(
            wpath.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            NULL
        );
        
        if (hDir == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            // 可以记录错误日志
            return false;
        }
        
        // 创建目录信息
        DirectoryInfo info;
        info.handle = hDir;
        info.recursive = recursive;
        info.overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        info.buffer.resize(64 * 1024); // 64KB 缓冲区
        
        watched_directories_[path] = std::move(info);
        
        // 如果已经在运行，开始监听变化
        if (running_) {
            startMonitoring(path);
        }
        
        return true;
    }
        bool removeWatch(const std::string& path){
            std::lock_guard<std::mutex> lock(mutex_);
        
            auto it = watched_directories_.find(path);
            if (it != watched_directories_.end()) {
                DirectoryInfo& info = it->second;
            
                // 取消异步操作
                CancelIo(info.handle);
            
                // 关闭事件和句柄
                if (info.overlapped.hEvent != NULL) {
                    CloseHandle(info.overlapped.hEvent);
                }
                CloseHandle(info.handle);
            
                watched_directories_.erase(it);
                return true;
            }
        
            return false;
            }
        void run(){
        if (running_) {
            return;
        }
        
        running_ = true;
        stop_requested_ = false;
        
        // 启动工作线程
        worker_thread_ = std::thread([this]() {
            monitoringLoop();
        });
        
        // 开始监听所有已添加的目录
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [path, info] : watched_directories_) {
            startMonitoring(path);
        }
    }
        void stop(){
        if (!running_) {
            return;
        }
        
        stop_requested_ = true;
        running_ = false;
        
        // 通知工作线程退出
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [path, info] : watched_directories_) {
                CancelIo(info.handle);
            }
        }
        
        // 等待工作线程结束
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        
        // 清理资源
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [path, info] : watched_directories_) {
            if (info.overlapped.hEvent != NULL) {
                CloseHandle(info.overlapped.hEvent);
                info.overlapped.hEvent = NULL;
            }
            if (info.handle != INVALID_HANDLE_VALUE) {
                CloseHandle(info.handle);
                info.handle = INVALID_HANDLE_VALUE;
            }
        }
    }
        bool isRunning() const { return running_; }
        
    private:
        struct DirectoryInfo {
            HANDLE handle = INVALID_HANDLE_VALUE;
            bool recursive = false;
            OVERLAPPED overlapped = {};
            std::vector<BYTE> buffer;
            std::atomic<bool> pending_io{false};
             // 默认构造函数
            DirectoryInfo() = default;
    
            // 禁止拷贝
            DirectoryInfo(const DirectoryInfo&) = delete;
            DirectoryInfo& operator=(const DirectoryInfo&) = delete;
             // 允许移动
            DirectoryInfo(DirectoryInfo&& other) noexcept
                : handle(std::exchange(other.handle, INVALID_HANDLE_VALUE))
                , recursive(other.recursive)
                , overlapped(other.overlapped)
                , buffer(std::move(other.buffer))
                , pending_io(other.pending_io.load()) {
                // 重置原对象的 overlapped
                memset(&other.overlapped, 0, sizeof(OVERLAPPED));
            }
            DirectoryInfo& operator=(DirectoryInfo&& other) noexcept {
                if (this != &other) {
                // 清理当前资源
                cleanup();
            
                // 移动资源
                handle = std::exchange(other.handle, INVALID_HANDLE_VALUE);
                recursive = other.recursive;
                overlapped = other.overlapped;
                buffer = std::move(other.buffer);
                pending_io.store(other.pending_io.load());
            
                // 重置原对象
                memset(&other.overlapped, 0, sizeof(OVERLAPPED));
            }
            return *this;
        }
        ~DirectoryInfo() {
            cleanup();
        }
    private:
        void cleanup() {
            if (overlapped.hEvent != NULL) {
                CloseHandle(overlapped.hEvent);
                overlapped.hEvent = NULL;
            }
            if (handle != INVALID_HANDLE_VALUE) {
                CloseHandle(handle);
                handle = INVALID_HANDLE_VALUE;
            }
        }
        };

        std::atomic<bool> stop_requested_{false};
        std::thread worker_thread_;
        std::unordered_map<std::string, DirectoryInfo> watched_directories_;
        mutable std::mutex mutex_;
        std::atomic<bool> running_{false};
        // 回调函数指针（从 FileWatcher 传递进来）
        std::function<void(const std::string&, FileChangeType)> on_change_callback_;
        void startMonitoring(const std::string& path) {
        auto it = watched_directories_.find(path);
        if (it == watched_directories_.end()) {
            return;
        }
        
        DirectoryInfo& info = it->second;
        
        // 重置重叠结构
        memset(&info.overlapped, 0, sizeof(OVERLAPPED));
        info.overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        
        DWORD bytes_returned = 0;
        
        // 开始异步监听目录变化
        BOOL success = ReadDirectoryChangesW(
            info.handle,
            info.buffer.data(),
            static_cast<DWORD>(info.buffer.size()),
            info.recursive, // 是否递归监控子目录
            FILE_NOTIFY_CHANGE_FILE_NAME |     // 文件创建、删除、重命名
            FILE_NOTIFY_CHANGE_DIR_NAME |      // 目录创建、删除、重命名
            FILE_NOTIFY_CHANGE_SIZE |          // 文件大小变化
            FILE_NOTIFY_CHANGE_LAST_WRITE |    // 最后写入时间变化
            FILE_NOTIFY_CHANGE_CREATION |      // 创建时间变化
            FILE_NOTIFY_CHANGE_LAST_ACCESS,    // 最后访问时间变化
            &bytes_returned,
            &info.overlapped,
            NULL // 没有完成例程，使用 GetOverlappedResult
        );
        
        if (!success) {
            DWORD error = GetLastError();
            // 处理错误
        } else {
            info.pending_io = true;
        }
    }
    void monitoringLoop() {
        const DWORD timeout_ms = 100; // 100ms 超时
        
        while (!stop_requested_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
            std::lock_guard<std::mutex> lock(mutex_);
            
            for (auto& [path, info] : watched_directories_) {
                if (!info.pending_io) {
                    continue;
                }
                
                DWORD bytes_transferred = 0;
                BOOL result = GetOverlappedResult(
                    info.handle,
                    &info.overlapped,
                    &bytes_transferred,
                    FALSE // 非阻塞
                );
                
                if (result && bytes_transferred > 0) {
                    // 处理文件变化
                    processFileChanges(path, info.buffer.data(), bytes_transferred);
                    
                    // 重新开始监听
                    startMonitoring(path);
                } else if (GetLastError() == ERROR_IO_INCOMPLETE) {
                    // IO 操作尚未完成，继续等待
                    continue;
                } else {
                    // 发生错误，尝试重新开始
                    startMonitoring(path);
                }
            }
        }
    }
    void processFileChanges(const std::string& directory_path, 
                           BYTE* buffer, 
                           DWORD buffer_size) {
        BYTE* current = buffer;
        
        while (true) {
            FILE_NOTIFY_INFORMATION* info = 
                reinterpret_cast<FILE_NOTIFY_INFORMATION*>(current);
            
            // 转换文件名从宽字符到 UTF-8
            std::wstring wfilename(info->FileName, 
                                  info->FileNameLength / sizeof(WCHAR));
            std::string filename = wideToUtf8(wfilename);
            
            // 构建完整路径
            std::string full_path = directory_path;
            if (!full_path.empty() && full_path.back() != '\\' && full_path.back() != '/') {
                full_path += '\\';
            }
            full_path += filename;
            
            // 确定事件类型
            FileChangeType change_type = FileChangeType::MODIFIED;
            switch (info->Action) {
                case FILE_ACTION_ADDED:
                    change_type = FileChangeType::CREATED;
                    break;
                case FILE_ACTION_REMOVED:
                    change_type = FileChangeType::DELETED;
                    break;
                case FILE_ACTION_MODIFIED:
                    change_type = FileChangeType::MODIFIED;
                    break;
                case FILE_ACTION_RENAMED_OLD_NAME:
                case FILE_ACTION_RENAMED_NEW_NAME:
                    change_type = FileChangeType::RENAMED;
                    break;
            }
            
            // 这里应该触发回调
            // 注意：实际实现中需要从 FileWatcher 类传递回调进来
            // if (on_change_callback_) {
            //     on_change_callback_(full_path, change_type);
            // }
            
            // 或者可以在这里生成 FileChangeEvent 并推送到队列
            
            if (info->NextEntryOffset == 0) {
                break;
            }
            current += info->NextEntryOffset;
        }
    }
    std::string wideToUtf8(const std::wstring& wstr) {
        if (wstr.empty()) {
            return {};
        }
        
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, 
            wstr.c_str(), static_cast<int>(wstr.size()),
            NULL, 0, NULL, NULL);
        
        if (size_needed == 0) {
            return {};
        }
        
        std::string result(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0,
            wstr.c_str(), static_cast<int>(wstr.size()),
            result.data(), size_needed, NULL, NULL);
        
        return result;
    }
    
    void setChangeCallback(std::function<void(const std::string&, FileChangeType)> callback) {
        on_change_callback_ = std::move(callback);
    }
    };
    
    std::unique_ptr<WindowsImpl> impl_;  // ✅ 现在 WindowsImpl 是完整类型
#else
    class LinuxImpl {
        // Linux 实现...
    };
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