// DataCacheService.cpp
// 专门负责数据缓存的服务类实现
// 使用现有的DataServiceCache作为底层实现

#include "DataCacheService.h"
#include "DataServiceCache.h"
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <memory>

// PIMPL实现类
class DataCacheService::Impl {
public:
    Impl(DataCacheService* parent) : m_parent(parent), m_defaultTTL(3600), m_enabled(true) {
        qDebug() << "DataCacheService::Impl: 创建";
    }
    
    ~Impl() {
        qDebug() << "DataCacheService::Impl: 销毁";
    }
    
    bool initialize() {
        try {
            qDebug() << "DataCacheService::Impl: 初始化缓存服务...";
            
            // 获取DataServiceCache单例并初始化
            DataServiceCache& cache = DataServiceCache::getInstance();
            if (!cache.initializeCache()) {
                qWarning() << "DataCacheService::Impl: 缓存初始化失败";
                return false;
            }
            
            m_initialized = true;
            qDebug() << "✅ DataCacheService::Impl: 缓存服务初始化成功";
            return true;
            
        } catch (const std::exception& e) {
            QString error = QString("缓存服务初始化失败: %1").arg(e.what());
            qCritical() << "DataCacheService::Impl:" << error;
            emit m_parent->cacheError(error);
            return false;
        }
    }
    
    // 数据查询缓存
    QVariantList getCachedData(const QString& symbol, 
                               const QString& startDate, 
                               const QString& endDate) {
        if (!isReady()) {
            return QVariantList();
        }
        
        try {
            DataServiceCache& cache = DataServiceCache::getInstance();
            QVariantList data = cache.getCachedData(symbol, startDate, endDate);
            
            if (!data.isEmpty()) {
                emit m_parent->cacheHit(DataCacheService::generateDataKey(symbol, startDate, endDate), "data");
            } else {
                emit m_parent->cacheMiss(DataCacheService::generateDataKey(symbol, startDate, endDate), "data");
            }
            
            return data;
            
        } catch (const std::exception& e) {
            QString error = QString("获取缓存数据失败: %1").arg(e.what());
            qWarning() << "DataCacheService::Impl:" << error;
            emit m_parent->cacheError(error);
            return QVariantList();
        }
    }
    
    void cacheData(const QString& symbol, 
                   const QString& startDate, 
                   const QString& endDate,
                   const QVariantList& data) {
        if (!isReady() || data.isEmpty()) {
            return;
        }
        
        try {
            DataServiceCache& cache = DataServiceCache::getInstance();
            cache.cacheData(symbol, startDate, endDate, data);
            
            qDebug() << "DataCacheService::Impl: 缓存数据" 
                     << (symbol.isEmpty() ? "ALL" : symbol)
                     << "，" << data.size() << "条记录";
            
        } catch (const std::exception& e) {
            QString error = QString("缓存数据失败: %1").arg(e.what());
            qWarning() << "DataCacheService::Impl:" << error;
            emit m_parent->cacheError(error);
        }
    }
    
    // 数据清洗结果缓存
    QVariantList getCachedCleaningResult(const QString& requestId) {
        if (!isReady()) {
            return QVariantList();
        }
        
        try {
            DataServiceCache& cache = DataServiceCache::getInstance();
            QVariantList data = cache.getCachedCleaningResult(requestId);
            
            if (!data.isEmpty()) {
                emit m_parent->cacheHit(DataCacheService::generateCleaningKey(requestId), "cleaning");
            } else {
                emit m_parent->cacheMiss(DataCacheService::generateCleaningKey(requestId), "cleaning");
            }
            
            return data;
            
        } catch (const std::exception& e) {
            QString error = QString("获取清洗缓存失败: %1").arg(e.what());
            qWarning() << "DataCacheService::Impl:" << error;
            emit m_parent->cacheError(error);
            return QVariantList();
        }
    }
    
    void cacheCleaningResult(const QString& requestId, const QVariantList& data) {
        if (!isReady() || data.isEmpty()) {
            return;
        }
        
        try {
            DataServiceCache& cache = DataServiceCache::getInstance();
            cache.cacheCleaningResult(requestId, data);
            
            qDebug() << "DataCacheService::Impl: 缓存清洗结果" 
                     << requestId << "，" << data.size() << "条记录";
            
        } catch (const std::exception& e) {
            QString error = QString("缓存清洗结果失败: %1").arg(e.what());
            qWarning() << "DataCacheService::Impl:" << error;
            emit m_parent->cacheError(error);
        }
    }
    
    // 通用缓存操作
    void store(const QString& key, const QVariantList& data, int ttlSeconds = 3600) {
        if (!isReady() || data.isEmpty()) {
            return;
        }
        
        try {
            DataServiceCache& cache = DataServiceCache::getInstance();
            cache.storeData(key, data);
            
            qDebug() << "DataCacheService::Impl: 存储缓存" << key 
                     << "，" << data.size() << "条记录，TTL:" << ttlSeconds << "秒";
            
        } catch (const std::exception& e) {
            QString error = QString("存储缓存失败: %1").arg(e.what());
            qWarning() << "DataCacheService::Impl:" << error;
            emit m_parent->cacheError(error);
        }
    }
    
