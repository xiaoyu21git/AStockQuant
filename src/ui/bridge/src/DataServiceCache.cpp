// DataServiceCache.cpp
// DataService缓存集成实现

#include "DataServiceCache.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QCryptographicHash>
#include <QSet>

// 缓存门面头文件
#include "../../../cache/include/cache_facade.h"

using namespace AStockQuantEngine::Cache;

// 单例实例
static DataServiceCache* g_instance = nullptr;

DataServiceCache::DataServiceCache(QObject* parent)
    : QObject(parent)
    , m_stats{0, 0, 0, 0.0, ""}
{
    // m_dataKeys 默认为空，不需要显式初始化
    // m_dataKeysMutex 默认可构造，不需要显式初始化
}

DataServiceCache::~DataServiceCache()
{
    qDebug() << "DataServiceCache: Destroyed";
}

DataServiceCache& DataServiceCache::getInstance()
{
    static DataServiceCache instance;
    return instance;
}

bool DataServiceCache::initializeCache()
{
    QMutexLocker locker(&m_statsMutex);
    
    if (m_initialized) {
        qDebug() << "DataServiceCache: Already initialized";
        return true;
    }
    
    try {
        qDebug() << "DataServiceCache: Initializing cache system...";

        // 获取缓存门面单例实例
        m_cacheFacade = &AStockQuantEngine::Cache::CacheFacade::getInstance();

        // 创建缓存配置 - 使用缓存门面的CacheConfig结构
        AStockQuantEngine::Cache::CacheConfig config;
        config.enabled = m_config.enabled;
        config.defaultTtl = std::chrono::seconds(m_config.dataCacheTTL);
        
        // 配置本地缓存
        config.localCache.enabled = true;
        config.localCache.maxSize = m_config.maxDataCacheSize;
        config.localCache.expireAfterAccess = std::chrono::seconds(m_config.dataCacheTTL);
        config.localCache.expireAfterWrite = std::chrono::seconds(m_config.dataCacheTTL);
        
        // 配置Redis缓存（如果可用）
        config.redisCache.enabled = false; // 默认禁用，需要时启用
        
        // 初始化缓存门面
        if (!m_cacheFacade->initialize(config)) {
            m_lastError = "Failed to initialize cache facade";
            qCritical() << "DataServiceCache:" << m_lastError;
            return false;
        }
        
        m_initialized = true;
        qDebug() << "✅ DataServiceCache: Cache system initialized successfully";
        qDebug() << "   Data cache TTL:" << m_config.dataCacheTTL << "seconds";
        qDebug() << "   Max data cache size:" << m_config.maxDataCacheSize << "items";
        
        return true;
        
    } catch (const std::exception& e) {
        m_lastError = QString("Cache initialization error: %1").arg(e.what());
        qCritical() << "DataServiceCache:" << m_lastError;
        return false;
    }
}

QVariantList DataServiceCache::getCachedData(const QString& symbol, 
                                            const QString& startDate, 
                                            const QString& endDate)
{
    if (!m_initialized || !m_config.enabled) {
        QMutexLocker locker(&m_statsMutex);
        m_stats.misses++;
        emit cacheMiss(generateDataKey(symbol, startDate, endDate), "data");
        return QVariantList();
    }
    
    try {
        QString key = generateDataKey(symbol, startDate, endDate);
        
        // 尝试从缓存获取
        std::string cachedData;
        if (m_cacheFacade->get(key.toStdString(), cachedData)) {
            // 反序列化数据
            QByteArray dataBytes(cachedData.c_str(), cachedData.size());
            QVariantList data = deserializeData(dataBytes);
            
            if (!data.isEmpty()) {
                QMutexLocker locker(&m_statsMutex);
                m_stats.hits++;
                m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
                
                emit cacheHit(key, "data");
                emit cacheStatsUpdated(m_stats);
                
                qDebug() << "DataServiceCache: Cache hit for" << key << ", data size:" << data.size();
                return data;
            }
        }
        
        // 缓存未命中
        QMutexLocker locker(&m_statsMutex);
        m_stats.misses++;
        m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
        
        emit cacheMiss(key, "data");
        emit cacheStatsUpdated(m_stats);
        
        qDebug() << "DataServiceCache: Cache miss for" << key;
        return QVariantList();
        
    } catch (const std::exception& e) {
        QString error = QString("Cache get error: %1").arg(e.what());
        qWarning() << "DataServiceCache:" << error;
        emit cacheError(error);
        return QVariantList();
    }
}

void DataServiceCache::cacheData(const QString& symbol, 
                                const QString& startDate, 
                                const QString& endDate,
                                const QVariantList& data)
{
    if (!m_initialized || !m_config.enabled || data.isEmpty()) {
        return;
    }
    
    try {
        QString key = generateDataKey(symbol, startDate, endDate);
        
        // 序列化数据
        QByteArray dataBytes = serializeData(data);
        std::string cacheData(dataBytes.constData(), dataBytes.size());
        
        // 存储到缓存
        m_cacheFacade->set(key.toStdString(), cacheData, 
                          std::chrono::seconds(m_config.dataCacheTTL));
        
        // 更新统计
        QMutexLocker locker(&m_statsMutex);
        m_stats.size++;
        
        qDebug() << "DataServiceCache: Cached data for" << key 
                 << ", size:" << data.size() << "records";
        
    } catch (const std::exception& e) {
        QString error = QString("Cache set error: %1").arg(e.what());
        qWarning() << "DataServiceCache:" << error;
        emit cacheError(error);
    }
}

