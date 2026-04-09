#include "database/StrategyRepository.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QVariant>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QDebug>
#include <QRandomGenerator>
#include <algorithm>
#include <ctime>
#include <random>
#include <sstream>
#include <iomanip>

namespace astock {
namespace database {

namespace {

QString normalizePersistedStatus(const QString& rawStatus)
{
    const QStringList validStatuses = {"ACTIVE", "INACTIVE", "TESTING", "ARCHIVED"};
    if (validStatuses.contains(rawStatus)) {
        return rawStatus;
    }
    return "ACTIVE";
}

QString normalizePersistedLanguage(const QString& rawLanguage)
{
    const QString normalized = rawLanguage.trimmed().toUpper();
    if (normalized == "CPP" || normalized == "C++") {
        return "CPP";
    }
    if (normalized == "JULIA") {
        return "JULIA";
    }
    if (normalized == "R") {
        return "R";
    }
    if (normalized == "PYTHON" || normalized == "PY" || normalized == "QML" || normalized == "JS" || normalized == "JAVASCRIPT") {
        return "PYTHON";
    }
    return "PYTHON";
}

QVariantMap buildPersistedParameters(const QVariantMap& strategy)
{
    QVariantMap parameters = strategy.value("parameters").toMap();

    const QStringList passthroughKeys = {
        "asset_type",
        "time_frame",
        "risk_level",
        "optimization_method",
        "symbol_pool",
        "backtest_settings",
        "advanced_options",
        "performance_metrics",
        "tags"
    };

    for (const QString& key : passthroughKeys) {
        if (!strategy.contains(key)) {
            continue;
        }

        const QVariant value = strategy.value(key);
        if (!value.isValid() || value.isNull()) {
            continue;
        }

        parameters.insert(key, value);
    }

    return parameters;
}

void restoreStrategyExtrasFromParameters(QVariantMap& strategy)
{
    const QVariantMap parameters = strategy.value("parameters").toMap();
    if (parameters.isEmpty()) {
        return;
    }

    const QStringList passthroughKeys = {
        "asset_type",
        "time_frame",
        "risk_level",
        "optimization_method",
        "symbol_pool",
        "backtest_settings",
        "advanced_options",
        "performance_metrics",
        "tags"
    };

    for (const QString& key : passthroughKeys) {
        if (parameters.contains(key)) {
            strategy.insert(key, parameters.value(key));
        }
    }
}

}

StrategyRepository::StrategyRepository() 
    : m_initialized(false) {
}

StrategyRepository::~StrategyRepository() {
}

QVariantMap StrategyRepository::findById(const QString& strategyId) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return QVariantMap();
    }
    
    QSqlQuery query(conn.get());
    query.prepare("SELECT strategy_id, strategy_code, strategy_name, strategy_type, "
                  "description, version, author, language, status, "
                  "created_at, updated_at "
                  "FROM strategy WHERE strategy_id = ?");
    query.addBindValue(strategyId);
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to find strategy by id:" << query.lastError().text();
        return QVariantMap();
    }
    
    if (query.next()) {
        QVariantMap strategy = rowToStrategyMap(query);
        
        // 加载参数
        QVariantMap parameters = loadStrategyParameters(strategyId, conn.get());
        if (!parameters.isEmpty()) {
            strategy["parameters"] = parameters;
            restoreStrategyExtrasFromParameters(strategy);
        }
        
        return strategy;
    }
    
    return QVariantMap();
}

QVariantMap StrategyRepository::findByCode(const QString& strategyCode) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return QVariantMap();
    }
    
    QSqlQuery query(conn.get());
    query.prepare("SELECT strategy_id, strategy_code, strategy_name, strategy_type, "
                  "description, version, author, language, status, "
                  "created_at, updated_at "
                  "FROM strategy WHERE strategy_code = ?");
    query.addBindValue(strategyCode);
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to find strategy by code:" << query.lastError().text();
        return QVariantMap();
    }
    
    if (query.next()) {
        QString strategyId = query.value("strategy_id").toString();
        QVariantMap strategy = rowToStrategyMap(query);
        
        // 加载参数
        QVariantMap parameters = loadStrategyParameters(strategyId, conn.get());
        if (!parameters.isEmpty()) {
            strategy["parameters"] = parameters;
            restoreStrategyExtrasFromParameters(strategy);
        }
        
        return strategy;
    }
    
    return QVariantMap();
}

