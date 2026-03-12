// FactorRepository.cpp
// 因子仓储实现，使用QueryBuilder链式调用

#include "database/FactorRepository.h"
#include "database/DatabaseConfig.h"
#include "database/DatabaseService.h"
#include <QDebug>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <stdexcept>

using namespace astock::database;

FactorRepository::FactorRepository(std::shared_ptr<QtMySQLDatabase> database)
    : m_database(database)
    , m_initialized(false)
{
    // 如果传入的database为空，则使用DatabaseService单例
    if (!m_database) {
        qDebug() << "FactorRepository: 使用DatabaseService单例";
        // 注意：这里不立即初始化，延迟到第一次使用时
    } else {
        initializeDatabase();
    }
    qDebug() << "FactorRepository: Constructor";
}

FactorRepository::~FactorRepository()
{
    qDebug() << "FactorRepository: Destructor";
}

// IFactorRepository接口实现

QVariantMap FactorRepository::findById(const QString& factorId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!checkDatabaseConnection()) {
        qWarning() << "FactorRepository::findById: Database connection not available";
        return QVariantMap();
    }
    
    try {
        auto builder = getQueryBuilder();
        if (!builder) {
            qWarning() << "FactorRepository::findById: Failed to get QueryBuilder";
            return QVariantMap();
        }
        
        // 使用QueryBuilder链式调用查询因子
        auto query = builder->from("factors")
                             .select("*")
                             .where("factor_id", ConditionType::EQUAL, factorId)
                             .limit(1);
        
        QueryResult result = query.execute();
        
        if (result.getRows().empty()) {
            qDebug() << "FactorRepository::findById: Factor not found:" << factorId;
            return QVariantMap();
        }
        
        QVariantMap factor = resultRowToFactorMap(result.getRows()[0]);
        
        // 加载因子标签
        QStringList tags = loadFactorTags(factorId);
        factor["tags"] = tags;
        
        qDebug() << "FactorRepository::findById: Found factor:" << factorId;
        return factor;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorRepository::findById: Error:" << e.what();
        return QVariantMap();
    }
}

std::vector<QVariantMap> FactorRepository::findAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    qDebug() << "=== FactorRepository::findAll 开始 ===";
    
    try {
        // 确保数据库连接
        if (!checkDatabaseConnection()) {
            qWarning() << "FactorRepository::findAll: 数据库连接不可用";
            return {};
        }
        
        // 最简单的查询：排除JSON列
        QString simpleSql = "SELECT factor_id, factor_name, display_name, major_category, "
                           "sub_category, description, ic_value, ir_value, validity_days, "
                           "turnover_rate, is_recommended, is_favorite, status, creator, "
                           "create_date FROM factors ORDER BY create_date DESC";
        
        qDebug() << "FactorRepository::findAll: 执行简单查询（排除JSON列）";
        
        QueryResult result = m_database->executeQuery(simpleSql, {});
        
        qDebug() << "FactorRepository::findAll: 查询返回行数:" << result.getRows().size();
        
        std::vector<QVariantMap> factors;
        for (const auto& row : result.getRows()) {
            QVariantMap factor;
            
            factor["factorId"] = row.getString("factor_id");
            factor["factorName"] = row.getString("factor_name");
            factor["displayName"] = row.getString("display_name");
            factor["majorCategory"] = row.getString("major_category");
            factor["subCategory"] = row.getString("sub_category");
            factor["description"] = row.getString("description");
            factor["icValue"] = row.getDouble("ic_value");
            factor["irValue"] = row.getDouble("ir_value");
            factor["validityDays"] = row.getInt("validity_days");
            factor["turnoverRate"] = row.getDouble("turnover_rate");
            factor["isRecommended"] = row.getValueAs<bool>("is_recommended", false);
            factor["isFavorite"] = row.getValueAs<bool>("is_favorite", false);
            factor["status"] = row.getString("status");
            factor["creator"] = row.getString("creator");
            factor["createDate"] = row.getString("create_date");
            factor["groupReturns"] = QVariant::fromValue(QVector<double>()); // 空数组
            
            // 加载因子标签
            QString factorId = factor["factorId"].toString();
            QStringList tags = loadFactorTags(factorId);
            factor["tags"] = tags;
            
            factors.push_back(factor);
        }
        
        qDebug() << "FactorRepository::findAll: 找到" << factors.size() << "个因子";
        
        if (!factors.empty()) {
            qDebug() << "✅ 成功找到因子ID列表:";
            for (const auto& factor : factors) {
                qDebug() << "  -" << factor["factorId"].toString() << "(" << factor["displayName"].toString() << ")";
            }
        }
        
        qDebug() << "=== FactorRepository::findAll 结束 ===";
        return factors;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorRepository::findAll: 错误:" << e.what();
        qDebug() << "=== FactorRepository::findAll 结束 (异常) ===";
        return {};
    }
}

