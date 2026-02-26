// DataServiceCache.cpp
// DataService缓存集成实现

#include "DataServiceCache.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QCryptographicHash>

// 缓存门面头文件
#include "../../../cache/include/cache_facade.h"

using namespace AStockQuantEngine::Cache;

// 单例实例
static DataServiceCache* g_instance = nullptr;

DataServiceCache::DataServiceCache(QObject* parent)
    : QObject(parent)
    , m_stats{0, 0, 0, 0.0, ""}
{
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
        
        qDebug() << "DataServiceCache::storeData: Stored" << data.size() 
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
    // 注意：此方法在当前的CacheFacade实现中可能不支持
    // 返回空列表，因为模式匹配获取所有key的功能需要缓存后端支持
    qDebug() << "DataServiceCache::getAllDataKeys: Functionality not implemented in current cache backend";
    return QStringList();
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
