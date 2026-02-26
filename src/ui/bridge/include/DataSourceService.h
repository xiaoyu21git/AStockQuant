// DataSourceService.h - 数据源服务，负责添加和管理数据源
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

class DataSourceService : public QObject {
    Q_OBJECT
    
    // QML属性
    Q_PROPERTY(QString currentDataSource READ currentDataSource WRITE setCurrentDataSource NOTIFY currentDataSourceChanged)
    Q_PROPERTY(QVariantList availableDataSources READ availableDataSources NOTIFY availableDataSourcesChanged)
    Q_PROPERTY(bool isConnecting READ isConnecting NOTIFY isConnectingChanged)
    
public:
    explicit DataSourceService(QObject* parent = nullptr);
    ~DataSourceService();
    
    // QML可调用的方法
    Q_INVOKABLE void addDataSource(const QString& provider, const QString& market,
                                  const QStringList& symbols, const QString& startDate,
                                  const QString& endDate, const QString& dataType);
    
    Q_INVOKABLE void loadFromDatabase(const QString& symbol, 
                                     const QString& startDate, 
                                     const QString& endDate);
    
    Q_INVOKABLE void queryData(const QString& symbol, 
                              const QString& startDate, 
                              const QString& endDate);
    
    Q_INVOKABLE void testConnection(const QString& provider);
    
    Q_INVOKABLE void refreshAvailableDataSources();
    
    // 属性getter/setter
    QString currentDataSource() const;
    void setCurrentDataSource(const QString& dataSource);
    
    QVariantList availableDataSources() const;
    bool isConnecting() const;
    
signals:
    // 属性变化信号
    void currentDataSourceChanged();
    void availableDataSourcesChanged();
    void isConnectingChanged();
    
    // 操作结果信号
    void dataSourceAdded(bool success, const QString& message, const QVariantMap& sourceInfo);
    void dataLoaded(bool success, const QString& message, const QVariantList& data);
    void connectionTested(bool success, const QString& message);
    
    // 进度信号
    void progress(int progress, const QString& message);
    void error(const QString& errorMessage);
    
private:
    DataSourceService(const DataSourceService&) = delete;
    DataSourceService& operator=(const DataSourceService&) = delete;
    
    // 私有实现方法
    class Impl;
    std::unique_ptr<Impl> m_impl;
};