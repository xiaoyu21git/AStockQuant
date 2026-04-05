// FactorService.h
// 因子服务层 - 负责业务逻辑：数据库操作、缓存、因子管理
#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QMutex>
#include <QReadWriteLock>
#include <memory>
#include <atomic>

namespace astock {
namespace database {
    class IFactorRepository;
    class QtMySQLDatabase;
}
}

namespace factor {
class DataAvailabilityChecker;
class FactorCacheManager;
class FactorInstanceManager;
}

class FactorViewModel;

class FactorService : public QObject {
    Q_OBJECT
    
public:
    // 单例访问
    static FactorService* instance();
    
    // 禁止拷贝
    FactorService(const FactorService&) = delete;
    FactorService& operator=(const FactorService&) = delete;
    
    // 初始化方法
    Q_INVOKABLE void initialize();
    
    // 因子管理方法
    Q_INVOKABLE QString addFactor(const QVariantMap& factorData);
    Q_INVOKABLE bool updateFactor(const QString& factorId, const QVariantMap& factorData);
    Q_INVOKABLE bool deleteFactor(const QString& factorId);
    Q_INVOKABLE QVariantMap getFactorById(const QString& factorId);
    Q_INVOKABLE QVariantList getAllFactors();
    Q_INVOKABLE QVariantList searchFactors(const QString& keyword);
    Q_INVOKABLE QVariantList filterFactorsByCategory(const QString& category);
    Q_INVOKABLE QVariantList filterFactorsByTags(const QStringList& tags);
    
    // 因子值获取方法（用于回测）
    Q_INVOKABLE QVariantMap getFactorValues(const QString& factorId, const QString& date);
    Q_INVOKABLE QVariantMap getFactorValuesBatch(const QString& factorId, const QStringList& dates);
    Q_INVOKABLE QString getLatestAvailableTradeDate();
    
    // 属性访问器
    bool isInitialized() const { return m_initialized.load(); }
    bool isLoading() const { return m_isLoading.load(); }
    bool isCacheLoaded() const { return m_cacheLoaded.load(); }
    
    // 获取视图模型 - 标记为Q_INVOKABLE以便QML调用
    Q_INVOKABLE FactorViewModel* getViewModel() { return m_viewModel; }
    
    // 设置视图模型 - 由QML传递FactorViewModel实例
    Q_INVOKABLE void setViewModel(FactorViewModel* viewModel) { 
        m_viewModel = viewModel; 
        qDebug() << "FactorService: 设置视图模型，地址:" << m_viewModel;
    }
    
signals:
    // 业务操作信号
    void factorAdded(const QString& factorId, const QVariantMap& factorData);
    void factorUpdated(const QString& factorId, const QVariantMap& factorData);
    void factorDeleted(const QString& factorId);
    void factorsLoaded(const QVariantList& factors);
    
    // 数据变更信号 - 通知视图层更新
    void dataChanged();
    
    // 属性变化信号
    void initializedChanged();
    void isLoadingChanged();
    void cacheLoadedChanged();
    
private:
    // 私有构造函数
    explicit FactorService(QObject* parent = nullptr);
    ~FactorService();
    
    // 初始化仓储
    void initializeRepository();
    
    // 数据库操作方法
    bool saveFactorToDatabase(const QVariantMap& factorData);
    bool updateFactorInDatabase(const QString& factorId, const QVariantMap& factorData);
    bool deleteFactorFromDatabase(const QString& factorId);
    QVariantList loadFactorsFromDatabase();
    
    // 缓存操作方法
    void saveFactorToCache(const QString& factorId, const QVariantMap& factorData);
    QVariantMap loadFactorFromCache(const QString& factorId);
    void removeFactorFromCache(const QString& factorId);
    void clearAllCache();
    
    // 数据验证
    bool validateFactorData(const QVariantMap& factorData, QString& errorMessage);
    
    // 生成因子ID
    QString generateFactorId(const QString& factorName);
    
    // 查询数据库数据（私有辅助方法）
    QVariantList queryDatabaseData(const QString& minDate, const QString& maxDate);

    // 新 domain/factor 接口适配
    bool initializeFactorDomainRuntime();
    QString resolveDomainInstanceId(const QString& factorId) const;
    QString determineDomainInstanceId(const QVariantMap& factorData) const;
    QString resolveRepositoryFactorId(const QString& factorId) const;
    QVariantMap getFactorDefinitionFromDomain(const QString& factorId) const;
    QVariantList getAllFactorDefinitionsFromDomain() const;
    bool syncFactorDefinitionToDomain(const QVariantMap& factorData);
    bool verifyDomainInstanceReady(const QString& instanceId, QString* errorMessage = nullptr);
    bool removeFactorDefinitionFromDomain(const QString& factorId);
    QVariantMap getFactorValuesFromDomain(const QString& factorId,
                                          const QString& resolvedInstanceId,
                                          const QString& date);
    QVariantMap getFactorValuesBatchFromDomain(const QString& factorId,
                                               const QString& resolvedInstanceId,
                                               const QStringList& dates);
    
    // 新增辅助方法：数据字段提取和调试
    QString extractDateFromDataMap(const QVariantMap& dataMap, int itemIndex);
    QString extractSymbolFromDataMap(const QVariantMap& dataMap, int itemIndex);
    double extractClosePriceFromDataMap(const QVariantMap& dataMap, int itemIndex);
    void logDataExtractionDebugInfo(const QVariantMap& dataMap, int itemIndex, 
                                   const QString& extractedDate, 
                                   const QString& extractedSymbol, 
                                   double extractedClosePrice);
    
private:
    // 单例实例
    static FactorService* m_instance;
    static QMutex m_instanceMutex;
    
    std::shared_ptr<astock::database::IFactorRepository> m_repository;
    std::shared_ptr<astock::database::QtMySQLDatabase> m_database;
    std::shared_ptr<factor::DataAvailabilityChecker> m_dataChecker;
    std::shared_ptr<factor::FactorCacheManager> m_factorCacheManager;
    std::shared_ptr<factor::FactorInstanceManager> m_factorInstanceManager;
    
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
    
    // 视图模型 - 使用原始指针，由QML管理生命周期
    FactorViewModel* m_viewModel;
};
