// DatabaseService.h
// 通用数据库服务单例类 - 集成连接池和链式调用
#pragma once

#include "QtMySQLDatabase.h"
#include "QueryBuilder.h"
#include "DatabaseConfig.h"
#include <memory>
#include <mutex>
#include <map>
#include <string>
#include <functional>
#include <QDebug>
#include <QString>

namespace astock {
namespace database {

/**
 * @brief 数据库服务单例类
 * 
 * 功能特性：
 * 1. 单例模式，全局唯一实例
 * 2. 集成连接池管理
 * 3. 提供链式调用接口
 * 4. 支持多数据源配置
 * 5. 自动连接管理
 * 6. 事务支持
 * 7. 统计和监控
 */
class DatabaseService {
public:
    // 单例访问
    static DatabaseService& instance() {
        static DatabaseService instance;
        return instance;
    }
    
    // 禁止拷贝
    DatabaseService(const DatabaseService&) = delete;
    DatabaseService& operator=(const DatabaseService&) = delete;
    
    /**
     * @brief 初始化默认数据库连接
     * @param config 数据库配置
     * @param useConnectionPool 是否使用连接池
     * @return 初始化是否成功
     */
    bool initialize(const DatabaseConfig& config, bool useConnectionPool = true) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (m_defaultDatabase && m_defaultDatabase->isOpen()) {
            qDebug() << "DatabaseService::initialize: 默认数据库连接已存在且已打开";
            return true;
        }
        
        try {
            qDebug() << "DatabaseService::initialize: 创建默认数据库连接...";
            qDebug() << "  主机:" << QString::fromStdString(config.host);
            qDebug() << "  端口:" << config.port;
            qDebug() << "  数据库:" << QString::fromStdString(config.database);
            qDebug() << "  用户名:" << QString::fromStdString(config.username);
            qDebug() << "  使用连接池:" << useConnectionPool;
            
            m_defaultDatabase = std::make_shared<QtMySQLDatabase>(config, useConnectionPool);
            
            if (!m_defaultDatabase) {
                qWarning() << "DatabaseService::initialize: 创建数据库连接失败";
                return false;
            }
            
            if (!m_defaultDatabase->open()) {
                QString error = m_defaultDatabase->getLastError();
                qWarning() << "DatabaseService::initialize: 打开数据库连接失败:" << error;
                m_defaultDatabase.reset();
                return false;
            }
            
            qDebug() << "✅ DatabaseService::initialize: 默认数据库连接成功";
            return true;
            
        } catch (const std::exception& e) {
            qCritical() << "DatabaseService::initialize: 异常:" << e.what();
            m_defaultDatabase.reset();
            return false;
        }
    }
    
    /**
     * @brief 添加数据源
     * @param name 数据源名称
     * @param config 数据库配置
     * @param useConnectionPool 是否使用连接池
     * @return 添加是否成功
     */
    bool addDataSource(const std::string& name, const DatabaseConfig& config, bool useConnectionPool = true) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (m_dataSources.find(name) != m_dataSources.end()) {
            qWarning() << "DatabaseService::addDataSource: 数据源已存在:" << QString::fromStdString(name);
            return false;
        }
        
        try {
            qDebug() << "DatabaseService::addDataSource: 创建数据源:" << QString::fromStdString(name);
            
            auto database = std::make_shared<QtMySQLDatabase>(config, useConnectionPool);
            
            if (!database) {
                qWarning() << "DatabaseService::addDataSource: 创建数据库连接失败";
                return false;
            }
            
            if (!database->open()) {
                QString error = database->getLastError();
                qWarning() << "DatabaseService::addDataSource: 打开数据库连接失败:" << error;
                return false;
            }
            
            m_dataSources[name] = database;
            qDebug() << "✅ DatabaseService::addDataSource: 数据源添加成功:" << QString::fromStdString(name);
            return true;
            
        } catch (const std::exception& e) {
            qCritical() << "DatabaseService::addDataSource: 异常:" << e.what();
            return false;
        }
    }
    
