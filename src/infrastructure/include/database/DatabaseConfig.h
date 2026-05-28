#ifndef ASTOCK_INFRASTRUCTURE_DATABASE_DATABASECONFIG_H
#define ASTOCK_INFRASTRUCTURE_DATABASE_DATABASECONFIG_H

#include <string>
#include <chrono>

namespace astock {
namespace database {

/**
 * @brief 数据库配置
 */
struct DatabaseConfig {
    // 连接信息
    std::string driver{"mysql"};          // 数据库驱动
    std::string host{"localhost"};        // 主机地址
    int port{3306};                        // 端口号
    std::string database{"astock_quant"}; // 数据库名
    std::string username{"root"};         // 用户名
    std::string password;                  // 密码
    std::string charset{"utf8mb4"};       // 字符集
    
    // 连接池配置
    size_t pool_size{10};                 // 连接池大小
    size_t max_overflow{20};              // 最大溢出连接数
    std::chrono::seconds pool_timeout{30}; // 获取连接超时
    std::chrono::seconds pool_recycle{3600}; // 连接回收时间
    bool pool_pre_ping{true};             // 连接前检测
    
    // 连接选项
    bool auto_reconnect{true};            // 自动重连
    std::chrono::seconds connect_timeout{10}; // 连接超时
    std::chrono::seconds read_timeout{300};   // 读超时，覆盖长区间回测查询
    std::chrono::seconds write_timeout{300};  // 写超时，避免大结果集/批量写入过早断开
    
    // SSL配置
    bool use_ssl{false};                  // 是否使用SSL
    std::string ssl_ca;                   // CA证书路径
    std::string ssl_cert;                 // 客户端证书
    std::string ssl_key;                  // 客户端密钥
    
    // 性能选项
    bool enable_query_cache{true};        // 启用查询缓存
    size_t max_allowed_packet{16777216};  // 最大包大小(16MB)
    
    /**
     * @brief 构建连接URL
     */
    std::string getConnectionUrl() const {
        return "mysql://" + username + ":" + password + 
               "@" + host + ":" + std::to_string(port) + 
               "/" + database + "?charset=" + charset;
    }
    
    /**
     * @brief 验证配置
     */
    bool validate() const {
        return !host.empty() && 
               !database.empty() && 
               !username.empty() && 
               port > 0 && port < 65536 &&
               pool_size > 0 &&
               pool_size <= 100;
    }
};

} // namespace database
} // namespace astock

#endif // ASTOCK_INFRASTRUCTURE_DATABASE_DATABASECONFIG_H