QVariantList DataServiceCache::getCachedCleaningResult(const QString& requestId)
{
    if (!m_initialized || !m_config.enabled) {
        QMutexLocker locker(&m_statsMutex);
        m_stats.misses++;
        emit cacheMiss(generateCleaningKey(requestId), "cleaning");
        return QVariantList();
    }
    
    try {
        QString key = generateCleaningKey(requestId);
        
        // 尝试从缓存获取
        std::string cachedData;
        if (m_cacheFacade->get(key.toStdString(), cachedData)) {
            // 反序列化数据
            QByteArray dataBytes(cachedData.c_str(), cachedData.size());
            QVariantList data = deserializeData(dataBytes);
            
            if (!data.isEmpty()) {
                QMutexLocker locker(&m_statsMutex);
                m_stats.hits++;
                m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
                
                emit cacheHit(key, "cleaning");
                emit cacheStatsUpdated(m_stats);
                
                qDebug() << "DataServiceCache: Cache hit for cleaning result" << key;
                return data;
            }
        }
        
        // 缓存未命中
        QMutexLocker locker(&m_statsMutex);
        m_stats.misses++;
        m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
        
        emit cacheMiss(key, "cleaning");
        emit cacheStatsUpdated(m_stats);
        
        return QVariantList();
        
    } catch (const std::exception& e) {
        QString error = QString("Cache get error: %1").arg(e.what());
        qWarning() << "DataServiceCache:" << error;
        emit cacheError(error);
        return QVariantList();
    }
}

void DataServiceCache::cacheCleaningResult(const QString& requestId, const QVariantList& data)
{
    if (!m_initialized || !m_config.enabled || data.isEmpty()) {
        return;
    }
    
    try {
        QString key = generateCleaningKey(requestId);
        
        // 序列化数据
        QByteArray dataBytes = serializeData(data);
        std::string cacheData(dataBytes.constData(), dataBytes.size());
        
        // 存储到缓存
        m_cacheFacade->set(key.toStdString(), cacheData, 
                          std::chrono::seconds(m_config.cleaningCacheTTL));
        
        // 更新统计
        QMutexLocker locker(&m_statsMutex);
        m_stats.size++;
        
        qDebug() << "DataServiceCache: Cached cleaning result for" << key;
        
    } catch (const std::exception& e) {
        QString error = QString("Cache set error: %1").arg(e.what());
        qWarning() << "DataServiceCache:" << error;
        emit cacheError(error);
    }
}

QVariantMap DataServiceCache::getCachedUserSession(const QString& sessionId)
{
    if (!m_initialized || !m_config.enabled) {
        return QVariantMap();
    }
    
    try {
        QString key = generateSessionKey(sessionId);
        
        // 尝试从缓存获取
        std::string cachedData;
        if (m_cacheFacade->get(key.toStdString(), cachedData)) {
            // 反序列化数据
            QByteArray dataBytes(cachedData.c_str(), cachedData.size());
            QVariantMap data = deserializeMap(dataBytes);
            
            if (!data.isEmpty()) {
                QMutexLocker locker(&m_statsMutex);
                m_stats.hits++;
                m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
                
                emit cacheHit(key, "session");
                emit cacheStatsUpdated(m_stats);
                
                return data;
            }
        }
        
        // 缓存未命中
        QMutexLocker locker(&m_statsMutex);
        m_stats.misses++;
        m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
        
        emit cacheMiss(key, "session");
        emit cacheStatsUpdated(m_stats);
        
        return QVariantMap();
        
    } catch (const std::exception& e) {
        QString error = QString("Cache get error: %1").arg(e.what());
        qWarning() << "DataServiceCache:" << error;
        emit cacheError(error);
        return QVariantMap();
    }
}

void DataServiceCache::cacheUserSession(const QString& sessionId, const QVariantMap& sessionData)
{
    if (!m_initialized || !m_config.enabled || sessionData.isEmpty()) {
        return;
    }
    
    try {
        QString key = generateSessionKey(sessionId);
        
        // 序列化数据
        QByteArray dataBytes = serializeMap(sessionData);
        std::string cacheData(dataBytes.constData(), dataBytes.size());
        
        // 存储到缓存
        m_cacheFacade->set(key.toStdString(), cacheData, 
                          std::chrono::seconds(m_config.sessionCacheTTL));
        
        // 更新统计
        QMutexLocker locker(&m_statsMutex);
        m_stats.size++;
        
    } catch (const std::exception& e) {
        QString error = QString("Cache set error: %1").arg(e.what());
        qWarning() << "DataServiceCache:" << error;
        emit cacheError(error);
    }
}

DataServiceCache::CacheStats DataServiceCache::getStats() const
{
    QMutexLocker locker(&m_statsMutex);
    return m_stats;
}

void DataServiceCache::resetStats()
{
    QMutexLocker locker(&m_statsMutex);
    m_stats = CacheStats{0, 0, 0, 0.0, ""};
    emit cacheStatsUpdated(m_stats);
}

void DataServiceCache::clearAllCache()
{
    if (!m_initialized) {
        return;
    }
    
    try {
        m_cacheFacade->clear();
        
        QMutexLocker locker(&m_statsMutex);
        m_stats.size = 0;
        
        qDebug() << "DataServiceCache: All cache cleared";
        emit cacheStatsUpdated(m_stats);
        
    } catch (const std::exception& e) {
        QString error = QString("Cache clear error: %1").arg(e.what());
        qWarning() << "DataServiceCache:" << error;
        emit cacheError(error);
    }
}

