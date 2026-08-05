// DataFetchController.h - 数据获取与清洗控制器
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QDateTime>
#include <QTimer>
#include <QVariantMap>
#include <functional>

#include "PreviewDataModel.h"

class DataCleaningServiceRefactored;

class DataFetchController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString dataSource READ dataSource WRITE setDataSource NOTIFY dataSourceChanged)
    Q_PROPERTY(QStringList symbols READ symbols WRITE setSymbols NOTIFY symbolsChanged)
    Q_PROPERTY(QString startDate READ startDate WRITE setStartDate NOTIFY startDateChanged)
    Q_PROPERTY(QString endDate READ endDate WRITE setEndDate NOTIFY endDateChanged)
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
    Q_INVOKABLE void refreshDataSetInfos();
    // 列出已清洗数据集(sourceType=="cleaning")，供清洗页增量更新选择器使用
    Q_INVOKABLE void refreshCleanedDataSetInfos();
    Q_INVOKABLE void fetchDataTypesBySource(const QString& dataSource,
                                            const QString& symbol,
                                            const QStringList& dataTypes,
                                            const QString& startDate,
                                            const QString& endDate,
                                            const QVariantMap& options = QVariantMap());
    Q_INVOKABLE void fetchDataByType(const QString& dataSource,
                                    const QString& symbol,
                                    const QString& dataType,
                                    const QString& startDate,
                                    const QString& endDate,
                                    const QVariantMap& options = QVariantMap());
    Q_INVOKABLE void onDropdownRefreshed(int count);
    Q_INVOKABLE void loadSymbolDetail(const QString& symbol, int page);
    Q_INVOKABLE void cleanDataFromDataSet(int dataSetId, const QVariantMap& rules);
    // 增量更新已清洗数据集（转发到清洗服务，UI 入口放在数据清洗页）
    Q_INVOKABLE void incrementalUpdateDataSet(int dataSetId);
    Q_INVOKABLE bool removeDataSet(int dataSetId);
    // 缓存数据查看：列出全部数据集(含清洗集) + 按 symbol 读取某数据集的所有行(清洗前后对比)
    Q_INVOKABLE QVariantList allDataSetInfos();
    Q_INVOKABLE QVariantList loadCacheRowsBySymbol(int dataId, const QString& symbol);

    QString dataSource() const { return m_dataSource; }
    void setDataSource(const QString& source);
    QStringList symbols() const { return m_symbols; }
    void setSymbols(const QStringList& symbols);
    QString startDate() const { return m_startDate; }
    void setStartDate(const QString& date);
    QString endDate() const { return m_endDate; }
    void setEndDate(const QString& date);
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
    void dataSetInfosRefreshed(const QVariantList& dataSetInfos);
    // 已清洗数据集列表(仅 sourceType=="cleaning")
    void cleanedDataSetInfosRefreshed(const QVariantList& dataSetInfos);
    void symbolDetailLoaded(const QString& symbol, int page, const QVariantList& data);
    void dataSetCleaned(int dataSetId, int resultDataSetId, const QString& message,
                        int inputRows, int outputRows);
    // 增量更新信号（转发自清洗服务）
    void datasetUpdateStarted(int dataSetId);
    void datasetUpdateProgress(int dataSetId, int pct, const QString& stage);
    void datasetUpdateFinished(int dataSetId, bool success, int newRows, const QString& message);
    void dataSetReadyForCleaning(int dataSetId);
    void previewModelChanged();
    void requestCleanData(const QVariantList& data, const QVariantMap& rules);

public slots:
    void onDataLoadProgress(int progress, const QString& message);
    void onDataLoadCompleted(bool success, const QString& message, const QVariantList& data);
    void onDataLoadError(const QString& error);
    void onDataCleaningProgress(int progress, const QString& message);
    void onDataCleaningProgressDetail(int progress, const QString& message,
                                      const QString& currentStock,
                                      int keptRecords, int removedRecords);
    void onDataCleaningCompleted(bool success, const QString& message, const QVariantList& cleanedData);
    void logInitMessage();
    void delayedCleanData();

private:
    void updateStatus(const QString& message, int progress = -1);
    void resetProgressState();
    void updateCleanStats(int inputCount, int outputCount);

    DataCleaningServiceRefactored* m_cleaningSvc;
    QString m_dataSource{"juejin"};
    QStringList m_symbols;
    QString m_startDate;
    QString m_endDate;
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
    QString m_currentSymbol;
    QString m_currentStartDate;
    QString m_currentEndDate;
    bool m_serviceAlreadyCachedCurrentRequest{false};
    bool m_pendingCleanAfterLoad{false};
    QStringList m_pendingFetchDataTypes;
    quint64 m_previewBuildGeneration{0};
    int m_pendingDoneTotal{0};
    QString m_pendingDoneMsg;
    PreviewDataModel* m_previewModel{nullptr};
    int m_lastStoredDataSetId{-1};
    QVariantMap m_pendingRules;
};