std::vector<QVariantMap> StrategyRepository::findAll() {
    qDebug() << "[StrategyRepository::findAll] 开始查询所有策略";
    
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository::findAll] Failed to get database connection";
        return {};
    }
    
    qDebug() << "[StrategyRepository::findAll] 数据库连接成功";
    
    QSqlQuery query(conn.get());
    // 不使用SELECT *，而是明确列出所有字段，避免JSON字段问题
    QString sql = "SELECT "
                  "strategy_id, strategy_code, strategy_name, strategy_type, "
                  "description, version, author, language, status, "
                  "created_at, updated_at, parameters "
                  "FROM strategy ORDER BY created_at DESC";
    query.prepare(sql);
    
    qDebug() << "[StrategyRepository::findAll] 准备执行SQL:" << sql;
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository::findAll] Failed to find all strategies:" << query.lastError().text();
        qWarning() << "[StrategyRepository::findAll] SQL error:" << query.lastError().databaseText();
        return {};
    }
    
    qDebug() << "[StrategyRepository::findAll] SQL执行成功";
    
    // 立即检查是否有结果
    if (query.lastError().isValid()) {
        qWarning() << "[StrategyRepository::findAll] 查询执行后有错误:" << query.lastError().text();
    }
    
    // 检查结果集是否有效
    if (!query.isActive()) {
        qWarning() << "[StrategyRepository::findAll] 查询不活动";
    }
    
    if (!query.isSelect()) {
        qWarning() << "[StrategyRepository::findAll] 查询不是SELECT类型";
    }
    
    int rowCount = 0;
    int processedCount = 0;
    std::vector<QVariantMap> strategies;
    
    // 先检查查询是否有结果
    if (query.isActive() && query.isSelect()) {
        qDebug() << "[StrategyRepository::findAll] 查询是活动的SELECT查询";
    } else {
        qWarning() << "[StrategyRepository::findAll] 查询不活动或不是SELECT查询";
    }
    
    // 调试信息：显示查询结果状态
    bool hasRows = query.size() > 0;
    qDebug() << "[StrategyRepository::findAll] 查询结果集大小:" << query.size();
    
    // 获取记录信息
    QSqlRecord record = query.record();
    int fieldCount = record.count();
    qDebug() << "[StrategyRepository::findAll] 记录字段数量:" << fieldCount;
    for (int i = 0; i < fieldCount; i++) {
        QString fieldName = record.fieldName(i);
        qDebug() << "[StrategyRepository::findAll] 字段" << i << ":" << fieldName;
    }
    
    if (hasRows) {
        qDebug() << "[StrategyRepository::findAll] 查询有结果集，开始处理";
    } else {
        qDebug() << "[StrategyRepository::findAll] 查询结果集大小为0或未知，尝试遍历";
    }
    
    // 尝试逐步诊断：先尝试一个没有JSON字段的简单查询
    qDebug() << "[StrategyRepository::findAll] === 第一步：尝试没有JSON字段的查询 ===";
    QSqlQuery simpleQueryNoJson(conn.get());
    if (simpleQueryNoJson.exec("SELECT strategy_id, strategy_name FROM strategy ORDER BY created_at DESC")) {
        int simpleCount = 0;
        while (simpleQueryNoJson.next()) {
            simpleCount++;
            QString id = simpleQueryNoJson.value(0).toString();
            QString name = simpleQueryNoJson.value(1).toString();
            qDebug() << "[StrategyRepository::findAll] 简单查询(无JSON)找到策略" << simpleCount << "ID:" << id << "名称:" << name;
        }
        qDebug() << "[StrategyRepository::findAll] 简单查询(无JSON)找到" << simpleCount << "个策略";
    } else {
        qWarning() << "[StrategyRepository::findAll] 简单查询(无JSON)失败:" << simpleQueryNoJson.lastError().text();
    }
    
    // 第二步：尝试没有parameters字段的查询
    qDebug() << "[StrategyRepository::findAll] === 第二步：尝试没有parameters字段的查询 ===";
    QSqlQuery queryWithoutParams(conn.get());
    QString sqlWithoutParams = "SELECT "
                               "strategy_id, strategy_code, strategy_name, strategy_type, "
                               "description, version, author, language, status, "
                               "created_at, updated_at "
                               "FROM strategy ORDER BY created_at DESC";
    
    if (queryWithoutParams.exec(sqlWithoutParams)) {
        int countWithoutParams = 0;
        while (queryWithoutParams.next()) {
            countWithoutParams++;
            QString id = queryWithoutParams.value("strategy_id").toString();
            qDebug() << "[StrategyRepository::findAll] 无parameters查询找到策略" << countWithoutParams << "ID:" << id;
        }
        qDebug() << "[StrategyRepository::findAll] 无parameters查询找到" << countWithoutParams << "个策略";
    } else {
        qWarning() << "[StrategyRepository::findAll] 无parameters查询失败:" << queryWithoutParams.lastError().text();
    }
    
    // 第三步：尝试将parameters字段转换为文本
    qDebug() << "[StrategyRepository::findAll] === 第三步：尝试将parameters字段转换为文本 ===";
    QSqlQuery queryWithTextParams(conn.get());
    QString sqlWithTextParams = "SELECT "
                                "strategy_id, strategy_code, strategy_name, strategy_type, "
                                "description, version, author, language, status, "
                                "created_at, updated_at, "
                                "CAST(parameters AS CHAR) as parameters_text "
                                "FROM strategy ORDER BY created_at DESC";
    
    if (queryWithTextParams.exec(sqlWithTextParams)) {
        int countWithTextParams = 0;
        while (queryWithTextParams.next()) {
            countWithTextParams++;
            QString id = queryWithTextParams.value("strategy_id").toString();
            QString paramsText = queryWithTextParams.value("parameters_text").toString();
            qDebug() << "[StrategyRepository::findAll] 文本parameters查询找到策略" << countWithTextParams << "ID:" << id << "参数长度:" << paramsText.length();
        }
        qDebug() << "[StrategyRepository::findAll] 文本parameters查询找到" << countWithTextParams << "个策略";
        
        // 如果这个查询成功，使用这个结果
        if (countWithTextParams > 0) {
            qDebug() << "[StrategyRepository::findAll] 使用文本参数查询结果";
            // 重新执行查询并处理
            queryWithTextParams.finish();
            if (queryWithTextParams.exec(sqlWithTextParams)) {
                while (queryWithTextParams.next()) {
                    QVariantMap strategy;
                    
                    // 获取所有字段
                    strategy["strategy_id"] = queryWithTextParams.value("strategy_id");
                    strategy["strategy_code"] = queryWithTextParams.value("strategy_code");
                    strategy["strategy_name"] = queryWithTextParams.value("strategy_name");
                    strategy["strategy_type"] = queryWithTextParams.value("strategy_type");
                    strategy["description"] = queryWithTextParams.value("description");
                    strategy["version"] = queryWithTextParams.value("version");
                    strategy["author"] = queryWithTextParams.value("author");
                    strategy["language"] = queryWithTextParams.value("language");
                    strategy["status"] = queryWithTextParams.value("status");
                    strategy["created_at"] = queryWithTextParams.value("created_at");
                    strategy["updated_at"] = queryWithTextParams.value("updated_at");
                    
                    // 解析parameters字段
                    QString parametersJson = queryWithTextParams.value("parameters_text").toString();
                    if (!parametersJson.isEmpty() && parametersJson != "{}") {
                        QJsonParseError parseError;
                        QJsonDocument doc = QJsonDocument::fromJson(parametersJson.toUtf8(), &parseError);
                        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                            strategy["parameters"] = doc.object().toVariantMap();
                            restoreStrategyExtrasFromParameters(strategy);
                        } else {
                            qWarning() << "[StrategyRepository::findAll] 无法解析转换后的JSON参数:" << parseError.errorString();
                            strategy["parameters"] = QVariantMap();
                        }
                    } else {
                        strategy["parameters"] = QVariantMap();
                    }
                    
                    strategies.push_back(strategy);
                    processedCount++;
                }
            }
        }
    } else {
        qWarning() << "[StrategyRepository::findAll] 文本parameters查询失败:" << queryWithTextParams.lastError().text();
    }
    
    // 如果前面的查询都没有结果，尝试不使用JSON字段的查询
    if (processedCount == 0) {
        qDebug() << "[StrategyRepository::findAll] === 使用两步查询法 ===";
        // 第一步：查询所有非JSON字段
        QSqlQuery baseQuery(conn.get());
        QString baseSql = "SELECT "
                         "strategy_id, strategy_code, strategy_name, strategy_type, "
                         "description, version, author, language, status, "
                         "created_at, updated_at "
                         "FROM strategy ORDER BY created_at DESC";
        
        if (baseQuery.exec(baseSql)) {
            while (baseQuery.next()) {
                QVariantMap strategy;
                
                // 获取所有基础字段
                strategy["strategy_id"] = baseQuery.value("strategy_id");
                strategy["strategy_code"] = baseQuery.value("strategy_code");
                strategy["strategy_name"] = baseQuery.value("strategy_name");
                strategy["strategy_type"] = baseQuery.value("strategy_type");
                strategy["description"] = baseQuery.value("description");
                strategy["version"] = baseQuery.value("version");
                strategy["author"] = baseQuery.value("author");
                strategy["language"] = baseQuery.value("language");
                strategy["status"] = baseQuery.value("status");
                strategy["created_at"] = baseQuery.value("created_at");
                strategy["updated_at"] = baseQuery.value("updated_at");
                
                // 第二步：单独查询JSON字段
                QString strategyId = baseQuery.value("strategy_id").toString();
                QSqlQuery paramQuery(conn.get());
                paramQuery.prepare("SELECT parameters FROM strategy WHERE strategy_id = ?");
                paramQuery.addBindValue(strategyId);
                
                if (paramQuery.exec() && paramQuery.next()) {
                    QString parametersJson = paramQuery.value("parameters").toString();
                    if (!parametersJson.isEmpty() && parametersJson != "{}") {
                        QJsonParseError parseError;
                        QJsonDocument doc = QJsonDocument::fromJson(parametersJson.toUtf8(), &parseError);
                        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                            strategy["parameters"] = doc.object().toVariantMap();
                            restoreStrategyExtrasFromParameters(strategy);
                        } else {
                            qWarning() << "[StrategyRepository::findAll] 无法解析单独查询的JSON参数:" << parseError.errorString();
                            strategy["parameters"] = QVariantMap();
                        }
                    } else {
                        strategy["parameters"] = QVariantMap();
                    }
                } else {
                    strategy["parameters"] = QVariantMap();
                }
                
                strategies.push_back(strategy);
                processedCount++;
            }
            rowCount = processedCount;
        }
    }
    
    qDebug() << "[StrategyRepository::findAll] 查询完成，成功处理" << processedCount << "个策略";
    
    // 输出策略数量验证
    if (processedCount == 0) {
        qWarning() << "[StrategyRepository::findAll] 警告：成功处理0条记录，但数据库中有策略数据";
        // 再次验证查询结果
        QSqlQuery countQuery = QSqlQuery(conn.get());
        if (countQuery.exec("SELECT COUNT(*) FROM strategy") && countQuery.next()) {
            int dbCount = countQuery.value(0).toInt();
            qWarning() << "[StrategyRepository::findAll] 数据库实际策略数量:" << dbCount;
        }
    }
    
    return strategies;
}