std::vector<QVariantMap> FactorRepository::findByType(const QString& type)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!checkDatabaseConnection()) {
        qWarning() << "FactorRepository::findByType: Database connection not available";
        return {};
    }
    
    try {
        auto builder = getQueryBuilder();
        if (!builder) {
            qWarning() << "FactorRepository::findByType: Failed to get QueryBuilder";
            return {};
        }
        
        // 使用QueryBuilder链式调用查询指定类型的因子
        auto query = builder->from("factors")
                             .select("*")
                             .where("major_category", ConditionType::EQUAL, type)
                             .orWhere("sub_category", ConditionType::EQUAL, type)
                             .orderBy("create_date", OrderType::DESC);
        
        QueryResult result = query.execute();
        
        std::vector<QVariantMap> factors;
        for (const auto& row : result.getRows()) {
            QVariantMap factor = resultRowToFactorMap(row);
            
            // 加载因子标签
            QString factorId = factor["factorId"].toString();
            QStringList tags = loadFactorTags(factorId);
            factor["tags"] = tags;
            
            factors.push_back(factor);
        }
        
        qDebug() << "FactorRepository::findByType: Found" << factors.size() << "factors of type:" << type;
        return factors;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorRepository::findByType: Error:" << e.what();
        return {};
    }
}

std::vector<QVariantMap> FactorRepository::findByCategory(const QString& category)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!checkDatabaseConnection()) {
        qWarning() << "FactorRepository::findByCategory: Database connection not available";
        return {};
    }
    
    try {
        auto builder = getQueryBuilder();
        if (!builder) {
            qWarning() << "FactorRepository::findByCategory: Failed to get QueryBuilder";
            return {};
        }
        
        // 使用QueryBuilder链式调用查询指定分类的因子
        auto query = builder->from("factors")
                             .select("*")
                             .where("major_category", ConditionType::EQUAL, category)
                             .orderBy("create_date", OrderType::DESC);
        
        QueryResult result = query.execute();
        
        std::vector<QVariantMap> factors;
        for (const auto& row : result.getRows()) {
            QVariantMap factor = resultRowToFactorMap(row);
            
            // 加载因子标签
            QString factorId = factor["factorId"].toString();
            QStringList tags = loadFactorTags(factorId);
            factor["tags"] = tags;
            
            factors.push_back(factor);
        }
        
        qDebug() << "FactorRepository::findByCategory: Found" << factors.size() << "factors in category:" << category;
        return factors;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorRepository::findByCategory: Error:" << e.what();
        return {};
    }
}

std::vector<QVariantMap> FactorRepository::findByTags(const QStringList& tags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!checkDatabaseConnection() || tags.isEmpty()) {
        qWarning() << "FactorRepository::findByTags: Database connection not available or tags empty";
        return {};
    }
    
    try {
        auto builder = getQueryBuilder();
        if (!builder) {
            qWarning() << "FactorRepository::findByTags: Failed to get QueryBuilder";
            return {};
        }
        
        // 使用QueryBuilder链式调用查询指定标签的因子
        // 首先查询因子标签表，然后关联查询因子表
        auto query = builder->from("factor_tags")
                             .select("DISTINCT factor_id")
                             .where("tag", ConditionType::IN, tags)
                             .orderBy("factor_id", OrderType::ASC);
        
        QueryResult tagResult = query.execute();
        
        if (tagResult.getRows().empty()) {
            qDebug() << "FactorRepository::findByTags: No factors found with tags:" << tags;
            return {};
        }
        
        // 获取因子ID列表
        QStringList factorIds;
        for (const auto& row : tagResult.getRows()) {
            factorIds.append(row.getString("factor_id"));
        }
        
        // 查询因子详细信息
        builder->reset();
        auto factorQuery = builder->from("factors")
                                   .select("*")
                                   .where("factor_id", ConditionType::IN, factorIds)
                                   .orderBy("create_date", OrderType::DESC);
        
        QueryResult factorResult = factorQuery.execute();
        
        std::vector<QVariantMap> factors;
        for (const auto& row : factorResult.getRows()) {
            QVariantMap factor = resultRowToFactorMap(row);
            
            // 加载因子标签
            QString factorId = factor["factorId"].toString();
            QStringList factorTags = loadFactorTags(factorId);
            factor["tags"] = factorTags;
            
            factors.push_back(factor);
        }
        
        qDebug() << "FactorRepository::findByTags: Found" << factors.size() << "factors with tags:" << tags;
        return factors;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorRepository::findByTags: Error:" << e.what();
        return {};
    }
}

