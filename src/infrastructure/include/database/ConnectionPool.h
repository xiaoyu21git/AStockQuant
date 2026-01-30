#ifndef ASTOCK_INFRASTRUCTURE_DATABASE_CONNECTIONPOOL_H
#define ASTOCK_INFRASTRUCTURE_DATABASE_CONNECTIONPOOL_H

#include "DatabaseConfig.h"
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <mysql/mysql.h>

namespace astock {
namespace database {

/**
 * @brief MySQL连接包装器
 */
class MySQLConnection {
public:
    MySQLConnection(MYSQL* conn) : conn_(conn), in_use_(false) {}
    
    ~MySQLConnection() {
        if (conn_) {
            mysql_close(conn_);
            conn_ = nullptr;
        }
    }
    
    MYSQL* get() { return conn_; }
    
    bool isValid() {
        if (!conn_) return false;
        return mysql_ping(conn_) == 0;
    }
    
    void markInUse(bool in_use) { in_use_ = in_use; }
    bool isInUse() const { return in_use_; }
    
    auto getLastUseTime() const { return last_use_time_; }
    void updateLastUseTime() { last_use_time_ = std::chrono::steady_clock::now(); }
    
private:
    MYSQL* conn_;
    bool in_use_;
    std::chrono::steady_clock::time_point last_use_time_;
};

/**
 * @brief 数据库连接池
 * 
 * 功能：
 * - 连接复用
 * - 自动扩展
 * - 连接健康检查
 * - 超时回收
 */
class ConnectionPool {
public:
    explicit ConnectionPool(const DatabaseConfig& config);
    ~ConnectionPool();
    
    // 禁止拷贝
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;
    
    /**
     * @brief 初始化连接池
     */
    bool initialize();
    
    /**
     * @brief 获取连接
     * @return 连接指针，使用完需调用releaseConnection归还
     */
    std::shared_ptr<MySQLConnection> acquireConnection();
    
    /**
     * @brief 归还连接
     */
    void releaseConnection(std::shared_ptr<MySQLConnection> conn);
    
    /**
     * @brief 关闭连接池
     */
    void shutdown();
    
    /**
     * @brief 获取连接池统计信息
     */
    struct PoolStats {
        size_t total_connections;
        size_t active_connections;
        size_t idle_connections;
        size_t failed_acquisitions;
        size_t total_acquisitions;
    };
    
    PoolStats getStats() const;
    
private:
    /**
     * @brief 创建新连接
     */
    MYSQL* createConnection();
    
    /**
     * @brief 验证连接有效性
     */
    bool validateConnection(MYSQL* conn);
    
    /**
     * @brief 连接回收线程
     */
    void recycleThread();
    
    DatabaseConfig config_;
    
    // 连接队列
    std::queue<std::shared_ptr<MySQLConnection>> available_connections_;
    std::vector<std::shared_ptr<MySQLConnection>> all_connections_;
    
    // 同步
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    
    // 状态
    bool initialized_{false};
    bool shutdown_{false};
    
    // 统计
    mutable size_t failed_acquisitions_{0};
    mutable size_t total_acquisitions_{0};
    
    // 回收线程
    std::unique_ptr<std::thread> recycle_thread_;
};

/**
 * @brief RAII连接守卫
 * 
 * 自动管理连接的获取和释放
 */
class ConnectionGuard {
public:
    ConnectionGuard(ConnectionPool& pool) 
        : pool_(pool), conn_(pool.acquireConnection()) {}
    
    ~ConnectionGuard() {
        if (conn_) {
            pool_.releaseConnection(conn_);
        }
    }
    
    // 禁止拷贝
    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;
    
    // 允许移动
    ConnectionGuard(ConnectionGuard&& other) noexcept
        : pool_(other.pool_), conn_(std::move(other.conn_)) {}
    
    MYSQL* get() const { return conn_ ? conn_->get() : nullptr; }
    
    explicit operator bool() const { return conn_ != nullptr; }
    
private:
    ConnectionPool& pool_;
    std::shared_ptr<MySQLConnection> conn_;
};

} // namespace database
} // namespace astock

#endif // ASTOCK_INFRASTRUCTURE_DATABASE_CONNECTIONPOOL_H
