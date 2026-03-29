#include "database/QtMySQLDatabase.h"

#if USE_QT_MYSQL
#include <QCoreApplication>
#include <QThread>
#include <QElapsedTimer>
#include <QDebug>
#include <iostream>
#include <random>
#include <QUuid>

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
            // 自动提交模式下尝试提交
            if (!db_.commit()) {
                qWarning() << "Failed to auto-commit transaction:" << db_.lastError().text();
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
    : config_(config), useConnectionPool_(useConnectionPool), initialized_(false) {
    
    // 检查Qt是否支持MySQL驱动
    QStringList drivers = QSqlDatabase::drivers();
    
    // 确定要使用的驱动
    QString configuredDriver = QString::fromStdString(config_.driver).toUpper();
    
    if (configuredDriver == "MYSQL" || configuredDriver == "QMYSQL") {
        if (!drivers.contains("QMYSQL")) {
            throw QtMySQLException("QMYSQL driver is not available. Available drivers: " + drivers.join(", "));
        }
        driverType_ = "QMYSQL";
    } else if (configuredDriver == "ODBC" || configuredDriver == "QODBC") {
        if (!drivers.contains("QODBC")) {
            throw QtMySQLException("QODBC driver is not available. Available drivers: " + drivers.join(", "));
        }
        driverType_ = "QODBC";
    } else {
        // 默认尝试QMYSQL
        if (drivers.contains("QMYSQL")) {
            driverType_ = "QMYSQL";
        } else if (drivers.contains("QODBC")) {
            driverType_ = "QODBC";
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
      connections_(std::move(other.connections_)),
      stats_(other.stats_),
      driverType_(std::move(other.driverType_)),
      queryTimeoutMs_(other.queryTimeoutMs_),
      initialized_(other.initialized_.load()) {
    
    other.initialized_ = false;
}

QtMySQLDatabase& QtMySQLDatabase::operator=(QtMySQLDatabase&& other) noexcept {
    if (this != &other) {
        close();
        
        config_ = std::move(other.config_);
        useConnectionPool_ = other.useConnectionPool_;
        connections_ = std::move(other.connections_);
        stats_ = other.stats_;
        driverType_ = std::move(other.driverType_);
        queryTimeoutMs_ = other.queryTimeoutMs_;
        initialized_ = other.initialized_.load();
        
        other.initialized_ = false;
    }
    return *this;
}

bool QtMySQLDatabase::open() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return true;
    }
    
    try {
        if (useConnectionPool_) {
            // 初始化连接池
            for (size_t i = 0; i < config_.pool_size; ++i) {
                QSqlDatabase conn = createConnection();
                if (conn.isOpen()) {
                    connections_.push_back(conn);
                }
            }
            
            if (connections_.empty()) {
                throw QtMySQLException("Failed to initialize connection pool");
            }
        } else {
            // 创建单个连接
            QSqlDatabase conn = createConnection();
            if (!conn.isOpen()) {
                throw QtMySQLException("Failed to open database connection: " + conn.lastError().text());
            }
            connections_.push_back(conn);
        }
        
        initialized_ = true;
        return true;
        
    } catch (const std::exception& e) {
        qCritical() << "Failed to open database:" << e.what();
        return false;
    }
}

void QtMySQLDatabase::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return;
    }
    
    // 先标记为未初始化，防止新连接被创建
    initialized_ = false;
    
    // 第一步：等待所有活跃查询完成
    const int maxWaitAttempts = 10;
    const int waitIntervalMs = 100;
    
    for (int attempt = 0; attempt < maxWaitAttempts; ++attempt) {
        bool allQueriesFinished = true;
        
        for (auto& conn : connections_) {
            if (conn.isOpen() && conn.isValid()) {
                // 检查是否有活跃查询
                QSqlQuery checkQuery(conn);
                if (checkQuery.isActive()) {
                    allQueriesFinished = false;
                    checkQuery.finish(); // 尝试完成查询
                }
                
                // 提交任何未完成的事务
                if (conn.isOpen() && conn.isValid()) {
                    conn.commit();
                }
            }
        }
        
        if (allQueriesFinished) {
            break;
        }
        
        // 等待一段时间再重试
        QThread::msleep(waitIntervalMs);
    }
    
    // 关闭所有连接并记录连接名称
    QStringList connectionNames;
    for (auto& conn : connections_) {
        QString connName = conn.connectionName();
        if (conn.isOpen()) {
            conn.close();
        }
        // 记录连接名称以便稍后移除
        if (!connName.isEmpty()) {
            connectionNames.append(connName);
        }
    }
    connections_.clear();
    
    // 移除所有数据库连接
    // 在连接池中的所有连接都已关闭，可以安全移除
    for (const QString& connName : connectionNames) {
        if (QSqlDatabase::contains(connName)) {
            QSqlDatabase::removeDatabase(connName);
        }
    }
}