std::vector<QVariantMap> FactorRepository::search(const QString& keyword)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!checkDatabaseConnection() || keyword.isEmpty()) {
        qWarning() << "FactorRepository::search: Database connection not available or keyword empty";
        return {};
    }
    
    try {
        auto builder = getQueryBuilder();
        if (!builder) {
            qWarning() << "FactorRepository::search: Failed to get QueryBuilder";
            return {};
        }
        
        // 使用QueryBuilder链式调用搜索因子
        QString searchPattern = QString("%%1%").arg(keyword);
        
        auto query = builder->from("factors")
                             .select("*")
                             .where("factor_name", ConditionType::LIKE, searchPattern)
                             .orWhere("display_name", ConditionType::LIKE, searchPattern)
                             .orWhere("description", ConditionType::LIKE, searchPattern)
                             .orWhere("major_category", ConditionType::LIKE, searchPattern)
                             .orWhere("sub_category", ConditionType::LIKE, searchPattern)
                             .orderBy("create_date", OrderType::DESC);
        
        QueryResult result = query.execute();
        
        std::vector<QVariantMap> factors;
        for (const auto& row : result.getRows()) {
            QVariantMap factor = resultRowToFactorMap(row);
            
            // 加载因子标签
            QString factorId = factor["factorId"].toString();
            QStringList tags = loadFactorTags(factorId);
            factor["tags"] = tags;
            
            factors.push_back(factor);
        }
        
        qDebug() << "FactorRepository::search: Found" << factors.size() << "factors matching keyword:" << keyword;
        return factors;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorRepository::search: Error:" << e.what();
        return {};
    }
}

size_t FactorRepository::saveBatch(const std::vector<QVariantMap>& factors)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!checkDatabaseConnection()) {
        qWarning() << "FactorRepository::saveBatch: Database connection not available";
        return 0;
    }
    
    size_t savedCount = 0;
    
    try {
        // 开始事务
        if (!m_database->beginTransaction()) {
            qWarning() << "FactorRepository::saveBatch: Failed to begin transaction";
            return 0;
        }
        
        for (const auto& factor : factors) {
            if (save(factor)) {
                savedCount++;
            }
        }
        
        // 提交事务
        if (!m_database->commitTransaction()) {
            qWarning() << "FactorRepository::saveBatch: Failed to commit transaction";
            m_database->rollbackTransaction();
            return 0;
        }
        
        qDebug() << "FactorRepository::saveBatch: Saved" << savedCount << "out of" << factors.size() << "factors";
        return savedCount;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorRepository::saveBatch: Error:" << e.what();
        m_database->rollbackTransaction();
        return 0;
    }
}