    /**
     * @brief 获取默认数据库连接
     * @return 数据库连接指针
     */
    std::shared_ptr<QtMySQLDatabase> getDatabase() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (!m_defaultDatabase) {
            qWarning() << "DatabaseService::getDatabase: 默认数据库连接未初始化";
            return nullptr;
        }
        
        if (!m_defaultDatabase->isOpen()) {
            qDebug() << "DatabaseService::getDatabase: 数据库连接已关闭，重新打开...";
            if (!m_defaultDatabase->open()) {
                qWarning() << "DatabaseService::getDatabase: 重新打开数据库连接失败";
                return nullptr;
            }
        }
        
        return m_defaultDatabase;
    }
    
    /**
     * @brief 获取指定数据源
     * @param name 数据源名称
     * @return 数据库连接指针
     */
    std::shared_ptr<QtMySQLDatabase> getDataSource(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        auto it = m_dataSources.find(name);
        if (it == m_dataSources.end()) {
            qWarning() << "DatabaseService::getDataSource: 数据源不存在:" << QString::fromStdString(name);
            return nullptr;
        }
        
        if (!it->second->isOpen()) {
            qDebug() << "DatabaseService::getDataSource: 数据源连接已关闭，重新打开...";
            if (!it->second->open()) {
                qWarning() << "DatabaseService::getDataSource: 重新打开数据源连接失败";
                return nullptr;
            }
        }
        
        return it->second;
    }
    
    /**
     * @brief 创建查询构建器（使用默认数据库）
     * @return 查询构建器指针
     */
    std::shared_ptr<QueryBuilder> createQueryBuilder() {
        auto database = getDatabase();
        if (!database) {
            qWarning() << "DatabaseService::createQueryBuilder: 无法获取数据库连接";
            return nullptr;
        }
        
        return ::astock::database::createQueryBuilder(database);
    }
    
    /**
     * @brief 创建查询构建器（使用指定数据源）
     * @param dataSourceName 数据源名称
     * @return 查询构建器指针
     */
    std::shared_ptr<QueryBuilder> createQueryBuilder(const std::string& dataSourceName) {
        auto database = getDataSource(dataSourceName);
        if (!database) {
            qWarning() << "DatabaseService::createQueryBuilder: 无法获取数据源:" << QString::fromStdString(dataSourceName);
            return nullptr;
        }
        
        return ::astock::database::createQueryBuilder(database);
    }
    
    /**
     * @brief 执行查询（快捷方法）
     * @param sql SQL语句
     * @param params 查询参数
     * @return 查询结果
     */
    QueryResult executeQuery(const QString& sql, const std::map<QString, QVariant>& params = {}) {
        auto database = getDatabase();
        if (!database) {
            qWarning() << "DatabaseService::executeQuery: 无法获取数据库连接";
            return QueryResult();
        }
        
        try {
            return database->executeQuery(sql, params);
        } catch (const std::exception& e) {
            qWarning() << "DatabaseService::executeQuery: 执行查询失败:" << e.what();
            return QueryResult();
        }
    }
    
    /**
     * @brief 执行更新（快捷方法）
     * @param sql SQL语句
     * @param params 查询参数
     * @return 受影响的行数
     */
    int executeUpdate(const QString& sql, const std::map<QString, QVariant>& params = {}) {
        auto database = getDatabase();
        if (!database) {
            qWarning() << "DatabaseService::executeUpdate: 无法获取数据库连接";
            return -1;
        }
        
        try {
            return database->executeUpdate(sql, params);
        } catch (const std::exception& e) {
            qWarning() << "DatabaseService::executeUpdate: 执行更新失败:" << e.what();
            return -1;
        }
    }
    
    /**
     * @brief 开始事务
     * @return 事务守卫对象
     */
    std::unique_ptr<TransactionGuard> beginTransaction(bool autoCommit = true) {
        auto database = getDatabase();
        if (!database) {
            qWarning() << "DatabaseService::beginTransaction: 无法获取数据库连接";
            return nullptr;
        }
        
        try {
            return database->beginTransaction(autoCommit);
        } catch (const std::exception& e) {
            qWarning() << "DatabaseService::beginTransaction: 开始事务失败:" << e.what();
            return nullptr;
        }
    }
    
    /**
     * @brief 检查数据库连接状态
     * @return 连接是否有效
     */
    bool isConnected() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_defaultDatabase && m_defaultDatabase->isOpen();
    }
    
    /**
     * @brief 检查数据源连接状态
     * @param name 数据源名称
     * @return 连接是否有效
     */
    bool isDataSourceConnected(const std::string& name) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        auto it = m_dataSources.find(name);
        if (it == m_dataSources.end()) {
            return false;
        }
        
        return it->second->isOpen();
    }
    
    /**
     * @brief 关闭所有数据库连接
     */
    void closeAll() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (m_defaultDatabase) {
            qDebug() << "DatabaseService::closeAll: 关闭默认数据库连接";
            m_defaultDatabase->close();
            m_defaultDatabase.reset();
        }
        
        for (auto& [name, database] : m_dataSources) {
            qDebug() << "DatabaseService::closeAll: 关闭数据源:" << QString::fromStdString(name);
            database->close();
        }
        
        m_dataSources.clear();
        qDebug() << "DatabaseService::closeAll: 所有数据库连接已关闭";
    }
    
    /**
     * @brief 获取数据源列表
     * @return 数据源名称列表
     */
    std::vector<std::string> getDataSourceNames() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        std::vector<std::string> names;
        for (const auto& [name, _] : m_dataSources) {
            names.push_back(name);
        }
        
        return names;
    }
    
    /**
     * @brief 移除数据源
     * @param name 数据源名称
     * @return 移除是否成功
     */
    bool removeDataSource(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        auto it = m_dataSources.find(name);
        if (it == m_dataSources.end()) {
            qWarning() << "DatabaseService::removeDataSource: 数据源不存在:" << QString::fromStdString(name);
            return false;
        }
        
        qDebug() << "DatabaseService::removeDataSource: 关闭并移除数据源:" << QString::fromStdString(name);
        it->second->close();
        m_dataSources.erase(it);
        
        return true;
    }
    
    /**
     * @brief 链式调用快捷方法 - 从指定表开始查询
     * @param table 表名
     * @return 查询构建器引用（用于链式调用）
     */
    QueryBuilder& from(const QString& table) {
        auto builder = createQueryBuilder();
        if (!builder) {
            throw std::runtime_error("无法创建查询构建器");
        }
        
        builder->reset();
        return builder->from(table);
    }
    
    /**
     * @brief 链式调用快捷方法 - 从指定表开始查询（使用指定数据源）
     * @param table 表名
     * @param dataSourceName 数据源名称
     * @return 查询构建器引用（用于链式调用）
     */
    QueryBuilder& from(const QString& table, const std::string& dataSourceName) {
        auto builder = createQueryBuilder(dataSourceName);
        if (!builder) {
            throw std::runtime_error("无法创建查询构建器");
        }
        
        builder->reset();
        return builder->from(table);
    }
    
private:
    DatabaseService() {
        qDebug() << "DatabaseService: 构造函数";
    }
    
    ~DatabaseService() {
        qDebug() << "DatabaseService: 析构函数";
        closeAll();
    }
    
private:
    std::shared_ptr<QtMySQLDatabase> m_defaultDatabase;
    std::map<std::string, std::shared_ptr<QtMySQLDatabase>> m_dataSources;
    mutable std::mutex m_mutex;
};

} // namespace database
} // namespace astock

// 全局快捷访问宏
#define DB_SERVICE astock::database::DatabaseService::instance()
#define DB_QUERY(table) DB_SERVICE.from(table)
#define DB_EXECUTE(sql) DB_SERVICE.executeQuery(sql)
#define DB_UPDATE(sql) DB_SERVICE.executeUpdate(sql)