    QVariantList retrieve(const QString& key) {
        if (!isReady()) {
            return QVariantList();
        }
        
        try {
            DataServiceCache& cache = DataServiceCache::getInstance();
            QVariantList data = cache.getData(key);
            
            if (!data.isEmpty()) {
                emit m_parent->cacheHit(key, "generic");
            } else {
                emit m_parent->cacheMiss(key, "generic");
            }
            
            return data;
            
        } catch (const std::exception& e) {
            QString error = QString("获取缓存失败: %1").arg(e.what());
            qWarning() << "DataCacheService::Impl:" << error;
            emit m_parent->cacheError(error);
            return QVariantList();
        }
    }
    
    bool exists(const QString& key) const {
        if (!isReady()) {
            return false;
        }
        
        try {
            DataServiceCache& cache = DataServiceCache::getInstance();
            return cache.hasData(key);
            
        } catch (const std::exception& e) {
            qWarning() << "DataCacheService::Impl: 检查缓存存在失败:" << e.what();
            return false;
        }
    }
    
    void remove(const QString& key) {
        if (!isReady()) {
            return;
        }
        
        try {
            DataServiceCache& cache = DataServiceCache::getInstance();
            cache.removeData(key);
            
            qDebug() << "DataCacheService::Impl: 移除缓存" << key;
            
        } catch (const std::exception& e) {
            QString error = QString("移除缓存失败: %1").arg(e.what());
            qWarning() << "DataCacheService::Impl:" << error;
            emit m_parent->cacheError(error);
        }
    }
    
    void clear() {
        if (!isReady()) {
            return;
        }
        
        try {
            DataServiceCache& cache = DataServiceCache::getInstance();
            cache.clearAllData();
            
            qDebug() << "DataCacheService::Impl: 清空所有缓存";
            emit m_parent->cacheCleared("all");
            
        } catch (const std::exception& e) {
            QString error = QString("清空缓存失败: %1").arg(e.what());
            qWarning() << "DataCacheService::Impl:" << error;
            emit m_parent->cacheError(error);
        }
    }
    
    // 股票数据缓存（专门接口）
    void cacheStockData(const QString& symbol, 
                        const QString& startDate, 
                        const QString& endDate, 
                        const QVariantList& data) {
        cacheData(symbol, startDate, endDate, data);
    }
    
    QVariantList getCachedStockData(const QString& symbol, 
                                    const QString& startDate, 
                                    const QString& endDate) {
        return getCachedData(symbol, startDate, endDate);
    }
    
    // 缓存统计和控制
    CacheStats getStats() const {
        CacheStats stats;
        
        if (!isReady()) {
            stats.lastError = "缓存服务未初始化";
            return stats;
        }
        
        try {
            QMutexLocker locker(&m_statsMutex);
            
            // 从DataServiceCache获取统计
            DataServiceCache& cache = DataServiceCache::getInstance();
            auto cacheStats = cache.getStats();
            
            stats.hits = cacheStats.hits;
            stats.misses = cacheStats.misses;
            stats.size = cacheStats.size;
            stats.hitRate = cacheStats.hitRate;
            stats.lastUpdated = QDateTime::currentDateTime();
            
            return stats;
            
        } catch (const std::exception& e) {
            stats.lastError = QString("获取统计失败: %1").arg(e.what());
            qWarning() << "DataCacheService::Impl:" << stats.lastError;
            return stats;
        }
    }
    
    void resetStats() {
        if (!isReady()) {
            return;
        }
        
        try {
            DataServiceCache& cache = DataServiceCache::getInstance();
            cache.resetStats();
            
            qDebug() << "DataCacheService::Impl: 重置缓存统计";
            
        } catch (const std::exception& e) {
            QString error = QString("重置统计失败: %1").arg(e.what());
            qWarning() << "DataCacheService::Impl:" << error;
            emit m_parent->cacheError(error);
        }
    }
    
    void clearAllCache() {
        clear();
    }
    
    void clearDataCache() {
        if (!isReady()) {
            return;
        }
        
        try {
            DataServiceCache& cache = DataServiceCache::getInstance();
            cache.clearDataCache();
            
            qDebug() << "DataCacheService::Impl: 清空数据缓存";
            emit m_parent->cacheCleared("data");
            
        } catch (const std::exception& e) {
            QString error = QString("清空数据缓存失败: %1").arg(e.what());
            qWarning() << "DataCacheService::Impl:" << error;
            emit m_parent->cacheError(error);
        }
    }
    
