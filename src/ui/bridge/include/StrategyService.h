// StrategyService.h
// 策略服务层 - 负责业务逻辑：数据库操作、缓存、策略管理
#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QMutex>
#include <QReadWriteLock>
#include <QHash>
#include <memory>
#include <atomic>

#include "foundation/Utils/Uuid.h"

namespace astock {
namespace database {
    class IStrategyRepository;
}
}

namespace engine {
struct EventFormat;
class EventBus;
}

class StrategyViewModel;

class StrategyService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isInitialized READ isInitialized NOTIFY initializedChanged)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
    Q_PROPERTY(bool isCacheLoaded READ isCacheLoaded NOTIFY cacheLoadedChanged)
    
public:
    // 单例访问
    static StrategyService* instance();
    
    // 禁止拷贝
    StrategyService(const StrategyService&) = delete;
    StrategyService& operator=(const StrategyService&) = delete;
    
    // 初始化方法
    Q_INVOKABLE void initialize();
    
    // 策略管理方法
    Q_INVOKABLE QString createStrategy(const QVariantMap& strategyData);
    Q_INVOKABLE bool updateStrategy(const QString& strategyId, const QVariantMap& strategyData);
    Q_INVOKABLE bool deleteStrategy(const QString& strategyId);
    Q_INVOKABLE QVariantMap getStrategyById(const QString& strategyId);
    Q_INVOKABLE QVariantMap getStrategyByCode(const QString& strategyCode);
    Q_INVOKABLE QVariantList getAllStrategies();
    Q_INVOKABLE QVariantList getStrategiesByType(const QString& strategyType);
    Q_INVOKABLE QVariantList getStrategiesByStatus(const QString& status);
    
    // 搜索和过滤
    Q_INVOKABLE QVariantList searchStrategies(const QString& keyword);
    
    // 批量操作
    Q_INVOKABLE bool importStrategies(const QVariantList& strategies);
    Q_INVOKABLE bool exportStrategies(const QString& format, const QString& filePath);
    
    // 策略状态管理
    Q_INVOKABLE bool activateStrategy(const QString& strategyId);
    Q_INVOKABLE bool deactivateStrategy(const QString& strategyId);
    Q_INVOKABLE bool archiveStrategy(const QString& strategyId);
    Q_INVOKABLE bool duplicateStrategy(const QString& sourceStrategyId, const QString& newName);
    
    // 参数管理
    Q_INVOKABLE bool updateStrategyParameters(const QString& strategyId, const QVariantMap& parameters);
    Q_INVOKABLE QVariantMap getStrategyParameters(const QString& strategyId);
    
    // 性能指标管理
    Q_INVOKABLE bool updateStrategyPerformance(const QString& strategyId, const QVariantMap& performance);
    Q_INVOKABLE QVariantMap getStrategyPerformance(const QString& strategyId);
    
    // 数据同步
    Q_INVOKABLE void syncWithDatabase();
    Q_INVOKABLE void clearCache();
    
    // 属性访问器
    Q_INVOKABLE bool isInitialized() const { return m_initialized.load(); }
    Q_INVOKABLE bool isLoading() const { return m_isLoading.load(); }
    Q_INVOKABLE bool isCacheLoaded() const { return m_cacheLoaded.load(); }
    
    // 获取视图模型 - 标记为Q_INVOKABLE以便QML调用
    Q_INVOKABLE StrategyViewModel* getViewModel();
    
    // 设置视图模型 - 允许QML替换ViewModel实例
    Q_INVOKABLE void setViewModel(StrategyViewModel* viewModel);
    
    // 策略工厂方法 - 根据类型创建预定义策略
    Q_INVOKABLE QVariantMap createDefaultStrategy(const QString& strategyType, const QString& strategyName = "");
    Q_INVOKABLE QVariantMap getStrategyTemplate(const QString& strategyType);
    Q_INVOKABLE QStringList getAvailableStrategyTypes();
    Q_INVOKABLE QVariantMap getStrategyTypeDescriptions();

    // 事件链路调试入口：允许人工注入一条市场事件
    Q_INVOKABLE bool publishSyntheticMarketEvent(const QVariantMap& marketEvent);
    
