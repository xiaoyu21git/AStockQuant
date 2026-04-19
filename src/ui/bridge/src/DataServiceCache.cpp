// DataServiceCache.cpp
// DataService缓存集成实现

#include "DataServiceCache.h"
#include "DataManager.h"  // 添加DataManager头文件
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

// 缓存门面头文件
#include "../../../cache/include/cache_facade.h"

using namespace AStockQuantEngine::Cache;

namespace {
bool shouldExposeDataKey(const QString& key)
{
    if (key.isEmpty() || key == "current_stock_data") {
        return false;
    }

    if (key.startsWith("factor_") || key.startsWith("session:") || key.startsWith("cleaning:")) {
        return false;
    }

    return key.startsWith("data:stock:") || key.startsWith("index_") || key.startsWith("dataset_");
}

bool shouldCreateDataSetForStoreKey(const QString& key)
{
    if (!shouldExposeDataKey(key)) {
        return false;
    }

    // data:stock: 走 cacheData 路径时已经会生成数据集，storeData 不再重复生成。
    return !key.startsWith("data:stock:");
}

QString extractSourceCacheKey(const QString& description)
{
    static const QString kCachePrefix = QStringLiteral("从缓存存储的数据: ");
    static const QString kStorePrefix = QStringLiteral("从通用缓存存储的数据: ");

    if (description.startsWith(kCachePrefix)) {
        return description.mid(kCachePrefix.size());
    }

    if (description.startsWith(kStorePrefix)) {
        return description.mid(kStorePrefix.size());
    }

    return QString();
}

QVariantList toVariantList(const QVector<int>& values)
{
    QVariantList result;
    result.reserve(values.size());
    for (int value : values) {
        result.append(value);
    }
    return result;
}

QVector<int> toIntVector(const QVariantList& values)
{
    QVector<int> result;
    result.reserve(values.size());

    QSet<int> seen;
    for (const QVariant& value : values) {
        const int parsed = value.toInt();
        if (parsed <= 0 || seen.contains(parsed)) {
            continue;
        }

        seen.insert(parsed);
        result.append(parsed);
    }

    std::sort(result.begin(), result.end());
    return result;
}
}

DataServiceCache::DataServiceCache(QObject* parent)
    : QObject(parent)
    , m_stats{0, 0, 0, 0.0, ""}
{
    // m_dataKeys 默认为空，不需要显式初始化
    // m_dataKeysMutex 默认可构造，不需要显式初始化
}




DataServiceCache::~DataServiceCache()
{
    // 缓存应该一直存在，不应该被销毁
    // 如果被销毁，说明有严重问题
    qCritical() << "DataServiceCache: WARNING - Cache is being destroyed! This should not happen!";
}

DataServiceCache& DataServiceCache::getInstance()
{
    // 缓存设计上是进程级常驻对象，不应在静态析构阶段销毁。
    static DataServiceCache* instance = new DataServiceCache();
    return *instance;
}