bool QtMySQLDatabase::isOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_ || connections_.empty()) {
        return false;
    }
    
    // 检查至少有一个连接是打开的
    for (const auto& conn : connections_) {
        if (conn.isOpen()) {
            return true;
        }
    }
    
    return false;
}

QString QtMySQLDatabase::getLastError() const {
    QSqlError error = getLastSqlError();
    return error.isValid() ? error.text() : QString();
}

QSqlError QtMySQLDatabase::getLastSqlError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& conn : connections_) {
        QSqlError error = conn.lastError();
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
    
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.totalQueries++;
    }
    
    QSqlDatabase conn = getConnection();
    if (!conn.isOpen()) {
        throw QtMySQLException("Database connection is not open");
    }
    
    try {
        // 直接在convertToQueryResult中执行查询，避免查询对象复制问题
        QueryResult result = convertToQueryResult(conn, sql, params);
        
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.totalQueryTimeMs += timer.elapsed();
        
        returnResult(conn);
        return result;
        
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.failedQueries++;
        
        returnResult(conn);
        throw QtMySQLException(QString("Query failed: %1").arg(e.what()));
    }
}

int QtMySQLDatabase::executeUpdate(const QString& sql, 
                                  const std::map<QString, QVariant>& params) {
    QElapsedTimer timer;
    timer.start();
    
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.totalUpdates++;
    }
    
    QSqlDatabase conn = getConnection();
    if (!conn.isOpen()) {
        throw QtMySQLException("Database connection is not open");
    }
    
    try {
        QSqlQuery query = executeQuery(conn, sql, params);
        
        int affectedRows = query.numRowsAffected();
        if (affectedRows == -1) {
            affectedRows = 0;
        }
        
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.totalUpdateTimeMs += timer.elapsed();
        
        returnResult(conn);
        return affectedRows;
        
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.failedUpdates++;
        
        returnResult(conn);
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
    
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.totalUpdates++;
    }
    
    QSqlDatabase conn = getConnection();
    if (!conn.isOpen()) {
        throw QtMySQLException("Database connection is not open");
    }
    
    try {
        if (!conn.transaction()) {
            throw QtMySQLException("Failed to begin transaction: " + conn.lastError().text());
        }
        
        int totalAffectedRows = 0;
        
        for (const auto& params : batchParams) {
            QSqlQuery query = executeQuery(conn, sql, params);
            int affectedRows = query.numRowsAffected();
            if (affectedRows > 0) {
                totalAffectedRows += affectedRows;
            }
        }
        
        if (!conn.commit()) {
            conn.rollback();
            throw QtMySQLException("Failed to commit batch update: " + conn.lastError().text());
        }
        
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.totalUpdateTimeMs += timer.elapsed();
        
        returnResult(conn);
        return totalAffectedRows;
        
    } catch (const std::exception& e) {
        conn.rollback();
        
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.failedUpdates++;
        
        returnResult(conn);
        throw QtMySQLException(QString("Batch update failed: %1").arg(e.what()));
    }
}

QueryResult QtMySQLDatabase::executeProcedure(const QString& procedureName,
                                             const std::map<QString, QVariant>& params,
                                             std::map<QString, QVariant>* outParams) {
    // 构建存储过程调用
    QStringList placeholders;
    for (const auto& [key, value] : params) {
        placeholders.append(":" + key);
    }
    
    QString sql = QString("CALL %1(%2)").arg(procedureName).arg(placeholders.join(", "));
    
    QElapsedTimer timer;
    timer.start();
    
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.totalQueries++;
    }
    
    QSqlDatabase conn = getConnection();
    if (!conn.isOpen()) {
        throw QtMySQLException("Database connection is not open");
    }
    
    try {
        QSqlQuery query = executeQuery(conn, sql, params);
        
        QueryResult result = convertToQueryResult(query);
        
        // 如果有输出参数，尝试获取（MySQL存储过程的OUT参数通过SELECT返回）
        if (outParams && !result.isEmpty()) {
            const auto& row = result.getRow(0);
            for (const auto& [key, value] : row.getValues()) {
                (*outParams)[key] = value;
            }
        }
        
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.totalQueryTimeMs += timer.elapsed();
        
        returnResult(conn);
        return result;
        
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.failedQueries++;
        
        returnResult(conn);
        throw QtMySQLException(QString("Procedure execution failed: %1").arg(e.what()));
    }
}

