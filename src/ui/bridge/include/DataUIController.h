// DataUIController.h
// UI层数据控制器 - 提供QML接口，处理UI交互
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QDateTime>
#include <QMutex>
#include <memory>

// 前向声明
class DataQueryService;
class DatabaseConnectionService;

namespace foundation::thread {
    class IExecutor;
}

// UI层数据控制器 - QML交互接口
class DataUIController : public QObject {
    Q_OBJECT
public:
    // 数据频率类型（UI层使用，便于QML访问）
    enum class DataFrequencyUI {
        DAILY,
        WEEKLY,
        MINUTE_1,
        MINUTE_5,
        MINUTE_15,
        MINUTE_30,
        MINUTE_60
    };
    Q_ENUM(DataFrequencyUI)
    
private:
    Q_PROPERTY(QString dataSource READ dataSource WRITE setDataSource NOTIFY dataSourceChanged)
    Q_PROPERTY(QStringList symbols READ symbols WRITE setSymbols NOTIFY symbolsChanged)
    Q_PROPERTY(QString startDate READ startDate WRITE setStartDate NOTIFY startDateChanged)
    Q_PROPERTY(QString endDate READ endDate WRITE setEndDate NOTIFY endDateChanged)
    Q_PROPERTY(DataFrequencyUI dataFrequency READ dataFrequency WRITE setDataFrequency NOTIFY dataFrequencyChanged)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionStatusChanged)
    Q_PROPERTY(bool isQuerying READ isQuerying NOTIFY queryStatusChanged)
    Q_PROPERTY(int queryProgress READ queryProgress NOTIFY queryProgressChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QVariantList queryResults READ queryResults NOTIFY queryResultsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)
    
public:
    explicit DataUIController(QObject* parent = nullptr);
    ~DataUIController();
    
    // QML可调用的方法
    Q_INVOKABLE void initializeDatabase();
    Q_INVOKABLE void connectToDatabase();
    Q_INVOKABLE void disconnectFromDatabase();
    
    Q_INVOKABLE void queryData();
    Q_INVOKABLE void queryDataWithParams(const QString& symbol, 
                                        const QString& startDate, 
                                        const QString& endDate,
                                        DataFrequencyUI frequency = DataFrequencyUI::DAILY);
    
    Q_INVOKABLE void queryMultipleSymbols(const QStringList& symbols,
                                         const QString& startDate,
                                         const QString& endDate,
                                         DataFrequencyUI frequency = DataFrequencyUI::DAILY);
    
    Q_INVOKABLE void cancelCurrentQuery();
    Q_INVOKABLE void clearResults();
    
    // 属性访问器
    QString dataSource() const;
    void setDataSource(const QString& source);
    
    QStringList symbols() const;
    void setSymbols(const QStringList& symbols);
    
    QString startDate() const;
    void setStartDate(const QString& date);
    
    QString endDate() const;
    void setEndDate(const QString& date);
    
    DataFrequencyUI dataFrequency() const;
    void setDataFrequency(DataFrequencyUI frequency);
    
    bool isConnected() const;
    bool isQuerying() const;
    int queryProgress() const;
    QString statusMessage() const;
    QVariantList queryResults() const;
    QString lastError() const;
    
    // 静态工具方法（QML可访问）
    Q_INVOKABLE static QString frequencyToString(DataFrequencyUI frequency);
    Q_INVOKABLE static DataFrequencyUI stringToFrequency(const QString& freqStr);
    
signals:
    // 属性变化信号
    void dataSourceChanged();
    void symbolsChanged();
    void startDateChanged();
    void endDateChanged();
    void dataFrequencyChanged();
    void connectionStatusChanged();
    void queryStatusChanged();
    void queryProgressChanged();
    void statusMessageChanged();
    void queryResultsChanged();
    void errorOccurred();
    
    // 操作信号
    void databaseInitialized(bool success, const QString& message);
    void connectionEstablished(bool success, const QString& message);
    void queryStarted(const QString& queryId);
    void queryProgressUpdated(int progress, const QString& message);
    void queryCompleted(bool success, const QString& message, int resultCount);
    void queryCancelled(const QString& queryId);
    
private slots:
    // 内部槽函数，处理底层服务事件
    void onConnectionStatusChanged(bool connected, const QString& message);
    void onQueryEvent(int eventType, const QString& queryId, const QString& message, const QVariant& resultData);
    
private:
    // 初始化底层服务
    bool initializeServices();
    void cleanupServices();
    
    // 状态更新
    void updateStatus(const QString& message, int progress = -1);
    void updateError(const QString& error);
    void updateQueryResults(const QVariantList& results);
    
    // 类型转换
    QVariantList convertQueryResults(const std::vector<std::map<std::string, std::string>>& rawResults);
    QVariantMap convertRowToVariantMap(const std::map<std::string, std::string>& row);
    
    // 异步操作处理
    void handleAsyncQueryResult(const QString& queryId, bool success, 
                               const QString& message, const QVariantList& results);
    
    // 配置管理
    struct DatabaseConfig {
        QString host;
        int port;
        QString database;
        QString username;
        QString password;
        QString charset;
        
        // 从应用程序配置加载
        static DatabaseConfig loadFromAppConfig();
    };
    
private:
    // UI状态
    QString m_dataSource{"mysql"}; // mysql, postgresql, sqlite
    QStringList m_symbols;
    QString m_startDate;
    QString m_endDate;
    DataFrequencyUI m_dataFrequency{DataFrequencyUI::DAILY};
    bool m_isConnected{false};
    bool m_isQuerying{false};
    int m_queryProgress{0};
    QString m_statusMessage{"就绪"};
    QVariantList m_queryResults;
    QString m_lastError;
    
    // 当前查询状态
    QString m_currentQueryId;
    QStringList m_pendingSymbols;
    int m_currentSymbolIndex{0};
    
    // 底层服务
    std::shared_ptr<DatabaseConnectionService> m_connectionService;
    std::shared_ptr<DataQueryService> m_queryService;
    std::shared_ptr<foundation::thread::IExecutor> m_executor;
    
    // 配置
    DatabaseConfig m_dbConfig;
    
    // 互斥锁（Qt线程安全）
    QMutex m_mutex;
};
