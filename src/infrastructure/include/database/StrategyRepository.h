#ifndef ASTOCK_INFRASTRUCTURE_DATABASE_STRATEGYREPOSITORY_H
#define ASTOCK_INFRASTRUCTURE_DATABASE_STRATEGYREPOSITORY_H

#include "database/NativeMySQLConnectionPool.h"
#include "../../../ui/bridge/include/StrategyLifecycleStatus.h"
#include "../../../domain/types/ResolvedStrategyBehavior.h"
#include "../../../domain/strategies/include/StrategyDefinitionTypes.h"
#include <QDateTime>
#include <QString>
#include <QVariantMap>
#include <QMutex>

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

struct PersistedStrategyData {
    std::string strategyId;
    qulonglong engineStrategyId{0};
    std::string strategyCode;
    domain::strategies::StrategyMetadata metadata;
    domain::backtest::ResolvedStrategyIdentity strategyIdentity;
    int strategyTypeIndex{0};
    std::string version;
    std::string author;
    StrategyLanguageCode language{StrategyLanguageCode::Python};
    strategy_view::StrategyLifecycleStatus status{strategy_view::StrategyLifecycleStatus::Unknown};
    QDateTime createdAt;
    QDateTime updatedAt;
    QVariantMap parameters;
    QVariantMap performanceMetrics;
    StrategyRuntimeProperties runtime;

    bool isValid() const;
    QVariantMap toVariantMap() const;
    static PersistedStrategyData fromVariantMap(const QVariantMap& strategyMap);
};

/**
 * @brief 策略仓储接口
 * 
 * 提供策略数据的持久化存储和检索
 */
class IStrategyRepository {
public:
    virtual ~IStrategyRepository() = default;
    
    virtual std::optional<PersistedStrategyData> findById(const QString& strategyId) = 0;
    virtual std::optional<PersistedStrategyData> findByCode(const QString& strategyCode) = 0;
    virtual std::vector<PersistedStrategyData> findAll() = 0;
    virtual std::vector<PersistedStrategyData> findByType(domain::backtest::StrategyStoredType strategyType) = 0;
    virtual std::vector<PersistedStrategyData> findByStatus(strategy_view::StrategyLifecycleStatus status) = 0;
    virtual std::vector<PersistedStrategyData> search(const QString& keyword) = 0;
    virtual QString save(const PersistedStrategyData& strategy) = 0;
    virtual bool update(const QString& strategyId, const PersistedStrategyData& strategy) = 0;
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
    std::optional<PersistedStrategyData> findById(const QString& strategyId) override;
    std::optional<PersistedStrategyData> findByCode(const QString& strategyCode) override;
    std::vector<PersistedStrategyData> findAll() override;
    std::vector<PersistedStrategyData> findByType(domain::backtest::StrategyStoredType strategyType) override;
    std::vector<PersistedStrategyData> findByStatus(strategy_view::StrategyLifecycleStatus status) override;
    std::vector<PersistedStrategyData> search(const QString& keyword) override;
    QString save(const PersistedStrategyData& strategy) override;
    bool update(const QString& strategyId, const PersistedStrategyData& strategy) override;
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
    virtual std::vector<PersistedStrategyData> findActiveStrategies();
    virtual std::vector<PersistedStrategyData> findDraftStrategies();
    
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
            : m_db(NativeMySQLConnectionPool::instance().getConnection())
        {
        }
        ~ScopedConnection() = default;
        std::shared_ptr<ISqlDatabase>& db() { return m_db; }
        bool isValid() const { return m_db && m_db->isOpen(); }
        void release() { m_db.reset(); }
        SqlQueryResult exec(const std::string& sql, const std::vector<SqlParam>& p={}) { return m_db?m_db->executeQuery(sql,p):SqlQueryResult{}; }
        int execUpdate(const std::string& sql, const std::vector<SqlParam>& p={}) { return m_db?m_db->executeUpdate(sql,p):0; }
        bool beginTx() { return m_db?m_db->beginTransaction():false; }
        bool commitTx() { return m_db?m_db->commitTransaction():false; }

        ScopedConnection(const ScopedConnection&) = delete;
        ScopedConnection& operator=(const ScopedConnection&) = delete;
        ScopedConnection(ScopedConnection&&) = delete;
        ScopedConnection& operator=(ScopedConnection&&) = delete;
    private:
        std::shared_ptr<ISqlDatabase> m_db;
    };
    
    // 辅助方法
    PersistedStrategyData rowToStrategyData(const SqlQueryResultRow& row);
    QVariantMap loadStrategyParameters(const QString& strategyId, std::shared_ptr<ISqlDatabase>& db);
    
    /**
     * @brief 内部保存方法，使用传入的连接
     * 
     * 被 save() 和 update() 复用，确保事务中使用同一连接
     * @return 保存成功的策略ID，失败返回空字符串
     */
    QString saveStrategyInternal(const PersistedStrategyData& strategy, std::shared_ptr<ISqlDatabase>& db, bool isUpdate = false);
    
    /**
     * @brief 生成策略代码（如果未提供）
     */
    QString generateStrategyCode(const PersistedStrategyData& strategy) const;
    
private:
    bool m_initialized;
    QMutex m_initMutex;
};

} // namespace database
} // namespace astock

#endif // ASTOCK_INFRASTRUCTURE_DATABASE_STRATEGYREPOSITORY_H