    void clearCleaningCache() {
        if (!isReady()) {
            return;
        }
        
        try {
            DataServiceCache& cache = DataServiceCache::getInstance();
            cache.clearCleaningCache();
            
            qDebug() << "DataCacheService::Impl: 清空清洗缓存";
            emit m_parent->cacheCleared("cleaning");
            
        } catch (const std::exception& e) {
            QString error = QString("清空清洗缓存失败: %1").arg(e.what());
            qWarning() << "DataCacheService::Impl:" << error;
            emit m_parent->cacheError(error);
        }
    }
    
    // 缓存配置
    void setDefaultTTL(int seconds) { m_defaultTTL = seconds; }
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
    
private:
    bool isReady() const {
        return m_initialized && m_enabled;
    }
    
    DataCacheService* m_parent;
    bool m_initialized{false};
    int m_defaultTTL;
    bool m_enabled;
    mutable QMutex m_statsMutex;
};

// ============ DataCacheService 公共接口实现 ============

DataCacheService::DataCacheService(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(this)) {
    qDebug() << "DataCacheService: 创建";
}

DataCacheService::~DataCacheService() {
    qDebug() << "DataCacheService: 销毁";
}

bool DataCacheService::initialize() {
    if (m_initialized) {
        return true;
    }
    
    bool success = m_impl->initialize();
    if (success) {
        m_initialized = true;
        qDebug() << "✅ DataCacheService: 初始化成功";
    } else {
        qCritical() << "❌ DataCacheService: 初始化失败";
    }
    
    return success;
}

// 数据查询缓存
QVariantList DataCacheService::getCachedData(const QString& symbol, 
                                            const QString& startDate, 
                                            const QString& endDate) {
    return m_impl->getCachedData(symbol, startDate, endDate);
}

void DataCacheService::cacheData(const QString& symbol, 
                                const QString& startDate, 
                                const QString& endDate,
                                const QVariantList& data) {
    m_impl->cacheData(symbol, startDate, endDate, data);
}

// 数据清洗结果缓存
QVariantList DataCacheService::getCachedCleaningResult(const QString& requestId) {
    return m_impl->getCachedCleaningResult(requestId);
}

void DataCacheService::cacheCleaningResult(const QString& requestId, const QVariantList& data) {
    m_impl->cacheCleaningResult(requestId, data);
}

// 通用缓存操作
void DataCacheService::store(const QString& key, const QVariantList& data, int ttlSeconds) {
    m_impl->store(key, data, ttlSeconds);
}

QVariantList DataCacheService::retrieve(const QString& key) {
    return m_impl->retrieve(key);
}

bool DataCacheService::exists(const QString& key) const {
    return m_impl->exists(key);
}

void DataCacheService::remove(const QString& key) {
    m_impl->remove(key);
}

void DataCacheService::clear() {
    m_impl->clear();
}

// 股票数据缓存（专门接口）
void DataCacheService::cacheStockData(const QString& symbol, 
                                     const QString& startDate, 
                                     const QString& endDate, 
                                     const QVariantList& data) {
    m_impl->cacheStockData(symbol, startDate, endDate, data);
}

QVariantList DataCacheService::getCachedStockData(const QString& symbol, 
                                                 const QString& startDate, 
                                                 const QString& endDate) {
    return m_impl->getCachedStockData(symbol, startDate, endDate);
}

// 缓存统计和控制
CacheStats DataCacheService::getStats() const {
    return m_impl->getStats();
}

void DataCacheService::resetStats() {
    m_impl->resetStats();
}

void DataCacheService::clearAllCache() {
    m_impl->clearAllCache();
}

void DataCacheService::clearDataCache() {
    m_impl->clearDataCache();
}

void DataCacheService::clearCleaningCache() {
    m_impl->clearCleaningCache();
}

// 缓存配置
void DataCacheService::setDefaultTTL(int seconds) {
    m_impl->setDefaultTTL(seconds);
}

void DataCacheService::setEnabled(bool enabled) {
    m_impl->setEnabled(enabled);
}

bool DataCacheService::isEnabled() const {
    return m_impl->isEnabled();
}

// 工具函数
QString DataCacheService::generateDataKey(const QString& symbol, 
                                         const QString& startDate, 
                                         const QString& endDate) {
    // 与DataServiceCache保持一致
    return DataServiceCache::generateStockCacheKey(symbol, startDate, endDate);
}

QString DataCacheService::generateCleaningKey(const QString& requestId) {
    // 生成清洗缓存键
    return QString("cleaning_%1").arg(requestId);
}

QString DataCacheService::generateSessionKey(const QString& sessionId) {
    // 生成会话缓存键
    return QString("session_%1").arg(sessionId);
}

// 工厂函数
std::shared_ptr<DataCacheService> createDataCacheService(QObject* parent) {
    auto service = std::make_shared<DataCacheService>(parent);
    
    // 初始化服务
    bool initialized = service->initialize();
    if (!initialized) {
        qWarning() << "DataCacheService创建失败";
        return nullptr;
    }
    
    qDebug() << "✅ DataCacheService创建成功";
    return service;
}