void DataServiceCache::clearDataCache()
{
    if (!m_initialized) {
        return;
    }
    
    try {
        // 清除所有数据缓存（以"data:"开头的key）
        m_cacheFacade->invalidatePattern("data:*");
        
        qDebug() << "DataServiceCache: Data cache cleared";
        
    } catch (const std::exception& e) {
        QString error = QString("Cache clear error: %1").arg(e.what());
        qWarning() << "DataServiceCache:" << error;
        emit cacheError(error);
    }
}

void DataServiceCache::clearCleaningCache()
{
    if (!m_initialized) {
        return;
    }
    
    try {
        // 清除所有清洗缓存（以"cleaning:"开头的key）
        m_cacheFacade->invalidatePattern("cleaning:*");
        
        qDebug() << "DataServiceCache: Cleaning cache cleared";
        
    } catch (const std::exception& e) {
        QString error = QString("Cache clear error: %1").arg(e.what());
        qWarning() << "DataServiceCache:" << error;
        emit cacheError(error);
    }
}

void DataServiceCache::clearSessionCache()
{
    if (!m_initialized) {
        return;
    }
    
    try {
        // 清除所有会话缓存（以"session:"开头的key）
        m_cacheFacade->invalidatePattern("session:*");
        
        qDebug() << "DataServiceCache: Session cache cleared";
        
    } catch (const std::exception& e) {
        QString error = QString("Cache clear error: %1").arg(e.what());
        qWarning() << "DataServiceCache:" << error;
        emit cacheError(error);
    }
}

bool DataServiceCache::isCacheEnabled() const
{
    return m_initialized && m_config.enabled;
}

void DataServiceCache::setDataCacheTTL(int seconds)
{
    m_config.dataCacheTTL = seconds;
    qDebug() << "DataServiceCache: Data cache TTL set to" << seconds << "seconds";
}

void DataServiceCache::setCleaningCacheTTL(int seconds)
{
    m_config.cleaningCacheTTL = seconds;
    qDebug() << "DataServiceCache: Cleaning cache TTL set to" << seconds << "seconds";
}

void DataServiceCache::setSessionCacheTTL(int seconds)
{
    m_config.sessionCacheTTL = seconds;
    qDebug() << "DataServiceCache: Session cache TTL set to" << seconds << "seconds";
}

QString DataServiceCache::generateDataKey(const QString& symbol, 
                                         const QString& startDate, 
                                         const QString& endDate) const
{
    // 统一缓存键格式，与DataManager保持一致
    // 格式：data:stock:[symbol]_[startDate]_[endDate]
    // 如果symbol为空，表示查询所有股票，使用"ALL"作为占位符
    QString actualSymbol = symbol.isEmpty() ? "ALL" : symbol;
    
    // 使用与DataManager兼容的格式，但添加"data:stock:"前缀以便在CacheFacade中分类
    QString key = QString("data:stock:%1_%2_%3")
                 .arg(actualSymbol)
                 .arg(startDate)
                 .arg(endDate);
    
    qDebug() << "DataServiceCache: Generated data cache key:" << key;
    return key;
}

QString DataServiceCache::generateCleaningKey(const QString& requestId) const
{
    QString key = QString("cleaning:result:%1").arg(requestId);
    
    // 使用MD5生成固定长度的key
    QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Md5);
    return "cleaning:" + hash.toHex();
}

QString DataServiceCache::generateSessionKey(const QString& sessionId) const
{
    QString key = QString("session:user:%1").arg(sessionId);
    
    // 使用MD5生成固定长度的key
    QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Md5);
    return "session:" + hash.toHex();
}

QByteArray DataServiceCache::serializeData(const QVariantList& data) const
{
    QJsonArray jsonArray;
    
    for (const QVariant& item : data) {
        if (item.type() == QVariant::Map) {
            QVariantMap map = item.toMap();
            QJsonObject jsonObj;
            
            for (auto it = map.begin(); it != map.end(); ++it) {
                jsonObj[it.key()] = QJsonValue::fromVariant(it.value());
            }
            
            jsonArray.append(jsonObj);
        }
    }
    
    QJsonDocument doc(jsonArray);
    return doc.toJson(QJsonDocument::Compact);
}

QVariantList DataServiceCache::deserializeData(const QByteArray& data) const
{
    QVariantList result;
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isArray()) {
        return result;
    }
    
    QJsonArray jsonArray = doc.array();
    for (const QJsonValue& value : jsonArray) {
        if (value.isObject()) {
            QJsonObject jsonObj = value.toObject();
            QVariantMap map;
            
            for (auto it = jsonObj.begin(); it != jsonObj.end(); ++it) {
                map[it.key()] = it.value().toVariant();
            }
            
            result.append(map);
        }
    }
    
    return result;
}

QByteArray DataServiceCache::serializeMap(const QVariantMap& map) const
{
    QJsonObject jsonObj;
    
    for (auto it = map.begin(); it != map.end(); ++it) {
        jsonObj[it.key()] = QJsonValue::fromVariant(it.value());
    }
    
    QJsonDocument doc(jsonObj);
    return doc.toJson(QJsonDocument::Compact);
}

QVariantMap DataServiceCache::deserializeMap(const QByteArray& data) const
{
    QVariantMap result;
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        return result;
    }
    
    QJsonObject jsonObj = doc.object();
    for (auto it = jsonObj.begin(); it != jsonObj.end(); ++it) {
        result[it.key()] = it.value().toVariant();
    }
    
    return result;
}

// ==================== 基础缓存接口实现 (兼容DataManager) ====================

