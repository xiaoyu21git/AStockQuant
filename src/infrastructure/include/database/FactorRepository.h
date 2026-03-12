#ifndef ASTOCK_INFRASTRUCTURE_DATABASE_FACTORREPOSITORY_H
#define ASTOCK_INFRASTRUCTURE_DATABASE_FACTORREPOSITORY_H

#include "IRepository.h"
#include "database/QueryBuilder.h"
#include "database/QtMySQLDatabase.h"
#include <QString>
#include <QVariantMap>
#include <vector>
#include <memory>
#include <mutex>

namespace astock {
namespace database {

/**
 * @brief 因子仓储接口
 * 
 * 提供因子数据的持久化存储和检索
 */
class IFactorRepository {
public:
    virtual ~IFactorRepository() = default;
    
    /**
     * @brief 根据ID查询因子
     */
    virtual QVariantMap findById(const QString& factorId) = 0;
    
    /**
     * @brief 查询所有因子
     */
    virtual std::vector<QVariantMap> findAll() = 0;
    
    /**
     * @brief 根据类型查询因子
     */
    virtual std::vector<QVariantMap> findByType(const QString& type) = 0;
    
    /**
     * @brief 根据分类查询因子
     */
    virtual std::vector<QVariantMap> findByCategory(const QString& category) = 0;
    
    /**
     * @brief 根据标签查询因子
     */
    virtual std::vector<QVariantMap> findByTags(const QStringList& tags) = 0;
    
    /**
     * @brief 搜索因子
     */
    virtual std::vector<QVariantMap> search(const QString& keyword) = 0;
    
    /**
     * @brief 保存因子
     */
    virtual bool save(const QVariantMap& factor) = 0;
    
    /**
     * @brief 批量保存因子
     */
    virtual size_t saveBatch(const std::vector<QVariantMap>& factors) = 0;
    
    /**
     * @brief 更新因子
     */
    virtual bool update(const QString& factorId, const QVariantMap& factor) = 0;
    
    /**
     * @brief 删除因子
     */
    virtual bool remove(const QString& factorId) = 0;
    
    /**
     * @brief 统计因子数量
     */
    virtual size_t count() = 0;
    
    /**
     * @brief 检查因子是否存在
     */
    virtual bool exists(const QString& factorId) = 0;
    
    /**
     * @brief 初始化数据库表
     */
    virtual bool initialize() = 0;
    
    /**
     * @brief 清空所有因子数据
     */
    virtual bool clearAll() = 0;
};

/**
 * @brief 因子仓储实现
 * 
 * 使用MySQL数据库存储因子数据，支持QueryBuilder链式调用
 */
class FactorRepository : public IFactorRepository {
public:
    FactorRepository(std::shared_ptr<QtMySQLDatabase> database = nullptr);
    virtual ~FactorRepository();
    
    // IFactorRepository接口实现
    QVariantMap findById(const QString& factorId) override;
    std::vector<QVariantMap> findAll() override;
    std::vector<QVariantMap> findByType(const QString& type) override;
    std::vector<QVariantMap> findByCategory(const QString& category) override;
    std::vector<QVariantMap> findByTags(const QStringList& tags) override;
    std::vector<QVariantMap> search(const QString& keyword) override;
    bool save(const QVariantMap& factor) override;
    size_t saveBatch(const std::vector<QVariantMap>& factors) override;
    bool update(const QString& factorId, const QVariantMap& factor) override;
    bool remove(const QString& factorId) override;
    size_t count() override;
    bool exists(const QString& factorId) override;
    bool initialize() override;
    bool clearAll() override;
    
    /**
     * @brief 设置数据库连接
     */
    void setDatabase(std::shared_ptr<QtMySQLDatabase> database);
    
private:
    /**
     * @brief 创建因子表
     */
    bool createFactorTable();
    
    /**
     * @brief 创建因子标签表
     */
    bool createFactorTagsTable();
    
    /**
     * @brief 创建因子索引
     */
    bool createIndexes();
    
    /**
     * @brief 从查询结果转换为QVariantMap
     */
    QVariantMap resultRowToFactorMap(const QueryResultRow& row) const;
    
    /**
     * @brief 保存因子标签
     */
    bool saveFactorTags(const QString& factorId, const QStringList& tags);
    
    /**
     * @brief 加载因子标签
     */
    QStringList loadFactorTags(const QString& factorId);
    
    /**
     * @brief 删除因子标签
     */
    bool deleteFactorTags(const QString& factorId);
    
    /**
     * @brief 获取QueryBuilder实例
     */
    std::shared_ptr<QueryBuilder> getQueryBuilder();
    
    /**
     * @brief 检查数据库连接
     */
    bool checkDatabaseConnection();
    
    /**
     * @brief 初始化数据库连接
     */
    bool initializeDatabase();
    
private:
    std::shared_ptr<QtMySQLDatabase> m_database;
    mutable std::mutex m_mutex;
    bool m_initialized;
};

} // namespace database
} // namespace astock

#endif // ASTOCK_INFRASTRUCTURE_DATABASE_FACTORREPOSITORY_H