bool FactorRepository::save(const QVariantMap& factor)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!checkDatabaseConnection()) {
        qWarning() << "FactorRepository::save: Database connection not available";
        return false;
    }
    
    try {
        // 检查因子ID是否已存在
        QString factorId = factor["factorId"].toString();
        if (factorId.isEmpty()) {
            qWarning() << "FactorRepository::save: Factor ID is empty";
            return false;
        }
        
        if (exists(factorId)) {
            qDebug() << "FactorRepository::save: Factor already exists, updating:" << factorId;
            return update(factorId, factor);
        }
        
        auto builder = getQueryBuilder();
        if (!builder) {
            qWarning() << "FactorRepository::save: Failed to get QueryBuilder";
            return false;
        }
        
        // 准备插入数据
        QString currentDate = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        
        // 构建插入SQL - 使用命名参数
        QString sql = "INSERT INTO factors (factor_id, factor_name, display_name, "
                      "major_category, sub_category, description, ic_value, ir_value, "
                      "validity_days, turnover_rate, is_recommended, is_favorite, "
                      "status, creator, create_date, group_returns) "
                      "VALUES (:factor_id, :factor_name, :display_name, "
                      ":major_category, :sub_category, :description, :ic_value, :ir_value, "
                      ":validity_days, :turnover_rate, :is_recommended, :is_favorite, "
                      ":status, :creator, :create_date, :group_returns)";
        
        // 获取参数 - 使用命名绑定
        std::map<QString, QVariant> params;
        params["factor_id"] = factorId;
        params["factor_name"] = factor["factorName"].toString();
        params["display_name"] = factor["displayName"].toString();
        params["major_category"] = factor["majorCategory"].toString();
        params["sub_category"] = factor["subCategory"].toString();
        params["description"] = factor["description"].toString();
        params["ic_value"] = factor["icValue"].toDouble();
        params["ir_value"] = factor["irValue"].toDouble();
        params["validity_days"] = factor["validityDays"].toInt();
        params["turnover_rate"] = factor["turnoverRate"].toDouble();
        params["is_recommended"] = factor["isRecommended"].toBool();
        params["is_favorite"] = factor["isFavorite"].toBool();
        params["status"] = factor["status"].toString();
        params["creator"] = factor["creator"].toString();
        params["create_date"] = currentDate;
        
        // 处理group_returns数组
        QVariant groupReturnsVar = factor["groupReturns"];
        QString groupReturnsJson = "[]";
        if (groupReturnsVar.canConvert<QVector<double>>()) {
            QVector<double> groupReturns = groupReturnsVar.value<QVector<double>>();
            QJsonArray jsonArray;
            for (double value : groupReturns) {
                jsonArray.append(value);
            }
            QJsonDocument doc(jsonArray);
            groupReturnsJson = doc.toJson(QJsonDocument::Compact);
        }
        params["group_returns"] = groupReturnsJson;
        
        qDebug() << "FactorRepository::save: Executing insert for factor:" << factorId;
        qDebug() << "SQL:" << sql;
        qDebug() << "参数数量:" << params.size();
        
        // 执行插入
        int affectedRows = m_database->executeUpdate(sql, params);
        
        // 检查插入是否成功
        // 对于INSERT语句，应该至少影响1行
        // 但某些数据库驱动可能返回0，所以我们检查是否没有异常抛出
        qDebug() << "FactorRepository::save: Insert executed, affected rows:" << affectedRows;
        
        if (affectedRows < 0) {
            qWarning() << "FactorRepository::save: Insert failed, affected rows:" << affectedRows;
            return false;
        }
        
        // 保存因子标签
        QStringList tags = factor["tags"].toStringList();
        if (!tags.isEmpty()) {
            if (!saveFactorTags(factorId, tags)) {
                qWarning() << "FactorRepository::save: Failed to save factor tags";
                return false;
            }
        }
        
        qDebug() << "✅ FactorRepository::save: Successfully saved factor:" << factorId;
        return true;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorRepository::save: Error:" << e.what();
        return false;
    }
}

bool FactorRepository::update(const QString& factorId, const QVariantMap& factor)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!checkDatabaseConnection()) {
        qWarning() << "FactorRepository::update: Database connection not available";
        return false;
    }
    
    try {
        auto builder = getQueryBuilder();
        if (!builder) {
            qWarning() << "FactorRepository::update: Failed to get QueryBuilder";
            return false;
        }
        
        // 构建更新SQL
        QString sql = "UPDATE factors SET "
                      "factor_name = ?, display_name = ?, major_category = ?, "
                      "sub_category = ?, description = ?, ic_value = ?, ir_value = ?, "
                      "validity_days = ?, turnover_rate = ?, is_recommended = ?, "
                      "is_favorite = ?, status = ?, creator = ?, group_returns = ? "
                      "WHERE factor_id = ?";
        
        // 获取参数
        std::map<QString, QVariant> params;
        params["factor_name"] = factor["factorName"].toString();
        params["display_name"] = factor["displayName"].toString();
        params["major_category"] = factor["majorCategory"].toString();
        params["sub_category"] = factor["subCategory"].toString();
        params["description"] = factor["description"].toString();
        params["ic_value"] = factor["icValue"].toDouble();
        params["ir_value"] = factor["irValue"].toDouble();
        params["validity_days"] = factor["validityDays"].toInt();
        params["turnover_rate"] = factor["turnoverRate"].toDouble();
        params["is_recommended"] = factor["isRecommended"].toBool();
        params["is_favorite"] = factor["isFavorite"].toBool();
        params["status"] = factor["status"].toString();
        params["creator"] = factor["creator"].toString();
        
        // 处理group_returns数组
        QVariant groupReturnsVar = factor["groupReturns"];
        QString groupReturnsJson = "[]";
        if (groupReturnsVar.canConvert<QVector<double>>()) {
            QVector<double> groupReturns = groupReturnsVar.value<QVector<double>>();
            QJsonArray jsonArray;
            for (double value : groupReturns) {
                jsonArray.append(value);
            }
            QJsonDocument doc(jsonArray);
            groupReturnsJson = doc.toJson(QJsonDocument::Compact);
        }
        params["group_returns"] = groupReturnsJson;
        params["factor_id"] = factorId;
        
        // 执行更新
        QueryResult result = m_database->executeQuery(sql, params);
        
        if (result.getRows().empty()) {
            qWarning() << "FactorRepository::update: Failed to update factor:" << factorId;
            return false;
        }
        
        // 更新因子标签
        QStringList tags = factor["tags"].toStringList();
        deleteFactorTags(factorId);
        if (!tags.isEmpty()) {
            saveFactorTags(factorId, tags);
        }
        
        qDebug() << "FactorRepository::update: Updated factor:" << factorId;
        return true;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorRepository::update: Error:" << e.what();
        return false;
    }
}