std::vector<QVariantMap> StrategyRepository::findByType(const QString& strategyType) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return {};
    }
    
    QSqlQuery query(conn.get());
    query.prepare("SELECT * FROM strategy WHERE strategy_type = ? ORDER BY created_at DESC");
    query.addBindValue(strategyType);
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to find strategies by type:" << query.lastError().text();
        return {};
    }
    
    std::vector<QVariantMap> strategies;
    while (query.next()) {
        QString strategyId = query.value("strategy_id").toString();
        QVariantMap strategy = rowToStrategyMap(query);
        
        // 加载参数
        QVariantMap parameters = loadStrategyParameters(strategyId, conn.get());
        if (!parameters.isEmpty()) {
            strategy["parameters"] = parameters;
            restoreStrategyExtrasFromParameters(strategy);
        }
        
        strategies.push_back(strategy);
    }
    
    return strategies;
}

std::vector<QVariantMap> StrategyRepository::findByStatus(const QString& status) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return {};
    }
    
    QSqlQuery query(conn.get());
    query.prepare("SELECT * FROM strategy WHERE status = ? ORDER BY created_at DESC");
    query.addBindValue(status);
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to find strategies by status:" << query.lastError().text();
        return {};
    }
    
    std::vector<QVariantMap> strategies;
    while (query.next()) {
        QString strategyId = query.value("strategy_id").toString();
        QVariantMap strategy = rowToStrategyMap(query);
        
        // 加载参数
        QVariantMap parameters = loadStrategyParameters(strategyId, conn.get());
        if (!parameters.isEmpty()) {
            strategy["parameters"] = parameters;
            restoreStrategyExtrasFromParameters(strategy);
        }
        
        strategies.push_back(strategy);
    }
    
    return strategies;
}

