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

// 缓存门面前向声明
namespace AStockQuantEngine {
namespace Cache {
    class CacheFacade;
}
}

// DataService缓存管理器 - 统一缓存接口
class DataServiceCache : public QObject {
    Q_OBJECT
    
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
    void cacheHit(const QString& key, const QString& type);
    void cacheMiss(const QString& key, const QString& type);
    void cacheError(const QString& error);
    void cacheStatsUpdated(const CacheStats& stats);
    
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
    
    // 数据序列化/反序列化
    QByteArray serializeData(const QVariantList& data) const;
    QVariantList deserializeData(const QByteArray& data) const;
    
    QByteArray serializeMap(const QVariantMap& map) const;
    QVariantMap deserializeMap(const QByteArray& data) const;
    
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