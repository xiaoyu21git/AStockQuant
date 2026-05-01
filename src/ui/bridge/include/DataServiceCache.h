// DataServiceCache.h
// DataService缓存集成模块

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QDateTime>
#include <QMutex>
#include <memory>
#include <QVector>
#include <QMap>
#include <QSet>
#include <functional>

// 缓存门面前向声明
namespace AStockQuantEngine {
namespace Cache {
    class CacheFacade;
}
}

// DataService缓存管理器 - 统一缓存接口
class DataServiceCache : public QObject {
    Q_OBJECT

    friend class DataServiceCacheTestAccess;
    
public:
    static DataServiceCache& getInstance();
    
    // 初始化缓存系统
    bool initializeCache();
    
    // ==== 高级缓存接口 ====
    // 数据查询缓存
    QVariantList getCachedData(const QString& symbol, 
                              const QString& startDate, 
                              const QString& endDate);
    
    void cacheData(const QString& symbol, 
                  const QString& startDate, 
                  const QString& endDate,
                  const QVariantList& data);
    
    // 数据清洗结果缓存
    QVariantList getCachedCleaningResult(const QString& requestId);
    void cacheCleaningResult(const QString& requestId, const QVariantList& data);
    
    // 用户会话缓存
    QVariantMap getCachedUserSession(const QString& sessionId);
    void cacheUserSession(const QString& sessionId, const QVariantMap& sessionData);
    
    // ==== 基础缓存接口 (兼容DataManager) ====
    // 通用数据存储
    void storeData(const QString& key, const QVariantList& data);
    QVariantList getData(const QString& key);
    bool hasData(const QString& key);
    void removeData(const QString& key);
    void clearAllData();
    
    // 股票数据缓存
    void cacheStockData(const QString& symbol, 
                       const QString& startDate, 
                       const QString& endDate, 
                       const QVariantList& data);
    QVariantList getCachedStockData(const QString& symbol, 
                                   const QString& startDate, 
                                   const QString& endDate);
    
    // 工具函数
    static QString generateStockCacheKey(const QString& symbol, 
                                        const QString& startDate, 
                                        const QString& endDate);
    QStringList getAllDataKeys() const;
    QString getStatistics() const;
    
    // 缓存统计
    struct CacheStats {
        uint64_t hits{0};
        uint64_t misses{0};
        uint64_t size{0};
        double hitRate{0.0};
        QString lastError;
    };
    
    // ==== 新接口：基于ID的数据集管理 ====
    // 数据集元数据
    struct DataSetInfo {
        int id{-1};                     // 唯一标识符（负值表示无效）
        QString displayName;            // 显示名称（如"沪深300清洗结果"）
        QString description;            // 描述信息
        QString sourceType;             // 来源类型："cleaning", "query", "import"
        QDateTime createdTime;          // 创建时间
        int rowCount{0};                // 数据行数
        int schemaVersion{1};           // 数据集格式版本
        bool isBacktestReady{false};    // 是否满足最新回测字段要求
        QStringList availableFields;    // 数据集中实际可用字段
        
        // 查询索引字段
        QStringList stockCodes;         // 包含的股票代码列表
        QDate startDate;                // 数据开始日期
        QDate endDate;                  // 数据结束日期
        QStringList tags;               // 标签（如"大盘股", "技术指标"）
        
        // 默认构造函数
        DataSetInfo() = default;
        
        // 构造函数
        DataSetInfo(const QString& name, const QString& desc = "", 
                   const QString& type = "cleaning")
            : displayName(name), description(desc), sourceType(type), createdTime(QDateTime::currentDateTime()) {}
    };
    
    // 数据集查询条件
    struct DataSetQuery {
        QString stockCode;              // 股票代码（可选）
        QDate startDate;                // 开始日期（可选）
        QDate endDate;                  // 结束日期（可选）
        QString sourceType;             // 来源类型（可选）
        QStringList tags;               // 标签过滤（可选）
        QString displayNameFilter;      // 名称过滤（可选）
        
        bool isEmpty() const {
            return stockCode.isEmpty() && !startDate.isValid() && !endDate.isValid() &&
                   sourceType.isEmpty() && tags.isEmpty() && displayNameFilter.isEmpty();
        }
    };
    
    // ==== 基于ID的新接口 ====
    // 存储数据并返回ID（不使用字符串键）
    int storeDataSet(const QVariantList& data,
                    const DataSetInfo& info,
                    const std::function<void(int current, int total)>& progressCallback = {});
    
    // 通过ID获取数据
    QVariantList getDataSetById(int dataId);
    
    // 通过ID获取数据集信息
    DataSetInfo getDataSetInfo(int dataId) const;
    
    // 查询数据集（支持股票代码、日期范围等）
    QVector<DataSetInfo> queryDataSets(const DataSetQuery& query) const;
    
    // 获取所有数据集信息（替代getAllDataKeys）
    QVector<DataSetInfo> getAllDataSetInfos() const;
    
    // 按显示名称查找ID
    int findDataSetId(const QString& displayName) const;
    
    // 通过ID删除数据集
    bool removeDataSetById(int dataId);
    
    // ==== 现有接口（保持向后兼容）====
    CacheStats getStats() const;
    void resetStats();
    
    // 缓存控制
    void clearAllCache();
    void clearDataCache();
    void clearCleaningCache();
    void clearSessionCache();
    
    // 检查缓存是否启用
    bool isCacheEnabled() const;
    
    // 设置缓存TTL
    void setDataCacheTTL(int seconds);
    void setCleaningCacheTTL(int seconds);
    void setSessionCacheTTL(int seconds);
    
signals:
    // 原有信号
    void cacheHit(const QString& key, const QString& type);
    void cacheMiss(const QString& key, const QString& type);
    void cacheError(const QString& error);
    void cacheStatsUpdated(const CacheStats& stats);
    