bool FactorRepository::remove(const QString& factorId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!checkDatabaseConnection()) {
        qWarning() << "FactorRepository::remove: Database connection not available";
        return false;
    }
    
    try {
        auto builder = getQueryBuilder();
        if (!builder) {
            qWarning() << "FactorRepository::remove: Failed to get QueryBuilder";
            return false;
        }
        
        // 开始事务
        if (!m_database->beginTransaction()) {
            qWarning() << "FactorRepository::remove: Failed to begin transaction";
            return false;
        }
        
        // 删除因子标签
        deleteFactorTags(factorId);
        
        // 删除因子
        auto query = builder->from("factors")
                             .where("factor_id", ConditionType::EQUAL, factorId);
        
        int deletedCount = query.executeDelete();
        
        if (deletedCount > 0) {
            // 提交事务
            if (!m_database->commitTransaction()) {
                qWarning() << "FactorRepository::remove: Failed to commit transaction";
                m_database->rollbackTransaction();
                return false;
            }
            
            qDebug() << "FactorRepository::remove: Removed factor:" << factorId;
            return true;
        } else {
            m_database->rollbackTransaction();
            qDebug() << "FactorRepository::remove: Factor not found:" << factorId;
            return false;
        }
        
    } catch (const std::exception& e) {
        qWarning() << "FactorRepository::remove: Error:" << e.what();
        m_database->rollbackTransaction();
        return false;
    }
}

size_t FactorRepository::count()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!checkDatabaseConnection()) {
        qWarning() << "FactorRepository::count: Database connection not available";
        return 0;
    }
    
    try {
        auto builder = getQueryBuilder();
        if (!builder) {
            qWarning() << "FactorRepository::count: Failed to get QueryBuilder";
            return 0;
        }
        
        // 使用QueryBuilder链式调用统计因子数量
        auto query = builder->from("factors")
                             .select("COUNT(*) as count");
        
        QueryResult result = query.execute();
        
        if (result.getRows().empty()) {
            return 0;
        }
        
        size_t count = result.getRows()[0].getInt("count");
        qDebug() << "FactorRepository::count: Total factors:" << count;
        return count;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorRepository::count: Error:" << e.what();
        return 0;
    }
}

bool FactorRepository::exists(const QString& factorId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!checkDatabaseConnection()) {
        qWarning() << "FactorRepository::exists: Database connection not available";
        return false;
    }
    
    try {
        auto builder = getQueryBuilder();
        if (!builder) {
            qWarning() << "FactorRepository::exists: Failed to get QueryBuilder";
            return false;
        }
        
        // 使用QueryBuilder链式调用检查因子是否存在
        auto query = builder->from("factors")
                             .select("1")
                             .where("factor_id", ConditionType::EQUAL, factorId)
                             .limit(1);
        
        QueryResult result = query.execute();
        
        bool exists = !result.getRows().empty();
        qDebug() << "FactorRepository::exists: Factor" << factorId << (exists ? "exists" : "does not exist");
        return exists;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorRepository::exists: Error:" << e.what();
        return false;
    }
}

bool FactorRepository::initialize()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_initialized) {
        return true;
    }
    
    if (!checkDatabaseConnection()) {
        qWarning() << "FactorRepository::initialize: Database connection not available";
        return false;
    }
    
    try {
        // 创建因子表
        if (!createFactorTable()) {
            qWarning() << "FactorRepository::initialize: Failed to create factor table";
            return false;
        }
        
        // 创建因子标签表
        if (!createFactorTagsTable()) {
            qWarning() << "FactorRepository::initialize: Failed to create factor tags table";
            return false;
        }
        
        // 创建索引
        if (!createIndexes()) {
            qWarning() << "FactorRepository::initialize: Failed to create indexes";
            return false;
        }
        
        m_initialized = true;
        qDebug() << "FactorRepository::initialize: Tables created successfully";
        return true;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorRepository::initialize: Error:" << e.what();
        return false;
    }
}

