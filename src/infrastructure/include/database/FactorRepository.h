#ifndef ASTOCK_INFRASTRUCTURE_DATABASE_FACTORREPOSITORY_H
#define ASTOCK_INFRASTRUCTURE_DATABASE_FACTORREPOSITORY_H

#include "IRepository.h"
#include "database/ConnectionPool.h"
#include <QString>
#include <QVariantMap>
#include <QMutex>
#include <QSqlDatabase>
#include <vector>
#include <memory>

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
    
    virtual QVariantMap findById(const QString& factorId) = 0;
    virtual std::vector<QVariantMap> findAll() = 0;
    virtual std::vector<QVariantMap> findByType(const QString& type) = 0;
    virtual std::vector<QVariantMap> findByCategory(const QString& category) = 0;
    virtual std::vector<QVariantMap> findByTags(const QStringList& tags) = 0;
    virtual std::vector<QVariantMap> search(const QString& keyword) = 0;
    virtual bool save(const QVariantMap& factor) = 0;
    virtual size_t saveBatch(const std::vector<QVariantMap>& factors) = 0;
    virtual bool update(const QString& factorId, const QVariantMap& factor) = 0;
    virtual bool remove(const QString& factorId) = 0;
    virtual size_t count() = 0;
    virtual bool exists(const QString& factorId) = 0;
    virtual bool initialize() = 0;
    virtual bool clearAll() = 0;
};

/**
 * @brief 因子仓储实现
 * 
 * 使用ConnectionPool连接池管理数据库连接，RAII模式自动释放
 */
class FactorRepository : public IFactorRepository {
public:
    explicit FactorRepository();
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
    
private:
    /**
     * @brief RAII 连接管理类
     * 
     * 自动管理数据库连接的获取和释放，确保异常安全
     * 无论函数如何退出（正常返回、异常、提前返回），连接都会被自动释放
     */
    class ScopedConnection {
    public:
        ScopedConnection() 
            : m_db(ConnectionPool::instance().getConnection())
            , m_released(false) 
        {
        }
        
        ~ScopedConnection() {
            release();
        }
        
        /**
         * @brief 获取数据库连接引用
         */
        QSqlDatabase& get() { return m_db; }
        
        /**
         * @brief 检查连接是否有效
         */
        bool isValid() const { return m_db.isValid() && m_db.isOpen(); }
        
        /**
         * @brief 手动释放连接（可选，析构时会自动释放）
         */
        void release() {
            if (!m_released && m_db.isValid()) {
                ConnectionPool::instance().releaseConnection(m_db);
                m_released = true;
            }
        }
        
        // 禁止拷贝和移动
        ScopedConnection(const ScopedConnection&) = delete;
        ScopedConnection& operator=(const ScopedConnection&) = delete;
        ScopedConnection(ScopedConnection&&) = delete;
        ScopedConnection& operator=(ScopedConnection&&) = delete;
        
    private:
        QSqlDatabase m_db;
        bool m_released;
    };
    
    // 辅助方法 - 公共方法使用
    QVariantMap rowToFactorMap(const QSqlQuery& query);
    QStringList loadFactorTags(const QString& factorId, QSqlDatabase& db);
    bool saveFactorTags(const QString& factorId, const QStringList& tags, QSqlDatabase& db);
    bool deleteFactorTags(const QString& factorId, QSqlDatabase& db);
    
    /**
     * @brief 内部保存方法，使用传入的连接
     * 
     * 被 save() 和 saveBatch() 复用，确保事务中使用同一连接
     */
    bool saveFactorInternal(const QVariantMap& factor, QSqlDatabase& db);
    
private:
    bool m_initialized;
    QMutex m_initMutex;
};

} // namespace database
} // namespace astock

#endif // ASTOCK_INFRASTRUCTURE_DATABASE_FACTORREPOSITORY_H
