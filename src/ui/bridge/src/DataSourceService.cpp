// DataSourceService.cpp - 数据源服务实现
#include "DataSourceService.h"
#include <QDebug>
#include <QVariant>
#include <QThread>

// PIMPL实现类
class DataSourceService::Impl {
public:
    Impl(DataSourceService* parent) 
        : m_parent(parent)
        , m_currentDataSource("")
        , m_isConnecting(false) {
        qDebug() << "DataSourceService::Impl: 创建";
    }
    
    ~Impl() {
        qDebug() << "DataSourceService::Impl: 销毁";
    }
    
    void addDataSource(const QString& provider, const QString& market,
                      const QStringList& symbols, const QString& startDate,
                      const QString& endDate, const QString& dataType) {
        qDebug() << "添加数据源:" << provider << market << symbols.size() << "个代码" 
                 << startDate << "-" << endDate << dataType;
        
        // 模拟异步操作
        QThread::msleep(100);
        
        QVariantMap sourceInfo = {
            {"provider", provider},
            {"market", market},
            {"symbolCount", symbols.size()},
            {"startDate", startDate},
            {"endDate", endDate},
            {"dataType", dataType},
            {"name", provider + "_" + market}
        };
        
        emit m_parent->dataSourceAdded(true, "数据源添加成功", sourceInfo);
    }
    
    void loadFromDatabase(const QString& symbol, 
                         const QString& startDate, 
                         const QString& endDate) {
        qDebug() << "从数据库加载数据:" << symbol << startDate << "-" << endDate;
        
        // 模拟异步操作
        m_isConnecting = true;
        emit m_parent->isConnectingChanged();
        
        QThread::msleep(200);
        
        // 模拟数据
        QVariantList data;
        data.append(QVariantMap{{"date", startDate}, {"code", symbol}, {"price", 100.0}});
        data.append(QVariantMap{{"date", endDate}, {"code", symbol}, {"price", 105.0}});
        
        m_isConnecting = false;
        emit m_parent->isConnectingChanged();
        emit m_parent->dataLoaded(true, "数据加载成功", data);
    }
    
    void testConnection(const QString& provider) {
        qDebug() << "测试连接:" << provider;
        
        m_isConnecting = true;
        emit m_parent->isConnectingChanged();
        
        QThread::msleep(150);
        
        m_isConnecting = false;
        emit m_parent->isConnectingChanged();
        emit m_parent->connectionTested(true, provider + " 连接测试成功");
    }
    
    void refreshAvailableDataSources() {
        qDebug() << "刷新可用数据源";
        
        m_availableDataSources = {
            QVariantMap{{"name", "沪深股票"}, {"provider", "local"}, {"type", "stock"}},
            QVariantMap{{"name", "期货行情"}, {"provider", "local"}, {"type", "futures"}},
            QVariantMap{{"name", "指数数据"}, {"provider", "jq"}, {"type", "index"}},
            QVariantMap{{"name", "基本面数据"}, {"provider", "local"}, {"type", "fundamental"}}
        };
        
        emit m_parent->availableDataSourcesChanged();
    }
    
    QString currentDataSource() const { return m_currentDataSource; }
    void setCurrentDataSource(const QString& dataSource) { 
        if (m_currentDataSource != dataSource) {
            m_currentDataSource = dataSource; 
            emit m_parent->currentDataSourceChanged();
        }
    }
    
    QVariantList availableDataSources() const { return m_availableDataSources; }
    bool isConnecting() const { return m_isConnecting; }
    
private:
    DataSourceService* m_parent;
    QString m_currentDataSource;
    QVariantList m_availableDataSources;
    bool m_isConnecting;
};

// DataSourceService 公共接口实现
DataSourceService::DataSourceService(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(this)) {
    qDebug() << "DataSourceService: 创建";
}

DataSourceService::~DataSourceService() {
    qDebug() << "DataSourceService: 销毁";
}

void DataSourceService::addDataSource(const QString& provider, const QString& market,
                                     const QStringList& symbols, const QString& startDate,
                                     const QString& endDate, const QString& dataType) {
    m_impl->addDataSource(provider, market, symbols, startDate, endDate, dataType);
}

void DataSourceService::loadFromDatabase(const QString& symbol, 
                                        const QString& startDate, 
                                        const QString& endDate) {
    m_impl->loadFromDatabase(symbol, startDate, endDate);
}

void DataSourceService::queryData(const QString& symbol, 
                                 const QString& startDate, 
                                 const QString& endDate) {
    qDebug() << "查询数据:" << symbol << startDate << "-" << endDate;
    loadFromDatabase(symbol, startDate, endDate);
}

void DataSourceService::testConnection(const QString& provider) {
    m_impl->testConnection(provider);
}

void DataSourceService::refreshAvailableDataSources() {
    m_impl->refreshAvailableDataSources();
}

QString DataSourceService::currentDataSource() const {
    return m_impl->currentDataSource();
}

void DataSourceService::setCurrentDataSource(const QString& dataSource) {
    m_impl->setCurrentDataSource(dataSource);
}

QVariantList DataSourceService::availableDataSources() const {
    return m_impl->availableDataSources();
}

bool DataSourceService::isConnecting() const {
    return m_impl->isConnecting();
}