std::vector<QVariantMap> StrategyRepository::search(const QString& keyword) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return {};
    }
    
    QSqlQuery query(conn.get());
    QString searchPattern = "%" + keyword + "%";
    query.prepare("SELECT * FROM strategy WHERE "
                  "strategy_name LIKE ? OR "
                  "strategy_code LIKE ? OR "
                  "description LIKE ? "
                  "ORDER BY created_at DESC");
    query.addBindValue(searchPattern);
    query.addBindValue(searchPattern);
    query.addBindValue(searchPattern);
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to search strategies:" << query.lastError().text();
        return {};
    }
    
    std::vector<QVariantMap> strategies;
    while (query.next()) {
        QString strategyId = query.value("strategy_id").toString();
        QVariantMap strategy = rowToStrategyMap(query);
        
        // 加载参数
        QVariantMap parameters = loadStrategyParameters(strategyId, conn.get());
        if (!parameters.isEmpty()) {
            strategy["parameters"] = parameters;
            restoreStrategyExtrasFromParameters(strategy);
        }
        
        strategies.push_back(strategy);
    }
    
    return strategies;
}

QString StrategyRepository::save(const QVariantMap& strategy) {
    if (!validateStrategy(strategy)) {
        qWarning() << "[StrategyRepository] Invalid strategy data";
        return QString();
    }
    
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return QString();
    }
    
    QSqlDatabase& db = conn.get();
    
    // 开始事务
    if (!db.transaction()) {
        qWarning() << "[StrategyRepository] Failed to start transaction";
        return QString();
    }
    
    QString strategyId = saveStrategyInternal(strategy, db, false);
    bool success = !strategyId.isEmpty();
    
    if (success) {
        if (!db.commit()) {
            qWarning() << "[StrategyRepository] Failed to commit transaction";
            strategyId.clear();
            db.rollback();
        }
    } else {
        db.rollback();
    }
    
    return strategyId;
}

