#include "../include/database/ConnectionPool.h"
#include <stdexcept>
#include <thread>
#include <sstream>

namespace astock {
namespace database {

ConnectionPool::ConnectionPool(const DatabaseConfig& config)
    : config_(config) {
}

ConnectionPool::~ConnectionPool() {
    shutdown();
}

bool ConnectionPool::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return true;
    }
    
    // 验证配置
    if (!config_.validate()) {
        return false;
    }
    
    // 初始化MySQL库
    if (mysql_library_init(0, nullptr, nullptr) != 0) {
        return false;
    }
    
    // 创建初始连接
    for (size_t i = 0; i < config_.pool_size; ++i) {
        MYSQL* conn = createConnection();
        if (conn) {
            auto wrapper = std::make_shared<MySQLConnection>(conn);
            available_connections_.push(wrapper);
            all_connections_.push_back(wrapper);
        } else {
            // 清理已创建的连接
            shutdown();
            return false;
        }
    }
    
    initialized_ = true;
    
    // 启动回收线程
    recycle_thread_ = std::make_unique<std::thread>(
        &ConnectionPool::recycleThread, this);
    
    return true;
}

MYSQL* ConnectionPool::createConnection() {
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) {
        return nullptr;
    }
    
    // 设置连接选项
    bool reconnect = config_.auto_reconnect;
    mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);
    
    unsigned int connect_timeout = config_.connect_timeout.count();
    mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &connect_timeout);
    
    unsigned int read_timeout = config_.read_timeout.count();
    mysql_options(conn, MYSQL_OPT_READ_TIMEOUT, &read_timeout);
    
    unsigned int write_timeout = config_.write_timeout.count();
    mysql_options(conn, MYSQL_OPT_WRITE_TIMEOUT, &write_timeout);
    
    // 设置字符集
    mysql_options(conn, MYSQL_SET_CHARSET_NAME, config_.charset.c_str());
    
    // 连接数据库
    if (!mysql_real_connect(
            conn,
            config_.host.c_str(),
            config_.username.c_str(),
            config_.password.c_str(),
            config_.database.c_str(),
            config_.port,
            nullptr,
            CLIENT_MULTI_STATEMENTS)) {
        mysql_close(conn);
        return nullptr;
    }
    
    return conn;
}

bool ConnectionPool::validateConnection(MYSQL* conn) {
    if (!conn) return false;
    
    // Ping检查连接
    if (config_.pool_pre_ping) {
        return mysql_ping(conn) == 0;
    }
    
    return true;
}

std::shared_ptr<MySQLConnection> ConnectionPool::acquireConnection() {
    std::unique_lock<std::mutex> lock(mutex_);
    
    ++total_acquisitions_;
    
    // 等待可用连接
    if (!cv_.wait_for(lock, config_.pool_timeout, [this] {
        return !available_connections_.empty() || shutdown_;
    })) {
        ++failed_acquisitions_;
        return nullptr;
    }
    
    if (shutdown_) {
        return nullptr;
    }
    
    // 获取连接
    auto conn = available_connections_.front();
    available_connections_.pop();
    
    // 验证连接
    if (!conn->isValid()) {
        // 重新创建连接
        MYSQL* new_conn = createConnection();
        if (new_conn) {
            conn = std::make_shared<MySQLConnection>(new_conn);
        } else {
            ++failed_acquisitions_;
            return nullptr;
        }
    }
    
    conn->markInUse(true);
    conn->updateLastUseTime();
    
    return conn;
}

void ConnectionPool::releaseConnection(std::shared_ptr<MySQLConnection> conn) {
    if (!conn) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    conn->markInUse(false);
    conn->updateLastUseTime();
    
    available_connections_.push(conn);
    cv_.notify_one();
}

void ConnectionPool::recycleThread() {
    while (!shutdown_) {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto now = std::chrono::steady_clock::now();
        
        // 检查并回收长时间未使用的连接
        for (auto& conn : all_connections_) {
            if (!conn->isInUse()) {
                auto idle_time = std::chrono::duration_cast<std::chrono::seconds>(
                    now - conn->getLastUseTime());
                
                if (idle_time > config_.pool_recycle) {
                    // 重新创建连接
                    MYSQL* new_conn = createConnection();
                    if (new_conn) {
                        conn = std::make_shared<MySQLConnection>(new_conn);
                    }
                }
            }
        }
    }
}

void ConnectionPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
    }
    
    cv_.notify_all();
    
    if (recycle_thread_ && recycle_thread_->joinable()) {
        recycle_thread_->join();
    }
    
    // 清空连接队列
    while (!available_connections_.empty()) {
        available_connections_.pop();
    }
    
    // 关闭所有连接
    all_connections_.clear();
    
    // 清理MySQL库
    mysql_library_end();
    
    initialized_ = false;
}

ConnectionPool::PoolStats ConnectionPool::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t active = 0;
    for (const auto& conn : all_connections_) {
        if (conn->isInUse()) {
            ++active;
        }
    }
    
    return PoolStats{
        all_connections_.size(),
        active,
        available_connections_.size(),
        failed_acquisitions_,
        total_acquisitions_
    };
}

} // namespace database
} // namespace astock
