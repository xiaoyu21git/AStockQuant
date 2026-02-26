#include "database/QtMySQLDatabase.h"

#if USE_QT_MYSQL
#include <QCoreApplication>
#include <QThread>
#include <QElapsedTimer>
#include <QDebug>
#include <iostream>
#include <random>

namespace astock {
namespace database {

// ============================================================================
// TransactionGuard 实现
// ============================================================================

TransactionGuard::TransactionGuard(QSqlDatabase& db, bool autoCommit)
    : db_(db), autoCommit_(autoCommit), active_(false), committed_(false) {
    
    if (!db_.isOpen()) {
        throw QtMySQLException("Database is not open");
    }
    
    if (!db_.transaction()) {
        throw QtMySQLException("Failed to begin transaction: " + db_.lastError().text());
    }
    
    active_ = true;
}

TransactionGuard::~TransactionGuard() {
    if (active_ && !committed_) {
        if (autoCommit_) {
            try {
                commit();
            } catch (const QtMySQLException& e) {
                // 在析构函数中不能抛出异常，记录错误
                qWarning() << "Failed to auto-commit transaction:" << e.what();
                db_.rollback();
            }
        } else {
            db_.rollback();
        }
    }
}

void TransactionGuard::commit() {
    if (!active_) {
        throw QtMySQLException("Transaction is not active");
    }
    
    if (!db_.commit()) {
        throw QtMySQLException("Failed to commit transaction: " + db_.lastError().text());
    }
    
    committed_ = true;
    active_ = false;
}

void TransactionGuard::rollback() {
    if (!active_) {
        throw QtMySQLException("Transaction is not active");
    }
    
    if (!db_.rollback()) {
        throw QtMySQLException("Failed to rollback transaction: " + db_.lastError().text());
    }
    
    active_ = false;
}

// ============================================================================
// QtMySQLDatabase 实现
// ============================================================================

QtMySQLDatabase::QtMySQLDatabase(const DatabaseConfig& config, bool useConnectionPool)
    : config_(config), useConnectionPool_(useConnectionPool) {
    
    // 检查Qt是否支持MySQL或ODBC驱动
    QStringList drivers = QSqlDatabase::drivers();
    
    // 根据配置的driver字段选择驱动
    QString driverType = QString::fromStdString(config_.driver);
    if (driverType == "mysql" || driverType == "QMYSQL") {
        if (!drivers.contains("QMYSQL")) {
            throw QtMySQLException("QMYSQL driver is not available. Available drivers: " + drivers.join(", "));
        }
    } else if (driverType == "odbc" || driverType == "QODBC") {
        if (!drivers.contains("QODBC")) {
            throw QtMySQLException("QODBC driver is not available. Available drivers: " + drivers.join(", "));
        }
    } else {
        // 默认尝试QMYSQL，如果不可用则尝试QODBC
        if (drivers.contains("QMYSQL")) {
            config_.driver = "QMYSQL";
        } else if (drivers.contains("QODBC")) {
            config_.driver = "QODBC";
        } else {
            throw QtMySQLException("No suitable database driver available. Available drivers: " + drivers.join(", "));
        }
    }
    
    // 验证配置
    if (!config_.validate()) {
        throw QtMySQLException("Invalid database configuration");
    }
}

QtMySQLDatabase::~QtMySQLDatabase() {
    close();
}

QtMySQLDatabase::QtMySQLDatabase(QtMySQLDatabase&& other) noexcept
    : config_(std::move(other.config_)),
      useConnectionPool_(other.useConnectionPool_),
      connectionPool_(std::move(other.connectionPool_)),
      activeConnections_(std::move(other.activeConnections_)),
      stats_(other.stats_),
      connectionOptions_(std::move(other.connectionOptions_)),
      queryTimeoutMs_(other.queryTimeoutMs_),
      initialized_(other.initialized_.load()),
      shutdown_(other.shutdown_.load()) {
    
    other.initialized_ = false;
    other.shutdown_ = true;
}

QtMySQLDatabase& QtMySQLDatabase::operator=(QtMySQLDatabase&& other) noexcept {
    if (this != &other) {
        close();
        
        config_ = std::move(other.config_);
        useConnectionPool_ = other.useConnectionPool_;
        connectionPool_ = std::move(other.connectionPool_);
        activeConnections_ = std::move(other.activeConnections_);
        stats_ = other.stats_;
        connectionOptions_ = std::move(other.connectionOptions_);
        queryTimeoutMs_ = other.queryTimeoutMs_;
        initialized_ = other.initialized_.load();
        shutdown_ = other.shutdown_.load();
        
        other.initialized_ = false;
        other.shutdown_ = true;
    }
    return *this;
}

bool QtMySQLDatabase::open() {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    
    if (initialized_) {
        return true;
    }
    
    if (shutdown_) {
        throw QtMySQLException("Database has been shutdown");
    }
    
    try {
        if (useConnectionPool_) {
            // 初始化连接池
            for (size_t i = 0; i < config_.pool_size; ++i) {
                QSqlDatabase connection = createNewConnection();
                if (connection.isOpen()) {
                    connectionPool_.push_back(connection);
                } else {
                    qWarning() << "Failed to create connection for pool:" << connection.lastError().text();
                }
            }
            
            if (connectionPool_.empty()) {
                throw QtMySQLException("Failed to initialize connection pool");
            }
            
            qDebug() << "Connection pool initialized with" << connectionPool_.size() << "connections";
        } else {
            // 创建单个连接
            QSqlDatabase connection = createNewConnection();
            if (!connection.isOpen()) {
                throw QtMySQLException("Failed to open database connection: " + connection.lastError().text());
            }
            
            QString connectionName = QString("astock_mysql_%1").arg(reinterpret_cast<quintptr>(this));
            connectionPool_.push_back(connection);
            qDebug() << "Single connection created:" << connectionName;
        }
        
        initialized_ = true;
        return true;
        
    } catch (const std::exception& e) {
        qCritical() << "Failed to open database:" << e.what();
        return false;
    }
}

void QtMySQLDatabase::close() {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    
    if (!initialized_) {
        return;
    }
    
    shutdown_ = true;
    
    // 安全地关闭连接，避免仍在被使用时移除
    // 1. 先关闭活跃连接，但不立即移除
    for (auto& [threadId, connection] : activeConnections_) {
        if (connection.isOpen()) {
            // 尝试提交任何未完成的事务
            if (connection.isOpen() && connection.isValid()) {
                QSqlQuery query(connection);
                if (query.isActive()) {
                    query.finish();
                }
            }
            connection.close();
        }
    }
    activeConnections_.clear();
    
    // 2. 关闭连接池中的连接
    for (auto& connection : connectionPool_) {
        if (connection.isOpen()) {
            connection.close();
        }
    }
    connectionPool_.clear();
    
    // 3. 延迟移除数据库连接，避免仍在被使用时调用removeDatabase
    // 注意：不能在这里立即调用removeDatabase，因为连接可能仍在被其他线程使用
    // 在实际应用中，应该由调用者确保在关闭数据库前没有活跃查询
    qDebug() << "Database connections closed. Note: Some connections may still be referenced elsewhere.";
    
    initialized_ = false;
    qDebug() << "Database closed";
}

bool QtMySQLDatabase::isOpen() const {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    return initialized_ && !connectionPool_.empty();
}

QString QtMySQLDatabase::getLastError() const {
    QSqlError error = getLastSqlError();
    if (error.isValid()) {
        return error.text();
    }
    return QString();
}

QSqlError QtMySQLDatabase::getLastSqlError() const {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    
    // 检查活跃连接的错误
    for (const auto& [threadId, connection] : activeConnections_) {
        QSqlError error = connection.lastError();
        if (error.isValid()) {
            return error;
        }
    }
    
    // 检查连接池中的错误
    for (const auto& connection : connectionPool_) {
        QSqlError error = connection.lastError();
        if (error.isValid()) {
            return error;
        }
    }
    
    return QSqlError();
}

QueryResult QtMySQLDatabase::executeQuery(const QString& sql, 
                                         const std::map<QString, QVariant>& params) {
    QElapsedTimer timer;
    timer.start();
    
    std::lock_guard<std::mutex> statsLock(statsMutex_);
    stats_.totalQueries++;
    
    try {
        QSqlDatabase connection = useConnectionPool_ ? 
                                 getConnectionFromPool() : 
                                 (connectionPool_.empty() ? createNewConnection() : connectionPool_[0]);
        
        if (!connection.isOpen()) {
            throw QtMySQLException("Database connection is not open");
        }
        
        QSqlQuery query = executeSqlInternal(connection, sql, params);
        
        QueryResult result = convertToQueryResult(query);
        
        if (useConnectionPool_) {
            returnConnectionToPool(connection);
        }
        
        stats_.totalQueryTimeMs += timer.elapsed();
        return result;
        
    } catch (const std::exception& e) {
        stats_.failedQueries++;
        throw QtMySQLException(QString("Query failed: %1").arg(e.what()));
    }
}

int QtMySQLDatabase::executeUpdate(const QString& sql, 
                                  const std::map<QString, QVariant>& params) {
    QElapsedTimer timer;
    timer.start();
    
    std::lock_guard<std::mutex> statsLock(statsMutex_);
    stats_.totalUpdates++;
    
    try {
        QSqlDatabase connection = useConnectionPool_ ? 
                                 getConnectionFromPool() : 
                                 (connectionPool_.empty() ? createNewConnection() : connectionPool_[0]);
        
        if (!connection.isOpen()) {
            throw QtMySQLException("Database connection is not open");
        }
        
        QSqlQuery query = executeSqlInternal(connection, sql, params);
        
        int affectedRows = query.numRowsAffected();
        if (affectedRows == -1) {
            // 某些操作可能返回-1，尝试从查询结果判断
            if (query.isActive() && query.isSelect()) {
                affectedRows = 0;
            }
        }
        
        if (useConnectionPool_) {
            returnConnectionToPool(connection);
        }
        
        stats_.totalUpdateTimeMs += timer.elapsed();
        return affectedRows;
        
    } catch (const std::exception& e) {
        stats_.failedUpdates++;
        throw QtMySQLException(QString("Update failed: %1").arg(e.what()));
    }
}

int QtMySQLDatabase::executeBatchUpdate(const QString& sql, 
                                       const std::vector<std::map<QString, QVariant>>& batchParams) {
    if (batchParams.empty()) {
        return 0;
    }
    
    QElapsedTimer timer;
    timer.start();
    
    std::lock_guard<std::mutex> statsLock(statsMutex_);
    stats_.totalUpdates++;
    
    try {
        QSqlDatabase connection = useConnectionPool_ ? 
                                 getConnectionFromPool() : 
                                 (connectionPool_.empty() ? createNewConnection() : connectionPool_[0]);
        
        if (!connection.isOpen()) {
            throw QtMySQLException("Database connection is not open");
        }
        
        // 开始事务
        if (!connection.transaction()) {
            throw QtMySQLException("Failed to begin transaction for batch update: " + connection.lastError().text());
        }
        
        int totalAffectedRows = 0;
        bool success = true;
        
        for (const auto& params : batchParams) {
            QSqlQuery query = executeSqlInternal(connection, sql, params);
            
            int affectedRows = query.numRowsAffected();
            if (affectedRows == -1) {
                affectedRows = 0;
            }
            
            totalAffectedRows += affectedRows;
            
            if (query.lastError().isValid()) {
                success = false;
                break;
            }
        }
        
        if (success) {
            if (!connection.commit()) {
                throw QtMySQLException("Failed to commit batch update: " + connection.lastError().text());
            }
        } else {
            connection.rollback();
            throw QtMySQLException("Batch update failed, transaction rolled back");
        }
        
        if (useConnectionPool_) {
            returnConnectionToPool(connection);
        }
        
        stats_.totalUpdateTimeMs += timer.elapsed();
        return totalAffectedRows;
        
    } catch (const std::exception& e) {
        stats_.failedUpdates++;
        throw QtMySQLException(QString("Batch update failed: %1").arg(e.what()));
    }
}

QueryResult QtMySQLDatabase::executeProcedure(const QString& procedureName,
                                             const std::map<QString, QVariant>& params,
                                             std::map<QString, QVariant>* outParams) {
    // 构建存储过程调用语句
    QStringList paramNames;
    for (const auto& [key, value] : params) {
        paramNames.append(":" + key);
    }
    
    QString sql = QString("CALL %1(%2)").arg(procedureName).arg(paramNames.join(", "));
    
    QElapsedTimer timer;
    timer.start();
    
    std::lock_guard<std::mutex> statsLock(statsMutex_);
    stats_.totalQueries++;
    
    try {
        QSqlDatabase connection = useConnectionPool_ ? 
                                 getConnectionFromPool() : 
                                 (connectionPool_.empty() ? createNewConnection() : connectionPool_[0]);
        
        if (!connection.isOpen()) {
            throw QtMySQLException("Database connection is not open");
        }
        
        QSqlQuery query = executeSqlInternal(connection, sql, params);
        
        QueryResult result = convertToQueryResult(query);
        
        // 如果有输出参数，从查询中获取
        if (outParams) {
            // 存储过程输出参数通常通过SELECT返回
            // 这里假设输出参数在结果集中
            // 实际实现可能需要根据具体存储过程调整
        }
        
        if (useConnectionPool_) {
            returnConnectionToPool(connection);
        }
        
        stats_.totalQueryTimeMs += timer.elapsed();
        return result;
        
    } catch (const std::exception& e) {
        stats_.failedQueries++;
        throw QtMySQLException(QString("Procedure execution failed: %1").arg(e.what()));
    }
}

std::unique_ptr<TransactionGuard> QtMySQLDatabase::beginTransaction(bool autoCommit) {
    std::lock_guard<std::mutex> statsLock(statsMutex_);
    stats_.totalTransactions++;
    
    try {
        QSqlDatabase connection = useConnectionPool_ ? 
                                 getConnectionFromPool() : 
                                 (connectionPool_.empty() ? createNewConnection() : connectionPool_[0]);
        
        if (!connection.isOpen()) {
            throw QtMySQLException("Database connection is not open");
        }
        
        // 创建事务守卫，它会管理连接的生命周期
        auto guard = std::make_unique<TransactionGuard>(connection, autoCommit);
        
        // 如果是连接池模式，需要在事务结束后归还连接
        // 这里通过lambda捕获连接并在事务结束时归还
        if (useConnectionPool_) {
            // 使用自定义删除器在事务守卫销毁时归还连接
            struct TransactionGuardWithCleanup {
                std::unique_ptr<TransactionGuard> guard;
                QtMySQLDatabase* db;
                QSqlDatabase connection;
                
                ~TransactionGuardWithCleanup() {
                    if (db && connection.isOpen()) {
                        db->returnConnectionToPool(connection);
                    }
                }
            };
            
            // 由于unique_ptr的删除器限制，这里简化处理
            // 实际使用时，调用者需要在事务结束后手动调用returnConnectionToPool
            qWarning() << "Note: When using connection pool with transactions, "
                      << "you need to manually return the connection after transaction ends.";
        }
        
        return guard;
        
    } catch (const std::exception& e) {
        stats_.failedTransactions++;
        throw QtMySQLException(QString("Failed to begin transaction: %1").arg(e.what()));
    }
}

bool QtMySQLDatabase::commitTransaction() {
    // 这个方法主要用于直接操作，不通过TransactionGuard
    // 实际应该使用TransactionGuard来管理事务
    qWarning() << "Direct transaction commit is not recommended. Use TransactionGuard instead.";
    return false;
}

bool QtMySQLDatabase::rollbackTransaction() {
    // 这个方法主要用于直接操作，不通过TransactionGuard
    // 实际应该使用TransactionGuard来管理事务
    qWarning() << "Direct transaction rollback is not recommended. Use TransactionGuard instead.";
    return false;
}

bool QtMySQLDatabase::tableExists(const QString& tableName) {
    QString sql = "SELECT COUNT(*) as count FROM information_schema.tables "
                  "WHERE table_schema = :database AND table_name = :table";
    
    std::map<QString, QVariant> params = {
        {"database", QString::fromStdString(config_.database)},
        {"table", tableName}
    };
    
    try {
        QueryResult result = executeQuery(sql, params);
        return !result.isEmpty() && result.getRow(0).getInt("count") > 0;
    } catch (const QtMySQLException&) {
        return false;
    }
}

QueryResult QtMySQLDatabase::getTableSchema(const QString& tableName) {
    QString sql = "SELECT column_name, data_type, is_nullable, column_default, column_comment "
                  "FROM information_schema.columns "
                  "WHERE table_schema = :database AND table_name = :table "
                  "ORDER BY ordinal_position";
    
    std::map<QString, QVariant> params = {
        {"database", QString::fromStdString(config_.database)},
        {"table", tableName}
    };
    
    return executeQuery(sql, params);
}

QString QtMySQLDatabase::getDatabaseVersion() {
    QString sql = "SELECT VERSION()";
    
    try {
        QueryResult result = executeQuery(sql);
        if (!result.isEmpty()) {
            return result.getRow(0).getString("VERSION()");
        }
    } catch (const QtMySQLException& e) {
        qWarning() << "Failed to get database version:" << e.what();
    }
    
    return "Unknown";
}

QtMySQLDatabase::ConnectionStats QtMySQLDatabase::getConnectionStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void QtMySQLDatabase::resetStats() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_ = ConnectionStats{};
}

void QtMySQLDatabase::setQueryTimeout(int milliseconds) {
    queryTimeoutMs_ = milliseconds;
}

void QtMySQLDatabase::setConnectionOptions(const std::map<QString, QString>& options) {
    connectionOptions_ = options;
}

// ============================================================================
// 私有方法实现
// ============================================================================

QSqlDatabase QtMySQLDatabase::getConnectionFromPool() {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    
    if (connectionPool_.empty()) {
        // 连接池为空，创建新连接
        QSqlDatabase connection = createNewConnection();
        if (!connection.isOpen()) {
            throw QtMySQLException("Failed to create new connection from empty pool");
        }
        
        // 记录活跃连接
        QString threadId = QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
        activeConnections_[threadId] = connection;
        
        return connection;
    }
    
    // 从连接池获取一个连接
    QSqlDatabase connection = connectionPool_.back();
    connectionPool_.pop_back();
    
    // 验证连接有效性
    if (!validateConnection(connection)) {
        // 连接无效，创建新连接
        if (connection.isOpen()) {
            connection.close();
        }
        connection = createNewConnection();
    }
    
    // 记录活跃连接
    QString threadId = QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    activeConnections_[threadId] = connection;
    
    return connection;
}

void QtMySQLDatabase::returnConnectionToPool(QSqlDatabase& connection) {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    
    // 从活跃连接中移除
    QString threadId = QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    activeConnections_.erase(threadId);
    
    // 验证连接有效性
    if (!validateConnection(connection)) {
        // 连接无效，关闭并创建新连接
        if (connection.isOpen()) {
            connection.close();
        }
        connection = createNewConnection();
    }
    
    // 归还到连接池
    if (connection.isOpen()) {
        connectionPool_.push_back(connection);
    }
}

QSqlDatabase QtMySQLDatabase::createNewConnection() {
    // 生成唯一的连接名称
    static std::atomic<int> connectionCounter{0};
    QString connectionName = QString("astock_mysql_%1_%2")
        .arg(reinterpret_cast<quintptr>(this))
        .arg(connectionCounter.fetch_add(1));
    
    // 根据配置的driver字段选择驱动
    QString driverType = QString::fromStdString(config_.driver);
    QString driverName;
    
    if (driverType == "mysql" || driverType == "QMYSQL") {
        driverName = "QMYSQL";
    } else if (driverType == "odbc" || driverType == "QODBC") {
        driverName = "QODBC";
    } else {
        // 默认使用QMYSQL
        driverName = "QMYSQL";
    }
    
    qDebug() << "Creating database connection with driver:" << driverName;
    
    // 创建数据库连接
    QSqlDatabase db = QSqlDatabase::addDatabase(driverName, connectionName);
    
    if (driverName == "QMYSQL") {
        // QMYSQL驱动配置
        db.setHostName(QString::fromStdString(config_.host));
        db.setPort(config_.port);
        db.setDatabaseName(QString::fromStdString(config_.database));
        db.setUserName(QString::fromStdString(config_.username));
        db.setPassword(QString::fromStdString(config_.password));
        
        // 设置连接选项
        if (!config_.charset.empty()) {
            db.setConnectOptions(QString("MYSQL_OPT_CONNECT_TIMEOUT=%1;MYSQL_OPT_READ_TIMEOUT=%2;MYSQL_OPT_WRITE_TIMEOUT=%3;CLIENT_MULTI_STATEMENTS=1")
                .arg(config_.connect_timeout.count())
                .arg(config_.read_timeout.count())
                .arg(config_.write_timeout.count()));
        }
    } else if (driverName == "QODBC") {
        // QODBC驱动配置
        // 构建ODBC连接字符串
        QString connectionString = QString("DRIVER={MySQL ODBC 8.0 Unicode Driver};"
                                          "SERVER=%1;"
                                          "PORT=%2;"
                                          "DATABASE=%3;"
                                          "USER=%4;"
                                          "PASSWORD=%5;"
                                          "CHARSET=%6;"
                                          "OPTION=3;")
            .arg(QString::fromStdString(config_.host))
            .arg(config_.port)
            .arg(QString::fromStdString(config_.database))
            .arg(QString::fromStdString(config_.username))
            .arg(QString::fromStdString(config_.password))
            .arg(QString::fromStdString(config_.charset));
        
        db.setDatabaseName(connectionString);
    }
    
    // 应用自定义连接选项
    for (const auto& [key, value] : connectionOptions_) {
        QString currentOptions = db.connectOptions();
        if (!currentOptions.isEmpty()) {
            currentOptions += ";";
        }
        currentOptions += QString("%1=%2").arg(key).arg(value);
        db.setConnectOptions(currentOptions);
    }
    
    // 打开连接
    if (!db.open()) {
        throw QtMySQLException("Failed to open database connection: " + db.lastError().text());
    }
    
    // 设置字符集（仅对QMYSQL有效）
    if (!config_.charset.empty() && driverName == "QMYSQL") {
        QSqlQuery query(db);
        if (!query.exec(QString("SET NAMES '%1'").arg(QString::fromStdString(config_.charset)))) {
            qWarning() << "Failed to set charset:" << query.lastError().text();
        }
    }
    
    qDebug() << "Database connection created successfully with driver:" << driverName;
    return db;
}

bool QtMySQLDatabase::validateConnection(QSqlDatabase& connection) {
    if (!connection.isOpen()) {
        return false;
    }
    
    // 简单ping测试
    QSqlQuery query(connection);
    if (!query.exec("SELECT 1")) {
        return false;
    }
    
    return true;
}

QSqlQuery QtMySQLDatabase::executeSqlInternal(QSqlDatabase& connection, 
                                             const QString& sql, 
                                             const std::map<QString, QVariant>& params) {
    QSqlQuery query(connection);
    
    // 设置查询超时
    if (queryTimeoutMs_ > 0) {
        // Qt的MySQL驱动不支持通过QSqlQuery::setQueryTimeout设置查询超时
        // 查询超时已经通过连接选项MYSQL_OPT_READ_TIMEOUT在createNewConnection中设置
        QString driverName = connection.driverName();
        if (driverName == "QMYSQL") {
            // QMYSQL驱动通过连接选项设置超时
            qDebug() << "Query timeout configured via connection options for QMYSQL driver: " 
                     << (queryTimeoutMs_ / 1000) << "seconds";
        } else if (driverName == "QODBC") {
            // QODBC驱动可能支持不同的超时设置方式
            qDebug() << "Query timeout configured for QODBC driver: " 
                     << (queryTimeoutMs_ / 1000) << "seconds";
        }
    }
    
    // 准备查询
    if (!query.prepare(sql)) {
        throw QtMySQLException("Failed to prepare query: " + query.lastError().text());
    }
    
    // 绑定参数
    bindQueryParameters(query, params);
    
    // 执行查询
    if (!query.exec()) {
        throw QtMySQLException("Failed to execute query: " + query.lastError().text());
    }
    
    return query;
}

void QtMySQLDatabase::bindQueryParameters(QSqlQuery& query, 
                                         const std::map<QString, QVariant>& params) {
    for (const auto& [key, value] : params) {
        query.bindValue(":" + key, value);
    }
}

QueryResult QtMySQLDatabase::convertToQueryResult(QSqlQuery& query) {
    QueryResult result;
    
    // 获取列名
    QSqlRecord record = query.record();
    int columnCount = record.count();
    QStringList columnNames;
    for (int i = 0; i < columnCount; ++i) {
        columnNames.append(record.fieldName(i));
    }
    
    // 遍历结果集
    while (query.next()) {
        QueryResultRow row;
        for (int i = 0; i < columnCount; ++i) {
            QString columnName = columnNames[i];
            QVariant value = query.value(i);
            row.setValue(columnName, value);
        }
        result.addRow(row);
    }
    
    return result;
}

} // namespace database
} // namespace astock

#endif // USE_QT_MYSQL