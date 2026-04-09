// DatabaseConnectionManager.h
// 数据库连接管理器 - 单例模式，管理全局数据库连接
#pragma once

#include "../../../infrastructure/include/database/QtMySQLDatabase.h"
#include "../../../infrastructure/include/database/DatabaseConfig.h"
#include "../../../infrastructure/include/database/ConnectionPool.h"
#include "../../../foundation/include/foundation.h"
#include <memory>
#include <mutex>
#include <QDebug>
#include <QtGlobal>

namespace {

inline bool verboseDatabasePoolLogging()
{
    return qEnvironmentVariableIntValue("ASTOCK_VERBOSE_DB_POOL") > 0;
}

}

namespace astock {
namespace database {

// FROZEN INTERFACE - 数据库连接管理器
// 此接口已冻结，不再添加新的公共方法
// 如需扩展功能，请创建新的DatabaseService类
class DatabaseConnectionManager {
public:
    // 单例访问
    static DatabaseConnectionManager& instance() {
        // 进程生命周期单例，避免 Qt SQL/连接池在静态析构阶段出现销毁顺序竞态。
        static DatabaseConnectionManager* instance = new DatabaseConnectionManager();
        return *instance;
    }
    
    // 获取数据库连接
    std::shared_ptr<QtMySQLDatabase> getDatabase() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (!m_database) {
            qDebug() << "DatabaseConnectionManager::getDatabase: 创建新的数据库连接...";
            initializeDatabase();
        }
        
        if (m_database && !m_database->isOpen()) {
            qDebug() << "DatabaseConnectionManager::getDatabase: 数据库连接已关闭，重新打开...";
            if (!m_database->open()) {
                qWarning() << "DatabaseConnectionManager::getDatabase: 重新打开数据库连接失败";
                return nullptr;
            }
        }
        
        return m_database;
    }
    
    // 初始化数据库连接
    bool initialize() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (m_database && m_database->isOpen()) {
            if (verboseDatabasePoolLogging()) {
                qDebug() << "DatabaseConnectionManager::initialize: 数据库连接已存在且已打开";
            }
            return true;
        }
        
        return initializeDatabase();
    }
    
    // 关闭数据库连接
    void close() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (m_database) {
            qDebug() << "DatabaseConnectionManager::close: 关闭数据库连接";
            m_database->close();
            m_database.reset();
        }
    }
    
    // 检查数据库连接状态
    bool isConnected() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_database && m_database->isOpen();
    }
    
private:
    DatabaseConnectionManager() {
        qDebug() << "DatabaseConnectionManager: 构造函数";
    }
    
    ~DatabaseConnectionManager() {
        qDebug() << "DatabaseConnectionManager: 析构函数";
        close();
    }
    
    // 禁止拷贝
    DatabaseConnectionManager(const DatabaseConnectionManager&) = delete;
    DatabaseConnectionManager& operator=(const DatabaseConnectionManager&) = delete;
    
    // 初始化数据库连接
    bool initializeDatabase() {
        try {
            // 创建数据库配置
            DatabaseConfig config;
            
            // 尝试从配置管理系统读取数据库配置
            try {
                // 使用ConfigManager的便捷方法读取配置
                auto& configManager = foundation::config::ConfigManager::instance();
                
                // 读取MySQL配置
                config.host = configManager.get_app_config_string("mysql.host", "127.0.0.1");
                config.port = configManager.get_app_config_int("mysql.port", 3306);
                config.database = configManager.get_app_config_string("mysql.database", "astock_quant");
                config.username = configManager.get_app_config_string("mysql.user", "root");
                config.password = configManager.get_app_config_string("mysql.password", "123456a");
                config.charset = "utf8mb4";
                config.pool_size = 5; // 启用连接池，设置连接池大小为5
                
                qDebug() << "DatabaseConnectionManager::initializeDatabase: 从配置管理系统读取数据库配置";
            } catch (const std::exception& e) {
                qWarning() << "DatabaseConnectionManager::initializeDatabase: 读取配置失败:" << e.what() << "，使用默认配置";
                // 使用默认配置
                config.host = "127.0.0.1";
                config.port = 3306;
                config.database = "astock_quant";
                config.username = "root";
                config.password = "123456a";
                config.charset = "utf8mb4";
                config.pool_size = 5;
            }
            
            qDebug() << "DatabaseConnectionManager::initializeDatabase: 创建数据库连接...";
            qDebug() << "  主机:" << QString::fromStdString(config.host);
            qDebug() << "  端口:" << config.port;
            qDebug() << "  数据库:" << QString::fromStdString(config.database);
            qDebug() << "  用户名:" << QString::fromStdString(config.username);
            qDebug() << "  连接池大小:" << config.pool_size;
            
            // 配置ConnectionPool（重要！）
            // FactorRepository使用ConnectionPool，所以需要配置它
            try {
                auto& connectionPool = astock::database::ConnectionPool::instance();
                connectionPool.configure(
                    QString::fromStdString(config.host),
                    QString::fromStdString(config.database),
                    QString::fromStdString(config.username),
                    QString::fromStdString(config.password),
                    config.port,
                    config.pool_size
                );
                qDebug() << "✅ DatabaseConnectionManager::initializeDatabase: ConnectionPool配置完成";
            } catch (const std::exception& e) {
                qWarning() << "DatabaseConnectionManager::initializeDatabase: 配置ConnectionPool失败:" << e.what();
                // 继续执行，因为QtMySQLDatabase可能仍然可以工作
            }
            
            // 创建数据库连接（启用连接池）
            m_database = std::make_shared<QtMySQLDatabase>(config, true);
            
            if (!m_database) {
                qWarning() << "DatabaseConnectionManager::initializeDatabase: 创建数据库连接失败";
                return false;
            }
            
            qDebug() << "DatabaseConnectionManager::initializeDatabase: 打开数据库连接...";
            if (!m_database->open()) {
                QString error = m_database->getLastError();
                qWarning() << "DatabaseConnectionManager::initializeDatabase: 打开数据库连接失败:" << error;
                m_database.reset();
                return false;
            }
            
            qDebug() << "✅ DatabaseConnectionManager::initializeDatabase: 数据库连接成功（已启用连接池）";
            return true;
            
        } catch (const std::exception& e) {
            qCritical() << "DatabaseConnectionManager::initializeDatabase: 异常:" << e.what();
            m_database.reset();
            return false;
        }
    }
    
private:
    std::shared_ptr<QtMySQLDatabase> m_database;
    mutable std::mutex m_mutex;
};

} // namespace database
} // namespace astock