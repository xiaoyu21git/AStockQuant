// DataManager.h
#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include <QVariantList>
#include <QMutex>
#include <memory>

/**
 * @brief 数据管理器 - 统一管理C++内部数据传递
 * 
 * 解决QML之间传递大量数据的问题，将数据存储在C++内部，
 * QML只传递数据标识符，通过标识符从C++获取数据。
 */
class DataManager : public QObject {
    Q_OBJECT
    
public:
    /**
     * @brief 获取单例实例
     * @return DataManager单例指针
     */
    static DataManager* instance();
    
    /**
     * @brief 存储数据
     * @param key 数据标识符
     * @param data 数据列表
     */
    void storeData(const QString& key, const QVariantList& data);
    
    /**
     * @brief 获取数据
     * @param key 数据标识符
     * @return 数据列表，如果不存在则返回空列表
     */
    QVariantList getData(const QString& key);
    
    /**
     * @brief 检查是否存在数据
     * @param key 数据标识符
     * @return 是否存在
     */
    bool hasData(const QString& key);
    
    /**
     * @brief 删除数据
     * @param key 数据标识符
     */
    void removeData(const QString& key);
    
    /**
     * @brief 清空所有数据
     */
    void clearAllData();
    
    /**
     * @brief 缓存股票数据
     * @param symbol 股票代码
     * @param startDate 开始日期
     * @param endDate 结束日期
     * @param data 数据列表
     */
    void cacheStockData(const QString& symbol, const QString& startDate, 
                       const QString& endDate, const QVariantList& data);
    
    /**
     * @brief 获取缓存的股票数据
     * @param symbol 股票代码
     * @param startDate 开始日期
     * @param endDate 结束日期
     * @return 数据列表，如果不存在则返回空列表
     */
    QVariantList getCachedStockData(const QString& symbol, const QString& startDate, 
                                   const QString& endDate);
    
    /**
     * @brief 生成股票数据缓存键
     * @param symbol 股票代码
     * @param startDate 开始日期
     * @param endDate 结束日期
     * @return 缓存键
     */
    static QString generateStockCacheKey(const QString& symbol, 
                                        const QString& startDate, 
                                        const QString& endDate);
    
    /**
     * @brief 获取所有数据键
     * @return 数据键列表
     */
    QStringList getAllDataKeys() const;
    
    /**
     * @brief 获取数据统计信息
     * @return 包含数据数量的字符串
     */
    QString getStatistics() const;
    
signals:
    /**
     * @brief 数据存储信号
     * @param key 数据标识符
     * @param count 数据数量
     */
    void dataStored(const QString& key, int count);
    
    /**
     * @brief 数据删除信号
     * @param key 数据标识符
     */
    void dataRemoved(const QString& key);
    
    /**
     * @brief 数据清空信号
     */
    void dataCleared();
    
public:
    DataManager(QObject* parent = nullptr);
    ~DataManager();
    
    // 禁止拷贝和赋值
    DataManager(const DataManager&) = delete;
    DataManager& operator=(const DataManager&) = delete;
    
    // 数据存储
    QHash<QString, QVariantList> m_dataStore;
    
    // 线程安全
    mutable QMutex m_mutex;
    
    // 单例实例
    static std::unique_ptr<DataManager> s_instance;
    static QMutex s_instanceMutex;
};