bool DataServiceCache::initializeCache()
{
    QMutexLocker locker(&m_statsMutex);
    
    if (m_initialized) {
        m_cacheFacade = &AStockQuantEngine::Cache::CacheFacade::getInstance();
        if (m_cacheFacade != nullptr && m_cacheFacade->isEnabled()) {
            qDebug() << "DataServiceCache: Already initialized";
            return true;
        }

        qDebug() << "DataServiceCache: Cache facade was shut down, reinitializing";
        m_initialized = false;
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
        {
            QMutexLocker locker(&m_statsMutex);
            m_stats.misses++;
        }
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
                CacheStats statsSnapshot;
                {
                    QMutexLocker locker(&m_statsMutex);
                    m_stats.hits++;
                    m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
                    statsSnapshot = m_stats;
                }
                
                emit cacheHit(key, "data");
                emit cacheStatsUpdated(statsSnapshot);
                
                qDebug() << "DataServiceCache: Cache hit for" << key << ", data size:" << data.size();
                return data;
            }
        }
        
        // 缓存未命中
        CacheStats statsSnapshot;
        {
            QMutexLocker locker(&m_statsMutex);
            m_stats.misses++;
            m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
            statsSnapshot = m_stats;
        }
        
        emit cacheMiss(key, "data");
        emit cacheStatsUpdated(statsSnapshot);
        
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
        qDebug() << "DataServiceCache::cacheData: Cache not initialized or data empty";
        return;
    }
    
    QString key; // 在函数作用域声明变量
    try {
        key = generateDataKey(symbol, startDate, endDate);
        qDebug() << "DataServiceCache::cacheData: Generating key:" << key 
                 << "for symbol:" << (symbol.isEmpty() ? "ALL" : symbol) 
                 << "startDate:" << startDate << "endDate:" << endDate;
        
        // 序列化数据
        QByteArray dataBytes = serializeData(data);
        qDebug() << "DataServiceCache::cacheData: Serialized" << data.size() 
                 << "items to" << dataBytes.size() << "bytes";
        std::string cacheData(dataBytes.constData(), dataBytes.size());
        
        // 存储到缓存
        m_cacheFacade->set(key.toStdString(), cacheData, 
                          std::chrono::seconds(m_config.dataCacheTTL));
        
        qDebug() << "✅ DataServiceCache::cacheData: Successfully stored data with key:" << key;
        
        // 将键添加到数据集列表
        {
            QMutexLocker keysLocker(&m_dataKeysMutex);
            m_dataKeys.insert(key);
        }
        qDebug() << "DataServiceCache::cacheData: Added key to m_dataKeys:" << key 
                 << ", total keys now:" << m_dataKeys.size();
        
        // 更新统计
        {
            QMutexLocker locker(&m_statsMutex);
            m_stats.size++;
        }
        
        qDebug() << "DataServiceCache::cacheData: Cached data for" << key 
                 << ", size:" << data.size() << "records, added to data keys";
        
    } catch (const std::exception& e) {
        QString error = QString("Cache set error: %1").arg(e.what());
        qWarning() << "DataServiceCache::cacheData:" << error;
        emit cacheError(error);
        return; // 如果主缓存存储失败，直接返回
    }
    
    // 同时创建数据集信息，以便 getAllDataSetInfos() 可以返回数据
    // 这会确保 cleanDataFromCacheByIndex 可以找到数据集
    try {
        rebuildIndexIfNeeded();
        DataSetInfo dataSetInfo;
        const QString displayName = QString("缓存数据: %1 %2-%3").arg(
            symbol.isEmpty() ? "ALL" : symbol).arg(startDate).arg(endDate);
        bool updatingExisting = false;
        {
            QMutexLocker locker(&m_indexMutex);
            if (m_nameToIdIndex.contains(displayName)) {
                dataSetInfo.id = m_nameToIdIndex.value(displayName);
                updatingExisting = true;
            } else {
                dataSetInfo.id = m_nextDataSetId++; // 使用自动生成的ID
            }
        }
        dataSetInfo.displayName = displayName;
        dataSetInfo.description = QString("从缓存存储的数据: %1").arg(key);
        dataSetInfo.sourceType = "cache";
        dataSetInfo.createdTime = QDateTime::currentDateTime();
        dataSetInfo.rowCount = data.size();
        dataSetInfo.stockCodes = symbol.isEmpty() ? QStringList() : QStringList{symbol};
        dataSetInfo.startDate = QDate::fromString(startDate, "yyyy-MM-dd");
        dataSetInfo.endDate = QDate::fromString(endDate, "yyyy-MM-dd");
        dataSetInfo.tags = QStringList{"cached", "data"};
        
        // 添加到索引
        if (updatingExisting) {
            updateIndex(dataSetInfo.id, dataSetInfo);
        } else {
            addToIndex(dataSetInfo.id, dataSetInfo);
        }
        
        // 同时存储数据集信息到缓存
        QByteArray infoBytes = serializeDataSetInfo(dataSetInfo);
        std::string cacheInfo(infoBytes.constData(), infoBytes.size());
        
        QString infoKey = generateDataSetInfoKey(dataSetInfo.id);
        m_cacheFacade->set(infoKey.toStdString(), cacheInfo,
                          std::chrono::seconds(m_config.dataCacheTTL));
        
        // 注意：自动生成的数据集仅保存元信息，真实数据仍只保存在原始 key 下，
        // 避免同一份数据在缓存层出现第二份副本。
        // 注意：不再在DataManager中存储，避免双重存储和内存浪费
        // DataManager::instance()->storeData(key, data);
        // DataManager::instance()->storeData(QString::number(dataSetInfo.id), data);
        
        qDebug() << "DataServiceCache::cacheData:" << (updatingExisting ? "Updated" : "Created")
             << "dataset info ID:" << dataSetInfo.id 
             << "for key:" << key;
        
    } catch (const std::exception& e) {
        qWarning() << "DataServiceCache::cacheData: Failed to create dataset info:" << e.what();
        // 不抛出异常，数据集信息是可选的
    }
}

QVariantList DataServiceCache::getCachedCleaningResult(const QString& requestId)
{
    if (!m_initialized || !m_config.enabled) {
        {
            QMutexLocker locker(&m_statsMutex);
            m_stats.misses++;
        }
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
                CacheStats statsSnapshot;
                {
                    QMutexLocker locker(&m_statsMutex);
                    m_stats.hits++;
                    m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
                    statsSnapshot = m_stats;
                }
                
                emit cacheHit(key, "cleaning");
                emit cacheStatsUpdated(statsSnapshot);
                
                qDebug() << "DataServiceCache: Cache hit for cleaning result" << key;
                return data;
            }
        }
        
        // 缓存未命中
        CacheStats statsSnapshot;
        {
            QMutexLocker locker(&m_statsMutex);
            m_stats.misses++;
            m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
            statsSnapshot = m_stats;
        }
        
        emit cacheMiss(key, "cleaning");
        emit cacheStatsUpdated(statsSnapshot);
        
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
                CacheStats statsSnapshot;
                {
                    QMutexLocker locker(&m_statsMutex);
                    m_stats.hits++;
                    m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
                    statsSnapshot = m_stats;
                }
                
                emit cacheHit(key, "session");
                emit cacheStatsUpdated(statsSnapshot);
                
                return data;
            }
        }
        
        // 缓存未命中
        CacheStats statsSnapshot;
        {
            QMutexLocker locker(&m_statsMutex);
            m_stats.misses++;
            m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
            statsSnapshot = m_stats;
        }
        
        emit cacheMiss(key, "session");
        emit cacheStatsUpdated(statsSnapshot);
        
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
    CacheStats statsSnapshot;
    {
        QMutexLocker locker(&m_statsMutex);
        m_stats = CacheStats{0, 0, 0, 0.0, ""};
        statsSnapshot = m_stats;
    }
    emit cacheStatsUpdated(statsSnapshot);
}