bool StrategyRepository::update(const QString& strategyId, const QVariantMap& strategy) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return false;
    }
    
    QSqlDatabase& db = conn.get();
    
    // 检查策略是否存在
    QSqlQuery checkQuery(db);
    checkQuery.prepare("SELECT COUNT(*) FROM strategy WHERE strategy_id = ?");
    checkQuery.addBindValue(strategyId);
    
    if (!checkQuery.exec() || !checkQuery.next() || checkQuery.value(0).toInt() == 0) {
        qWarning() << "[StrategyRepository] Strategy not found for update:" << strategyId;
        return false;
    }
    
    // 开始事务
    if (!db.transaction()) {
        qWarning() << "[StrategyRepository] Failed to start transaction";
        return false;
    }
    
    QSqlQuery existingQuery(db);
    existingQuery.prepare("SELECT strategy_id, strategy_code, strategy_name, strategy_type, "
                          "description, version, author, language, status, "
                          "created_at, updated_at "
                          "FROM strategy WHERE strategy_id = ?");
    existingQuery.addBindValue(strategyId);

    if (!existingQuery.exec() || !existingQuery.next()) {
        qWarning() << "[StrategyRepository] Failed to load existing strategy for update:" << strategyId
                   << existingQuery.lastError().text();
        return false;
    }

    QVariantMap updatedStrategy = rowToStrategyMap(existingQuery);
    const QVariantMap existingParameters = loadStrategyParameters(strategyId, db);
    if (!existingParameters.isEmpty()) {
        updatedStrategy["parameters"] = existingParameters;
        restoreStrategyExtrasFromParameters(updatedStrategy);
    }

    for (auto it = strategy.begin(); it != strategy.end(); ++it) {
        updatedStrategy[it.key()] = it.value();
    }
    updatedStrategy["strategy_id"] = strategyId;
    
    QString resultId = saveStrategyInternal(updatedStrategy, db, true);
    bool success = !resultId.isEmpty();
    
    if (success) {
        if (!db.commit()) {
            qWarning() << "[StrategyRepository] Failed to commit transaction";
            success = false;
            db.rollback();
        }
    } else {
        db.rollback();
    }
    
    return success;
}

bool StrategyRepository::remove(const QString& strategyId) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return false;
    }
    
    QSqlDatabase& db = conn.get();
    
    // 开始事务
    if (!db.transaction()) {
        qWarning() << "[StrategyRepository] Failed to start transaction";
        return false;
    }
    
    // 删除参数
    if (!deleteStrategyParameters(strategyId, db)) {
        qWarning() << "[StrategyRepository] Failed to delete strategy parameters";
        db.rollback();
        return false;
    }
    
    // 删除策略
    QSqlQuery query(db);
    query.prepare("DELETE FROM strategy WHERE strategy_id = ?");
    query.addBindValue(strategyId);
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to delete strategy:" << query.lastError().text();
        db.rollback();
        return false;
    }
    
    if (query.numRowsAffected() == 0) {
        qWarning() << "[StrategyRepository] Strategy not found for deletion:" << strategyId;
        db.rollback();
        return false;
    }
    
    if (!db.commit()) {
        qWarning() << "[StrategyRepository] Failed to commit transaction";
        db.rollback();
        return false;
    }
    
    return true;
}

size_t StrategyRepository::count() {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return 0;
    }
    
    QSqlQuery query(conn.get());
    query.prepare("SELECT COUNT(*) FROM strategy");
    
    if (!query.exec() || !query.next()) {
        qWarning() << "[StrategyRepository] Failed to count strategies:" << query.lastError().text();
        return 0;
    }
    
    return query.value(0).toUInt();
}

