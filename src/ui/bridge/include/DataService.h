// DataService.h - 核心功能接口（保持简洁但完整）
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

class DataService : public QObject {
    Q_OBJECT
    
    // QML属性 - 用于数据访问
    Q_PROPERTY(QVariantList fetchedData READ fetchedData NOTIFY fetchedDataChanged)
    
public:
    explicit DataService(QObject* parent = nullptr);
    ~DataService();
    
    // 核心方法1：查询数据（兼容旧接口）
    Q_INVOKABLE void queryData(const QString& symbol, 
                              const QString& startDate, 
                              const QString& endDate);
    
    // 核心方法2：清洗数据
    Q_INVOKABLE void cleanData(const QVariantList& data, 
                              const QVariantMap& rules);
    
    // 核心方法3：查询并清洗（完整流程）
    Q_INVOKABLE void queryAndCleanData(const QString& symbol,
                                      const QString& startDate,
                                      const QString& endDate,
                                      const QVariantMap& rules);
    
    // 核心方法4：从数据库加载数据（QML调用）
    Q_INVOKABLE void loadFromDatabase(const QString& symbol, 
                                     const QString& startDate, 
                                     const QString& endDate);
    
    // 核心方法5：异步清洗数据（QML调用）
    Q_INVOKABLE void cleanDataAsync(const QVariantList& data, 
                                   const QVariantMap& rules);
    
    // 属性getter
    QVariantList fetchedData() const;
    
signals:
    // 查询相关信号
    void queryProgress(int progress, const QString& message);
    void queryCompleted(bool success, const QString& message, const QVariantList& data);
    
    // 清洗相关信号
    void cleaningProgress(int progress, const QString& message);
    void cleaningCompleted(bool success, const QString& message, const QVariantList& cleanedData);
    
    // 错误信号
    void error(const QString& errorMessage);
    
    // 兼容旧接口的信号（用于QML连接）
    void dataLoadedFromDatabase(bool success, const QString& message, int count);
    
    // 属性变化信号
    void fetchedDataChanged();
    
private:
    DataService(const DataService&) = delete;
    DataService& operator=(const DataService&) = delete;
    
    // 私有实现方法
    class Impl;
    std::unique_ptr<Impl> m_impl;
    
    // 缓存的数据
    QVariantList m_fetchedData;
};