std::unique_ptr<TransactionGuard> QtMySQLDatabase::beginTransaction(bool autoCommit) {
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.totalTransactions++;
    }
    
    QSqlDatabase conn = getConnection();
    if (!conn.isOpen()) {
        throw QtMySQLException("Database connection is not open");
    }
    
    try {
        auto guard = std::make_unique<TransactionGuard>(conn, autoCommit);
        
        // 如果是连接池模式，需要在事务守卫销毁时归还连接
        if (useConnectionPool_) {
            // 这里简化处理：返回原始守卫，调用者需要手动归还
            // 实际使用时可以封装一个带清理的守卫
            qDebug() << "Transaction started with connection pool mode";
        }
        
        return guard;
        
    } catch (const std::exception& e) {
        returnResult(conn);
        
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.failedTransactions++;
        
        throw QtMySQLException(QString("Failed to begin transaction: %1").arg(e.what()));
    }
}

bool QtMySQLDatabase::commitTransaction() {
    qWarning() << "Direct transaction commit is deprecated. Use TransactionGuard instead.";
    return false;
}

bool QtMySQLDatabase::rollbackTransaction() {
    qWarning() << "Direct transaction rollback is deprecated. Use TransactionGuard instead.";
    return false;
}

bool QtMySQLDatabase::tableExists(const QString& tableName) {
    QString sql = "SELECT COUNT(*) FROM information_schema.tables "
                  "WHERE table_schema = DATABASE() AND table_name = ?";
    
    try {
        QueryResult result = executeQuery(sql, {{"", tableName}});
        return !result.isEmpty() && result.getSingleValue<int>() > 0;
    } catch (const QtMySQLException&) {
        return false;
    }
}

QueryResult QtMySQLDatabase::getTableSchema(const QString& tableName) {
    QString sql = "SELECT column_name, data_type, is_nullable, column_default, column_comment "
                  "FROM information_schema.columns "
                  "WHERE table_schema = DATABASE() AND table_name = ? "
                  "ORDER BY ordinal_position";
    
    return executeQuery(sql, {{"", tableName}});
}

