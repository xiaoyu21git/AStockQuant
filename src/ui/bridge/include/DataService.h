// DataService.h
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QDateTime>
#include <QMutex>
#include <memory>
#include <QUuid>
#include <atomic>

#include "DataCleaningEngine.h"
#include "database/QtMySQLDatabase.h"

// Foundation线程池前向声明
namespace foundation {
namespace thread {
    class IExecutor;
}
}

// 前向声明
namespace engine {
    class GlobalEventBus;
    struct EventFormat;
}

// 数据服务类 - 处理所有耗时操作
class DataService : public QObject {
    Q_OBJECT
    
public:
    explicit DataService(QObject* parent = nullptr);
    ~DataService();
    
    // 初始化数据库（在应用程序启动时调用）
    bool initializeDatabase();
    
    // 异步数据获取
    void fetchDataAsync(const QStringList& symbols, const QString& startDate, const QString& endDate);
    
    // 异步数据保存
    void saveDataAsync(const QVariantList& data);
    
    // 异步数据加载
    void loadDataAsync(const QString& symbol, const QString& startDate, const QString& endDate);
    
    // 异步数据清洗
    void cleanDataAsync(const QVariantList& data, const QVariantMap& rules);
    
    // 取消当前操作
    void cancelCurrentOperation();
    
    // 规则转换（公共方法）
    QVector<DataCleaningEngine::CleaningRule> convertRules(const QVariantMap& rules);
    
signals:
    // 数据获取信号
    void dataFetchStarted();
    void dataFetchProgress(int progress, const QString& message);
    void dataFetchCompleted(bool success, const QString& message, const QVariantList& data);
    void dataFetchError(const QString& error);
    
    // 数据保存信号
    void dataSaveStarted();
    void dataSaveProgress(int progress, const QString& message);
    void dataSaveCompleted(bool success, const QString& message, int savedCount);
    void dataSaveError(const QString& error);
    
    // 数据加载信号
    void dataLoadStarted();
    void dataLoadProgress(int progress, const QString& message);
    void dataLoadCompleted(bool success, const QString& message, const QVariantList& data);
    void dataLoadError(const QString& error);
    
    // 数据清洗信号
    void dataCleaningStarted();
    void dataCleaningProgress(int progress, const QString& message);
    void dataCleaningCompleted(bool success, const QString& message, const QVariantList& cleanedData);
    void dataCleaningError(const QString& error);
    
    // 数据库状态信号
    void databaseInitialized(bool success, const QString& message);
    void databaseError(const QString& error);
    
private:
    // 数据库实例
    std::shared_ptr<astock::database::QtMySQLDatabase> m_database;
    
    // 数据清洗引擎
    std::unique_ptr<DataCleaningEngine> m_cleaningEngine;
    
    // Foundation线程池
    std::shared_ptr<foundation::thread::IExecutor> m_threadPool;
    
    // 线程安全
    QMutex m_mutex;
    
    // 状态标志
    bool m_databaseInitialized{false};
    bool m_operationInProgress{false};
    
    // 私有方法
    bool ensureDatabaseInitialized();
    
    // EventBus相关方法
    void loadDataFromDatabaseAfterFetch(const QString& requestId, const QStringList& symbols, 
                                       const QString& startDate, const QString& endDate,
                                       QVariantList& data, std::atomic<bool>& completed);
    
    // 掘金数据回退方法
    bool fetchFromJuejinInLambda(QVariantList& data, QString& errorMessage, 
                                const QString& symbol, const QString& startDate, const QString& endDate);
    void saveJuejinDataToDatabase(const QVariantList& data);
    
    // 友元类声明
    friend class DataLoadTask;
    friend class DataCleaningTask;
    friend class DataSaveTask;
};