void DataServiceCache::storeData(const QString& key, const QVariantList& data)
{
    if (!m_initialized || !m_config.enabled || data.isEmpty()) {
        qDebug() << "DataServiceCache::storeData: Cache not initialized or data empty for key" << key;
        return;
    }
    
    try {
        // 序列化数据
        QByteArray dataBytes = serializeData(data);
        std::string cacheData(dataBytes.constData(), dataBytes.size());
        
        // 使用自定义前缀存储，避免与其他缓存冲突
        QString cacheKey = "manager:" + key;
        
        // 存储到缓存 - 使用较短的TTL，因为DataManager通常是临时存储
        m_cacheFacade->set(cacheKey.toStdString(), cacheData, 
                          std::chrono::seconds(300)); // 5分钟TTL
        
        // 更新统计
        QMutexLocker locker(&m_statsMutex);
        m_stats.size++;
        
        // 添加到数据集列表
        m_dataKeys.insert(key);
        
        qDebug() << "✅ DataServiceCache::storeData: Stored" << data.size() 
                 << "items with key" << key << "(cache key:" << cacheKey << ")";
        
        // 发送信号
        emit cacheStatsUpdated(m_stats);
        
    } catch (const std::exception& e) {
        QString error = QString("Cache storeData error: %1").arg(e.what());
        qWarning() << "DataServiceCache::storeData:" << error;
        emit cacheError(error);
    }
}

QVariantList DataServiceCache::getData(const QString& key)
{
    if (!m_initialized || !m_config.enabled) {
        QMutexLocker locker(&m_statsMutex);
        m_stats.misses++;
        qDebug() << "DataServiceCache::getData: Cache not initialized for key" << key;
        return QVariantList();
    }
    
    try {
        QString cacheKey = "manager:" + key;
        
        // 尝试从缓存获取
        std::string cachedData;
        if (m_cacheFacade->get(cacheKey.toStdString(), cachedData)) {
            // 反序列化数据
            QByteArray dataBytes(cachedData.c_str(), cachedData.size());
            QVariantList data = deserializeData(dataBytes);
            
            if (!data.isEmpty()) {
                QMutexLocker locker(&m_statsMutex);
                m_stats.hits++;
                m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
                
                qDebug() << "DataServiceCache::getData: Retrieved" << data.size() 
                         << "items with key" << key;
                emit cacheStatsUpdated(m_stats);
                return data;
            }
        }
        
        // 缓存未命中
        QMutexLocker locker(&m_statsMutex);
        m_stats.misses++;
        m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
        
        qDebug() << "DataServiceCache::getData: No data found for key" << key;
        emit cacheStatsUpdated(m_stats);
        return QVariantList();
        
    } catch (const std::exception& e) {
        QString error = QString("Cache getData error: %1").arg(e.what());
        qWarning() << "DataServiceCache::getData:" << error;
        emit cacheError(error);
        return QVariantList();
    }
}

bool DataServiceCache::hasData(const QString& key)
{
    if (!m_initialized || !m_config.enabled) {
        return false;
    }
    
    try {
        QString cacheKey = "manager:" + key;
        
        // 检查缓存是否存在
        bool exists = m_cacheFacade->exists(cacheKey.toStdString());
        
        qDebug() << "DataServiceCache::hasData: Key" << key 
                 << (exists ? "exists" : "does not exist");
        return exists;
        
    } catch (const std::exception& e) {
        QString error = QString("Cache hasData error: %1").arg(e.what());
        qWarning() << "DataServiceCache::hasData:" << error;
        return false;
    }
}

void DataServiceCache::removeData(const QString& key)
{
    if (!m_initialized) {
        return;
    }
    
    try {
        QString cacheKey = "manager:" + key;
        
        if (m_cacheFacade->remove(cacheKey.toStdString())) {
            QMutexLocker locker(&m_statsMutex);
            if (m_stats.size > 0) {
                m_stats.size--;
            }
            
            // 从数据集键列表中移除
            QMutexLocker keysLocker(&m_dataKeysMutex);
            m_dataKeys.remove(key);
            
            qDebug() << "DataServiceCache::removeData: Removed data with key" << key;
            emit cacheStatsUpdated(m_stats);
        } else {
            qDebug() << "DataServiceCache::removeData: No data found for key" << key;
        }
        
    } catch (const std::exception& e) {
        QString error = QString("Cache removeData error: %1").arg(e.what());
        qWarning() << "DataServiceCache::removeData:" << error;
        emit cacheError(error);
    }
}

void DataServiceCache::clearAllData()
{
    if (!m_initialized) {
        return;
    }
    
    try {
        // 清除所有以"manager:"开头的缓存
        m_cacheFacade->invalidatePattern("manager:*");
        
        // 更新统计
        QMutexLocker locker(&m_statsMutex);
        m_stats.size = 0;
        
        qDebug() << "DataServiceCache::clearAllData: All manager cache cleared";
        emit cacheStatsUpdated(m_stats);
        
    } catch (const std::exception& e) {
        QString error = QString("Cache clearAllData error: %1").arg(e.what());
        qWarning() << "DataServiceCache::clearAllData:" << error;
        emit cacheError(error);
    }
}

void DataServiceCache::cacheStockData(const QString& symbol, 
                                     const QString& startDate, 
                                     const QString& endDate, 
                                     const QVariantList& data)
{
    QString cacheKey = generateStockCacheKey(symbol, startDate, endDate);
    storeData(cacheKey, data);
    
    qDebug() << "DataServiceCache::cacheStockData: Cached" << data.size() 
             << "items for" << symbol << "from" << startDate << "to" << endDate;
}

QVariantList DataServiceCache::getCachedStockData(const QString& symbol, 
                                                 const QString& startDate, 
                                                 const QString& endDate)
{
    QString cacheKey = generateStockCacheKey(symbol, startDate, endDate);
    return getData(cacheKey);
}