bool StrategyRepository::exists(const QString& strategyId) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return false;
    }
    
    QSqlQuery query(conn.get());
    query.prepare("SELECT COUNT(*) FROM strategy WHERE strategy_id = ?");
    query.addBindValue(strategyId);
    
    if (!query.exec() || !query.next()) {
        qWarning() << "[StrategyRepository] Failed to check strategy existence:" << query.lastError().text();
        return false;
    }
    
    return query.value(0).toInt() > 0;
}

bool StrategyRepository::existsByCode(const QString& strategyCode) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return false;
    }
    
    QSqlQuery query(conn.get());
    query.prepare("SELECT COUNT(*) FROM strategy WHERE strategy_code = ?");
    query.addBindValue(strategyCode);
    
    if (!query.exec() || !query.next()) {
        qWarning() << "[StrategyRepository] Failed to check strategy existence by code:" << query.lastError().text();
        return false;
    }
    
    return query.value(0).toInt() > 0;
}

bool StrategyRepository::initialize() {
    QMutexLocker locker(&m_initMutex);
    
    if (m_initialized) {
        return true;
    }
    
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection for initialization";
        return false;
    }
    
    // 检查表是否存在，如果不存在则创建
    QSqlQuery checkTableQuery(conn.get());
    checkTableQuery.prepare("SHOW TABLES LIKE 'strategy'");
    
    if (!checkTableQuery.exec()) {
        qWarning() << "[StrategyRepository] Failed to check table existence:" << checkTableQuery.lastError().text();
        return false;
    }
    
    bool tableExists = checkTableQuery.next();
    
    if (!tableExists) {
        qInfo() << "[StrategyRepository] Strategy table does not exist, it will be created by database initialization script";
        // 表创建由数据库初始化脚本处理，这里不创建表
    }
    
    m_initialized = true;
    return true;
}

bool StrategyRepository::clearAll() {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return false;
    }
    
    QSqlDatabase& db = conn.get();
    
    // 开始事务
    if (!db.transaction()) {
        qWarning() << "[StrategyRepository] Failed to start transaction";
        return false;
    }
    
    // 清空参数表
    QSqlQuery clearParamsQuery(db);
    if (!clearParamsQuery.exec("DELETE FROM backtest_config WHERE config_id IN (SELECT config_id FROM backtest_config)")) {
        qWarning() << "[StrategyRepository] Failed to clear backtest config:" << clearParamsQuery.lastError().text();
        db.rollback();
        return false;
    }
    
    // 清空策略表
    QSqlQuery clearStrategyQuery(db);
    if (!clearStrategyQuery.exec("DELETE FROM strategy")) {
        qWarning() << "[StrategyRepository] Failed to clear strategy table:" << clearStrategyQuery.lastError().text();
        db.rollback();
        return false;
    }
    
    if (!db.commit()) {
        qWarning() << "[StrategyRepository] Failed to commit transaction";
        db.rollback();
        return false;
    }
    
    return true;
}

bool StrategyRepository::updateStatus(const QString& strategyId, const QString& status) {
    ScopedConnection conn;
    if (!conn.isValid()) {
        qWarning() << "[StrategyRepository] Failed to get database connection";
        return false;
    }
    
    QSqlQuery query(conn.get());
    query.prepare("UPDATE strategy SET status = ?, updated_at = NOW() WHERE strategy_id = ?");
    query.addBindValue(status);
    query.addBindValue(strategyId);
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to update strategy status:" << query.lastError().text();
        return false;
    }
    
    return query.numRowsAffected() > 0;
}

bool StrategyRepository::updateParameters(const QString& strategyId, const QVariantMap& parameters) {
    return update(strategyId, QVariantMap{{"parameters", parameters}});
}

bool StrategyRepository::updatePerformance(const QString& strategyId, const QVariantMap& performance) {
    return update(strategyId, QVariantMap{{"performance_metrics", performance}});
}

std::vector<QVariantMap> StrategyRepository::findActiveStrategies() {
    return findByStatus("ACTIVE");
}

std::vector<QVariantMap> StrategyRepository::findDraftStrategies() {
    return findByStatus("DRAFT");
}

QVariantMap StrategyRepository::rowToStrategyMap(const QSqlQuery& query) {
    QVariantMap strategy;
    
    strategy["strategy_id"] = query.value("strategy_id");
    strategy["strategy_code"] = query.value("strategy_code");
    strategy["strategy_name"] = query.value("strategy_name");
    strategy["strategy_type"] = query.value("strategy_type");
    strategy["description"] = query.value("description");
    strategy["version"] = query.value("version");
    strategy["author"] = query.value("author");
    strategy["language"] = query.value("language");
    strategy["status"] = query.value("status");
    strategy["created_at"] = query.value("created_at");
    strategy["updated_at"] = query.value("updated_at");
    
    // 解析参数JSON
    int parametersColumn = query.record().indexOf("parameters");
    if (parametersColumn >= 0) {
        QString parametersJson = query.value(parametersColumn).toString();
        if (!parametersJson.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(parametersJson.toUtf8());
            if (doc.isObject()) {
                strategy["parameters"] = doc.object().toVariantMap();
                restoreStrategyExtrasFromParameters(strategy);
            }
        }
    }
    
    return strategy;
}

