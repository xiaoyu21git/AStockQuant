// FactorRepository.cpp
// 使用ConnectionPool连接池的因子仓储实现

#include "database/FactorRepository.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <QThread>
#include <QDateTime>

using namespace astock::database;

namespace {

bool hasTable(QSqlDatabase& db, const QString& tableName)
{
    const QStringList tableNames = db.tables();
    for (const QString& existing : tableNames) {
        if (existing.compare(tableName, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

bool columnExists(QSqlDatabase& db, const QString& tableName, const QString& columnName)
{
    QSqlQuery query(db);
    query.prepare(
        "SELECT COUNT(*) FROM information_schema.COLUMNS "
        "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = :table_name AND COLUMN_NAME = :column_name");
    query.bindValue(":table_name", tableName);
    query.bindValue(":column_name", columnName);
    if (!query.exec()) {
        qWarning() << "FactorRepository: Failed to inspect column" << tableName << columnName
                   << query.lastError().text();
        return false;
    }

    return query.next() && query.value(0).toInt() > 0;
}

bool ensureFactorSchema(QSqlDatabase& db)
{
    if (!columnExists(db, QStringLiteral("factors"), QStringLiteral("update_date"))) {
        QSqlQuery query(db);
        if (!query.exec(
                "ALTER TABLE factors ADD COLUMN update_date DATETIME DEFAULT CURRENT_TIMESTAMP "
                "ON UPDATE CURRENT_TIMESTAMP COMMENT '更新日期' AFTER create_date")) {
            qWarning() << "FactorRepository: Failed to add factors.update_date:" << query.lastError().text();
            return false;
        }
    }

    return true;
}

}

FactorRepository::FactorRepository()
    : m_initialized(false)
{
    qDebug() << "FactorRepository created";
}

FactorRepository::~FactorRepository()
{
    qDebug() << "FactorRepository destroyed";
}

bool FactorRepository::initialize()
{
    QMutexLocker locker(&m_initMutex);
    
    if (m_initialized) {
        return true;
    }
    
    try {
        // Repository 不应该负责配置连接池，只检查连接是否可用
        // 连接池配置应该由应用层（如 DatabaseConnectionManager）负责
        
        // 从连接池获取连接（使用 RAII 模式）
        ScopedConnection conn;
        if (!conn.isValid()) {
            qCritical() << "Failed to get database connection for initialization";
            return false;
        }

        QSqlDatabase& db = conn.get();
        const QStringList requiredTables = {
            QStringLiteral("factors"),
            QStringLiteral("factor_tags")
        };
        for (const QString& tableName : requiredTables) {
            if (!hasTable(db, tableName)) {
                qCritical() << "FactorRepository::initialize: 缺少必需数据表:" << tableName;
                return false;
            }
        }

        if (!ensureFactorSchema(db)) {
            qCritical() << "FactorRepository::initialize: 因子表 schema 迁移失败";
            return false;
        }
        
        // 检查表是否存在（如果需要的话）
        // 这里可以添加表检查逻辑，但为了简化，我们只检查连接是否有效
        
        m_initialized = true;
        qDebug() << "✅ FactorRepository::initialize: 数据库初始化完成";
        return true;
        
    } catch (const std::exception& e) {
        qCritical() << "FactorRepository::initialize: 异常:" << e.what();
        return false;
    }
}

QVariantMap FactorRepository::findById(const QString& factorId)
{
    QVariantMap result;
    
    try {
        // 使用 RAII 连接管理
        ScopedConnection conn;
        if (!conn.isValid()) {
            qWarning() << "No database connection available";
            return result;
        }
        
        QSqlDatabase& db = conn.get();
        
        QSqlQuery query(db);
        query.prepare("SELECT * FROM factors WHERE factor_id = ?");
        query.addBindValue(factorId);
        
        if (!query.exec()) {
            qWarning() << "Query failed:" << query.lastError().text();
            return result;
        }
        
        if (query.next()) {
            result = rowToFactorMap(query);
            
            // 加载标签
            QStringList tags = loadFactorTags(factorId, db);
            result["tags"] = tags;

            // 参数真源统一为 factor_instance.full_config，仓储层不再读取 factor_params 快照。
            result["parameters"] = QVariantMap();
        }
        
    } catch (const std::exception& e) {
        qWarning() << "Error in findById:" << e.what();
    }
    
    return result;
}

std::vector<QVariantMap> FactorRepository::findAll()
{
    std::vector<QVariantMap> results;
    
    try {
        // 使用 RAII 连接管理
        ScopedConnection conn;
        if (!conn.isValid()) {
            qWarning() << "No database connection available";
            return results;
        }
        
        QSqlDatabase& db = conn.get();
        
        QSqlQuery query(db);
        // 排除JSON列避免复杂处理
        if (!query.exec("SELECT factor_id, factor_name, display_name, major_category, "
                "sub_category, description, ic_value, ir_value, validity_days, "
                "turnover_rate, is_recommended, is_favorite, status, creator, "
                "create_date, update_date FROM factors ORDER BY create_date DESC")) {
            qWarning() << "Query failed:" << query.lastError().text();
            return results;
        }
        
        while (query.next()) {
            QVariantMap factor = rowToFactorMap(query);
            
            // 逐个加载标签
            QString factorId = factor["factorId"].toString();
            QStringList tags = loadFactorTags(factorId, db);
            factor["tags"] = tags;
            // 参数真源统一为 factor_instance.full_config，仓储层不再读取 factor_params 快照。
            factor["parameters"] = QVariantMap();
            
            results.push_back(factor);
        }
        
        qDebug() << "Found" << results.size() << "factors";
        
    } catch (const std::exception& e) {
        qWarning() << "Error in findAll:" << e.what();
    }
    
    return results;
}

std::vector<QVariantMap> FactorRepository::findByType(const QString& type)
{
    std::vector<QVariantMap> results;
    
    try {
        // 使用 RAII 连接管理
        ScopedConnection conn;
        if (!conn.isValid()) {
            qWarning() << "No database connection available";
            return results;
        }
        
        QSqlDatabase& db = conn.get();
        
        QSqlQuery query(db);
        query.prepare("SELECT * FROM factors WHERE major_category = ? OR sub_category = ? ORDER BY create_date DESC");
        query.addBindValue(type);
        query.addBindValue(type);
        
        if (!query.exec()) {
            qWarning() << "Query failed:" << query.lastError().text();
            return results;
        }
        
        while (query.next()) {
            QVariantMap factor = rowToFactorMap(query);
            
            QString factorId = factor["factorId"].toString();
            QStringList tags = loadFactorTags(factorId, db);
            factor["tags"] = tags;
            // 参数真源统一为 factor_instance.full_config，仓储层不再读取 factor_params 快照。
            factor["parameters"] = QVariantMap();
            
            results.push_back(factor);
        }
        
        qDebug() << "Found" << results.size() << "factors of type:" << type;
        
    } catch (const std::exception& e) {
        qWarning() << "Error in findByType:" << e.what();
    }
    
    return results;
}

std::vector<QVariantMap> FactorRepository::findByCategory(const QString& category)
{
    std::vector<QVariantMap> results;
    
    try {
        // 使用 RAII 连接管理
        ScopedConnection conn;
        if (!conn.isValid()) {
            qWarning() << "No database connection available";
            return results;
        }
        
        QSqlDatabase& db = conn.get();
        
        QSqlQuery query(db);
        query.prepare("SELECT * FROM factors WHERE major_category = ? ORDER BY create_date DESC");
        query.addBindValue(category);
        
        if (!query.exec()) {
            qWarning() << "Query failed:" << query.lastError().text();
            return results;
        }
        
        while (query.next()) {
            QVariantMap factor = rowToFactorMap(query);
            
            QString factorId = factor["factorId"].toString();
            QStringList tags = loadFactorTags(factorId, db);
            factor["tags"] = tags;
            // 参数真源统一为 factor_instance.full_config，仓储层不再读取 factor_params 快照。
            factor["parameters"] = QVariantMap();
            
            results.push_back(factor);
        }
        
        qDebug() << "Found" << results.size() << "factors in category:" << category;
        
    } catch (const std::exception& e) {
        qWarning() << "Error in findByCategory:" << e.what();
    }
    
    return results;
}

std::vector<QVariantMap> FactorRepository::findByTags(const QStringList& tags)
{
    std::vector<QVariantMap> results;
    
    if (tags.isEmpty()) {
        return results;
    }
    
    try {
        // 使用 RAII 连接管理
        ScopedConnection conn;
        if (!conn.isValid()) {
            qWarning() << "No database connection available";
            return results;
        }
        
        QSqlDatabase& db = conn.get();
        
        // 构建IN子句
        QString placeholders;
        for (int i = 0; i < tags.size(); ++i) {
            placeholders += "?";
            if (i < tags.size() - 1) {
                placeholders += ",";
            }
        }
        
        QSqlQuery query(db);
        query.prepare(QString("SELECT DISTINCT factor_id FROM factor_tags WHERE tag IN (%1)").arg(placeholders));
        
        for (int i = 0; i < tags.size(); ++i) {
            query.addBindValue(tags[i]);
        }
        
        if (!query.exec()) {
            qWarning() << "Query failed:" << query.lastError().text();
            return results;
        }
        
        QStringList factorIds;
        while (query.next()) {
            factorIds.append(query.value(0).toString());
        }
        
        if (factorIds.isEmpty()) {
            return results;
        }
        
        // 查询因子详细信息
        QString factorPlaceholders;
        for (int i = 0; i < factorIds.size(); ++i) {
            factorPlaceholders += "?";
            if (i < factorIds.size() - 1) {
                factorPlaceholders += ",";
            }
        }
        
        QSqlQuery factorQuery(db);
        factorQuery.prepare(QString("SELECT * FROM factors WHERE factor_id IN (%1) ORDER BY create_date DESC").arg(factorPlaceholders));
        
        for (int i = 0; i < factorIds.size(); ++i) {
            factorQuery.addBindValue(factorIds[i]);
        }
        
        if (!factorQuery.exec()) {
            qWarning() << "Factor query failed:" << factorQuery.lastError().text();
            return results;
        }
        
        while (factorQuery.next()) {
            QVariantMap factor = rowToFactorMap(factorQuery);
            
            QString factorId = factor["factorId"].toString();
            QStringList factorTags = loadFactorTags(factorId, db);
            factor["tags"] = factorTags;
            // 参数真源统一为 factor_instance.full_config，仓储层不再读取 factor_params 快照。
            factor["parameters"] = QVariantMap();
            
            results.push_back(factor);
        }
        
        qDebug() << "Found" << results.size() << "factors with tags:" << tags;
        
    } catch (const std::exception& e) {
        qWarning() << "Error in findByTags:" << e.what();
    }
    
    return results;
}

std::vector<QVariantMap> FactorRepository::search(const QString& keyword)
{
    std::vector<QVariantMap> results;
    
    if (keyword.isEmpty()) {
        return results;
    }
    
    try {
        // 使用 RAII 连接管理
        ScopedConnection conn;
        if (!conn.isValid()) {
            qWarning() << "No database connection available";
            return results;
        }
        
        QSqlDatabase& db = conn.get();
        
        QString searchPattern = QString("%%1%").arg(keyword);
        
        QSqlQuery query(db);
        query.prepare("SELECT * FROM factors WHERE factor_name LIKE ? OR display_name LIKE ? OR description LIKE ? OR major_category LIKE ? OR sub_category LIKE ? ORDER BY create_date DESC");
        query.addBindValue(searchPattern);
        query.addBindValue(searchPattern);
        query.addBindValue(searchPattern);
        query.addBindValue(searchPattern);
        query.addBindValue(searchPattern);
        
        if (!query.exec()) {
            qWarning() << "Query failed:" << query.lastError().text();
            return results;
        }
        
        while (query.next()) {
            QVariantMap factor = rowToFactorMap(query);
            
            QString factorId = factor["factorId"].toString();
            QStringList tags = loadFactorTags(factorId, db);
            factor["tags"] = tags;
            // 参数真源统一为 factor_instance.full_config，仓储层不再读取 factor_params 快照。
            factor["parameters"] = QVariantMap();
            
            results.push_back(factor);
        }
        
        qDebug() << "Found" << results.size() << "factors matching keyword:" << keyword;
        
    } catch (const std::exception& e) {
        qWarning() << "Error in search:" << e.what();
    }
    
    return results;
}

bool FactorRepository::save(const QVariantMap& factor)
{
    QString factorId = factor["factorId"].toString();
    if (factorId.isEmpty()) {
        qWarning() << "Factor ID is empty";
        return false;
    }
    
    try {
        // 使用 RAII 连接管理
        ScopedConnection conn;
        if (!conn.isValid()) {
            qWarning() << "No database connection available";
            return false;
        }
        
        QSqlDatabase& db = conn.get();

        if (!db.transaction()) {
            qWarning() << "Failed to begin transaction";
            return false;
        }
        
        // 检查是否存在
        QSqlQuery checkQuery(db);
        checkQuery.prepare("SELECT 1 FROM factors WHERE factor_id = ?");
        checkQuery.addBindValue(factorId);
        
        if (!checkQuery.exec()) {
            qWarning() << "Check existence failed:" << checkQuery.lastError().text();
            return false;
        }
        
        bool exists = checkQuery.next();
        QSqlQuery query(db);
        
        if (exists) {
            // 更新
            query.prepare(R"(
                UPDATE factors SET 
                    factor_name = ?, display_name = ?, major_category = ?,
                    sub_category = ?, description = ?, ic_value = ?, ir_value = ?,
                    validity_days = ?, turnover_rate = ?, is_recommended = ?,
                    is_favorite = ?, status = ?, update_date = CURRENT_TIMESTAMP
                WHERE factor_id = ?
            )");
            
            query.addBindValue(factor["factorName"].toString());
            query.addBindValue(factor["displayName"].toString());
            query.addBindValue(factor["majorCategory"].toString());
            query.addBindValue(factor["subCategory"].toString());
            query.addBindValue(factor["description"].toString());
            query.addBindValue(factor["icValue"].toDouble());
            query.addBindValue(factor["irValue"].toDouble());
            query.addBindValue(factor["validityDays"].toInt());
            query.addBindValue(factor["turnoverRate"].toDouble());
            query.addBindValue(factor["isRecommended"].toBool());
            query.addBindValue(factor["isFavorite"].toBool());
            query.addBindValue(factor["status"].toString());
            query.addBindValue(factorId);
        } else {
            // 插入
            query.prepare(R"(
                INSERT INTO factors (
                    factor_id, factor_name, display_name, major_category,
                    sub_category, description, ic_value, ir_value,
                    validity_days, turnover_rate, is_recommended, is_favorite,
                    status, creator, create_date
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            )");
            
            query.addBindValue(factorId);
            query.addBindValue(factor["factorName"].toString());
            query.addBindValue(factor["displayName"].toString());
            query.addBindValue(factor["majorCategory"].toString());
            query.addBindValue(factor["subCategory"].toString());
            query.addBindValue(factor["description"].toString());
            query.addBindValue(factor["icValue"].toDouble());
            query.addBindValue(factor["irValue"].toDouble());
            query.addBindValue(factor["validityDays"].toInt());
            query.addBindValue(factor["turnoverRate"].toDouble());
            query.addBindValue(factor["isRecommended"].toBool());
            query.addBindValue(factor["isFavorite"].toBool());
            query.addBindValue(factor["status"].toString());
            query.addBindValue(factor["creator"].toString());
            const QString createDate = factor.value("createDate").toString().trimmed();
            query.addBindValue(createDate.isEmpty()
                ? QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
                : createDate);
        }
        
        if (!query.exec()) {
            qWarning() << "Save failed:" << query.lastError().text();
            db.rollback();
            return false;
        }
        
        // 处理标签
        QStringList tags = factor["tags"].toStringList();
        
        // 先删除旧标签
        bool tagsDeleted = deleteFactorTags(factorId, db);
        if (!tagsDeleted) {
            qWarning() << "Failed to delete old tags, but factor was saved";
            db.rollback();
            return false;
        }
        
        // 保存新标签
        bool tagsSaved = true;
        if (!tags.isEmpty()) {
            tagsSaved = saveFactorTags(factorId, tags, db);
            if (!tagsSaved) {
                qWarning() << "Failed to save tags, but factor was saved";
                db.rollback();
                return false;
            }
        }

        // 参数真源统一为 factor_instance.full_config，仓储层不再写入 factor_params 快照。
        if (!db.commit()) {
            qWarning() << "Failed to commit transaction";
            db.rollback();
            return false;
        }

        qDebug() << "Factor saved successfully:" << factorId;
        return tagsDeleted && tagsSaved;
        
    } catch (const std::exception& e) {
        qWarning() << "Error in save:" << e.what();
        if (auto db = QSqlDatabase::database(); db.isValid() && db.isOpen()) {
            db.rollback();
        }
        return false;
    }
}

size_t FactorRepository::saveBatch(const std::vector<QVariantMap>& factors)
{
    if (factors.empty()) {
        return 0;
    }
    
    try {
        // 使用 RAII 连接管理
        ScopedConnection conn;
        if (!conn.isValid()) {
            qWarning() << "No database connection available";
            return 0;
        }
        
        QSqlDatabase& db = conn.get();
        
        // 开始事务
        if (!db.transaction()) {
            qWarning() << "Failed to begin transaction";
            return 0;
        }
        
        size_t savedCount = 0;
        bool allSuccess = true;
        
        for (const auto& factor : factors) {
            // 使用内部保存方法，复用当前连接
            if (saveFactorInternal(factor, db)) {
                savedCount++;
            } else {
                allSuccess = false;
                qWarning() << "Failed to save factor in batch:" << factor["factorId"].toString();
            }
        }
        
        if (allSuccess && savedCount == factors.size()) {
            if (!db.commit()) {
                qWarning() << "Failed to commit transaction";
                db.rollback();
                return 0;
            }
            qDebug() << "Saved" << savedCount << "factors in batch";
            return savedCount;
        } else {
            db.rollback();
            qWarning() << "Batch save failed, rolled back. Saved" << savedCount << "out of" << factors.size();
            return 0;
        }
        
    } catch (const std::exception& e) {
        qWarning() << "Error in saveBatch:" << e.what();
        return 0;
    }
}

bool FactorRepository::update(const QString& factorId, const QVariantMap& factor)
{
    // 复用save逻辑
    QVariantMap updatedFactor = factor;
    updatedFactor["factorId"] = factorId;
    return save(updatedFactor);
}

bool FactorRepository::remove(const QString& factorId)
{
    try {
        QSqlDatabase db = ConnectionPool::instance().getConnection();
        if (!db.isValid() || !db.isOpen()) {
            qWarning() << "No database connection available";
            return false;
        }
        
        // 由于设置了ON DELETE CASCADE，只需删除主表记录
        QSqlQuery query(db);
        query.prepare("DELETE FROM factors WHERE factor_id = ?");
        query.addBindValue(factorId);
        
        if (!query.exec()) {
            qWarning() << "Delete failed:" << query.lastError().text();
            ConnectionPool::instance().releaseConnection(db);
            return false;
        }
        
        bool removed = query.numRowsAffected() > 0;
        ConnectionPool::instance().releaseConnection(db);
        
        if (removed) {
            qDebug() << "Factor removed:" << factorId;
        }
        
        return removed;
        
    } catch (const std::exception& e) {
        qWarning() << "Error in remove:" << e.what();
        return false;
    }
}

bool FactorRepository::exists(const QString& factorId)
{
    try {
        QSqlDatabase db = ConnectionPool::instance().getConnection();
        if (!db.isValid() || !db.isOpen()) {
            qWarning() << "No database connection available";
            return false;
        }
        
        QSqlQuery query(db);
        query.prepare("SELECT 1 FROM factors WHERE factor_id = ?");
        query.addBindValue(factorId);
        
        if (!query.exec()) {
            qWarning() << "Exists check failed:" << query.lastError().text();
            ConnectionPool::instance().releaseConnection(db);
            return false;
        }
        
        bool exists = query.next();
        ConnectionPool::instance().releaseConnection(db);
        
        qDebug() << "Factor" << factorId << (exists ? "exists" : "does not exist");
        return exists;
        
    } catch (const std::exception& e) {
        qWarning() << "Error in exists:" << e.what();
        return false;
    }
}

size_t FactorRepository::count()
{
    try {
        QSqlDatabase db = ConnectionPool::instance().getConnection();
        if (!db.isValid() || !db.isOpen()) {
            qWarning() << "No database connection available";
            return 0;
        }
        
        QSqlQuery query(db);
        if (!query.exec("SELECT COUNT(*) as count FROM factors")) {
            qWarning() << "Count query failed:" << query.lastError().text();
            ConnectionPool::instance().releaseConnection(db);
            return 0;
        }
        
        size_t count = 0;
        if (query.next()) {
            count = query.value(0).toUInt();
        }
        
        ConnectionPool::instance().releaseConnection(db);
        qDebug() << "Total factors:" << count;
        return count;
        
    } catch (const std::exception& e) {
        qWarning() << "Error in count:" << e.what();
        return 0;
    }
}

bool FactorRepository::clearAll()
{
    try {
        QSqlDatabase db = ConnectionPool::instance().getConnection();
        if (!db.isValid() || !db.isOpen()) {
            qWarning() << "No database connection available";
            return false;
        }
        
        // 开始事务
        if (!db.transaction()) {
            qWarning() << "Failed to begin transaction";
            ConnectionPool::instance().releaseConnection(db);
            return false;
        }
        
        // 清空因子标签表
        QSqlQuery tagsQuery(db);
        if (!tagsQuery.exec("DELETE FROM factor_tags")) {
            qWarning() << "Failed to clear factor tags:" << tagsQuery.lastError().text();
            db.rollback();
            ConnectionPool::instance().releaseConnection(db);
            return false;
        }
        
        // 清空因子表
        QSqlQuery factorsQuery(db);
        if (!factorsQuery.exec("DELETE FROM factors")) {
            qWarning() << "Failed to clear factors:" << factorsQuery.lastError().text();
            db.rollback();
            ConnectionPool::instance().releaseConnection(db);
            return false;
        }
        
        // 提交事务
        if (!db.commit()) {
            qWarning() << "Failed to commit transaction";
            db.rollback();
            ConnectionPool::instance().releaseConnection(db);
            return false;
        }
        
        ConnectionPool::instance().releaseConnection(db);
        qDebug() << "All factor data cleared";
        return true;
        
    } catch (const std::exception& e) {
        qWarning() << "Error in clearAll:" << e.what();
        return false;
    }
}

// ========== 私有辅助方法 ==========

QVariantMap FactorRepository::rowToFactorMap(const QSqlQuery& query)
{
    QVariantMap factor;
    QSqlRecord record = query.record();
    
    factor["factorId"] = query.value("factor_id").toString();
    factor["factorName"] = query.value("factor_name").toString();
    factor["displayName"] = query.value("display_name").toString();
    factor["majorCategory"] = query.value("major_category").toString();
    factor["subCategory"] = query.value("sub_category").toString();
    factor["description"] = query.value("description").toString();
    factor["icValue"] = query.value("ic_value").toDouble();
    factor["irValue"] = query.value("ir_value").toDouble();
    factor["validityDays"] = query.value("validity_days").toInt();
    factor["turnoverRate"] = query.value("turnover_rate").toDouble();
    factor["isRecommended"] = query.value("is_recommended").toBool();
    factor["isFavorite"] = query.value("is_favorite").toBool();
    factor["status"] = query.value("status").toString();
    factor["creator"] = query.value("creator").toString();
    factor["createDate"] = query.value("create_date").toString();
    factor["updateDate"] = record.contains("update_date")
        ? query.value("update_date").toString()
        : QString();
    
    return factor;
}

QStringList FactorRepository::loadFactorTags(const QString& factorId, QSqlDatabase& db)
{
    QStringList tags;
    
    QSqlQuery query(db);
    query.prepare("SELECT tag FROM factor_tags WHERE factor_id = ? ORDER BY tag");
    query.addBindValue(factorId);
    
    if (query.exec()) {
        while (query.next()) {
            tags.append(query.value(0).toString());
        }
    }
    
    return tags;
}

bool FactorRepository::saveFactorTags(const QString& factorId, const QStringList& tags, QSqlDatabase& db)
{
    QSqlQuery query(db);
    query.prepare("INSERT INTO factor_tags (factor_id, tag) VALUES (?, ?)");
    
    for (const QString& tag : tags) {
        query.addBindValue(factorId);
        query.addBindValue(tag.trimmed());
        
        if (!query.exec()) {
            qWarning() << "Failed to save tag:" << tag << query.lastError().text();
            return false;
        }
        
        query.finish();  // 准备下一次绑定
    }
    
    return true;
}

bool FactorRepository::deleteFactorTags(const QString& factorId, QSqlDatabase& db)
{
    QSqlQuery query(db);
    query.prepare("DELETE FROM factor_tags WHERE factor_id = ?");
    query.addBindValue(factorId);
    
    if (!query.exec()) {
        qWarning() << "Failed to delete tags:" << query.lastError().text();
        return false;
    }
    
    return true;
}

bool FactorRepository::saveFactorInternal(const QVariantMap& factor, QSqlDatabase& db)
{
    QString factorId = factor["factorId"].toString();
    if (factorId.isEmpty()) {
        qWarning() << "Factor ID is empty in saveFactorInternal";
        return false;
    }
    
    // 检查是否存在
    QSqlQuery checkQuery(db);
    checkQuery.prepare("SELECT 1 FROM factors WHERE factor_id = ?");
    checkQuery.addBindValue(factorId);
    
    if (!checkQuery.exec()) {
        qWarning() << "Check existence failed in saveFactorInternal:" << checkQuery.lastError().text();
        return false;
    }
    
    bool exists = checkQuery.next();
    QSqlQuery query(db);
    if (exists) {
        // 更新
        query.prepare(R"(
            UPDATE factors SET 
                factor_name = ?, display_name = ?, major_category = ?,
                sub_category = ?, description = ?, ic_value = ?, ir_value = ?,
                validity_days = ?, turnover_rate = ?, is_recommended = ?,
                is_favorite = ?, status = ?, update_date = CURRENT_TIMESTAMP
            WHERE factor_id = ?
        )");
        
        query.addBindValue(factor["factorName"].toString());
        query.addBindValue(factor["displayName"].toString());
        query.addBindValue(factor["majorCategory"].toString());
        query.addBindValue(factor["subCategory"].toString());
        query.addBindValue(factor["description"].toString());
        query.addBindValue(factor["icValue"].toDouble());
        query.addBindValue(factor["irValue"].toDouble());
        query.addBindValue(factor["validityDays"].toInt());
        query.addBindValue(factor["turnoverRate"].toDouble());
        query.addBindValue(factor["isRecommended"].toBool());
        query.addBindValue(factor["isFavorite"].toBool());
        query.addBindValue(factor["status"].toString());
        query.addBindValue(factorId);
    } else {
        // 插入
        query.prepare(R"(
            INSERT INTO factors (
                factor_id, factor_name, display_name, major_category,
                sub_category, description, ic_value, ir_value,
                validity_days, turnover_rate, is_recommended, is_favorite,
                status, creator, create_date
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        )");
        
        query.addBindValue(factorId);
        query.addBindValue(factor["factorName"].toString());
        query.addBindValue(factor["displayName"].toString());
        query.addBindValue(factor["majorCategory"].toString());
        query.addBindValue(factor["subCategory"].toString());
        query.addBindValue(factor["description"].toString());
        query.addBindValue(factor["icValue"].toDouble());
        query.addBindValue(factor["irValue"].toDouble());
        query.addBindValue(factor["validityDays"].toInt());
        query.addBindValue(factor["turnoverRate"].toDouble());
        query.addBindValue(factor["isRecommended"].toBool());
        query.addBindValue(factor["isFavorite"].toBool());
        query.addBindValue(factor["status"].toString());
        query.addBindValue(factor["creator"].toString());
        const QString createDate = factor.value("createDate").toString().trimmed();
        query.addBindValue(createDate.isEmpty()
            ? QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
            : createDate);
    }
    
    if (!query.exec()) {
        qWarning() << "Save failed in saveFactorInternal:" << query.lastError().text();
        return false;
    }
    
    // 处理标签
    QStringList tags = factor["tags"].toStringList();
    
    // 先删除旧标签
    bool tagsDeleted = deleteFactorTags(factorId, db);
    if (!tagsDeleted) {
        qWarning() << "Failed to delete old tags in saveFactorInternal, but factor was saved";
    }
    
    // 保存新标签
    bool tagsSaved = true;
    if (!tags.isEmpty()) {
        tagsSaved = saveFactorTags(factorId, tags, db);
        if (!tagsSaved) {
            qWarning() << "Failed to save tags in saveFactorInternal, but factor was saved";
        }
    }
    
    // 参数真源统一为 factor_instance.full_config，仓储层不再写入 factor_params 快照。
    return tagsDeleted && tagsSaved;
}

