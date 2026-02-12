// DataManager.cpp
#include "DataManager.h"
#include <QDebug>
#include <QMutexLocker>

// 静态成员初始化
std::unique_ptr<DataManager> DataManager::s_instance = nullptr;
QMutex DataManager::s_instanceMutex;

DataManager* DataManager::instance() {
    QMutexLocker locker(&s_instanceMutex);
    if (!s_instance) {
        s_instance = std::make_unique<DataManager>();
    }
    return s_instance.get();
}

DataManager::DataManager(QObject* parent)
    : QObject(parent) {
    qDebug() << "DataManager: Created";
}

DataManager::~DataManager() {
    qDebug() << "DataManager: Destroyed";
}

void DataManager::storeData(const QString& key, const QVariantList& data) {
    QMutexLocker locker(&m_mutex);
    
    m_dataStore[key] = data;
    
    qDebug() << "DataManager::storeData: Stored" << data.size() << "items with key" << key;
    emit dataStored(key, data.size());
}

QVariantList DataManager::getData(const QString& key) {
    QMutexLocker locker(&m_mutex);
    
    if (m_dataStore.contains(key)) {
        qDebug() << "DataManager::getData: Retrieved" << m_dataStore[key].size() << "items with key" << key;
        return m_dataStore[key];
    }
    
    qDebug() << "DataManager::getData: No data found for key" << key;
    return QVariantList();
}

bool DataManager::hasData(const QString& key) {
    QMutexLocker locker(&m_mutex);
    bool has = m_dataStore.contains(key);
    qDebug() << "DataManager::hasData: Key" << key << (has ? "exists" : "does not exist");
    return has;
}

void DataManager::removeData(const QString& key) {
    QMutexLocker locker(&m_mutex);
    
    if (m_dataStore.remove(key)) {
        qDebug() << "DataManager::removeData: Removed data with key" << key;
        emit dataRemoved(key);
    } else {
        qDebug() << "DataManager::removeData: No data found for key" << key;
    }
}

void DataManager::clearAllData() {
    QMutexLocker locker(&m_mutex);
    
    int count = m_dataStore.size();
    m_dataStore.clear();
    
    qDebug() << "DataManager::clearAllData: Cleared" << count << "data entries";
    emit dataCleared();
}

void DataManager::cacheStockData(const QString& symbol, const QString& startDate, 
                                const QString& endDate, const QVariantList& data) {
    QString cacheKey = generateStockCacheKey(symbol, startDate, endDate);
    storeData(cacheKey, data);
    
    qDebug() << "DataManager::cacheStockData: Cached" << data.size() 
             << "items for" << symbol << "from" << startDate << "to" << endDate;
}

QVariantList DataManager::getCachedStockData(const QString& symbol, const QString& startDate, 
                                            const QString& endDate) {
    QString cacheKey = generateStockCacheKey(symbol, startDate, endDate);
    return getData(cacheKey);
}

QString DataManager::generateStockCacheKey(const QString& symbol, 
                                          const QString& startDate, 
                                          const QString& endDate) {
    return QString("%1_%2_%3").arg(symbol).arg(startDate).arg(endDate);
}

QStringList DataManager::getAllDataKeys() const {
    QMutexLocker locker(&m_mutex);
    return m_dataStore.keys();
}

QString DataManager::getStatistics() const {
    QMutexLocker locker(&m_mutex);
    
    int totalEntries = m_dataStore.size();
    int totalItems = 0;
    
    for (const auto& data : m_dataStore) {
        totalItems += data.size();
    }
    
    return QString("Entries: %1, Total items: %2").arg(totalEntries).arg(totalItems);
}