void DataServiceCache::clearAllCache()
{
    if (!m_initialized) {
        return;
    }
    
    try {
        m_cacheFacade->clear();

        {
            QMutexLocker keysLocker(&m_dataKeysMutex);
            m_dataKeys.clear();
        }

        {
            QMutexLocker indexLocker(&m_indexMutex);
            m_nextDataSetId = 1;
            m_dataSetIndex.clear();
            m_nameToIdIndex.clear();
            m_stockCodeIndex.clear();
            m_sourceTypeIndex.clear();
            m_indexNeedsRebuild = false;
        }
        
        CacheStats statsSnapshot;
        {
            QMutexLocker locker(&m_statsMutex);
            m_stats.size = 0;
            statsSnapshot = m_stats;
        }
        
        qDebug() << "DataServiceCache: All cache cleared";
        emit cacheStatsUpdated(statsSnapshot);
        
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

QByteArray DataServiceCache::serializeData(const QVariantList& data,
                                          const std::function<void(int current, int total)>& progressCallback) const
{
    QJsonArray jsonArray;
    const int total = data.size();
    if (progressCallback) {
        progressCallback(0, total);
    }
    
    for (int index = 0; index < data.size(); ++index) {
        const QVariant& item = data[index];
        if (item.type() == QVariant::Map) {
            QVariantMap map = item.toMap();
            QJsonObject jsonObj;
            
            for (auto it = map.begin(); it != map.end(); ++it) {
                jsonObj[it.key()] = QJsonValue::fromVariant(it.value());
            }
            
            jsonArray.append(jsonObj);
        }

        if (progressCallback && ((index + 1) == total || ((index + 1) % 500 == 0))) {
            progressCallback(index + 1, total);
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
        
        // 只保存一份原始键，作为唯一真实数据源。
        m_cacheFacade->set(key.toStdString(), cacheData,
                          std::chrono::seconds(m_config.dataCacheTTL));
        
        if (shouldExposeDataKey(key)) {
            QMutexLocker keysLocker(&m_dataKeysMutex);
            m_dataKeys.insert(key);
        }

        // 为 storeData 路径补建数据集索引，避免 getAllDataSetInfos() 为空。
        if (shouldCreateDataSetForStoreKey(key)) {
            rebuildIndexIfNeeded();
            try {
                DataSetInfo dataSetInfo;
                bool updatingExisting = false;
                {
                    QMutexLocker locker(&m_indexMutex);
                    if (m_nameToIdIndex.contains(key)) {
                        dataSetInfo.id = m_nameToIdIndex.value(key);
                        updatingExisting = true;
                    } else {
                        dataSetInfo.id = m_nextDataSetId++;
                    }
                }

                dataSetInfo.displayName = key;
                dataSetInfo.description = QString("从通用缓存存储的数据: %1").arg(key);
                dataSetInfo.sourceType = key.startsWith("index_") ? "index" : "store";
                dataSetInfo.createdTime = QDateTime::currentDateTime();
                dataSetInfo.rowCount = data.size();
                dataSetInfo.tags = QStringList{"cached", "storeData"};

                QStringList keyParts = key.split('_');
                if (keyParts.size() >= 2) {
                    const QString& lastPart = keyParts[keyParts.size() - 1];
                    const QString& secondLastPart = keyParts[keyParts.size() - 2];
                    QDate parsedStart = QDate::fromString(secondLastPart, "yyyy-MM-dd");
                    QDate parsedEnd = QDate::fromString(lastPart, "yyyy-MM-dd");
                    if (parsedStart.isValid() && parsedEnd.isValid()) {
                        dataSetInfo.startDate = parsedStart;
                        dataSetInfo.endDate = parsedEnd;
                    }
                }

                if (key.startsWith("index_")) {
                    QStringList keyParts = key.split('_');
                    if (keyParts.size() >= 2) {
                        dataSetInfo.stockCodes = QStringList{keyParts[1]};
                    }
                    dataSetInfo.tags.append("index");
                }

                if (updatingExisting) {
                    updateIndex(dataSetInfo.id, dataSetInfo);
                } else {
                    addToIndex(dataSetInfo.id, dataSetInfo);
                }

                QByteArray infoBytes = serializeDataSetInfo(dataSetInfo);
                std::string cacheInfo(infoBytes.constData(), infoBytes.size());

                QString infoKey = generateDataSetInfoKey(dataSetInfo.id);
                m_cacheFacade->set(infoKey.toStdString(), cacheInfo,
                                  std::chrono::seconds(m_config.dataCacheTTL));

                qDebug() << "DataServiceCache::storeData:" << (updatingExisting ? "Updated" : "Created")
                         << "dataset info ID:" << dataSetInfo.id << "for key:" << key;
            } catch (const std::exception& e) {
                qWarning() << "DataServiceCache::storeData: Failed to create dataset info:" << e.what();
            }
        }

        CacheStats statsSnapshot;
        {
            QMutexLocker locker(&m_statsMutex);
            m_stats.size++;
            statsSnapshot = m_stats;
        }
        
        qDebug() << "✅ DataServiceCache::storeData: Stored" << data.size() 
                 << "items with key" << key;
        
        // 发送信号
        emit cacheStatsUpdated(statsSnapshot);
        
    } catch (const std::exception& e) {
        QString error = QString("Cache storeData error: %1").arg(e.what());
        qWarning() << "DataServiceCache::storeData:" << error;
        emit cacheError(error);
    }
}

QVariantList DataServiceCache::getData(const QString& key)
{
    if (!m_initialized || !m_config.enabled) {
        {
            QMutexLocker locker(&m_statsMutex);
            m_stats.misses++;
        }
        qDebug() << "DataServiceCache::getData: Cache not initialized for key" << key;
        return QVariantList();
    }
    
    try {
        // 尝试从缓存获取
        std::string cachedData;
        if (m_cacheFacade->get(key.toStdString(), cachedData)) {
            // 反序列化数据
            QByteArray dataBytes(cachedData.c_str(), cachedData.size());
            QVariantList data = deserializeData(dataBytes);
            
            if (!data.isEmpty()) {
                CacheStats statsSnapshot;
                {
                    QMutexLocker locker(&m_statsMutex);
                    m_stats.hits++;
                    m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
                    statsSnapshot = m_stats;
                }
                
                qDebug() << "DataServiceCache::getData: Retrieved" << data.size() 
                         << "items with key" << key << "(raw key)";
                emit cacheStatsUpdated(statsSnapshot);
                return data;
            }
        }
        
        // 如果前面都没找到，尝试解析data:stock:格式的键，调用getCachedData
        if (key.startsWith("data:stock:")) {
            qDebug() << "DataServiceCache::getData: Key is data:stock format, attempting to parse and use getCachedData";
            // 解析格式：data:stock:[symbol]_[startDate]_[endDate]
            QString suffix = key.mid(11); // 移除"data:stock:"前缀
            QStringList parts = suffix.split('_');
            if (parts.size() >= 3) {
                QString symbol = parts[0];
                QString startDate = parts[1];
                QString endDate = parts[2];
                
                // 对于"ALL"符号，需要转换为空字符串以匹配getCachedData的预期
                if (symbol == "ALL") {
                    symbol = "";
                }
                
                qDebug() << "DataServiceCache::getData: Parsed params - symbol:" << symbol 
                         << "startDate:" << startDate << "endDate:" << endDate;
                
                QVariantList cachedData = getCachedData(symbol, startDate, endDate);
                if (!cachedData.isEmpty()) {
                    CacheStats statsSnapshot;
                    {
                        QMutexLocker locker(&m_statsMutex);
                        m_stats.hits++;
                        m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
                        statsSnapshot = m_stats;
                    }
                    
                    qDebug() << "DataServiceCache::getData: Retrieved" << cachedData.size() 
                             << "items via getCachedData for key" << key;
                    emit cacheStatsUpdated(statsSnapshot);
                    return cachedData;
                }
            }
        }
        
                // 注意：不再从DataManager获取数据，避免双重存储
        // DataManager现在只用于简单的内存缓存，不用于大数据存储
        
        // 缓存未命中
        CacheStats statsSnapshot;
        {
            QMutexLocker locker(&m_statsMutex);
            m_stats.misses++;
            m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
            statsSnapshot = m_stats;
        }
        
        qDebug() << "DataServiceCache::getData: No data found for key" << key 
                 << "(tried raw key and data:stock parsing)";

        // 数据已过期或底层缓存不存在时，顺手清理残留的展示键，避免 UI 看到无效条目。
        {
            QMutexLocker keysLocker(&m_dataKeysMutex);
            m_dataKeys.remove(key);
        }

        emit cacheStatsUpdated(statsSnapshot);
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
        // 检查缓存是否存在
        bool exists = m_cacheFacade->exists(key.toStdString());
        
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
        bool removed = m_cacheFacade->remove(key.toStdString());

        int dataSetIdToRemove = -1;
        {
            QMutexLocker indexLocker(&m_indexMutex);
            if (m_nameToIdIndex.contains(key)) {
                dataSetIdToRemove = m_nameToIdIndex.value(key);
            }
        }

        if (dataSetIdToRemove > 0) {
            QString dataSetKey = generateDataSetKey(dataSetIdToRemove);
            QString infoKey = generateDataSetInfoKey(dataSetIdToRemove);
            removed = m_cacheFacade->remove(dataSetKey.toStdString()) || removed;
            removed = m_cacheFacade->remove(infoKey.toStdString()) || removed;
            removePersistentDataSetFiles(dataSetIdToRemove);
            removeFromIndex(dataSetIdToRemove);
        }

        if (removed) {
            {
                QMutexLocker keysLocker(&m_dataKeysMutex);
                m_dataKeys.remove(key);
            }

            CacheStats statsSnapshot;
            {
                QMutexLocker locker(&m_statsMutex);
                if (m_stats.size > 0) {
                    m_stats.size--;
                }
                statsSnapshot = m_stats;
            }
            
            qDebug() << "DataServiceCache::removeData: Removed data with key" << key;
            emit cacheStatsUpdated(statsSnapshot);
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
        QStringList keysToRemove;
        {
            QMutexLocker keysLocker(&m_dataKeysMutex);
            keysToRemove = m_dataKeys.values();
            m_dataKeys.clear();
        }

        QVector<int> dataSetIds;
        {
            QMutexLocker indexLocker(&m_indexMutex);
            dataSetIds = m_dataSetIndex.keys().toVector();
            m_nextDataSetId = 1;
            m_dataSetIndex.clear();
            m_nameToIdIndex.clear();
            m_stockCodeIndex.clear();
            m_sourceTypeIndex.clear();
            m_indexNeedsRebuild = false;
        }

        for (const QString& key : keysToRemove) {
            m_cacheFacade->remove(key.toStdString());
        }

        for (int dataId : dataSetIds) {
            QString dataSetKey = generateDataSetKey(dataId);
            QString infoKey = generateDataSetInfoKey(dataId);
            m_cacheFacade->remove(dataSetKey.toStdString());
            m_cacheFacade->remove(infoKey.toStdString());
            removePersistentDataSetFiles(dataId);
        }

        m_cacheFacade->remove(generateDataSetCatalogKey().toStdString());
        clearPersistentDataSetFiles();
        
        CacheStats statsSnapshot;
        {
            QMutexLocker locker(&m_statsMutex);
            m_stats.size = 0;
            statsSnapshot = m_stats;
        }
        

        clearPersistentDataSetFiles();
        qDebug() << "DataServiceCache::clearAllData: All cached data cleared";
        emit cacheStatsUpdated(statsSnapshot);
        
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
    
    // 调试：打印所有键
    for (const QString& key : keys) {
        qDebug() << "  Key:" << key;
    }
    
    // 如果m_dataKeys为空，尝试从缓存门面重建索引
    if (keys.isEmpty() && m_initialized && m_cacheFacade) {
        qDebug() << "DataServiceCache::getAllDataKeys: m_dataKeys is empty, attempting to rebuild from cache facade";
        // 注意：当前CacheFacade实现可能不支持列出所有键
        // 对于调试，我们可以尝试扫描已知模式
        // 这只是临时解决方案，需要CacheFacade支持列出键的功能
    }
    
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

QString DataServiceCache::generateDataSetCatalogKey() const
{
    return QStringLiteral("dataset:catalog");
}

QString DataServiceCache::persistentDataSetRootDir() const
{
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (baseDir.isEmpty()) {
        baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    if (baseDir.isEmpty()) {
        baseDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data_service_cache"));
    }
    return QDir(baseDir).filePath(QStringLiteral("datasets"));
}

QString DataServiceCache::persistentDataSetDataFilePath(int dataId) const
{
    return QDir(persistentDataSetRootDir()).filePath(QStringLiteral("dataset_%1_data.json").arg(dataId));
}

QString DataServiceCache::persistentDataSetInfoFilePath(int dataId) const
{
    return QDir(persistentDataSetRootDir()).filePath(QStringLiteral("dataset_%1_info.json").arg(dataId));
}

QString DataServiceCache::persistentDataSetCatalogFilePath() const
{
    return QDir(persistentDataSetRootDir()).filePath(QStringLiteral("dataset_catalog.json"));
}

bool DataServiceCache::ensurePersistentDataSetRootDir() const
{
    QDir dir(persistentDataSetRootDir());
    if (dir.exists()) {
        return true;
    }
    return QDir().mkpath(dir.path());
}

bool DataServiceCache::writePersistentCacheFile(const QString& filePath, const QByteArray& data) const
{
    if (!ensurePersistentDataSetRootDir()) {
        qWarning() << "DataServiceCache: Failed to create persistent dataset cache dir:" << persistentDataSetRootDir();
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "DataServiceCache: Failed to open persistent cache file for write:" << filePath;
        return false;
    }

    if (file.write(data) != data.size()) {
        qWarning() << "DataServiceCache: Failed to write persistent cache file:" << filePath;
        return false;
    }

    if (!file.commit()) {
        qWarning() << "DataServiceCache: Failed to commit persistent cache file:" << filePath;
        return false;
    }

    return true;
}

QByteArray DataServiceCache::readPersistentCacheFile(const QString& filePath) const
{
    QFile file(filePath);
    if (!file.exists()) {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "DataServiceCache: Failed to open persistent cache file for read:" << filePath;
        return {};
    }
    return file.readAll();
}

void DataServiceCache::removePersistentDataSetFiles(int dataId) const
{
    if (dataId <= 0) {
        return;
    }
    QFile::remove(persistentDataSetDataFilePath(dataId));
    QFile::remove(persistentDataSetInfoFilePath(dataId));
}

void DataServiceCache::clearPersistentDataSetFiles() const
{
    const QString rootDir = persistentDataSetRootDir();
    if (rootDir.isEmpty()) {
        return;
    }

    QDir dir(rootDir);
    if (!dir.exists()) {
        return;
    }

    dir.removeRecursively();
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
    jsonObj["schemaVersion"] = info.schemaVersion;
    jsonObj["isBacktestReady"] = info.isBacktestReady;
    
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

    QJsonArray availableFieldsArray;
    for (const QString& field : info.availableFields) {
        availableFieldsArray.append(field);
    }
    jsonObj["availableFields"] = availableFieldsArray;
    
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
    info.schemaVersion = jsonObj["schemaVersion"].toInt(1);
    info.isBacktestReady = jsonObj["isBacktestReady"].toBool(false);
    
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

    if (jsonObj.contains("availableFields")) {
        QJsonArray availableFieldsArray = jsonObj["availableFields"].toArray();
        for (const QJsonValue& value : availableFieldsArray) {
            info.availableFields.append(value.toString());
        }
    }
    
    return info;
}

void DataServiceCache::persistDataSetCatalog(const QVector<int>& dataSetIds, int nextDataSetId) const
{
    if (!m_initialized || !m_config.enabled) {
        return;
    }

    QVariantMap catalog;
    catalog["ids"] = toVariantList(dataSetIds);
    catalog["nextDataSetId"] = std::max(1, nextDataSetId);

    const QByteArray catalogBytes = serializeMap(catalog);
    std::string cacheCatalog(catalogBytes.constData(), catalogBytes.size());
    m_cacheFacade->set(generateDataSetCatalogKey().toStdString(),
                      cacheCatalog,
                      std::chrono::seconds(m_config.dataCacheTTL));
    writePersistentCacheFile(persistentDataSetCatalogFilePath(), catalogBytes);
}

bool DataServiceCache::loadDataSetCatalog(QVector<int>& dataSetIds, int& nextDataSetId) const
{
    dataSetIds.clear();
    nextDataSetId = 1;

    if (!m_initialized || !m_config.enabled) {
        return false;
    }

    std::string cachedCatalog;
    QByteArray catalogBytes;
    if (m_cacheFacade->get(generateDataSetCatalogKey().toStdString(), cachedCatalog)) {
        catalogBytes = QByteArray(cachedCatalog.c_str(), cachedCatalog.size());
    } else {
        catalogBytes = readPersistentCacheFile(persistentDataSetCatalogFilePath());
        if (catalogBytes.isEmpty()) {
            return false;
        }
    }

    const QVariantMap catalog = deserializeMap(catalogBytes);
    dataSetIds = toIntVector(catalog.value("ids").toList());

    const int maxCatalogId = dataSetIds.isEmpty() ? 0 : dataSetIds.last();
    const QVariant nextIdVariant = catalog.value("nextDataSetId");
    const int catalogNextDataSetId = nextIdVariant.isValid() ? nextIdVariant.toInt() : 1;
    nextDataSetId = std::max(catalogNextDataSetId, maxCatalogId + 1);
    return !catalog.isEmpty();
}

void DataServiceCache::rebuildIndexIfNeeded() const
{
    bool shouldRebuild = false;
    {
        QMutexLocker locker(&m_indexMutex);
        shouldRebuild = m_indexNeedsRebuild || m_dataSetIndex.isEmpty();
    }

    if (!shouldRebuild) {
        return;
    }

    qDebug() << "DataServiceCache: Rebuilding dataset index from catalog...";

    QVector<int> catalogIds;
    int nextDataSetId = 1;
    const bool hasCatalog = loadDataSetCatalog(catalogIds, nextDataSetId);

    QMap<int, DataSetInfo> rebuiltDataSetIndex;
    QMap<QString, int> rebuiltNameToIdIndex;
    QMap<QString, QSet<int>> rebuiltStockCodeIndex;
    QMap<QString, QSet<int>> rebuiltSourceTypeIndex;
    int maxSeenDataSetId = 0;

    for (int dataId : catalogIds) {
        QString infoKey = generateDataSetInfoKey(dataId);
        std::string cachedInfo;
        QByteArray infoBytes;
        if (m_cacheFacade->get(infoKey.toStdString(), cachedInfo)) {
            infoBytes = QByteArray(cachedInfo.c_str(), cachedInfo.size());
        } else {
            infoBytes = readPersistentCacheFile(persistentDataSetInfoFilePath(dataId));
            if (infoBytes.isEmpty()) {
                continue;
            }
        }

        const DataSetInfo info = deserializeDataSetInfo(infoBytes);
        if (info.id <= 0) {
            continue;
        }

        rebuiltDataSetIndex[info.id] = info;
        rebuiltNameToIdIndex[info.displayName] = info.id;
        for (const QString& stockCode : info.stockCodes) {
            rebuiltStockCodeIndex[stockCode].insert(info.id);
        }
        rebuiltSourceTypeIndex[info.sourceType].insert(info.id);
        maxSeenDataSetId = std::max(maxSeenDataSetId, info.id);
    }

    {
        QMutexLocker locker(&m_indexMutex);
        m_dataSetIndex = rebuiltDataSetIndex;
        m_nameToIdIndex = rebuiltNameToIdIndex;
        m_stockCodeIndex = rebuiltStockCodeIndex;
        m_sourceTypeIndex = rebuiltSourceTypeIndex;
        m_nextDataSetId = std::max(nextDataSetId, maxSeenDataSetId + 1);
        m_indexNeedsRebuild = false;
    }

    if (hasCatalog && rebuiltDataSetIndex.size() != catalogIds.size()) {
        persistDataSetCatalog(rebuiltDataSetIndex.keys().toVector(), std::max(nextDataSetId, maxSeenDataSetId + 1));
    }

    qDebug() << "DataServiceCache: Rebuilt dataset index with" << rebuiltDataSetIndex.size() << "datasets";
}

void DataServiceCache::addToIndex(int dataId, const DataSetInfo& info)
{
    int nextDataSetId = 1;
    QVector<int> dataSetIds;
    {
        QMutexLocker locker(&m_indexMutex);

        m_dataSetIndex[dataId] = info;
        m_nameToIdIndex[info.displayName] = dataId;

        for (const QString& code : info.stockCodes) {
            m_stockCodeIndex[code].insert(dataId);
        }

        m_sourceTypeIndex[info.sourceType].insert(dataId);
        m_indexNeedsRebuild = false;
        nextDataSetId = m_nextDataSetId;
        dataSetIds = m_dataSetIndex.keys().toVector();
    }

    persistDataSetCatalog(dataSetIds, nextDataSetId);
    qDebug() << "DataServiceCache: Added dataset" << dataId << "to index:" << info.displayName;
}

void DataServiceCache::removeFromIndex(int dataId)
{
    int nextDataSetId = 1;
    QVector<int> dataSetIds;
    {
        QMutexLocker locker(&m_indexMutex);

        if (!m_dataSetIndex.contains(dataId)) {
            return;
        }

        const DataSetInfo info = m_dataSetIndex[dataId];

        m_nameToIdIndex.remove(info.displayName);

        for (const QString& code : info.stockCodes) {
            if (m_stockCodeIndex.contains(code)) {
                m_stockCodeIndex[code].remove(dataId);
                if (m_stockCodeIndex[code].isEmpty()) {
                    m_stockCodeIndex.remove(code);
                }
            }
        }

        if (m_sourceTypeIndex.contains(info.sourceType)) {
            m_sourceTypeIndex[info.sourceType].remove(dataId);
            if (m_sourceTypeIndex[info.sourceType].isEmpty()) {
                m_sourceTypeIndex.remove(info.sourceType);
            }
        }

        m_dataSetIndex.remove(dataId);
        m_indexNeedsRebuild = false;
        nextDataSetId = m_nextDataSetId;
        dataSetIds = m_dataSetIndex.keys().toVector();
    }

    persistDataSetCatalog(dataSetIds, nextDataSetId);
    qDebug() << "DataServiceCache: Removed dataset" << dataId << "from index";
}

void DataServiceCache::updateIndex(int dataId, const DataSetInfo& info)
{
    int nextDataSetId = 1;
    QVector<int> dataSetIds;
    {
        QMutexLocker locker(&m_indexMutex);

        if (m_dataSetIndex.contains(dataId)) {
            const DataSetInfo oldInfo = m_dataSetIndex[dataId];
            m_nameToIdIndex.remove(oldInfo.displayName);

            for (const QString& code : oldInfo.stockCodes) {
                if (m_stockCodeIndex.contains(code)) {
                    m_stockCodeIndex[code].remove(dataId);
                    if (m_stockCodeIndex[code].isEmpty()) {
                        m_stockCodeIndex.remove(code);
                    }
                }
            }

            if (m_sourceTypeIndex.contains(oldInfo.sourceType)) {
                m_sourceTypeIndex[oldInfo.sourceType].remove(dataId);
                if (m_sourceTypeIndex[oldInfo.sourceType].isEmpty()) {
                    m_sourceTypeIndex.remove(oldInfo.sourceType);
                }
            }
        }

        m_dataSetIndex[dataId] = info;
        m_nameToIdIndex[info.displayName] = dataId;
        for (const QString& code : info.stockCodes) {
            m_stockCodeIndex[code].insert(dataId);
        }
        m_sourceTypeIndex[info.sourceType].insert(dataId);
        m_indexNeedsRebuild = false;
        nextDataSetId = m_nextDataSetId;
        dataSetIds = m_dataSetIndex.keys().toVector();
    }

    persistDataSetCatalog(dataSetIds, nextDataSetId);
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

int DataServiceCache::storeDataSet(const QVariantList& data,
                                  const DataSetInfo& info,
                                  const std::function<void(int current, int total)>& progressCallback)
{
    if (!m_initialized || !m_config.enabled || data.isEmpty()) {
        qDebug() << "DataServiceCache::storeDataSet: Cache not initialized or data empty";
        return -1;
    }
    
    try {
        rebuildIndexIfNeeded();
        DataSetInfo completeInfo = info;

        int dataId = -1;
        {
            QMutexLocker locker(&m_indexMutex);

            // 生成新的数据集ID
            dataId = m_nextDataSetId++;
        }

        // 更新数据集信息
        completeInfo.id = dataId;
        completeInfo.rowCount = data.size();
        if (!completeInfo.createdTime.isValid()) {
            completeInfo.createdTime = QDateTime::currentDateTime();
        }
        
        // 生成缓存键
        QString dataKey = generateDataSetKey(dataId);
        QString infoKey = generateDataSetInfoKey(dataId);
        
        // 序列化数据
        QByteArray dataBytes = serializeData(data, progressCallback);
        std::string cacheData(dataBytes.constData(), dataBytes.size());
        
        // 序列化数据集信息
        QByteArray infoBytes = serializeDataSetInfo(completeInfo);
        std::string cacheInfo(infoBytes.constData(), infoBytes.size());
        
        // 存储到缓存
        m_cacheFacade->set(dataKey.toStdString(), cacheData, 
                          std::chrono::seconds(m_config.dataCacheTTL));
        m_cacheFacade->set(infoKey.toStdString(), cacheInfo,
                          std::chrono::seconds(m_config.dataCacheTTL));
        writePersistentCacheFile(persistentDataSetDataFilePath(dataId), dataBytes);
        writePersistentCacheFile(persistentDataSetInfoFilePath(dataId), infoBytes);
        
        // 添加到索引。这里不能在持有 m_indexMutex 时再次调用 addToIndex，
        // 否则 QMutex 会发生同线程二次加锁卡死。
        addToIndex(dataId, completeInfo);
        
        CacheStats statsSnapshot;
        {
            QMutexLocker statsLocker(&m_statsMutex);
            m_stats.size++;
            statsSnapshot = m_stats;
        }
        
        qDebug() << "✅ DataServiceCache::storeDataSet: Stored dataset" << dataId 
                 << "with" << data.size() << "rows:" << completeInfo.displayName;
        
        // 信号必须在解锁后发送，避免槽函数同步回调时再次获取缓存锁导致卡死。
        emit dataSetStored(dataId, completeInfo);
        emit cacheStatsUpdated(statsSnapshot);
        
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
                CacheStats statsSnapshot;
                {
                    QMutexLocker locker(&m_statsMutex);
                    m_stats.hits++;
                    m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
                    statsSnapshot = m_stats;
                }
                
                qDebug() << "DataServiceCache::getDataSetById: Retrieved dataset" 
                         << dataId << "with" << data.size() << "rows";
                emit cacheStatsUpdated(statsSnapshot);
                return data;
            }
        }

        const QByteArray persistedDataBytes = readPersistentCacheFile(persistentDataSetDataFilePath(dataId));
        if (!persistedDataBytes.isEmpty()) {
            const QVariantList persistedData = deserializeData(persistedDataBytes);
            if (!persistedData.isEmpty()) {
                std::string persistedCacheData(persistedDataBytes.constData(), persistedDataBytes.size());
                m_cacheFacade->set(dataKey.toStdString(),
                                  persistedCacheData,
                                  std::chrono::seconds(m_config.dataCacheTTL));
                qDebug() << "DataServiceCache::getDataSetById: Loaded dataset" << dataId
                         << "from persistent storage with" << persistedData.size() << "rows";
                return persistedData;
            }
        }

        DataSetInfo info = getDataSetInfo(dataId);
        QString sourceCacheKey = extractSourceCacheKey(info.description);
        if (!sourceCacheKey.isEmpty()) {
            qDebug() << "DataServiceCache::getDataSetById: Dataset" << dataId
                     << "resolved to source cache key:" << sourceCacheKey;
            const QVariantList sourceData = getData(sourceCacheKey);
            if (!sourceData.isEmpty()) {
                return sourceData;
            }

            qWarning() << "DataServiceCache::getDataSetById: Source cache key returned no data for dataset"
                       << dataId << sourceCacheKey;
        }
        
        // 缓存未命中
        CacheStats statsSnapshot;
        {
            QMutexLocker locker(&m_statsMutex);
            m_stats.misses++;
            m_stats.hitRate = static_cast<double>(m_stats.hits) / (m_stats.hits + m_stats.misses);
            statsSnapshot = m_stats;
        }
        
        qDebug() << "DataServiceCache::getDataSetById: Dataset" << dataId << "not found";
        emit cacheStatsUpdated(statsSnapshot);
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

    rebuildIndexIfNeeded();
    
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
        QByteArray infoBytes;
        if (m_cacheFacade->get(infoKey.toStdString(), cachedInfo)) {
            infoBytes = QByteArray(cachedInfo.c_str(), cachedInfo.size());
        } else {
            infoBytes = readPersistentCacheFile(persistentDataSetInfoFilePath(dataId));
            if (!infoBytes.isEmpty()) {
                std::string cacheInfo(infoBytes.constData(), infoBytes.size());
                m_cacheFacade->set(infoKey.toStdString(),
                                  cacheInfo,
                                  std::chrono::seconds(m_config.dataCacheTTL));
            }
        }

        if (!infoBytes.isEmpty()) {
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
    QVector<DataSetInfo> results;
    results.reserve(m_nameToIdIndex.size());

    for (auto it = m_nameToIdIndex.constBegin(); it != m_nameToIdIndex.constEnd(); ++it) {
        const int dataId = it.value();
        if (m_dataSetIndex.contains(dataId)) {
            results.append(m_dataSetIndex.value(dataId));
        }
    }
    
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
        const bool removedPersistentData = QFile::remove(persistentDataSetDataFilePath(dataId));
        const bool removedPersistentInfo = QFile::remove(persistentDataSetInfoFilePath(dataId));
        const bool removedPersistent = removedPersistentData || removedPersistentInfo;
        removed = removed || removedPersistent;
        
        if (removed) {
            // 从索引中移除
            removeFromIndex(dataId);
            
            CacheStats statsSnapshot;
            {
                QMutexLocker locker(&m_statsMutex);
                if (m_stats.size > 0) {
                    m_stats.size--;
                }
                statsSnapshot = m_stats;
            }
            
            qDebug() << "DataServiceCache::removeDataSetById: Removed dataset" << dataId;
            
            // 发送信号时不持有任何互斥锁，避免同步槽重入导致卡死。
            emit dataSetRemoved(dataId);
            emit cacheStatsUpdated(statsSnapshot);
            
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
