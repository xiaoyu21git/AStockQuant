#ifndef ASTOCK_INFRASTRUCTURE_DATABASE_STRATEGYREPOSITORY_H
#define ASTOCK_INFRASTRUCTURE_DATABASE_STRATEGYREPOSITORY_H

#include "database/ConnectionPool.h"
#include "../../../ui/bridge/include/StrategyLifecycleStatus.h"
#include "../../../domain/backtest/include/ResolvedStrategyBehavior.h"
#include <QDateTime>
#include <QString>
#include <QVariantMap>
#include <QMutex>
#include <QSqlDatabase>
#include <vector>
#include <memory>
#include <optional>

namespace astock {
namespace database {

enum class StrategyLanguageCode {
    Python = 0,
    Cpp,
    Julia,
    R,
};

struct StrategyRuntimeProperties {
    int assetTypeIndex{0};
    int timeFrameIndex{0};
    int riskLevelIndex{0};

    bool hasAny() const
    {
        return assetTypeIndex > 0 || timeFrameIndex > 0 || riskLevelIndex > 0;
    }
};

struct StrategyData {
    QString strategyId;
    qulonglong engineStrategyId{0};
    QString strategyCode;
    QString strategyName;
    domain::backtest::ResolvedStrategyIdentity strategyIdentity;
    QString description;
    QString version;
    QString author;
    StrategyLanguageCode language{StrategyLanguageCode::Python};
    strategy_view::StrategyLifecycleStatus status{strategy_view::StrategyLifecycleStatus::Unknown};
    QDateTime createdAt;
    QDateTime updatedAt;
    QVariantMap parameters;
    QVariantMap performanceMetrics;
    StrategyRuntimeProperties runtime;

    bool isValid() const;
    QVariantMap toVariantMap() const;
    static StrategyData fromVariantMap(const QVariantMap& strategyMap);
};

/**
 * @brief 策略仓储接口
 * 
 * 提供策略数据的持久化存储和检索
 */
class IStrategyRepository {
public:
    virtual ~IStrategyRepository() = default;
    
    virtual std::optional<StrategyData> findById(const QString& strategyId) = 0;
    virtual std::optional<StrategyData> findByCode(const QString& strategyCode) = 0;
    virtual std::vector<StrategyData> findAll() = 0;
    virtual std::vector<StrategyData> findByType(domain::backtest::StrategyStoredType strategyType) = 0;
    virtual std::vector<StrategyData> findByStatus(strategy_view::StrategyLifecycleStatus status) = 0;
    virtual std::vector<StrategyData> search(const QString& keyword) = 0;
    virtual QString save(const StrategyData& strategy) = 0;
    virtual bool update(const QString& strategyId, const StrategyData& strategy) = 0;
    virtual bool updateStatus(const QString& strategyId, strategy_view::StrategyLifecycleStatus status) = 0;
    virtual bool updateParameters(const QString& strategyId, const QVariantMap& parameters) = 0;
    virtual bool updatePerformance(const QString& strategyId, const QVariantMap& performance) = 0;
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
    std::optional<StrategyData> findById(const QString& strategyId) override;
    std::optional<StrategyData> findByCode(const QString& strategyCode) override;
    std::vector<StrategyData> findAll() override;
    std::vector<StrategyData> findByType(domain::backtest::StrategyStoredType strategyType) override;
    std::vector<StrategyData> findByStatus(strategy_view::StrategyLifecycleStatus status) override;
    std::vector<StrategyData> search(const QString& keyword) override;
    QString save(const StrategyData& strategy) override;
    bool update(const QString& strategyId, const StrategyData& strategy) override;
    bool remove(const QString& strategyId) override;
    size_t count() override;
    bool exists(const QString& strategyId) override;
    bool existsByCode(const QString& strategyCode) override;
    bool initialize() override;
    bool clearAll() override;
    
    // 扩展方法
    bool updateStatus(const QString& strategyId, strategy_view::StrategyLifecycleStatus status) override;
    bool updateParameters(const QString& strategyId, const QVariantMap& parameters) override;
    bool updatePerformance(const QString& strategyId, const QVariantMap& performance) override;
    virtual std::vector<StrategyData> findActiveStrategies();
    virtual std::vector<StrategyData> findDraftStrategies();
    
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
    StrategyData rowToStrategyData(const QSqlQuery& query);
    QVariantMap loadStrategyParameters(const QString& strategyId, QSqlDatabase& db);
    bool saveStrategyParameters(const QString& strategyId, const QVariantMap& parameters, QSqlDatabase& db);
    bool deleteStrategyParameters(const QString& strategyId, QSqlDatabase& db);
    
    /**
     * @brief 内部保存方法，使用传入的连接
     * 
     * 被 save() 和 update() 复用，确保事务中使用同一连接
     * @return 保存成功的策略ID，失败返回空字符串
     */
    QString saveStrategyInternal(const StrategyData& strategy, QSqlDatabase& db, bool isUpdate = false);
    
    /**
     * @brief 验证策略数据
     */
    bool validateStrategy(const StrategyData& strategy) const;
    
    /**
     * @brief 生成策略代码（如果未提供）
     */
    QString generateStrategyCode(const StrategyData& strategy) const;
    
private:
    bool m_initialized;
    QMutex m_initMutex;
};

} // namespace database
} // namespace astock

#endif // ASTOCK_INFRASTRUCTURE_DATABASE_STRATEGYREPOSITORY_H