QVariantMap StrategyRepository::loadStrategyParameters(const QString& strategyId, QSqlDatabase& db) {
    // 策略参数存储在strategy表的parameters字段中（JSON格式）
    // 这个方法用于从JSON字段解析参数
    
    QSqlQuery query(db);
    query.prepare("SELECT parameters FROM strategy WHERE strategy_id = ?");
    query.addBindValue(strategyId);
    
    if (!query.exec() || !query.next()) {
        return QVariantMap();
    }
    
    QString parametersJson = query.value("parameters").toString();
    if (parametersJson.isEmpty()) {
        return QVariantMap();
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(parametersJson.toUtf8());
    if (!doc.isObject()) {
        return QVariantMap();
    }
    
    return doc.object().toVariantMap();
}

bool StrategyRepository::saveStrategyParameters(const QString& strategyId, const QVariantMap& parameters, QSqlDatabase& db) {
    // 将参数转换为JSON字符串
    QJsonObject jsonObj = QJsonObject::fromVariantMap(parameters);
    QJsonDocument doc(jsonObj);
    QString parametersJson = doc.toJson(QJsonDocument::Compact);
    
    QSqlQuery query(db);
    query.prepare("UPDATE strategy SET parameters = ? WHERE strategy_id = ?");
    query.addBindValue(parametersJson);
    query.addBindValue(strategyId);
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to save strategy parameters:" << query.lastError().text();
        return false;
    }
    
    return true;
}

bool StrategyRepository::deleteStrategyParameters(const QString& strategyId, QSqlDatabase& db) {
    // 参数存储在strategy表的JSON字段中，更新为空JSON即可
    QSqlQuery query(db);
    query.prepare("UPDATE strategy SET parameters = '{}' WHERE strategy_id = ?");
    query.addBindValue(strategyId);
    
    if (!query.exec()) {
        qWarning() << "[StrategyRepository] Failed to delete strategy parameters:" << query.lastError().text();
        return false;
    }
    
    return true;
}

QString StrategyRepository::saveStrategyInternal(const QVariantMap& strategy, QSqlDatabase& db, bool isUpdate) {
    QString strategyId = strategy.value("strategy_id").toString();
    QString strategyCode = strategy.value("strategy_code").toString();
    QString strategyName = strategy.value("strategy_name").toString();
    QString strategyType = strategy.value("strategy_type").toString();
    QString description = strategy.value("description").toString();
    QString version = strategy.value("version").toString();
    QString author = strategy.value("author").toString();
    QString language = normalizePersistedLanguage(strategy.value("language").toString());
    QString status = normalizePersistedStatus(strategy.value("status").toString());
    
    // 验证必填字段
    if (strategyName.isEmpty() || strategyType.isEmpty()) {
        qWarning() << "[StrategyRepository] Strategy name and type are required";
        return QString();
    }
    
    // 生成策略代码（如果未提供）
    if (strategyCode.isEmpty() && !isUpdate) {
        strategyCode = generateStrategyCode(strategy);
    }
    
    QSqlQuery query(db);
    
    if (isUpdate) {
        query.prepare("UPDATE strategy SET "
                      "strategy_code = ?, "
                      "strategy_name = ?, "
                      "strategy_type = ?, "
                      "description = ?, "
                      "version = ?, "
                      "author = ?, "
                      "language = ?, "
                      "status = ?, "
                      "updated_at = NOW() "
                      "WHERE strategy_id = ?");
        
        query.addBindValue(strategyCode);
        query.addBindValue(strategyName);
        query.addBindValue(strategyType);
        query.addBindValue(description);
        query.addBindValue(version);
        query.addBindValue(author);
        query.addBindValue(language);
        query.addBindValue(status);
        query.addBindValue(strategyId);
    } else {
        // 检查策略代码是否已存在
        if (existsByCode(strategyCode)) {
            const QString baseCode = strategyCode;
            bool resolved = false;

            for (int attempt = 0; attempt < 5; ++attempt) {
                const QString suffix = QString("_%1%2")
                    .arg(QDateTime::currentDateTimeUtc().toString("sszzz"))
                    .arg(QRandomGenerator::global()->bounded(1000, 10000));
                const int maxBaseLength = 100 - suffix.size();
                const QString candidateCode = baseCode.left(maxBaseLength) + suffix;
                if (!existsByCode(candidateCode)) {
                    strategyCode = candidateCode;
                    resolved = true;
                    qWarning() << "[StrategyRepository] Strategy code collision resolved:" << baseCode << "->" << strategyCode;
                    break;
                }
            }

            if (!resolved) {
                qWarning() << "[StrategyRepository] Strategy code already exists:" << strategyCode;
                return QString();
            }
        }
        
        // 如果 strategyId 为空，使用 strategyCode 作为默认ID
        if (strategyId.isEmpty()) {
            strategyId = strategyCode;  // 使用策略代码作为ID
        }
        
        // 插入包含 strategy_id 的记录
        query.prepare("INSERT INTO strategy ("
                      "strategy_id, strategy_code, strategy_name, strategy_type, "
                      "description, version, author, language, status, "
                      "created_at, updated_at) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, NOW(), NOW())");
        
        query.addBindValue(strategyId);
        query.addBindValue(strategyCode);
        query.addBindValue(strategyName);
        query.addBindValue(strategyType);
        query.addBindValue(description);
        query.addBindValue(version);
        query.addBindValue(author);
        query.addBindValue(language);
        query.addBindValue(status);
    }
    
    if (!query.exec()) {
        QSqlError error = query.lastError();
        qWarning() << "[StrategyRepository] Failed to save strategy. Error:" << error.text() 
                   << " | SQL:" << query.lastQuery() 
                   << " | Bound values:" << query.boundValues();
        return QString();
    }
    
    // 对于INSERT操作，使用我们设置的strategyId
    if (!isUpdate) {
        qDebug() << "[StrategyRepository] Created strategy with ID:" << strategyId;
    }
    
    const QVariantMap parameters = buildPersistedParameters(strategy);
    if (isUpdate) {
        if (!deleteStrategyParameters(strategyId, db)) {
            return QString();
        }
    }

    if (!parameters.isEmpty()) {
        if (!saveStrategyParameters(strategyId, parameters, db)) {
            return QString();
        }
    }
    
    return strategyId;
}