signals:
    // 业务操作信号
    void strategyCreated(const QString& strategyId, const QVariantMap& strategyData);
    void strategyUpdated(const QString& strategyId, const QVariantMap& strategyData);
    void strategyDeleted(const QString& strategyId);
    void strategyActivated(const QString& strategyId);
    void strategyDeactivated(const QString& strategyId);
    void strategiesLoaded(const QVariantList& strategies);
    void errorOccurred(const QString& error);
    void strategySignalPublished(const QVariantMap& signalData);
    
    // 导入失败信号 - 返回失败的策略列表
    void importFailed(const QVariantList& failedStrategies);
    
    // 数据变更信号 - 通知视图层更新
    void dataChanged();
    
    // 属性变化信号
    void initializedChanged();
    void isLoadingChanged();
    void cacheLoadedChanged();
    
private:
    // 私有构造函数
    explicit StrategyService(QObject* parent = nullptr);
    ~StrategyService();
    
    // 初始化仓储
    void initializeRepository();
    
    // 数据库操作方法
    QString saveStrategyToDatabase(const QVariantMap& strategyData);
    bool updateStrategyInDatabase(const QString& strategyId, const QVariantMap& strategyData);
    bool deleteStrategyFromDatabase(const QString& strategyId);
    QVariantList loadStrategiesFromDatabase();
    
    // 缓存操作方法
    void saveStrategyToCache(const QString& strategyId, const QVariantMap& strategyData);
    QVariantMap loadStrategyFromCache(const QString& strategyId);
    void removeStrategyFromCache(const QString& strategyId);
    void clearAllCache();
    
    // 批量更新缓存（用于导入操作）
    void updateCacheBatch(const std::vector<QVariantMap>& strategies);
    
    // 数据验证
    bool validateStrategyData(const QVariantMap& strategyData, QString& errorMessage);
    
    // 生成策略ID
    QString generateStrategyId(const QString& strategyName);
    
    // 生成策略代码
    QString generateStrategyCode(const QString& strategyName, const QString& strategyType);
    
    // 创建默认策略数据
    QVariantMap createTrendFollowingStrategy(const QString& name);
    QVariantMap createMeanReversionStrategy(const QString& name);
    QVariantMap createAlphaStrategy(const QString& name);
    QVariantMap createArbitrageStrategy(const QString& name);
    QVariantMap createCustomStrategy(const QString& name, const QString& type);
    void initializeEventBusIntegration();
    void handleMarketEvent(const engine::EventFormat& event, const QString& eventType);
    void publishStrategySignalForMarket(const QVariantMap& strategy,
                                        const QString& symbol,
                                        double latestPrice,
                                        double referencePrice,
                                        const QString& marketEventType,
                                        const QString& eventId);
    QString determineSignalAction(const QVariantMap& strategy,
                                  double latestPrice,
                                  double referencePrice) const;
    double determineSignalStrength(const QVariantMap& strategy,
                                   double latestPrice,
                                   double referencePrice) const;
    
private:
    // 单例实例
    static StrategyService* m_instance;
    static QMutex m_instanceMutex;
    
    std::shared_ptr<astock::database::IStrategyRepository> m_repository;
    
    // 使用读写锁替代互斥锁，提高并发读性能
    mutable QReadWriteLock m_rwLock;
    
    // 初始化专用互斥锁，防止并发初始化
    mutable QMutex m_initMutex;
    
    // 原子标志位
    std::atomic<bool> m_initialized;
    std::atomic<bool> m_isLoading;  // 防止递归加载
    
    // 内存缓存
    QMap<QString, QVariantMap> m_memoryCache;
    
    // 缓存是否已加载标志
    std::atomic<bool> m_cacheLoaded;
    
    // 是否自动初始化标志
    std::atomic<bool> m_autoInitialize;
    std::atomic<bool> m_eventBusIntegrated;
    foundation::utils::Uuid m_marketTickSubscription;
    foundation::utils::Uuid m_marketBarSubscription;
    QHash<QString, double> m_latestMarketPriceBySymbol;
    mutable QMutex m_eventBusMutex;
    
    // 视图模型 - 使用原始指针，由QML管理生命周期
    StrategyViewModel* m_viewModel;
};