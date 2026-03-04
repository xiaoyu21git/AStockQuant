// DataSourceModel.h - 数据源选择状态管理模型
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <vector>
#include <memory>

class DataSourceModel : public QObject {
    Q_OBJECT
    
    // QML属性
    Q_PROPERTY(QVariantList list READ list NOTIFY listChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantList details READ details NOTIFY detailsChanged)
    Q_PROPERTY(QString currentDataSource READ currentDataSource NOTIFY currentDataSourceChanged)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY isConnectedChanged)
    
public:
    explicit DataSourceModel(QObject* parent = nullptr);
    ~DataSourceModel();
    
    // QML可调用的方法
    Q_INVOKABLE void select(int index);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void connectDataSource(int index = -1);
    Q_INVOKABLE void disconnectDataSource();
    Q_INVOKABLE void addDataSource(const QString& name, const QString& type, 
                                  const QString& connectionString = QString());
    Q_INVOKABLE void removeDataSource(int index);
    Q_INVOKABLE void updateDataSource(int index, const QString& name, 
                                     const QString& type, const QString& connectionString);
    
    // 属性getter
    QVariantList list() const;
    int currentIndex() const;
    QString status() const;
    QVariantList details() const;
    QString currentDataSource() const;
    bool isConnected() const;
    
    // 属性setter
    void setCurrentIndex(int index);
    
signals:
    // 属性变化信号
    void listChanged();
    void currentIndexChanged();
    void statusChanged();
    void detailsChanged();
    void currentDataSourceChanged();
    void isConnectedChanged();
    
    // 操作结果信号
    void dataSourceSelected(int index, const QString& dataSourceName);
    void dataSourceConnected(bool success, const QString& message);
    void dataSourceDisconnected(bool success, const QString& message);
    void dataSourceAdded(bool success, const QString& message);
    void dataSourceRemoved(bool success, const QString& message);
    
private:
    // 数据源结构
    struct DataSourceInfo {
        QString name;
        QString type;  // "juejin", "akshare", "tushare", "mysql", "custom"
        QString connectionString;
        bool isConnected;
        QString lastConnectionTime;
        QVariantMap stats;  // 统计信息
    };
    
    // 私有实现方法
    class Impl;
    std::unique_ptr<Impl> m_impl;
    
    // 更新状态
    void updateStatus();
    void updateDetails();
    
    // 测试连接
    bool testConnection(const DataSourceInfo& source);
    
    // 模拟数据源（临时）
    void initializeSampleData();
};// DataSourceModel.h - 数据源选择状态管理模型
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <vector>
#include <memory>

class DataSourceModel : public QObject {
    Q_OBJECT
    
    // QML属性
    Q_PROPERTY(QVariantList list READ list NOTIFY listChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantList details READ details NOTIFY detailsChanged)
    Q_PROPERTY(QString currentDataSource READ currentDataSource NOTIFY currentDataSourceChanged)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY isConnectedChanged)
    
public:
    explicit DataSourceModel(QObject* parent = nullptr);
    ~DataSourceModel();
    
    // QML可调用的方法
    Q_INVOKABLE void select(int index);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void connectDataSource(int index = -1);
    Q_INVOKABLE void disconnectDataSource();
    Q_INVOKABLE void addDataSource(const QString& name, const QString& type, 
                                  const QString& connectionString = QString());
    Q_INVOKABLE void removeDataSource(int index);
    Q_INVOKABLE void updateDataSource(int index, const QString& name, 
                                     const QString& type, const QString& connectionString);
    
    // 属性getter
    QVariantList list() const;
    int currentIndex() const;
    QString status() const;
    QVariantList details() const;
    QString currentDataSource() const;
    bool isConnected() const;
    
    // 属性setter
    void setCurrentIndex(int index);
    
signals:
    // 属性变化信号
    void listChanged();
    void currentIndexChanged();
    void statusChanged();
    void detailsChanged();
    void currentDataSourceChanged();
    void isConnectedChanged();
    
    // 操作结果信号
    void dataSourceSelected(int index, const QString& dataSourceName);
    void dataSourceConnected(bool success, const QString& message);
    void dataSourceDisconnected(bool success, const QString& message);
    void dataSourceAdded(bool success, const QString& message);
    void dataSourceRemoved(bool success, const QString& message);
    
private:
    // 数据源结构
    struct DataSourceInfo {
        QString name;
        QString type;  // "juejin", "akshare", "tushare", "mysql", "custom"
        QString connectionString;
        bool isConnected;
        QString lastConnectionTime;
        QVariantMap stats;  // 统计信息
    };
    
    // 私有实现方法
    class Impl;
    std::unique_ptr<Impl> m_impl;
    
    // 更新状态
    void updateStatus();
    void updateDetails();
    
    // 测试连接
    bool testConnection(const DataSourceInfo& source);
    
    // 模拟数据源（临时）
    void initializeSampleData();
    
    // 测试连接
    // 测试连接
    
    
    class Impl;
    class Impl;
    