bool StrategyRepository::validateStrategy(const QVariantMap& strategy) const {
    if (!strategy.contains("strategy_name") || strategy.value("strategy_name").toString().isEmpty()) {
        qWarning() << "[StrategyRepository] Validation failed: strategy_name is required";
        return false;
    }
    
    if (!strategy.contains("strategy_type") || strategy.value("strategy_type").toString().isEmpty()) {
        qWarning() << "[StrategyRepository] Validation failed: strategy_type is required";
        return false;
    }
    
    // 验证策略类型是否有效
    QString strategyType = strategy.value("strategy_type").toString();
    QStringList validTypes = {"ALPHA", "ARBITRAGE", "TREND", "MEAN_REVERSION", "HFT", "PORTFOLIO", "CUSTOM"};
    if (!validTypes.contains(strategyType)) {
        qWarning() << "[StrategyRepository] Validation failed: invalid strategy_type:" << strategyType;
        return false;
    }
    
    // 验证状态（如果提供）
    if (strategy.contains("status")) {
        QString status = strategy.value("status").toString();
        QStringList validStatuses = {"DRAFT", "ACTIVE", "INACTIVE", "TESTING", "ARCHIVED", "DELETED"};
        if (!validStatuses.contains(status)) {
            qWarning() << "[StrategyRepository] Validation failed: invalid status:" << status;
            return false;
        }
    }
    
    return true;
}

QString StrategyRepository::generateStrategyCode(const QVariantMap& strategy) const {
    QString name = strategy.value("strategy_name").toString();
    QString type = strategy.value("strategy_type").toString();
    
    // 从名称生成简写代码
    QString codePrefix;
    if (type == "TREND") codePrefix = "TRD_";
    else if (type == "MEAN_REVERSION") codePrefix = "MR_";
    else if (type == "ALPHA") codePrefix = "ALPHA_";
    else if (type == "ARBITRAGE") codePrefix = "ARB_";
    else if (type == "PORTFOLIO") codePrefix = "PTF_";
    else codePrefix = "GEN_";
    
    // 使用名称的前几个字符，转换为大写，移除空格
    QString namePart = name.left(10).toUpper().replace(" ", "_").replace("-", "_");
    
    // 添加毫秒级时间戳和随机尾缀，避免高频创建时撞唯一键
    QString timestamp = QDateTime::currentDateTimeUtc().toString("yyyyMMddHHmmsszzz");
    QString entropy = QString::number(QRandomGenerator::global()->bounded(1000, 10000));
    
    return codePrefix + namePart + "_" + timestamp + entropy;
}

} // namespace database
} // namespace astock