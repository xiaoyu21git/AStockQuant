// DataFetchController.h - 改进版本，支持模型和数据缓存
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QDateTime>
#include <QTimer>
#include <QtQml/QQmlEngine>
#include "PreviewDataModel.h"

// 前向声明
class DataService;
class DataServiceCache;

// 数据获取控制器 - QML与C++的纯转发桥接
// 不执行任何耗时操作，只负责转发请求和信号
class DataFetchController : public QObject {
    Q_OBJECT
    // QML_ELEMENT宏需要包含QtQml，这里先注释掉，使用registerQmlTypes注册
    
    // QML属性
    Q_PROPERTY(QString dataSource READ dataSource WRITE setDataSource NOTIFY dataSourceChanged)
    Q_PROPERTY(QStringList symbols READ symbols WRITE setSymbols NOTIFY symbolsChanged)
    Q_PROPERTY(QString startDate READ startDate WRITE setStartDate NOTIFY startDateChanged)
    Q_PROPERTY(QString endDate READ endDate WRITE setEndDate NOTIFY endDateChanged)
    Q_PROPERTY(QString dataType READ dataType WRITE setDataType NOTIFY dataTypeChanged)
    Q_PROPERTY(bool isFetching READ isFetching NOTIFY isFetchingChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(PreviewDataModel* previewModel READ previewModel WRITE setPreviewModel NOTIFY previewModelChanged)
    
public:
    explicit DataFetchController(QObject* parent = nullptr);
    ~DataFetchController();
    
    // QML可调用的方法 - 纯转发，不执行耗时操作
    Q_INVOKABLE void fetchData();
    Q_INVOKABLE void cancelFetch();
    Q_INVOKABLE void clearData();
    Q_INVOKABLE void saveToDatabase();
    Q_INVOKABLE void loadFromDatabase(const QString& symbol, const QString& startDate, const QString& endDate);
    Q_INVOKABLE QVariantList cleanData(const QVariantList& data, const QVariantMap& rules);  // 同步清洗（小数据集）
    Q_INVOKABLE void cleanDataAsync(const QVariantMap& rules);     // 异步清洗（大数据集）
    
    // 数据源添加并自动加载数据
    Q_INVOKABLE void addDataSourceAndLoad(const QString& provider, const QString& market, 
                                         const QStringList& symbols, const QString& startDate, 
                                         const QString& endDate, const QString& dataType);
    
    // 缓存相关方法
    Q_INVOKABLE QVariantList getAllCacheKeys();  // 获取所有缓存键
    Q_INVOKABLE QVariantList getAllDataSetInfos();  // 获取所有数据集信息
    Q_INVOKABLE void loadFromCache(const QString& cacheKey);  // 从指定缓存键加载数据
    Q_INVOKABLE void loadDataSetById(int dataId);  // 通过数据集ID加载数据
    
    // 新的缓存管理方法 - 遵循不在QML中遍历数据的原则
    Q_INVOKABLE void refreshCacheKeys();  // 刷新缓存键列表（C++中遍历，通过信号传递结果）
    Q_INVOKABLE void refreshDataSetInfos();  // 刷新数据集信息（C++中遍历，通过信号传递结果）
    
    // 缓存信息获取方法 - 遵循不在QML中操作数据的原则
    Q_INVOKABLE void refreshAllCacheInfos();  // 刷新所有缓存信息（在C++中遍历）
    Q_INVOKABLE void cleanDataFromCacheByIndex(int cacheIndex, const QVariantMap& rules);  // 通过索引清洗缓存数据
    
    // 属性getter/setter
    QString dataSource() const { return m_dataSource; }
    void setDataSource(const QString& source);
    
    QStringList symbols() const { return m_symbols; }
    void setSymbols(const QStringList& symbols);
    
    QString startDate() const { return m_startDate; }
    void setStartDate(const QString& date);
    
    QString endDate() const { return m_endDate; }
    void setEndDate(const QString& date);
    
    QString dataType() const { return m_dataType; }
    void setDataType(const QString& type);
    
    bool isFetching() const { return m_isFetching; }
    
    int progress() const { return m_progress; }
    
    QString statusMessage() const { return m_statusMessage; }
    
    PreviewDataModel* previewModel() const { return m_previewModel; }
    void setPreviewModel(PreviewDataModel* model);
    
    QVariantList fetchedData() const { return m_fetchedData; }
    
signals:
    void dataSourceChanged();
    void symbolsChanged();
    void startDateChanged();
    void endDateChanged();
    void dataTypeChanged();
    void isFetchingChanged();
    void progressChanged();
    void statusMessageChanged();
    void fetchedDataChanged();
    
    // 通知信号 - 转发给QML
    void dataFetchStarted();
    void dataFetchProgress(int progress, const QString& message);
    void dataFetchCompleted(bool success, const QString& message, int dataCount);
    void dataFetchError(const QString& error);
    void dataSavedToDatabase(bool success, const QString& message);
    void dataLoadedFromDatabase(bool success, const QString& message, int dataCount);
    
    // 数据清洗信号 - 转发给QML
    void dataCleaningStarted();
    void dataCleaningProgress(int progress, const QString& message);
    void dataCleaningCompleted(bool success, const QString& message, const QVariantList& cleanedData);
    void dataCleaningError(const QString& error);
    
    // 缓存管理信号
    void cacheKeysRefreshed(const QVariantList& cacheKeys);  // 缓存键列表刷新完成
    void dataSetInfosRefreshed(const QVariantList& dataSetInfos);  // 数据集信息刷新完成
    void allCacheInfosRefreshed(const QVariantList& cacheInfos);  // 所有缓存信息刷新完成（包含索引）
    
    // 模型变更信号
    void previewModelChanged();
    
    // 内部请求信号 - 转发给DataService
    void requestFetchData(const QStringList& symbols, const QString& startDate, const QString& endDate);
    void requestSaveData(const QVariantList& data);
    void requestLoadData(const QString& symbol, const QString& startDate, const QString& endDate);
    void requestCleanData(const QVariantList& data, const QVariantMap& rules);
    void requestCancelOperation();
    
    // 内部槽函数 - 接收DataService的结果
public slots:
    void onDataLoadProgress(int progress, const QString& message);
    void onDataLoadCompleted(bool success, const QString& message, const QVariantList& data);
    void onDataLoadError(const QString& error);
    void onDataCleaningProgress(int progress, const QString& message);
    void onDataCleaningCompleted(bool success, const QString& message, const QVariantList& cleanedData);
    void logInitMessage();  // 简单日志槽函数
    void delayedCleanData();  // 延迟清洗数据槽函数
    
private:
    // 更新状态（快速操作）
    void updateStatus(const QString& message, int progress = -1);
    
    // 更新清洗结果模型
    void updateCleaningResultModel(const QVariantList& cleanedData);
    
    // 增强缓存数据获取辅助函数
    QVariantList getDataFromCacheEnhanced(DataServiceCache& cache, const QString& key);
    
private:
    // DataService实例
    DataService* m_dataService;
    
    // QML属性
    QString m_dataSource{"juejin"};  // 数据源: juejin, akshare, tushare
    QStringList m_symbols;  // 股票代码列表，可以为空
    QString m_startDate;
    QString m_endDate;
    QString m_dataType{"daily"};  // 数据类型: daily, minute, tick
    bool m_isFetching{false};
    int m_progress{0};
    QString m_statusMessage{"就绪"};
    QVariantList m_fetchedData;
    
    // 当前加载的数据标识
    QString m_currentSymbol;
    QString m_currentStartDate;
    QString m_currentEndDate;
    
    // PreviewDataModel实例
    PreviewDataModel* m_previewModel{nullptr};
    
    // 待处理的清洗规则
    QVariantMap m_pendingRules;
};