bool FactorRepository::clearAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!checkDatabaseConnection()) {
        qWarning() << "FactorRepository::clearAll: Database connection not available";
        return false;
    }
    
    try {
        // 开始事务
        if (!m_database->beginTransaction()) {
            qWarning() << "FactorRepository::clearAll: Failed to begin transaction";
            return false;
        }
        
        // 清空因子标签表
        QString deleteTagsSql = "DELETE FROM factor_tags";
        QueryResult tagsResult = m_database->executeQuery(deleteTagsSql, {});
        
        if (tagsResult.getRows().empty()) {
            qWarning() << "FactorRepository::clearAll: Failed to clear factor tags";
            m_database->rollbackTransaction();
            return false;
        }
        
        // 清空因子表
        QString deleteFactorsSql = "DELETE FROM factors";
        QueryResult factorsResult = m_database->executeQuery(deleteFactorsSql, {});
        
        if (factorsResult.getRows().empty()) {
            qWarning() << "FactorRepository::clearAll: Failed to clear factors";
            m_database->rollbackTransaction();
            return false;
        }
        
        // 提交事务
        if (!m_database->commitTransaction()) {
            qWarning() << "FactorRepository::clearAll: Failed to commit transaction";
            m_database->rollbackTransaction();
            return false;
        }
        
        qDebug() << "FactorRepository::clearAll: All factor data cleared";
        return true;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorRepository::clearAll: Error:" << e.what();
        m_database->rollbackTransaction();
        return false;
    }
}

void FactorRepository::setDatabase(std::shared_ptr<QtMySQLDatabase> database)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_database = database;
    m_initialized = false;
    qDebug() << "FactorRepository::setDatabase: Database connection updated";
}

// 私有方法实现

QVariantMap FactorRepository::resultRowToFactorMap(const QueryResultRow& row) const
{
    QVariantMap factor;
    
    factor["factorId"] = row.getString("factor_id");
    factor["factorName"] = row.getString("factor_name");
    factor["displayName"] = row.getString("display_name");
    factor["majorCategory"] = row.getString("major_category");
    factor["subCategory"] = row.getString("sub_category");
    factor["description"] = row.getString("description");
    factor["icValue"] = row.getDouble("ic_value");
    factor["irValue"] = row.getDouble("ir_value");
    factor["validityDays"] = row.getInt("validity_days");
    factor["turnoverRate"] = row.getDouble("turnover_rate");
    factor["isRecommended"] = row.getValueAs<bool>("is_recommended", false);
    factor["isFavorite"] = row.getValueAs<bool>("is_favorite", false);
    factor["status"] = row.getString("status");
    factor["creator"] = row.getString("creator");
    factor["createDate"] = row.getString("create_date");
    
    // 处理group_returns JSON字段
    QString groupReturnsJson = row.getString("group_returns");
    if (!groupReturnsJson.isEmpty() && groupReturnsJson != "[]") {
        QJsonDocument doc = QJsonDocument::fromJson(groupReturnsJson.toUtf8());
        if (doc.isArray()) {
            QJsonArray jsonArray = doc.array();
            QVector<double> groupReturns;
            for (const QJsonValue& value : jsonArray) {
                groupReturns.append(value.toDouble());
            }
            factor["groupReturns"] = QVariant::fromValue(groupReturns);
        }
    }
    
    return factor;
}

bool FactorRepository::saveFactorTags(const QString& factorId, const QStringList& tags)
{
    try {
        // 开始事务
        if (!m_database->beginTransaction()) {
            qWarning() << "FactorRepository::saveFactorTags: Failed to begin transaction";
            return false;
        }
        
        // 插入标签
        for (const QString& tag : tags) {
            QString sql = "INSERT INTO factor_tags (factor_id, tag) VALUES (?, ?) "
                          "ON DUPLICATE KEY UPDATE tag = VALUES(tag)";
            
            std::map<QString, QVariant> params;
            params["factor_id"] = factorId;
            params["tag"] = tag.trimmed();
            
            QueryResult result = m_database->executeQuery(sql, params);
            if (result.getRows().empty()) {
                qWarning() << "FactorRepository::saveFactorTags: Failed to save tag:" << tag;
                m_database->rollbackTransaction();
                return false;
            }
        }
        
        // 提交事务
        if (!m_database->commitTransaction()) {
            qWarning() << "FactorRepository::saveFactorTags: Failed to commit transaction";
            m_database->rollbackTransaction();
            return false;
        }
        
        qDebug() << "FactorRepository::saveFactorTags: Saved" << tags.size() << "tags for factor:" << factorId;
        return true;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorRepository::saveFactorTags: Error:" << e.what();
        m_database->rollbackTransaction();
        return false;
    }
}