    // 新增信号
    void dataSetStored(int dataId, const DataSetInfo& info);
    void dataSetRemoved(int dataId);
    void dataSetInfoUpdated(int dataId, const DataSetInfo& info);
    
private:
    DataServiceCache(QObject* parent = nullptr);
    ~DataServiceCache();
    
    // 禁止拷贝和移动
    DataServiceCache(const DataServiceCache&) = delete;
    DataServiceCache& operator=(const DataServiceCache&) = delete;
    DataServiceCache(DataServiceCache&&) = delete;
    DataServiceCache& operator=(DataServiceCache&&) = delete;
    
    // 生成缓存key
    QString generateDataKey(const QString& symbol, 
                           const QString& startDate, 
                           const QString& endDate) const;
    
    QString generateCleaningKey(const QString& requestId) const;
    QString generateSessionKey(const QString& sessionId) const;
    
    // 生成数据集ID的缓存key
    QString generateDataSetKey(int dataId) const;
    
    // 生成数据集信息的缓存key
    QString generateDataSetInfoKey(int dataId) const;

    bool ensureDataSetDateRange(int dataId, DataSetInfo& info) const;

    // 数据集目录缓存key
    QString generateDataSetCatalogKey() const;

    // 数据集磁盘持久化路径
    QString persistentDataSetRootDir() const;
    QString persistentDataSetDataFilePath(int dataId) const;
    QString persistentDataSetInfoFilePath(int dataId) const;
    QString persistentDataSetCatalogFilePath() const;
    bool ensurePersistentDataSetRootDir() const;
    bool writePersistentCacheFile(const QString& filePath, const QByteArray& data) const;
    QByteArray readPersistentCacheFile(const QString& filePath) const;
    void removePersistentDataSetFiles(int dataId) const;
    void clearPersistentDataSetFiles() const;
    
    // 数据序列化/反序列化
    QByteArray serializeData(const QVariantList& data,
                             const std::function<void(int current, int total)>& progressCallback = {}) const;
    QVariantList deserializeData(const QByteArray& data) const;
    
    QByteArray serializeMap(const QVariantMap& map) const;
    QVariantMap deserializeMap(const QByteArray& data) const;
    
    QByteArray serializeDataSetInfo(const DataSetInfo& info) const;
    DataSetInfo deserializeDataSetInfo(const QByteArray& data) const;

    void persistDataSetCatalog(const QVector<int>& dataSetIds, int nextDataSetId) const;
    bool loadDataSetCatalog(QVector<int>& dataSetIds, int& nextDataSetId) const;
    
    // 内部索引管理
    void rebuildIndexIfNeeded() const;
    void addToIndex(int dataId, const DataSetInfo& info);
    void removeFromIndex(int dataId);
    void updateIndex(int dataId, const DataSetInfo& info);
    
    // 查询辅助函数
    bool matchesQuery(const DataSetInfo& info, const DataSetQuery& query) const;
    
    // 缓存门面实例（单例引用）
    AStockQuantEngine::Cache::CacheFacade* m_cacheFacade;
    
    // 配置
    struct CacheConfig {
        bool enabled{true};
        int dataCacheTTL{3600};        // 1小时
        int cleaningCacheTTL{1800};    // 30分钟
        int sessionCacheTTL{86400};    // 24小时
        size_t maxDataCacheSize{10000};
        size_t maxCleaningCacheSize{1000};
        size_t maxSessionCacheSize{100};
    } m_config;
    
    // 统计
    mutable QMutex m_statsMutex;
    CacheStats m_stats;
    
    // 数据集名称列表（用于预览窗口） - 保持向后兼容
    mutable QMutex m_dataKeysMutex;
    QSet<QString> m_dataKeys;
    
    // 新索引结构 (标记为mutable以支持const方法中的延迟重建)
    mutable QMutex m_indexMutex;
    mutable int m_nextDataSetId{1};                    // 下一个可用的数据集ID
    mutable QMap<int, DataSetInfo> m_dataSetIndex;     // ID -> DataSetInfo
    mutable QMap<QString, int> m_nameToIdIndex;        // displayName -> ID
    mutable QMap<QString, QSet<int>> m_stockCodeIndex; // 股票代码 -> ID集合
    mutable QMap<QString, QSet<int>> m_sourceTypeIndex;// 来源类型 -> ID集合
    mutable bool m_indexNeedsRebuild{false};
    
    // 状态
    bool m_initialized{false};
    QString m_lastError;
};

// 缓存装饰器 - 为DataService添加缓存功能
class DataServiceCacheDecorator {
public:
    // 数据查询装饰器
    static QVariantList queryWithCache(const QString& symbol,
                                      const QString& startDate,
                                      const QString& endDate,
                                      std::function<QVariantList()> databaseQuery);
    
    // 数据清洗装饰器
    static QVariantList cleanWithCache(const QString& requestId,
                                      const QVariantList& data,
                                      const QVariantMap& rules,
                                      std::function<QVariantList()> cleaningFunction);
    
    // 批量查询装饰器
    static QVariantList batchQueryWithCache(const QStringList& symbols,
                                           const QString& startDate,
                                           const QString& endDate,
                                           std::function<QVariantList(const QString&)> queryFunction);
    
    // 缓存预热
    static void warmUpCache(const QStringList& symbols,
                           const QString& startDate,
                           const QString& endDate,
                           std::function<QVariantList(const QString&)> queryFunction);
    
    // 缓存失效
    static void invalidateDataCache(const QString& symbol = QString());
    static void invalidateCleaningCache(const QString& requestId = QString());
    static void invalidateSessionCache(const QString& sessionId = QString());
};