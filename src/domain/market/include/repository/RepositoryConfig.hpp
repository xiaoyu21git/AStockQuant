#pragma once
#include <string>
#include <chrono>

namespace domain::market::repository {

// 仓储配置结构
struct RepositoryConfig {
    // 连接配置
    std::string type = "memory";  // memory, sqlite, mysql
    std::string host = "localhost";
    int port = 3306;
    std::string database = "astock";
    std::string username = "root";
    std::string password = "";
    
    // 连接池配置
    int max_connections = 10;
    int min_connections = 2;
    std::chrono::seconds connection_timeout = std::chrono::seconds(30);
    std::chrono::seconds idle_timeout = std::chrono::seconds(300);
    
    // 性能配置
    size_t batch_size = 1000;
    size_t cache_size = 10000;
    bool enable_cache = true;
    bool enable_compression = false;
    
    // 日志配置
    bool enable_query_log = false;
    bool enable_slow_query_log = true;
    std::chrono::milliseconds slow_query_threshold = std::chrono::milliseconds(100);
    
    // 错误处理
    int max_retry_attempts = 3;
    std::chrono::milliseconds retry_delay = std::chrono::milliseconds(100);
    
    // 数据验证
    bool validate_data = true;
    bool check_constraints = true;
    bool enable_foreign_keys = true;
    
    // 从Foundation配置加载
    static RepositoryConfig loadFromFoundationConfig();
};

} // namespace domain::market::repository