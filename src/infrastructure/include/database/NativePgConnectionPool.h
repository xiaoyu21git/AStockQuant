 #pragma once

#include "DatabaseConfig.h"
#include "ISqlDatabase.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <functional>

namespace astock {
namespace database {

// 纯 C++ PG 连接池 — 每线程缓存一个 NativePgDatabase
// 不依赖 Qt，直接用 libpq
class NativePgConnectionPool {
public:
    static NativePgConnectionPool& instance();

    // 用 DatabaseConfig 初始化连接池
    bool initialize(const DatabaseConfig& config);

    // 获取当前线程的数据库连接（实现 ISqlDatabase 接口）
    // 如果线程号不存在，自动创建新连接
    std::shared_ptr<ISqlDatabase> getConnection();

    // 关闭所有连接
    void shutdown();

    // 连接池状态
    bool isInitialized() const;
    size_t activeConnectionCount() const;
    std::string statusReport() const;

private:
    NativePgConnectionPool();
    ~NativePgConnectionPool();
    NativePgConnectionPool(const NativePgConnectionPool&) = delete;
    NativePgConnectionPool& operator=(const NativePgConnectionPool&) = delete;

    struct PooledConnection {
        std::shared_ptr<ISqlDatabase> db;
        std::thread::id threadId;
    };

    std::shared_ptr<ISqlDatabase> createConnection();

    DatabaseConfig config_;
    mutable std::mutex mutex_;
    std::vector<PooledConnection> connections_;
    bool initialized_{false};
};

} // namespace database
} // namespace astock