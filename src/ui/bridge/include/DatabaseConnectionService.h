// DatabaseConnectionService.h
// 数据库连接服务 - 无Qt依赖，通用数据库服务
#pragma once

#include <memory>
#include <string>
#include <functional>
#include <stdexcept>

namespace foundation::thread {
    class IExecutor;
}

// 数据库连接配置
struct DatabaseConfig {
    std::string host;
    int port;
    std::string database;
    std::string username;
    std::string password;
    std::string charset;
    
    // 连接池配置
    int minConnections{2};
    int maxConnections{10};
    int connectionTimeout{30000}; // 毫秒
    int idleTimeout{600000};      // 毫秒
    
    // 重试配置
    int maxRetryAttempts{3};
    int retryDelayMs{1000};
};

// 数据库连接状态
enum class ConnectionStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    ERROR,
    CLOSING
};

// 连接事件类型
enum class ConnectionEventType {
    CONNECTION_STARTED,
    CONNECTION_SUCCESS,
    CONNECTION_FAILED,
    CONNECTION_LOST,
    CONNECTION_RECOVERED,
    CONNECTION_CLOSED
};

// 连接事件回调
using ConnectionEventCallback = std::function<void(
    ConnectionEventType eventType,
    const std::string& message,
    const std::string& details
)>;

// 数据库连接服务 - 纯C++，无Qt依赖
class DatabaseConnectionService {
public:
    // 禁止拷贝和赋值
    DatabaseConnectionService(const DatabaseConnectionService&) = delete;
    DatabaseConnectionService& operator=(const DatabaseConnectionService&) = delete;
    
    // 构造和析构
    explicit DatabaseConnectionService(const DatabaseConfig& config);
    ~DatabaseConnectionService();
    
    // 连接管理
    bool connect();
    bool disconnect();
    bool reconnect();
    ConnectionStatus getStatus() const;
    std::string getLastError() const;
    
    // 异步操作
    void connectAsync(std::function<void(bool, const std::string&)> callback = nullptr);
    void disconnectAsync(std::function<void(bool, const std::string&)> callback = nullptr);
    
    // 连接检查
    bool isConnected() const;
    bool testConnection() const;
    
    // 事件监听
    void addEventListener(ConnectionEventCallback callback);
    void removeEventListeners();
    
    // 获取数据库实例（供QueryBuilder使用）
    std::shared_ptr<astock::database::QtMySQLDatabase> getDatabase() const;
    
    // 统计信息
    struct Statistics {
        size_t totalConnections{0};
        size_t failedConnections{0};
        size_t successfulConnections{0};
        size_t reconnectionAttempts{0};
        size_t totalQueryTimeMs{0};
        size_t totalQueries{0};
        
        std::string toString() const;
    };
    
    Statistics getStatistics() const;
    void resetStatistics();
    
    // 配置管理
    void updateConfig(const DatabaseConfig& config);
    DatabaseConfig getCurrentConfig() const;
    
    // 健康检查
    bool performHealthCheck() const;
    std::string getHealthReport() const;
    
private:
    // 内部实现
    class Impl;
    std::unique_ptr<Impl> m_impl;
    
    // 事件通知
    void notifyEvent(ConnectionEventType type, 
                    const std::string& message, 
                    const std::string& details = "");
    
    // 线程池执行
    void executeAsync(std::function<void()> task);
};

// 异常类
class DatabaseConnectionException : public std::runtime_error {
public:
    explicit DatabaseConnectionException(const std::string& message, 
                                       ConnectionStatus status = ConnectionStatus::ERROR)
        : std::runtime_error(message), m_status(status) {}
    
    ConnectionStatus getStatus() const { return m_status; }
    
private:
    ConnectionStatus m_status;
};

// 工厂函数
std::shared_ptr<DatabaseConnectionService> createDatabaseConnectionService(
    const DatabaseConfig& config,
    std::shared_ptr<foundation::thread::IExecutor> executor = nullptr
);