QStringList FactorRepository::loadFactorTags(const QString& factorId)
{
    try {
        auto builder = getQueryBuilder();
        if (!builder) {
            qWarning() << "FactorRepository::loadFactorTags: Failed to get QueryBuilder";
            return {};
        }
        
        auto query = builder->from("factor_tags")
                             .select("tag")
                             .where("factor_id", ConditionType::EQUAL, factorId)
                             .orderBy("tag", OrderType::ASC);
        
        QueryResult result = query.execute();
        
        QStringList tags;
        for (const auto& row : result.getRows()) {
            tags.append(row.getString("tag"));
        }
        
        qDebug() << "FactorRepository::loadFactorTags: Loaded" << tags.size() << "tags for factor:" << factorId;
        return tags;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorRepository::loadFactorTags: Error:" << e.what();
        return {};
    }
}

bool FactorRepository::deleteFactorTags(const QString& factorId)
{
    try {
        auto builder = getQueryBuilder();
        if (!builder) {
            qWarning() << "FactorRepository::deleteFactorTags: Failed to get QueryBuilder";
            return false;
        }
        
        auto query = builder->from("factor_tags")
                             .where("factor_id", ConditionType::EQUAL, factorId);
        
        int deletedCount = query.executeDelete();
        
        qDebug() << "FactorRepository::deleteFactorTags: Deleted" << deletedCount << "tags for factor:" << factorId;
        return deletedCount >= 0;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorRepository::deleteFactorTags: Error:" << e.what();
        return false;
    }
}

std::shared_ptr<QueryBuilder> FactorRepository::getQueryBuilder()
{
    // 使用DatabaseService单例的链式调用功能
    auto& dbService = DatabaseService::instance();
    
        // 检查DatabaseService是否已连接，如果没有连接则尝试初始化
        if (!dbService.isConnected()) {
            qDebug() << "FactorRepository::getQueryBuilder: DatabaseService is not connected, attempting to initialize...";
            
            // 尝试初始化DatabaseService - 禁用连接池以避免"device or resource busy"错误
            DatabaseConfig config;
            config.host = "localhost";
            config.port = 3306;
            config.database = "astock_quant";
            config.username = "root";
            config.password = "123456a";
            config.charset = "utf8mb4";
            config.pool_size = 1; // 设置为1，实际上禁用连接池
            config.connect_timeout = std::chrono::seconds(30); // 增加连接超时时间
            
            if (!dbService.initialize(config, false)) { // 第二个参数为false，禁用连接池
                qWarning() << "FactorRepository::getQueryBuilder: Failed to initialize DatabaseService";
                return nullptr;
            }
            
            qDebug() << "FactorRepository::getQueryBuilder: DatabaseService initialized successfully";
        }
    
    // 获取QueryBuilder
    auto builder = dbService.createQueryBuilder();
    if (!builder) {
        qWarning() << "FactorRepository::getQueryBuilder: Failed to create QueryBuilder";
        
        // 尝试使用m_database（如果可用）
        if (m_database && m_database->isOpen()) {
            qDebug() << "FactorRepository::getQueryBuilder: Using m_database instead";
            return ::astock::database::createQueryBuilder(m_database);
        }
        
        return nullptr;
    }
    
    return builder;
}

bool FactorRepository::checkDatabaseConnection()
{
    if (!m_database) {
        return initializeDatabase();
    }
    
    return m_database->isOpen();
}

bool FactorRepository::initializeDatabase()
{
    if (m_database && m_database->isOpen()) {
        return true;
    }
    
    try {
        // 使用DatabaseService单例类
        auto& dbService = DatabaseService::instance();
        
        // 检查DatabaseService是否已初始化
        if (!dbService.isConnected()) {
            // 从配置文件读取数据库配置
            DatabaseConfig config;
            config.host = "localhost";
            config.port = 3306;
            config.database = "astock_quant";
            config.username = "root";
            config.password = "123456a";
            config.charset = "utf8mb4";
            config.pool_size = 1; // 设置为1，实际上禁用连接池
            
            // 初始化DatabaseService - 禁用连接池以避免"device or resource busy"错误
            if (!dbService.initialize(config, false)) {
                qCritical() << "FactorRepository::initializeDatabase: DatabaseService初始化失败";
                return false;
            }
        }

        // 从DatabaseService获取数据库连接
        m_database = dbService.getDatabase();

        if (!m_database || !m_database->isOpen()) {
            qCritical() << "FactorRepository::initializeDatabase: 无法获取有效的数据库连接";
            return false;
        }

        qDebug() << "FactorRepository::initializeDatabase: 使用DatabaseService单例建立数据库连接";
        return true;

    } catch (const std::exception& e) {
        qCritical() << "FactorRepository::initializeDatabase: 数据库初始化失败:" << e.what();
        return false;
    }
}