QString DataServiceCache::generateStockCacheKey(const QString& symbol, 
                                               const QString& startDate, 
                                               const QString& endDate)
{
    // 与DataManager保持一致的格式
    return QString("%1_%2_%3").arg(symbol).arg(startDate).arg(endDate);
}

QStringList DataServiceCache::getAllDataKeys() const
{
    // 返回存储的数据集键列表
    QMutexLocker locker(&m_dataKeysMutex);
    QStringList keys = m_dataKeys.values();
    qDebug() << "DataServiceCache::getAllDataKeys: Returning" << keys.size() << "data keys";
    return keys;
}

QString DataServiceCache::getStatistics() const
{
    QMutexLocker locker(&m_statsMutex);
    
    int totalEntries = m_stats.size;
    // 注：当前实现无法获取总条目数，使用size统计
    
    return QString("Cache Statistics: Hits=%1, Misses=%2, HitRate=%3%, Size=%4")
           .arg(m_stats.hits)
           .arg(m_stats.misses)
           .arg(QString::number(m_stats.hitRate * 100, 'f', 2))
           .arg(totalEntries);
}

// ==================== DataServiceCacheDecorator 实现 ====================

QVariantList DataServiceCacheDecorator::queryWithCache(const QString& symbol,
                                                      const QString& startDate,
                                                      const QString& endDate,
                                                      std::function<QVariantList()> databaseQuery)
{
    DataServiceCache& cache = DataServiceCache::getInstance();
    
    // 1. 尝试从缓存获取
    QVariantList cachedData = cache.getCachedData(symbol, startDate, endDate);
    if (!cachedData.isEmpty()) {
        qDebug() << "DataServiceCacheDecorator: Cache hit for" 
                 << (symbol.isEmpty() ? "ALL" : symbol) 
                 << startDate << "-" << endDate;
        return cachedData;
    }
    
    // 2. 缓存未命中，执行数据库查询
    qDebug() << "DataServiceCacheDecorator: Cache miss for" 
             << (symbol.isEmpty() ? "ALL" : symbol) 
             << startDate << "-" << endDate << ", querying database...";
    
    QVariantList data = databaseQuery();
    
    // 3. 如果查询到数据，缓存起来
    if (!data.isEmpty()) {
        cache.cacheData(symbol, startDate, endDate, data);
        qDebug() << "DataServiceCacheDecorator: Cached" << data.size() 
                 << "records for" << (symbol.isEmpty() ? "ALL" : symbol);
    }
    
    return data;
}

QVariantList DataServiceCacheDecorator::cleanWithCache(const QString& requestId,
                                                      const QVariantList& data,
                                                      const QVariantMap& rules,
                                                      std::function<QVariantList()> cleaningFunction)
{
    DataServiceCache& cache = DataServiceCache::getInstance();
    
    // 1. 尝试从缓存获取清洗结果
    QVariantList cachedResult = cache.getCachedCleaningResult(requestId);
    if (!cachedResult.isEmpty()) {
        qDebug() << "DataServiceCacheDecorator: Cache hit for cleaning result" << requestId;
        return cachedResult;
    }
    
    // 2. 缓存未命中，执行清洗
    qDebug() << "DataServiceCacheDecorator: Cache miss for cleaning result" 
             << requestId << ", performing cleaning...";
    
    QVariantList cleanedData = cleaningFunction();
    
    // 3. 如果清洗成功，缓存结果
    if (!cleanedData.isEmpty()) {
        cache.cacheCleaningResult(requestId, cleanedData);
        qDebug() << "DataServiceCacheDecorator: Cached cleaning result for" << requestId;
    }
    
    return cleanedData;
}

QVariantList DataServiceCacheDecorator::batchQueryWithCache(const QStringList& symbols,
                                                           const QString& startDate,
                                                           const QString& endDate,
                                                           std::function<QVariantList(const QString&)> queryFunction)
{
    QVariantList allData;
    
    for (const QString& symbol : symbols) {
        // 使用缓存装饰器查询每个股票
        QVariantList symbolData = queryWithCache(symbol, startDate, endDate, 
            [&]() { return queryFunction(symbol); });
        
        allData.append(symbolData);
    }
    
    return allData;
}

void DataServiceCacheDecorator::warmUpCache(const QStringList& symbols,
                                           const QString& startDate,
                                           const QString& endDate,
                                           std::function<QVariantList(const QString&)> queryFunction)
{
    qDebug() << "DataServiceCacheDecorator: Warming up cache for" 
             << symbols.size() << "symbols...";
    
    for (const QString& symbol : symbols) {
        // 异步预热缓存
        QVariantList data = queryFunction(symbol);
        if (!data.isEmpty()) {
            DataServiceCache::getInstance().cacheData(symbol, startDate, endDate, data);
            qDebug() << "DataServiceCacheDecorator: Warmed up cache for" << symbol 
                     << "(" << data.size() << "records)";
        }
    }
    
    qDebug() << "DataServiceCacheDecorator: Cache warm-up completed";
}

void DataServiceCacheDecorator::invalidateDataCache(const QString& symbol)
{
    DataServiceCache& cache = DataServiceCache::getInstance();
    
    if (symbol.isEmpty()) {
        cache.clearDataCache();
        qDebug() << "DataServiceCacheDecorator: Invalidated all data cache";
    } else {
        // 这里需要实现按symbol清除缓存的功能
        // 目前先清除所有数据缓存
        cache.clearDataCache();
        qDebug() << "DataServiceCacheDecorator: Invalidated data cache for" << symbol;
    }
}