QString QtMySQLDatabase::getDatabaseVersion() {
    try {
        QueryResult result = executeQuery("SELECT VERSION()", {});
        if (!result.isEmpty()) {
            return result.getSingleValue<QString>();
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

QSqlDatabase QtMySQLDatabase::getConnection() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_ || connections_.empty()) {
        throw QtMySQLException("Database not initialized or no connections available");
    }
    
    if (useConnectionPool_) {
        // 从连接池获取连接
        if (connections_.empty()) {
            throw QtMySQLException("Connection pool is empty");
        }
        
        QSqlDatabase conn = connections_.back();
        connections_.pop_back();
        
        // 只检查连接是否打开，不进行ping检查
        // ping检查可能导致"device or resource busy"错误
        if (!conn.isOpen()) {
            // 连接已关闭，创建新连接
            conn = createConnection();
        }
        
        return conn;
    } else {
        // 返回唯一的连接
        return connections_[0];
    }
}

void QtMySQLDatabase::returnResult(QSqlDatabase& conn) {
    if (!useConnectionPool_) {
        return; // 非连接池模式不需要归还
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 直接归还连接到连接池，不进行ping检查
    // 因为连接可能正在被使用，ping会导致"device or resource busy"错误
    if (conn.isOpen()) {
        connections_.push_back(conn);
    } else {
        // 如果连接已关闭，创建新连接
        qDebug() << "QtMySQLDatabase::returnResult: Connection is closed, creating new one";
        conn = createConnection();
        if (conn.isOpen()) {
            connections_.push_back(conn);
        }
    }
}

QSqlDatabase QtMySQLDatabase::createConnection() {
    static std::atomic<int> counter{0};
    QString connName = QString("astock_mysql_%1_%2")
        .arg(reinterpret_cast<quintptr>(this))
        .arg(counter.fetch_add(1));
    
    QSqlDatabase db = QSqlDatabase::addDatabase(QString::fromStdString(driverType_), connName);
    
    if (driverType_ == "QMYSQL") {
        // MySQL驱动配置
        db.setHostName(QString::fromStdString(config_.host));
        db.setPort(config_.port);
        db.setDatabaseName(QString::fromStdString(config_.database));
        db.setUserName(QString::fromStdString(config_.username));
        db.setPassword(QString::fromStdString(config_.password));
        
        // 设置连接选项
        QString connectOptions = QString("MYSQL_OPT_CONNECT_TIMEOUT=%1")
            .arg(config_.connect_timeout.count());
        
        if (config_.read_timeout.count() > 0) {
            connectOptions += QString(";MYSQL_OPT_READ_TIMEOUT=%1")
                .arg(config_.read_timeout.count());
        }
        
        if (config_.write_timeout.count() > 0) {
            connectOptions += QString(";MYSQL_OPT_WRITE_TIMEOUT=%1")
                .arg(config_.write_timeout.count());
        }
        
        db.setConnectOptions(connectOptions);
        
    } else if (driverType_ == "QODBC") {
        // ODBC驱动配置
        QString connStr = QString("DRIVER={MySQL ODBC 8.0 Unicode Driver};"
                                  "SERVER=%1;PORT=%2;DATABASE=%3;"
                                  "UID=%4;PWD=%5;CHARSET=%6;")
            .arg(QString::fromStdString(config_.host))
            .arg(config_.port)
            .arg(QString::fromStdString(config_.database))
            .arg(QString::fromStdString(config_.username))
            .arg(QString::fromStdString(config_.password))
            .arg(QString::fromStdString(config_.charset));
        
        db.setDatabaseName(connStr);
    }
    
    // 应用自定义选项
    if (!connectionOptions_.empty()) {
        QString currentOptions = db.connectOptions();
        for (const auto& [key, value] : connectionOptions_) {
            if (!currentOptions.isEmpty()) {
                currentOptions += ";";
            }
            currentOptions += QString("%1=%2").arg(key).arg(value);
        }
        db.setConnectOptions(currentOptions);
    }
    
    // 打开连接
    if (!db.open()) {
        QString errorMsg = "Failed to open connection: " + db.lastError().text();
        QSqlDatabase::removeDatabase(connName);
        throw QtMySQLException(errorMsg);
    }
    
        // 设置字符集和连接选项
        if (driverType_ == "QMYSQL") {
            // 设置连接选项，包括字符集
            QString connectOptions = QString("MYSQL_OPT_CONNECT_TIMEOUT=%1;MYSQL_OPT_READ_TIMEOUT=%2;MYSQL_OPT_WRITE_TIMEOUT=%3")
                .arg(config_.connect_timeout.count())
                .arg(config_.read_timeout.count())
                .arg(config_.write_timeout.count());
            
            if (!config_.charset.empty()) {
                connectOptions += QString(";MYSQL_SET_CHARSET_NAME=%1").arg(QString::fromStdString(config_.charset));
            }
            
            db.setConnectOptions(connectOptions);
            
            // 执行SET NAMES命令确保字符集和排序规则正确
            if (!config_.charset.empty()) {
                QSqlQuery query(db);
                // 设置字符集和排序规则，确保大小写不敏感
                QString charset = QString::fromStdString(config_.charset);
                QString collation = "utf8mb4_unicode_ci"; // 使用大小写不敏感的排序规则
                
                // 先设置字符集
                if (!query.exec(QString("SET NAMES '%1' COLLATE '%2'").arg(charset).arg(collation))) {
                    qWarning() << "Failed to set charset and collation:" << query.lastError().text();
                    // 如果失败，尝试只设置字符集
                    if (!query.exec(QString("SET NAMES '%1'").arg(charset))) {
                        qWarning() << "Failed to set charset:" << query.lastError().text();
                    } else {
                        qDebug() << "Character set set to:" << charset;
                    }
                } else {
                    qDebug() << "Character set set to:" << charset << "with collation:" << collation;
                }
                
                // 设置数据库的排序规则
                if (!query.exec(QString("SET collation_connection = '%1'").arg(collation))) {
                    qWarning() << "Failed to set collation_connection:" << query.lastError().text();
                }
            }
        }
    
    return db;
}

bool QtMySQLDatabase::pingConnection(QSqlDatabase& conn) {
    QSqlQuery query(conn);
    return query.exec("SELECT 1");
}

QSqlQuery QtMySQLDatabase::executeQuery(QSqlDatabase& conn, 
                                       const QString& sql,
                                       const std::map<QString, QVariant>& params) {
    QSqlQuery query(conn);
    
    qDebug() << "=== QtMySQLDatabase::executeQuery 私有方法开始 ===";
    qDebug() << "SQL:" << sql;
    qDebug() << "参数数量:" << params.size();
    
    // QMYSQL 在某些包含 TEXT/JSON 字段的预处理查询上，forward-only 模式会出现
    // 能拿到列结构但 next() 读不到数据行的问题，这里优先保证正确性。
    query.setForwardOnly(false);
    
    if (!query.prepare(sql)) {
        qDebug() << "查询准备失败:" << query.lastError().text();
        throw QtMySQLException("Failed to prepare query: " + query.lastError().text());
    }
    
    // 绑定参数
    for (const auto& [key, value] : params) {
        if (key.isEmpty()) {
            // 位置绑定
            query.addBindValue(value);
            qDebug() << "位置绑定参数:" << value.toString();
        } else {
            // 命名绑定
            // 注意：如果key已经包含冒号前缀，不要重复添加
            if (key.startsWith(":")) {
                query.bindValue(key, value);
                qDebug() << "命名绑定参数" << key << ":" << value.toString();
            } else {
                query.bindValue(":" + key, value);
                qDebug() << "命名绑定参数 :" << key << ":" << value.toString();
            }
        }
    }
    
    if (!query.exec()) {
        qDebug() << "查询执行失败:" << query.lastError().text();
        throw QtMySQLException("Failed to execute query: " + query.lastError().text());
    }
    
    qDebug() << "查询执行成功";
    qDebug() << "查询是否激活:" << query.isActive();
    qDebug() << "查询是否有效:" << query.isValid();
    qDebug() << "查询是否有错误:" << query.lastError().text();
    qDebug() << "查询类型:" << (query.isSelect() ? "SELECT" : "其他");
    qDebug() << "受影响行数:" << query.numRowsAffected();
    qDebug() << "查询是否只向前:" << query.isForwardOnly();
    qDebug() << "=== QtMySQLDatabase::executeQuery 私有方法结束 ===";
    
    return query;
}

QueryResult QtMySQLDatabase::convertToQueryResult(QSqlQuery& query) {
    QueryResult result;
    
    qDebug() << "=== QtMySQLDatabase::convertToQueryResult 开始 ===";
    
    // 检查查询状态
    qDebug() << "查询状态检查:";
    qDebug() << "  isActive():" << query.isActive();
    qDebug() << "  isValid():" << query.isValid();
    qDebug() << "  isSelect():" << query.isSelect();
    qDebug() << "  isForwardOnly():" << query.isForwardOnly();
    qDebug() << "  lastError():" << query.lastError().text();
    qDebug() << "  numRowsAffected():" << query.numRowsAffected();
    
    if (!query.isActive()) {
        qDebug() << "查询未激活，返回空结果";
        qDebug() << "=== QtMySQLDatabase::convertToQueryResult 结束 ===";
        return result;
    }
    
    // 检查是否是SELECT查询
    if (!query.isSelect()) {
        qDebug() << "查询不是SELECT类型，返回空结果";
        qDebug() << "=== QtMySQLDatabase::convertToQueryResult 结束 ===";
        return result;
    }
    
    // 获取结果集信息
    QSqlRecord record = query.record();
    int columnCount = record.count();
    
    qDebug() << "查询返回" << columnCount << "列";
    
    if (columnCount == 0) {
        qDebug() << "查询返回0列，可能是空结果集或非SELECT查询";
        qDebug() << "=== QtMySQLDatabase::convertToQueryResult 结束 ===";
        return result;
    }
    
    // 获取列名
    QStringList columnNames;
    for (int i = 0; i < columnCount; ++i) {
        QString fieldName = record.fieldName(i);
        columnNames.append(fieldName);
        qDebug() << "列" << i << ":" << fieldName;
    }
    
    int rowCount = 0;
    
    // 尝试获取第一行数据
    if (query.next()) {
        rowCount++;
        
        QueryResultRow row;
        for (int i = 0; i < columnCount; ++i) {
            QString columnName = columnNames[i];
            QVariant value = query.value(i);

            row.setValue(columnName, value);
        }
        result.addRow(row);
        
        // 继续处理剩余的行
        while (query.next()) {
            rowCount++;
            
            QueryResultRow row;
            for (int i = 0; i < columnCount; ++i) {
                QString columnName = columnNames[i];
                QVariant value = query.value(i);

                row.setValue(columnName, value);
            }
            result.addRow(row);
        }
    } else {
        qDebug() << "查询没有返回任何数据行";
    }
    
    qDebug() << "总共处理了" << rowCount << "行数据";
    qDebug() << "=== QtMySQLDatabase::convertToQueryResult 结束 ===";
    
    return result;
}

QueryResult QtMySQLDatabase::convertToQueryResult(QSqlDatabase& connection,
                                                 const QString& sql,
                                                 const std::map<QString, QVariant>& params) {
    QueryResult result;
    
    qDebug() << "=== QtMySQLDatabase::convertToQueryResult(connection) 开始 ===";
    qDebug() << "SQL:" << sql;
    qDebug() << "参数数量:" << params.size();
    
    // 创建查询对象
    QSqlQuery query(connection);
    
    // QMYSQL 在某些包含 TEXT/JSON 字段的预处理查询上，forward-only 模式会出现
    // 能拿到列结构但 next() 读不到数据行的问题，这里优先保证正确性。
    query.setForwardOnly(false);
    
    if (!query.prepare(sql)) {
        qDebug() << "查询准备失败:" << query.lastError().text();
        throw QtMySQLException("Failed to prepare query: " + query.lastError().text());
    }
    
    // 绑定参数
    for (const auto& [key, value] : params) {
        if (key.isEmpty()) {
            // 位置绑定
            query.addBindValue(value);
            qDebug() << "位置绑定参数:" << value.toString();
        } else {
            // 命名绑定
            // 注意：如果key已经包含冒号前缀，不要重复添加
            if (key.startsWith(":")) {
                query.bindValue(key, value);
                qDebug() << "命名绑定参数" << key << ":" << value.toString();
            } else {
                query.bindValue(":" + key, value);
                qDebug() << "命名绑定参数 :" << key << ":" << value.toString();
            }
        }
    }
    
    if (!query.exec()) {
        qDebug() << "查询执行失败:" << query.lastError().text();
        throw QtMySQLException("Failed to execute query: " + query.lastError().text());
    }
    
    qDebug() << "查询执行成功";
    qDebug() << "查询是否激活:" << query.isActive();
    qDebug() << "查询是否有效:" << query.isValid();
    qDebug() << "查询是否有错误:" << query.lastError().text();
    qDebug() << "查询类型:" << (query.isSelect() ? "SELECT" : "其他");
    qDebug() << "受影响行数:" << query.numRowsAffected();
    qDebug() << "查询是否只向前:" << query.isForwardOnly();
    
    // 检查是否是SELECT查询
    if (!query.isSelect()) {
        qDebug() << "查询不是SELECT类型，返回空结果";
        qDebug() << "=== QtMySQLDatabase::convertToQueryResult(connection) 结束 ===";
        return result;
    }
    
    // 获取结果集信息
    QSqlRecord record = query.record();
    int columnCount = record.count();
    
    qDebug() << "查询返回" << columnCount << "列";
    
    if (columnCount == 0) {
        qDebug() << "查询返回0列，可能是空结果集或非SELECT查询";
        qDebug() << "=== QtMySQLDatabase::convertToQueryResult(connection) 结束 ===";
        return result;
    }
    
    // 获取列名
    QStringList columnNames;
    for (int i = 0; i < columnCount; ++i) {
        QString fieldName = record.fieldName(i);
        columnNames.append(fieldName);
        qDebug() << "列" << i << ":" << fieldName;
    }
    
    // 处理结果集
    int rowCount = 0;
    
    // 尝试获取第一行数据
    if (query.next()) {
        rowCount++;
        
        QueryResultRow row;
        for (int i = 0; i < columnCount; ++i) {
            QString columnName = columnNames[i];
            QVariant value = query.value(i);

            row.setValue(columnName, value);
        }
        result.addRow(row);
        
        // 继续处理剩余的行
        while (query.next()) {
            rowCount++;
            
            QueryResultRow row;
            for (int i = 0; i < columnCount; ++i) {
                QString columnName = columnNames[i];
                QVariant value = query.value(i);

                row.setValue(columnName, value);
            }
            result.addRow(row);
        }
    } else {
        qDebug() << "查询没有返回任何数据行";
    }
    
    qDebug() << "总共处理了" << rowCount << "行数据";
    qDebug() << "=== QtMySQLDatabase::convertToQueryResult(connection) 结束 ===";
    
    return result;
}

} // namespace database
} // namespace astock

#endif // USE_QT_MYSQL