bool FactorRepository::createFactorTable()
{
    try {
        // 检查表是否已存在
        if (m_database->tableExists("factors")) {
            qDebug() << "FactorRepository::createFactorTable: Table 'factors' already exists";
            return true;
        }
        
        // 创建因子表
        QString sql = R"(
            CREATE TABLE factors (
                factor_id VARCHAR(100) NOT NULL PRIMARY KEY,
                factor_name VARCHAR(200) NOT NULL,
                display_name VARCHAR(200) NOT NULL,
                major_category VARCHAR(100) NOT NULL,
                sub_category VARCHAR(100) NOT NULL,
                description TEXT,
                ic_value DOUBLE DEFAULT 0.0,
                ir_value DOUBLE DEFAULT 0.0,
                validity_days INT DEFAULT 30,
                turnover_rate DOUBLE DEFAULT 0.0,
                is_recommended BOOLEAN DEFAULT FALSE,
                is_favorite BOOLEAN DEFAULT FALSE,
                status VARCHAR(50) DEFAULT 'active',
                creator VARCHAR(100) DEFAULT 'system',
                create_date DATETIME NOT NULL,
                group_returns JSON,
                INDEX idx_category (major_category, sub_category),
                INDEX idx_status (status),
                INDEX idx_create_date (create_date DESC),
                INDEX idx_recommended (is_recommended),
                INDEX idx_favorite (is_favorite)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        )";
        
        m_database->executeUpdate(sql, {});
        qDebug() << "FactorRepository::createFactorTable: Table 'factors' created successfully";
        return true;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorRepository::createFactorTable: Error:" << e.what();
        return false;
    }
}

bool FactorRepository::createFactorTagsTable()
{
    try {
        // 检查表是否已存在
        if (m_database->tableExists("factor_tags")) {
            qDebug() << "FactorRepository::createFactorTagsTable: Table 'factor_tags' already exists";
            return true;
        }
        
        // 创建因子标签表
        QString sql = R"(
            CREATE TABLE factor_tags (
                id INT AUTO_INCREMENT PRIMARY KEY,
                factor_id VARCHAR(100) NOT NULL,
                tag VARCHAR(100) NOT NULL,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                UNIQUE KEY uk_factor_tag (factor_id, tag),
                INDEX idx_factor_id (factor_id),
                INDEX idx_tag (tag),
                FOREIGN KEY (factor_id) REFERENCES factors(factor_id) ON DELETE CASCADE
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        )";
        
        m_database->executeUpdate(sql, {});
        qDebug() << "FactorRepository::createFactorTagsTable: Table 'factor_tags' created successfully";
        return true;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorRepository::createFactorTagsTable: Error:" << e.what();
        return false;
    }
}

bool FactorRepository::createIndexes()
{
    try {
        // 创建额外的索引
        QStringList indexQueries = {
            "CREATE INDEX IF NOT EXISTS idx_factor_name ON factors(factor_name)",
            "CREATE INDEX IF NOT EXISTS idx_display_name ON factors(display_name)",
            "CREATE INDEX IF NOT EXISTS idx_ic_value ON factors(ic_value DESC)",
            "CREATE INDEX IF NOT EXISTS idx_ir_value ON factors(ir_value DESC)",
            "CREATE INDEX IF NOT EXISTS idx_turnover_rate ON factors(turnover_rate DESC)",
            "CREATE INDEX IF NOT EXISTS idx_validity_days ON factors(validity_days DESC)"
        };
        
        for (const QString& sql : indexQueries) {
            try {
                m_database->executeUpdate(sql, {});
            } catch (const std::exception& e) {
                qWarning() << "FactorRepository::createIndexes: Failed to create index:" << e.what();
                // 继续创建其他索引
            }
        }
        
        qDebug() << "FactorRepository::createIndexes: Indexes created successfully";
        return true;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorRepository::createIndexes: Error:" << e.what();
        return false;
    }
}
