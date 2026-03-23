#ifndef ASTOCK_INFRASTRUCTURE_DATABASE_STRATEGYREPOSITORY_H
#define ASTOCK_INFRASTRUCTURE_DATABASE_STRATEGYREPOSITORY_H

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
 * @brief 策略仓储接口
 * 
 * 提供策略数据的持久化存储和检索
 */
class IStrategyRepository {
public:
    virtual ~IStrategyRepository() = default;
    
    virtual QVariantMap findById(const QString& strategyId) = 0;
    virtual QVariantMap findByCode(const QString& strategyCode) = 0;
    virtual std::vector<QVariantMap> findAll() = 0;
    virtual std::vector<QVariantMap> findByType(const QString& strategyType) = 0;
    virtual std::vector<QVariantMap> findByStatus(const QString& status) = 0;
    virtual std::vector<QVariantMap> search(const QString& keyword) = 0;
    virtual QString save(const QVariantMap& strategy) = 0;
    virtual bool update(const QString& strategyId, const QVariantMap& strategy) = 0;
    virtual bool remove(const QString& strategyId) = 0;
    virtual size_t count() = 0;
    virtual bool exists(const QString& strategyId) = 0;
    virtual bool existsByCode(const QString& strategyCode) = 0;
    virtual bool initialize() = 0;
    virtual bool clearAll() = 0;
};

/**
 * @brief 策略仓储实现
 * 
 * 使用ConnectionPool连接池管理数据库连接，RAII模式自动释放
 */
class StrategyRepository : public IStrategyRepository {
public:
    explicit StrategyRepository();
    virtual ~StrategyRepository();
    
    // IStrategyRepository接口实现
    QVariantMap findById(const QString& strategyId) override;
    QVariantMap findByCode(const QString& strategyCode) override;
    std::vector<QVariantMap> findAll() override;
    std::vector<QVariantMap> findByType(const QString& strategyType) override;
    std::vector<QVariantMap> findByStatus(const QString& status) override;
    std::vector<QVariantMap> search(const QString& keyword) override;
    QString save(const QVariantMap& strategy) override;
    bool update(const QString& strategyId, const QVariantMap& strategy) override;
    bool remove(const QString& strategyId) override;
    size_t count() override;
    bool exists(const QString& strategyId) override;
    bool existsByCode(const QString& strategyCode) override;
    bool initialize() override;
    bool clearAll() override;
    
    // 扩展方法
    virtual bool updateStatus(const QString& strategyId, const QString& status);
    virtual bool updateParameters(const QString& strategyId, const QVariantMap& parameters);
    virtual bool updatePerformance(const QString& strategyId, const QVariantMap& performance);
    virtual std::vector<QVariantMap> findActiveStrategies();
    virtual std::vector<QVariantMap> findDraftStrategies();
    
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
    
    // 辅助方法
    QVariantMap rowToStrategyMap(const QSqlQuery& query);
    QVariantMap loadStrategyParameters(const QString& strategyId, QSqlDatabase& db);
    bool saveStrategyParameters(const QString& strategyId, const QVariantMap& parameters, QSqlDatabase& db);
    bool deleteStrategyParameters(const QString& strategyId, QSqlDatabase& db);
    
    /**
     * @brief 内部保存方法，使用传入的连接
     * 
     * 被 save() 和 update() 复用，确保事务中使用同一连接
     * @return 保存成功的策略ID，失败返回空字符串
     */
    QString saveStrategyInternal(const QVariantMap& strategy, QSqlDatabase& db, bool isUpdate = false);
    
    /**
     * @brief 验证策略数据
     */
    bool validateStrategy(const QVariantMap& strategy) const;
    
    /**
     * @brief 生成策略代码（如果未提供）
     */
    QString generateStrategyCode(const QVariantMap& strategy) const;
    
private:
    bool m_initialized;
    QMutex m_initMutex;
};

} // namespace database
} // namespace astock

#endif // ASTOCK_INFRASTRUCTURE_DATABASE_STRATEGYREPOSITORY_H