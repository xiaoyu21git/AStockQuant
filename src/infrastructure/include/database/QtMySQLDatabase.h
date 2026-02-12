#ifndef ASTOCK_INFRASTRUCTURE_DATABASE_QTMYSQLDATABASE_H
#define ASTOCK_INFRASTRUCTURE_DATABASE_QTMYSQLDATABASE_H

#include "DatabaseConfig.h"
#include <memory>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <atomic>
#include <string>
#include <stdexcept>

// 检查是否定义了使用Qt MySQL的宏
#ifndef USE_QT_MYSQL
#define USE_QT_MYSQL 1  // 默认启用
#endif

#if USE_QT_MYSQL
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QString>
#include <QDateTime>
#include <QCoreApplication>
#include <QThread>
#include <QElapsedTimer>
#include <QDebug>
#include <QSqlRecord>
#endif

namespace astock {
namespace database {

/**
 * @brief Qt MySQL数据库异常类
 */
class QtMySQLException : public std::runtime_error {
public:
    QtMySQLException(const QString& message, const QSqlError& error = QSqlError())
        : std::runtime_error(message.toStdString()), 
          error_(error),
          message_(message) {}
    
    const QSqlError& getSqlError() const { return error_; }
    const QString& getMessage() const { return message_; }
    
private:
    QSqlError error_;
    QString message_;
};

/**
 * @brief 查询结果行
 */
class QueryResultRow {
public:
    QueryResultRow() = default;
    
    void setValue(const QString& column, const QVariant& value) {
        data_[column] = value;
    }
    
    QVariant getValue(const QString& column) const {
        auto it = data_.find(column);
        return it != data_.end() ? it->second : QVariant();
    }
    
    template<typename T>
    T getValueAs(const QString& column, const T& defaultValue = T()) const {
        QVariant value = getValue(column);
        if (value.isValid() && value.canConvert<T>()) {
            return value.value<T>();
        }
        return defaultValue;
    }
    
    QString getString(const QString& column, const QString& defaultValue = "") const {
        return getValueAs<QString>(column, defaultValue);
    }
    
    int getInt(const QString& column, int defaultValue = 0) const {
        return getValueAs<int>(column, defaultValue);
    }
    
    double getDouble(const QString& column, double defaultValue = 0.0) const {
        return getValueAs<double>(column, defaultValue);
    }
    
    QDateTime getDateTime(const QString& column, const QDateTime& defaultValue = QDateTime()) const {
        return getValueAs<QDateTime>(column, defaultValue);
    }
    
    bool contains(const QString& column) const {
        return data_.find(column) != data_.end();
    }
    
    const std::map<QString, QVariant>& getData() const { return data_; }
    
private:
    std::map<QString, QVariant> data_;
};

/**
 * @brief 查询结果集
 */
class QueryResult {
public:
    QueryResult() = default;
    
    void addRow(const QueryResultRow& row) {
        rows_.push_back(row);
    }
    
    size_t rowCount() const { return rows_.size(); }
    bool isEmpty() const { return rows_.empty(); }
    
    const QueryResultRow& getRow(size_t index) const {
        if (index >= rows_.size()) {
            throw QtMySQLException("Row index out of bounds");
        }
        return rows_[index];
    }
    
    const std::vector<QueryResultRow>& getRows() const { return rows_; }
    
    // 获取第一行第一列的值
    template<typename T>
    T getSingleValue(const T& defaultValue = T()) const {
        if (rows_.empty() || rows_[0].getData().empty()) {
            return defaultValue;
        }
        return rows_[0].getValueAs<T>(rows_[0].getData().begin()->first, defaultValue);
    }
    
    // 获取第一列的所有值
    template<typename T>
    std::vector<T> getColumnValues() const {
        std::vector<T> result;
        if (rows_.empty()) return result;
        
        QString firstColumn = rows_[0].getData().begin()->first;
        for (const auto& row : rows_) {
            result.push_back(row.getValueAs<T>(firstColumn));
        }
        return result;
    }
    
private:
    std::vector<QueryResultRow> rows_;
};

/**
 * @brief 事务守卫类（RAII）
 */
class TransactionGuard {
public:
    TransactionGuard(QSqlDatabase& db, bool autoCommit = true);
    ~TransactionGuard();
    
    void commit();
    void rollback();
    
    bool isActive() const { return active_; }
    
private:
    QSqlDatabase& db_;
    bool autoCommit_;
    bool active_;
    bool committed_;
};

/**
 * @brief Qt MySQL数据库封装类
 * 
 * 功能特性：
 * 1. 连接管理（单连接和连接池）
 * 2. SQL语句执行（查询、更新、插入、删除）
 * 3. 事务管理（自动提交、手动提交、回滚）
 * 4. 批量操作支持
 * 5. 预处理语句支持
 * 6. 错误处理和日志记录
 * 7. 连接状态监控
 */
class QtMySQLDatabase {
public:
    /**
     * @brief 构造函数
     * @param config 数据库配置
     * @param useConnectionPool 是否使用连接池
     */
    explicit QtMySQLDatabase(const DatabaseConfig& config, bool useConnectionPool = false);
    
    /**
     * @brief 析构函数
     */
    ~QtMySQLDatabase();
    