void DataServiceCacheDecorator::invalidateCleaningCache(const QString& requestId)
{
    DataServiceCache& cache = DataServiceCache::getInstance();
    
    if (requestId.isEmpty()) {
        cache.clearCleaningCache();
        qDebug() << "DataServiceCacheDecorator: Invalidated all cleaning cache";
    } else {
        // 这里需要实现按requestId清除缓存的功能
        // 目前先清除所有清洗缓存
        cache.clearCleaningCache();
        qDebug() << "DataServiceCacheDecorator: Invalidated cleaning cache for" << requestId;
    }
}

void DataServiceCacheDecorator::invalidateSessionCache(const QString& sessionId)
{
    DataServiceCache& cache = DataServiceCache::getInstance();

    if (sessionId.isEmpty()) {
        cache.clearSessionCache();
        qDebug() << "DataServiceCacheDecorator: Invalidated all session cache";
    } else {
        // 这里需要实现按sessionId清除缓存的功能
        // 目前先清除所有会话缓存
        cache.clearSessionCache();
        qDebug() << "DataServiceCacheDecorator: Invalidated session cache for" << sessionId;
    }
}

// ==================== 基于ID的数据集管理接口实现 ====================

QString DataServiceCache::generateDataSetKey(int dataId) const
{
    return QString("dataset:%1:data").arg(dataId);
}

QString DataServiceCache::generateDataSetInfoKey(int dataId) const
{
    return QString("dataset:%1:info").arg(dataId);
}

QByteArray DataServiceCache::serializeDataSetInfo(const DataSetInfo& info) const
{
    QJsonObject jsonObj;
    jsonObj["id"] = info.id;
    jsonObj["displayName"] = info.displayName;
    jsonObj["description"] = info.description;
    jsonObj["sourceType"] = info.sourceType;
    jsonObj["createdTime"] = info.createdTime.toString(Qt::ISODate);
    jsonObj["rowCount"] = info.rowCount;
    
    QJsonArray stockCodesArray;
    for (const QString& code : info.stockCodes) {
        stockCodesArray.append(code);
    }
    jsonObj["stockCodes"] = stockCodesArray;
    
    if (info.startDate.isValid()) {
        jsonObj["startDate"] = info.startDate.toString(Qt::ISODate);
    }
    if (info.endDate.isValid()) {
        jsonObj["endDate"] = info.endDate.toString(Qt::ISODate);
    }
    
    QJsonArray tagsArray;
    for (const QString& tag : info.tags) {
        tagsArray.append(tag);
    }
    jsonObj["tags"] = tagsArray;
    
    QJsonDocument doc(jsonObj);
    return doc.toJson(QJsonDocument::Compact);
}

DataServiceCache::DataSetInfo DataServiceCache::deserializeDataSetInfo(const QByteArray& data) const
{
    DataSetInfo info;
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        return info;
    }
    
    QJsonObject jsonObj = doc.object();
    info.id = jsonObj["id"].toInt(-1);
    info.displayName = jsonObj["displayName"].toString();
    info.description = jsonObj["description"].toString();
    info.sourceType = jsonObj["sourceType"].toString();
    info.createdTime = QDateTime::fromString(jsonObj["createdTime"].toString(), Qt::ISODate);
    info.rowCount = jsonObj["rowCount"].toInt();
    
    if (jsonObj.contains("stockCodes")) {
        QJsonArray stockCodesArray = jsonObj["stockCodes"].toArray();
        for (const QJsonValue& value : stockCodesArray) {
            info.stockCodes.append(value.toString());
        }
    }
    
    if (jsonObj.contains("startDate")) {
        info.startDate = QDate::fromString(jsonObj["startDate"].toString(), Qt::ISODate);
    }
    if (jsonObj.contains("endDate")) {
        info.endDate = QDate::fromString(jsonObj["endDate"].toString(), Qt::ISODate);
    }
    
    if (jsonObj.contains("tags")) {
        QJsonArray tagsArray = jsonObj["tags"].toArray();
        for (const QJsonValue& value : tagsArray) {
            info.tags.append(value.toString());
        }
    }
    
    return info;
}

void DataServiceCache::rebuildIndexIfNeeded() const
{
    QMutexLocker locker(&m_indexMutex);
    
    if (!m_indexNeedsRebuild) {
        return;
    }
    
    qDebug() << "DataServiceCache: Rebuilding dataset index...";
    
    // 注意：由于这是一个const方法，我们不能修改非mutable成员
    // 所以我们需要将索引重建逻辑移动到非const方法中
    // 这里我们只记录需要重建，实际重建在非const方法中进行
    qDebug() << "DataServiceCache: Index needs rebuild, but cannot rebuild in const method";
}

void DataServiceCache::addToIndex(int dataId, const DataSetInfo& info)
{
    QMutexLocker locker(&m_indexMutex);
    
    m_dataSetIndex[dataId] = info;
    m_nameToIdIndex[info.displayName] = dataId;
    
    // 更新股票代码索引
    for (const QString& code : info.stockCodes) {
        m_stockCodeIndex[code].insert(dataId);
    }
    
    // 更新来源类型索引
    m_sourceTypeIndex[info.sourceType].insert(dataId);
    
    qDebug() << "DataServiceCache: Added dataset" << dataId << "to index:" << info.displayName;
}

