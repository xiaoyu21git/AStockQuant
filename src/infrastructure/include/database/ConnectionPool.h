#ifndef CONNECTIONPOOL_H
#define CONNECTIONPOOL_H

#include <QObject>
#include <QSqlDatabase>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <QTimer>
#include <QDebug>
#include <QThread>

namespace astock {
namespace database {

/**
 * @brief 简洁的Qt风格数据库连接池
 * 
 * 功能特性：
 * - 单例模式，全局唯一实例
 * - 连接复用和自动管理
 * - 线程安全
 * - 自动清理空闲连接
 * - RAII连接守卫
 */
class ConnectionPool
{
public:
    static ConnectionPool& instance();  // 单例模式
    
    /**
     * @brief 获取数据库连接
     * @return QSqlDatabase对象，使用完需调用releaseConnection归还
     */
    QSqlDatabase getConnection();
    
    /**
     * @brief 释放数据库连接
     * @param db 要释放的数据库连接
     */
    void releaseConnection(const QSqlDatabase& db);
    
    /**
     * @brief 销毁连接池
     */
    void destroy();
    
    /**
     * @brief 配置连接池参数
     */
    void configure(const QString& hostName, const QString& databaseName,
                   const QString& username, const QString& password,
                   int port = 3306, int maxConnections = 10);

private:
    explicit ConnectionPool();
    ~ConnectionPool();
    
    // 禁止拷贝和赋值
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;
    
    QSqlDatabase createConnection();     // 创建新连接
    void cleanIdleConnections();         // 清理空闲连接

private:
    QQueue<QString> m_usedConnectionNames;   // 正在使用的连接名
    QQueue<QString> m_idleConnectionNames;    // 空闲的连接名
    
    QMutex m_mutex;
    QWaitCondition m_waitCondition;
    
    // 配置参数
    QString m_hostName;
    QString m_databaseName;
    QString m_username;
    QString m_password;
    int m_port;
    int m_maxConnectionCount;      // 最大连接数
    int m_connectionTimeout;        // 获取连接超时时间(ms)
    int m_cleanupInterval;          // 清理空闲连接间隔(ms)
    
    QTimer m_cleanupTimer;           // 清理定时器
    bool m_isDestroyed;              // 是否已销毁
};

/**
 * @brief RAII连接守卫类
 * 
 * 自动管理连接的获取和释放
 */
class ConnectionGuard {
public:
    explicit ConnectionGuard() 
        : m_db(ConnectionPool::instance().getConnection()), m_valid(true) {
    }
    
    ~ConnectionGuard() {
        if (m_valid) {
            ConnectionPool::instance().releaseConnection(m_db);
        }
    }
    
    QSqlDatabase& get() { return m_db; }
    operator QSqlDatabase&() { return m_db; }
    
    // 禁止拷贝
    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;

private:
    QSqlDatabase m_db;
    bool m_valid;
};

} // namespace database
} // namespace astock

#endif // CONNECTIONPOOL_H
