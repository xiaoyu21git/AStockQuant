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
    Q_PROPERTY(bool operationInProgress READ operationInProgress NOTIFY operationInProgressChanged)
    Q_PROPERTY(QString operationPhase READ operationPhase NOTIFY operationPhaseChanged)
    Q_PROPERTY(QString currentProgressStock READ currentProgressStock NOTIFY currentProgressStockChanged)
    Q_PROPERTY(int cleanInputRecordCount READ cleanInputRecordCount NOTIFY cleanStatsChanged)
    Q_PROPERTY(int cleanOutputRecordCount READ cleanOutputRecordCount NOTIFY cleanStatsChanged)
    Q_PROPERTY(int cleanRemovedRecordCount READ cleanRemovedRecordCount NOTIFY cleanStatsChanged)
    Q_PROPERTY(PreviewDataModel* previewModel READ previewModel WRITE setPreviewModel NOTIFY previewModelChanged)
    
public:
    explicit DataFetchController(QObject* parent = nullptr);
    ~DataFetchController();
    
    Q_INVOKABLE void cleanDataAsync(const QVariantMap& rules);
    Q_INVOKABLE void refreshCacheKeys();
    Q_INVOKABLE void refreshDataSetInfos();
    Q_INVOKABLE void cleanDataFromCacheByIndex(int cacheIndex, const QVariantMap& rules);
    
    // 通用数据获取方法（单选）
    Q_INVOKABLE void fetchDataByType(const QString& dataSource,           // 数据源：index, stock, all_market
                                    const QString& symbol,                // 代码：指数代码、股票代码或空字符串（全市场）
                                    const QString& dataType,              // 数据类型：kline_daily, kline_weekly, financial, news等
                                    const QString& startDate,             // 开始日期
                                    const QString& endDate,               // 结束日期
                                    const QVariantMap& options = QVariantMap()); // 其他选项
    
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

    bool operationInProgress() const { return m_operationInProgress; }

    QString operationPhase() const { return m_operationPhase; }

    QString currentProgressStock() const { return m_currentProgressStock; }

    int cleanInputRecordCount() const { return m_cleanInputRecordCount; }

    int cleanOutputRecordCount() const { return m_cleanOutputRecordCount; }

    int cleanRemovedRecordCount() const { return m_cleanRemovedRecordCount; }
    
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
    void operationInProgressChanged();
    void operationPhaseChanged();
    void currentProgressStockChanged();
    void cleanStatsChanged();
    void fetchedDataChanged();
    
    void dataFetchProgress(int progress, const QString& message);
    void dataFetchError(const QString& error);
    
    void dataCleaningStarted();
    void dataCleaningProgress(int progress, const QString& message);
    void dataCleaningCompleted(bool success, const QString& message, const QVariantList& cleanedData);
    void dataCleaningError(const QString& error);
    
    void cacheKeysRefreshed(const QVariantList& cacheKeys);
    void dataSetInfosRefreshed(const QVariantList& dataSetInfos);
    
    void previewModelChanged();
    
    void requestLoadData(const QString& symbol, const QString& startDate, const QString& endDate);
    void requestCleanData(const QVariantList& data, const QVariantMap& rules);

public slots:
    void onDataLoadProgress(int progress, const QString& message);
    void onDataLoadCompleted(bool success, const QString& message, const QVariantList& data);
    void onDataLoadError(const QString& error);
    void onDataCleaningProgress(int progress, const QString& message);
    void onDataCleaningProgressDetail(int progress, const QString& message, const QString& currentStock);
    void onDataCleaningCompleted(bool success, const QString& message, const QVariantList& cleanedData);
    void logInitMessage();  // 简单日志槽函数
    void delayedCleanData();  // 延迟清洗数据槽函数
    
private:
    void updateStatus(const QString& message, int progress = -1);
    void resetProgressState();
    void updateCleanStats(int inputCount, int outputCount);
    void loadFromDatabase(const QString& symbol, const QString& startDate, const QString& endDate);
    void cleanDataFromDataSetId(int dataId, const QVariantMap& rules);
    void cleanDataFromCacheKey(const QString& cacheKey, const QVariantMap& rules);
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
    bool m_operationInProgress{false};
    QString m_operationPhase;
    QString m_currentProgressStock;
    int m_cleanInputRecordCount{0};
    int m_cleanOutputRecordCount{0};
    int m_cleanRemovedRecordCount{0};
    QVariantList m_fetchedData;
    
    // 当前加载的数据标识
    QString m_currentSymbol;
    QString m_currentStartDate;
    QString m_currentEndDate;
    bool m_serviceAlreadyCachedCurrentRequest{false};
    
    // PreviewDataModel实例
    PreviewDataModel* m_previewModel{nullptr};
    
    // 待处理的清洗规则
    QVariantMap m_pendingRules;
};