void DataServiceCache::removeFromIndex(int dataId)
{
    QMutexLocker locker(&m_indexMutex);
    
    if (!m_dataSetIndex.contains(dataId)) {
        return;
    }
    
    DataSetInfo info = m_dataSetIndex[dataId];
    
    // 从名称索引中移除
    m_nameToIdIndex.remove(info.displayName);
    
    // 从股票代码索引中移除
    for (const QString& code : info.stockCodes) {
        if (m_stockCodeIndex.contains(code)) {
            m_stockCodeIndex[code].remove(dataId);
            if (m_stockCodeIndex[code].isEmpty()) {
                m_stockCodeIndex.remove(code);
            }
        }
    }
    
    // 从来源类型索引中移除
    if (m_sourceTypeIndex.contains(info.sourceType)) {
        m_sourceTypeIndex[info.sourceType].remove(dataId);
        if (m_sourceTypeIndex[info.sourceType].isEmpty()) {
            m_sourceTypeIndex.remove(info.sourceType);
        }
    }
    
    // 从主索引中移除
    m_dataSetIndex.remove(dataId);
    
    qDebug() << "DataServiceCache: Removed dataset" << dataId << "from index";
}

void DataServiceCache::updateIndex(int dataId, const DataSetInfo& info)
{
    // 先移除旧记录，再添加新记录
    removeFromIndex(dataId);
    addToIndex(dataId, info);
}

bool DataServiceCache::matchesQuery(const DataSetInfo& info, const DataSetQuery& query) const
{
    // 检查股票代码
    if (!query.stockCode.isEmpty()) {
        if (!info.stockCodes.contains(query.stockCode)) {
            return false;
        }
    }
    
    // 检查开始日期
    if (query.startDate.isValid() && info.startDate.isValid()) {
        if (info.startDate < query.startDate) {
            return false;
        }
    }
    
    // 检查结束日期
    if (query.endDate.isValid() && info.endDate.isValid()) {
        if (info.endDate > query.endDate) {
            return false;
        }
    }
    
    // 检查来源类型
    if (!query.sourceType.isEmpty()) {
        if (info.sourceType != query.sourceType) {
            return false;
        }
    }
    
    // 检查标签
    if (!query.tags.isEmpty()) {
        bool hasAllTags = true;
        for (const QString& tag : query.tags) {
            if (!info.tags.contains(tag)) {
                hasAllTags = false;
                break;
            }
        }
        if (!hasAllTags) {
            return false;
        }
    }
    
    // 检查名称过滤
    if (!query.displayNameFilter.isEmpty()) {
        if (!info.displayName.contains(query.displayNameFilter, Qt::CaseInsensitive)) {
            return false;
        }
    }
    
    return true;
}

// 主接口实现

int DataServiceCache::storeDataSet(const QVariantList& data, const DataSetInfo& info)
{
    if (!m_initialized || !m_config.enabled || data.isEmpty()) {
        qDebug() << "DataServiceCache::storeDataSet: Cache not initialized or data empty";
        return -1;
    }
    
    try {
        QMutexLocker locker(&m_indexMutex);
        
        // 生成新的数据集ID
        int dataId = m_nextDataSetId++;
        
        // 更新数据集信息
        DataSetInfo completeInfo = info;
        completeInfo.id = dataId;
        completeInfo.rowCount = data.size();
        if (!completeInfo.createdTime.isValid()) {
            completeInfo.createdTime = QDateTime::currentDateTime();
        }
        
        // 生成缓存键
        QString dataKey = generateDataSetKey(dataId);
        QString infoKey = generateDataSetInfoKey(dataId);
        
        // 序列化数据
        QByteArray dataBytes = serializeData(data);
        std::string cacheData(dataBytes.constData(), dataBytes.size());
        
        // 序列化数据集信息
        QByteArray infoBytes = serializeDataSetInfo(completeInfo);
        std::string cacheInfo(infoBytes.constData(), infoBytes.size());
        
        // 存储到缓存
        m_cacheFacade->set(dataKey.toStdString(), cacheData, 
                          std::chrono::seconds(m_config.dataCacheTTL));
        m_cacheFacade->set(infoKey.toStdString(), cacheInfo,
                          std::chrono::seconds(m_config.dataCacheTTL));
        
        // 添加到索引
        addToIndex(dataId, completeInfo);
        
        // 更新统计
        QMutexLocker statsLocker(&m_statsMutex);
        m_stats.size++;
        
        qDebug() << "✅ DataServiceCache::storeDataSet: Stored dataset" << dataId 
                 << "with" << data.size() << "rows:" << completeInfo.displayName;
        
        // 发送信号
        emit dataSetStored(dataId, completeInfo);
        emit cacheStatsUpdated(m_stats);
        
        return dataId;
        
    } catch (const std::exception& e) {
        QString error = QString("storeDataSet error: %1").arg(e.what());
        qWarning() << "DataServiceCache::storeDataSet:" << error;
        emit cacheError(error);
        return -1;
    }
}