    // 禁止拷贝
    QtMySQLDatabase(const QtMySQLDatabase&) = delete;
    QtMySQLDatabase& operator=(const QtMySQLDatabase&) = delete;
    
    // 允许移动
    QtMySQLDatabase(QtMySQLDatabase&& other) noexcept;
    QtMySQLDatabase& operator=(QtMySQLDatabase&& other) noexcept;
    
    /**
     * @brief 打开数据库连接
     */
    bool open();
    
    /**
     * @brief 关闭数据库连接
     */
    void close();
    
    /**
     * @brief 检查连接是否有效
     */
    bool isOpen() const;
    
    /**
     * @brief 获取最后错误信息
     */
    QString getLastError() const;
    
    /**
     * @brief 获取最后SQL错误
     */
    QSqlError getLastSqlError() const;
    
    /**
     * @brief 执行查询并返回结果集
     * @param sql SQL查询语句
     * @param params 查询参数
     * @return 查询结果
     */
    QueryResult executeQuery(const QString& sql, 
                            const std::map<QString, QVariant>& params = {});
    
    /**
     * @brief 执行更新操作（INSERT/UPDATE/DELETE）
     * @param sql SQL语句
     * @param params 参数
     * @return 受影响的行数
     */
    int executeUpdate(const QString& sql, 
                     const std::map<QString, QVariant>& params = {});
    
    /**
     * @brief 执行批量更新
     * @param sql SQL语句模板
     * @param batchParams 批量参数列表
     * @return 受影响的总行数
     */
    int executeBatchUpdate(const QString& sql, 
                          const std::vector<std::map<QString, QVariant>>& batchParams);
    
    /**
     * @brief 执行存储过程
     * @param procedureName 存储过程名称
     * @param params 输入参数
     * @param outParams 输出参数（引用）
     * @return 执行结果
     */
    QueryResult executeProcedure(const QString& procedureName,
                                const std::map<QString, QVariant>& params = {},
                                std::map<QString, QVariant>* outParams = nullptr);
    
    /**
     * @brief 开始事务
     * @return 事务守卫对象
     */
    std::unique_ptr<TransactionGuard> beginTransaction(bool autoCommit = true);
    
    /**
     * @brief 直接提交事务
     */
    bool commitTransaction();
    
    /**
     * @brief 直接回滚事务
     */
    bool rollbackTransaction();
    
    /**
     * @brief 检查表是否存在
     */
    bool tableExists(const QString& tableName);
    
    /**
     * @brief 获取表结构信息
     */
    QueryResult getTableSchema(const QString& tableName);
    
    /**
     * @brief 获取数据库版本
     */
    QString getDatabaseVersion();
    
    /**
     * @brief 获取连接统计信息
     */
    struct ConnectionStats {
        int totalQueries{0};
        int failedQueries{0};
        int totalUpdates{0};
        int failedUpdates{0};
        int totalTransactions{0};
        int failedTransactions{0};
        qint64 totalQueryTimeMs{0};
        qint64 totalUpdateTimeMs{0};
    };
    
    ConnectionStats getConnectionStats() const;
    
    /**
     * @brief 重置统计信息
     */
    void resetStats();
    
    /**
     * @brief 设置查询超时时间（毫秒）
     */
    void setQueryTimeout(int milliseconds);
    
    /**
     * @brief 设置连接选项
     */
    void setConnectionOptions(const std::map<QString, QString>& options);
    
private:
    /**
     * @brief 从连接池获取连接
     */
    QSqlDatabase getConnectionFromPool();
    
    /**
     * @brief 归还连接到连接池
     */
    void returnConnectionToPool(QSqlDatabase& connection);
    
    /**
     * @brief 创建新连接
     */
    QSqlDatabase createNewConnection();
    
    /**
     * @brief 验证连接有效性
     */
    bool validateConnection(QSqlDatabase& connection);
    
    /**
     * @brief 执行SQL语句（内部实现）
     */
    QSqlQuery executeSqlInternal(QSqlDatabase& connection, 
                                const QString& sql, 
                                const std::map<QString, QVariant>& params);
    
    /**
     * @brief 绑定查询参数
     */
    void bindQueryParameters(QSqlQuery& query, 
                            const std::map<QString, QVariant>& params);
    
    /**
     * @brief 将QSqlQuery转换为QueryResult
     */
    QueryResult convertToQueryResult(QSqlQuery& query);
    
    DatabaseConfig config_;
    bool useConnectionPool_;
    
    // 连接管理
    mutable std::mutex connectionMutex_;
    std::vector<QSqlDatabase> connectionPool_;
    std::map<QString, QSqlDatabase> activeConnections_;
    
    // 统计信息
    mutable std::mutex statsMutex_;
    ConnectionStats stats_;
    
    // 连接选项
    std::map<QString, QString> connectionOptions_;
    int queryTimeoutMs_{30000}; // 默认30秒
    
    // 状态
    std::atomic<bool> initialized_{false};
    std::atomic<bool> shutdown_{false};
};

} // namespace database
} // namespace astock

#endif // ASTOCK_INFRASTRUCTURE_DATABASE_QTMYSQLDATABASE_H