QVariantList DataServiceCache::getDataSetById(int dataId)
{
    if (!m_initialized || !m_config.enabled) {
        qDebug() << "DataServiceCache::getDataSetById: Cache not initialized";
        return QVariantList();
    }
    
    if (dataId <= 0) {
        qDebug() << "DataServiceCache::getDataSetById: Invalid dataset ID:" << dataId;
        return QVariantList();
    }
    
    try {
        QString dataKey = generateDataSetKey(dataId);
        
        // 尝试从缓存获取
        std::string cachedData;
        if (m_cacheFacade->get(dataKey.toStdString(), cachedData)) {
            // 反序列化数据
            QByteArray dataBytes(cachedData.c_str(), cachedData.size());
            QVariantList data = deserializeData(dataBytes);
            
            if (!data.isEmpty()) {
                // 更新统计
                QMutexLocker locker(&m_statsMutex);
                m_stats.hits++;
                m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
                
                qDebug() << "DataServiceCache::getDataSetById: Retrieved dataset" 
                         << dataId << "with" << data.size() << "rows";
                emit cacheStatsUpdated(m_stats);
                return data;
            }
        }
        
        // 缓存未命中
        QMutexLocker locker(&m_statsMutex);
        m_stats.misses++;
        m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
        
        qDebug() << "DataServiceCache::getDataSetById: Dataset" << dataId << "not found";
        emit cacheStatsUpdated(m_stats);
        return QVariantList();
        
    } catch (const std::exception& e) {
        QString error = QString("getDataSetById error: %1").arg(e.what());
        qWarning() << "DataServiceCache::getDataSetById:" << error;
        emit cacheError(error);
        return QVariantList();
    }
}

DataServiceCache::DataSetInfo DataServiceCache::getDataSetInfo(int dataId) const
{
    if (!m_initialized || dataId <= 0) {
        return DataSetInfo();
    }
    
    try {
        // 首先尝试从内存索引获取
        {
            QMutexLocker locker(&m_indexMutex);
            if (m_dataSetIndex.contains(dataId)) {
                return m_dataSetIndex[dataId];
            }
        }
        
        // 从缓存获取
        QString infoKey = generateDataSetInfoKey(dataId);
        std::string cachedInfo;
        if (m_cacheFacade->get(infoKey.toStdString(), cachedInfo)) {
            QByteArray infoBytes(cachedInfo.c_str(), cachedInfo.size());
            return deserializeDataSetInfo(infoBytes);
        }
        
        return DataSetInfo();
        
    } catch (const std::exception& e) {
        QString error = QString("getDataSetInfo error: %1").arg(e.what());
        qWarning() << "DataServiceCache::getDataSetInfo:" << error;
        return DataSetInfo();
    }
}

QVector<DataServiceCache::DataSetInfo> DataServiceCache::queryDataSets(const DataSetQuery& query) const
{
    rebuildIndexIfNeeded();
    
    QMutexLocker locker(&m_indexMutex);
    QVector<DataSetInfo> results;
    
    if (m_dataSetIndex.isEmpty()) {
        return results;
    }
    
    // 如果查询为空，返回所有数据集
    if (query.isEmpty()) {
        results = QVector<DataSetInfo>::fromList(m_dataSetIndex.values());
        qDebug() << "DataServiceCache::queryDataSets: Returning all" << results.size() << "datasets";
        return results;
    }
    
    // 如果有股票代码过滤，使用索引加速
    if (!query.stockCode.isEmpty()) {
        if (m_stockCodeIndex.contains(query.stockCode)) {
            const QSet<int>& dataIds = m_stockCodeIndex[query.stockCode];
            for (int dataId : dataIds) {
                if (m_dataSetIndex.contains(dataId)) {
                    const DataSetInfo& info = m_dataSetIndex[dataId];
                    if (matchesQuery(info, query)) {
                        results.append(info);
                    }
                }
            }
        }
    } else {
        // 没有股票代码过滤，遍历所有数据集
        for (const DataSetInfo& info : m_dataSetIndex.values()) {
            if (matchesQuery(info, query)) {
                results.append(info);
            }
        }
    }
    
    qDebug() << "DataServiceCache::queryDataSets: Found" << results.size() << "datasets matching query";
    return results;
}

QVector<DataServiceCache::DataSetInfo> DataServiceCache::getAllDataSetInfos() const
{
    rebuildIndexIfNeeded();
    
    QMutexLocker locker(&m_indexMutex);
    QVector<DataSetInfo> results = QVector<DataSetInfo>::fromList(m_dataSetIndex.values());
    
    qDebug() << "DataServiceCache::getAllDataSetInfos: Returning" << results.size() << "datasets";
    return results;
}

int DataServiceCache::findDataSetId(const QString& displayName) const
{
    rebuildIndexIfNeeded();
    
    QMutexLocker locker(&m_indexMutex);
    
    if (m_nameToIdIndex.contains(displayName)) {
        int dataId = m_nameToIdIndex[displayName];
        qDebug() << "DataServiceCache::findDataSetId: Found dataset" << displayName << "=> ID:" << dataId;
        return dataId;
    }
    
    qDebug() << "DataServiceCache::findDataSetId: Dataset not found:" << displayName;
    return -1;
}

bool DataServiceCache::removeDataSetById(int dataId)
{
    if (!m_initialized || dataId <= 0) {
        return false;
    }
    
    try {
        // 从缓存中删除数据和信息
        QString dataKey = generateDataSetKey(dataId);
        QString infoKey = generateDataSetInfoKey(dataId);
        
        bool removed = m_cacheFacade->remove(dataKey.toStdString());
        removed = m_cacheFacade->remove(infoKey.toStdString()) || removed;
        
        if (removed) {
            // 从索引中移除
            removeFromIndex(dataId);
            
            // 更新统计
            QMutexLocker locker(&m_statsMutex);
            if (m_stats.size > 0) {
                m_stats.size--;
            }
            
            qDebug() << "DataServiceCache::removeDataSetById: Removed dataset" << dataId;
            
            // 发送信号
            emit dataSetRemoved(dataId);
            emit cacheStatsUpdated(m_stats);
            
            return true;
        }
        
        return false;
        
    } catch (const std::exception& e) {
        QString error = QString("removeDataSetById error: %1").arg(e.what());
        qWarning() << "DataServiceCache::removeDataSetById:" << error;
        emit cacheError(error);
        return false;
    }
}
