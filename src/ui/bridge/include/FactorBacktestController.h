#ifndef FACTORBACKTESTCONTROLLER_H
#define FACTORBACKTESTCONTROLLER_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>
#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <vector>

#include "../../../domain/factor/include/FactorBacktestExecutor.h"
#include "../../../domain/factor/include/FactorInstanceManager.h"

namespace astock {
namespace database {
class QtMySQLDatabase;
}
}

namespace foundation::thread {
class ThreadPoolExecutor;
}

namespace factor {
class DataAvailabilityChecker;
class FactorBacktestExecutor;
class FactorCacheManager;
class FactorInstanceManager;
}

struct PendingBacktestLaunchResult {
    QString resolvedInstanceId;
    foundation::utils::Uuid taskId;
    std::shared_ptr<factor::FactorBacktestExecutor> executor;
    std::shared_ptr<std::future<factor::BacktestResult>> future;
    QString errorMessage;
};

struct PendingBacktestLaunchProgressState {
    std::atomic_int progress{0};
    mutable std::mutex mutex;
    QString currentStep{QStringLiteral("正在提交回测任务")};

    void update(int newProgress, const QString& step)
    {
        progress.store((std::max)(0, (std::min)(100, newProgress)));
        std::lock_guard<std::mutex> lock(mutex);
        if (!step.trimmed().isEmpty()) {
            currentStep = step.trimmed();
        }
    }

    int value() const
    {
        return progress.load();
    }

    QString stepText() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return currentStep;
    }
};

struct PendingBacktestBatchLaunchResult {
    foundation::utils::Uuid taskId;
    std::shared_ptr<factor::FactorBacktestExecutor> executor;
    std::shared_ptr<std::future<std::vector<factor::BacktestResult>>> future;
    QString errorMessage;
};

struct DetachedPendingBacktestBatchState {
    std::shared_ptr<std::future<PendingBacktestBatchLaunchResult>> launchFuture;
    std::shared_ptr<std::future<std::vector<factor::BacktestResult>>> future;
};

struct PendingBacktestTask {
    QString requestedFactorId;
    QString resolvedInstanceId;
    size_t batchIndex{0};
    std::shared_ptr<PendingBacktestLaunchProgressState> launchProgressState;
    std::shared_ptr<std::future<PendingBacktestLaunchResult>> launchFuture;
    foundation::utils::Uuid taskId;
    std::shared_ptr<factor::FactorBacktestExecutor> executor;
    std::shared_ptr<std::future<factor::BacktestResult>> future;
};

/**
 * @brief 因子回测控制器 - 简化版本
 * 
 * 只提供核心的回测功能，移除所有过度设计
 */
class FactorBacktestController : public QObject
{
    Q_OBJECT
    friend class FactorBacktestControllerTestAccess;
    
    // 回测状态属性
    Q_PROPERTY(bool isRunning READ isRunning NOTIFY isRunningChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    
    // 因子选择属性
    Q_PROPERTY(QVariantList selectedFactorIds READ selectedFactorIds WRITE setSelectedFactorIds NOTIFY selectedFactorIdsChanged)
    Q_PROPERTY(int selectedDatasetId READ selectedDatasetId WRITE setSelectedDatasetId NOTIFY selectedDatasetIdChanged)
    Q_PROPERTY(QString dataSourceMode READ dataSourceMode WRITE setDataSourceMode NOTIFY dataSourceModeChanged)
    Q_PROPERTY(QVariantList selectedStockPoolSymbols READ selectedStockPoolSymbols WRITE setSelectedStockPoolSymbols NOTIFY selectedStockPoolSymbolsChanged)
    Q_PROPERTY(QVariantMap backtestRuntimeParams READ backtestRuntimeParams WRITE setBacktestRuntimeParams NOTIFY backtestRuntimeParamsChanged)
    Q_PROPERTY(QVariantMap selectedDatasetBenchmarkMetadata READ selectedDatasetBenchmarkMetadata WRITE setSelectedDatasetBenchmarkMetadata NOTIFY selectedDatasetBenchmarkMetadataChanged)
    Q_PROPERTY(QVariantMap factorSupportMapCache READ factorSupportMapCache NOTIFY factorSupportMapCacheChanged)
    Q_PROPERTY(bool supportMapRequestInFlight READ supportMapRequestInFlight NOTIFY supportMapRequestInFlightChanged)
    
    // 回测结果属性
    Q_PROPERTY(QVariantMap backtestResult READ backtestResult NOTIFY backtestResultChanged)
    Q_PROPERTY(QVariantMap resultMetrics READ resultMetrics NOTIFY resultMetricsChanged)
    Q_PROPERTY(QVariantList lastPreflightFailures READ lastPreflightFailures NOTIFY lastPreflightFailuresChanged)
    
public:
    explicit FactorBacktestController(QObject *parent = nullptr);
    ~FactorBacktestController();
    
    // 属性访问器
    bool isRunning() const { return m_isRunning; }
    int progress() const { return m_progress; }
    QString status() const { return m_status; }
    QVariantList selectedFactorIds() const { return m_selectedFactorIds; }
    void setSelectedFactorIds(const QVariantList& factorIds);
    int selectedDatasetId() const { return m_selectedDatasetId; }
    void setSelectedDatasetId(int datasetId);
    QString dataSourceMode() const { return m_dataSourceMode; }
    void setDataSourceMode(const QString& dataSourceMode);
    QVariantList selectedStockPoolSymbols() const { return m_selectedStockPoolSymbols; }
    void setSelectedStockPoolSymbols(const QVariantList& stockPoolSymbols);
    QVariantMap selectedDatasetBenchmarkMetadata() const { return m_selectedDatasetBenchmarkMetadata; }
    void setSelectedDatasetBenchmarkMetadata(const QVariantMap& metadata);
    QVariantMap backtestRuntimeParams() const { return m_backtestRuntimeParams; }
    void setBacktestRuntimeParams(const QVariantMap& backtestRuntimeParams);
    QVariantMap backtestResult() const { return m_backtestResult; }
    QVariantMap resultMetrics() const { return m_resultMetrics; }
    QVariantList lastPreflightFailures() const { return m_lastPreflightFailures; }
    QVariantMap factorSupportMapCache() const { return m_factorSupportMapCache; }
    bool supportMapRequestInFlight() const { return m_supportMapRequestInFlight; }
    
    /**
     * @brief 开始回测 - 简化版本（使用控制器内部存储的因子ID）
     * @param groupText 分组数量文本（如"10组"）
     * @param startDate 开始日期（可选，格式：yyyy-MM-dd）
     * @param endDate 结束日期（可选，格式：yyyy-MM-dd）
     */
    Q_INVOKABLE void startBacktest(const QString& groupText, 
                                   const QString& startDate = "", 
                                   const QString& endDate = "",
                                   const QVariantMap& cacheSnapshot = QVariantMap());
    
    /**
     * @brief 开始回测 - 完整版本（传递因子ID列表）
     * @param factorIds 因子ID列表
     * @param groupText 分组数量文本（如"10组"）
     * @param startDate 开始日期（可选，格式：yyyy-MM-dd）
     * @param endDate 结束日期（可选，格式：yyyy-MM-dd）
     */
    Q_INVOKABLE void startBacktestWithFactors(
        const QVariantList& factorIds,
        const QString& groupText,
        const QString& startDate = "",
        const QString& endDate = "",
        const QVariantMap& cacheSnapshot = QVariantMap());
    Q_INVOKABLE QVariantMap buildFactorSupportMap(
        const QVariantList& factorIds,
        const QString& startDate = "",
        const QString& endDate = "",
        const QVariantMap& cacheSnapshot = QVariantMap());
    Q_INVOKABLE void requestFactorSupportMapAsync(
        const QVariantList& factorIds,
        const QString& startDate = "",
        const QString& endDate = "",
        const QVariantMap& cacheSnapshot = QVariantMap(),
        quint64 requestId = 0);
    Q_INVOKABLE QVariantMap preflightCategoryMeta(const QString& category) const;
    Q_INVOKABLE QString preflightFailureDetailText(const QVariantMap& failure,
                                                   const QString& factorDisplayName = "") const;
    Q_INVOKABLE QVariantMap factorValidationState(const QString& factorId,
                                                  const QString& factorDisplayName,
                                                  bool hasFactorDefinition,
                                                  const QVariantMap& supportInfo,
                                                  const QVariantList& preflightFailures,
                                                  const QVariantMap& backtestResult,
                                                  const QString& lastBacktestError,
                                                  const QVariantList& selectedFactorIds,
                                                  const QString& dataSourceMode,
                                                  bool hasAvailableCacheDataset,
                                                  int selectedDatasetId) const;
    Q_INVOKABLE bool datasetSelectableForBacktest(const QVariantMap& dataset) const;
    Q_INVOKABLE QVariantList buildBacktestDatasetOptions(const QVariantList& datasetList) const;
    Q_INVOKABLE QVariantList normalizeFactorIds(const QVariantList& factorIds) const;
    Q_INVOKABLE QVariantMap filterFactorIdsBySupport(const QVariantList& factorIds,
                                                     const QVariantMap& supportMap) const;
    Q_INVOKABLE int beginFactorSupportMapRefresh(const QVariantList& factorIds,
                                                 const QString& startDate = "",
                                                 const QString& endDate = "",
                                                 const QVariantMap& cacheSnapshot = QVariantMap());
    Q_INVOKABLE bool handleFactorSupportMapReady(int requestId,
                                                 const QVariantMap& supportMap);
    Q_INVOKABLE void markPendingFilterAfterSupportMap();
    Q_INVOKABLE bool takePendingFilterAfterSupportMap();
    Q_INVOKABLE QVariantMap buildStockPoolComparison(const QVariantMap& previousBacktestReport,
                                                     const QVariantMap& currentDatasetInfo) const;
    Q_INVOKABLE QString stockPoolComparisonText(const QVariantList& selectedFactorIds,
                                                const QVariantMap& comparison) const;
    Q_INVOKABLE QVariantList displayedBacktestResults(const QVariantMap& backtestResult) const;
    Q_INVOKABLE QString displayedBacktestResultName(const QVariantMap& entry) const;
    Q_INVOKABLE QVariantMap resolveDisplayedBacktestState(const QVariantMap& backtestResult,
                                                          int selectedResultIndex) const;
    Q_INVOKABLE QVariantMap resolveRiskConfigurationSnapshot(const QVariantMap& displayedBacktestResult,
                                                            const QVariantMap& appliedConfiguration,
                                                            const QVariantMap& currentConfiguration) const;
    Q_INVOKABLE QString riskConfigBenchmarkSymbol(const QVariantMap& snapshot,
                                                  const QString& fallbackSymbol = QStringLiteral("000300.SH")) const;
    Q_INVOKABLE QVariantList riskConfigMetricCards(const QVariantMap& snapshot) const;
    Q_INVOKABLE QVariantMap buildSingleFactorRunEntry(const QVariantMap& result,
                                                      const QString& fallbackFactorName = QStringLiteral("单因子")) const;
    Q_INVOKABLE QVariantList pushSingleFactorRunHistory(const QVariantList& existingHistory,
                                                        const QVariantMap& result,
                                                        int historyLimit,
                                                        const QString& fallbackFactorName = QStringLiteral("单因子")) const;
    
    /**
     * @brief 取消当前回测
     */
    Q_INVOKABLE void cancelBacktest();
    Q_INVOKABLE bool clearBacktestCache();
    Q_INVOKABLE bool saveResultToFile(const QString& filePath) const;
    Q_INVOKABLE bool loadResultFromFile(const QString& filePath);
    
signals:
    // 状态变化信号
    void isRunningChanged(bool isRunning);
    void progressChanged(int progress);
    void statusChanged(const QString& status);
    
    // 因子选择变化信号
    void selectedFactorIdsChanged(const QVariantList& factorIds);
    void selectedDatasetIdChanged(int datasetId);
    void dataSourceModeChanged(const QString& dataSourceMode);
    void selectedStockPoolSymbolsChanged(const QVariantList& stockPoolSymbols);
    void backtestRuntimeParamsChanged(const QVariantMap& backtestRuntimeParams);
    void selectedDatasetBenchmarkMetadataChanged(const QVariantMap& metadata);
    void factorSupportMapCacheChanged(const QVariantMap& supportMap);
    void supportMapRequestInFlightChanged(bool inFlight);
    
    // 结果变化信号
    void backtestResultChanged(const QVariantMap& result);
    void resultMetricsChanged(const QVariantMap& resultMetrics);
    void lastPreflightFailuresChanged(const QVariantList& failures);
    void factorSupportMapReady(quint64 requestId, const QVariantMap& supportMap);
    
    // 事件信号
    void backtestStarted(const QString& factorId);
    void backtestProgress(int progress, const QString& status);
    void backtestProgressDetailed(int progress, const QString& status, int currentGroup, int totalGroups);
    void backtestCompleted(const QVariantMap& result);
    void backtestFailed(const QString& error);
    void backtestCancelled();
    
private:
    // 解析分组数量
    int parseGroupCount(const QString& groupText) const;
    bool ensureInstanceRuntime();
    QString resolveInstanceId(const QVariant& factorId) const;
    factor::FactorInstanceInfo getInstanceInfo(const QString& resolvedInstanceId) const;
    factor::BacktestConfig buildBacktestConfig(const QString& resolvedInstanceId,
                                               const QString& groupText,
                                               const QString& startDate,
                                               const QString& endDate,
                                               const QString& dataSourceMode,
                                               int datasetId,
                                               const QVariantMap& datasetBenchmarkMetadata,
                                               const QVariantList& selectedStockPoolSymbols,
                                               const QVariantMap& backtestRuntimeParams,
                                               int batchFactorCount,
                                               int workerCount) const;
    std::shared_ptr<factor::FactorBacktestExecutor> ensureBatchExecutor(
        const std::shared_ptr<factor::FactorInstanceManager>& instanceManager);
    QVariantMap buildResultMap(const QString& requestedFactorId,
                               const factor::BacktestResult& result) const;
    QVariantMap buildAggregatedResultMap() const;
    void launchNextBacktestTask();
    void resetBatchState();
    void pollBacktestProgress();
    void finalizeBacktestSuccess(const QString& requestedFactorId,
                                 const factor::BacktestResult& result,
                                 size_t batchIndex);
    void finalizeBacktestFailure(const QString& errorMessage,
                                 bool cancelled);
    void detachPendingBacktestTasks();
    void cleanupDetachedBacktestTasks(bool waitForCompletion);
    void syncBacktestMetricsToFactor(const QString& requestedFactorId,
                                     const factor::BacktestResult& result);
    void applyPersistedResult(const QVariantMap& result);
    bool persistLatestResult() const;
    bool clearPersistedResult() const;
    QString persistedResultFilePath() const;
    void shutdownBacktestInfrastructure();
    void resetResults();
    void refreshBacktestRuntimeParamsFromRiskConfiguration();
    void invalidateSupportMapState(bool clearPreflightFailures);
    
private:
    std::shared_ptr<astock::database::QtMySQLDatabase> m_database;
    std::shared_ptr<foundation::thread::ThreadPoolExecutor> m_threadPool;
    std::shared_ptr<factor::DataAvailabilityChecker> m_dataChecker;
    std::shared_ptr<factor::FactorCacheManager> m_cacheManager;
    std::shared_ptr<factor::FactorInstanceManager> m_instanceManager;
    std::shared_ptr<factor::FactorBacktestExecutor> m_batchExecutor;
    mutable QHash<QString, QString> m_resolvedInstanceIdCache;
    mutable QHash<QString, factor::FactorInstanceInfo> m_instanceInfoCache;
    std::shared_ptr<PendingBacktestLaunchProgressState> m_pendingBatchLaunchProgressState;
    std::shared_ptr<std::future<PendingBacktestBatchLaunchResult>> m_pendingBatchLaunchFuture;
    foundation::utils::Uuid m_pendingBatchTaskId;
    std::shared_ptr<std::future<std::vector<factor::BacktestResult>>> m_pendingBatchFuture;
    std::vector<DetachedPendingBacktestBatchState> m_detachedPendingBacktestBatches;
    std::vector<PendingBacktestTask> m_pendingBacktestTasks;
    std::vector<PendingBacktestTask> m_detachedBacktestTasks;
    QTimer* m_progressTimer{nullptr};
    std::atomic_bool m_cancelRequested{false};
    
    // 状态变量
    bool m_isRunning = false;
    int m_progress = 0;
    QString m_status;
    
    // 因子选择变量
    QVariantList m_selectedFactorIds;
    int m_selectedDatasetId{-1};
        QVariantMap m_selectedDatasetBenchmarkMetadata;
    QString m_dataSourceMode{"cache"};
    QVariantList m_selectedStockPoolSymbols;
    QVariantMap m_backtestRuntimeParams;
    QVariantMap m_backtestRuntimeOverrides;
    QVariantList m_batchFactorIds;
    std::vector<QVariantMap> m_batchResultMaps;
    QString m_pendingGroupText;
    QString m_pendingStartDate;
    QString m_pendingEndDate;
    QString m_pendingDataSourceMode;
    int m_pendingDatasetId{-1};
    QVariantMap m_pendingDatasetBenchmarkMetadata;
    QVariantList m_pendingStockPoolSymbols;
    QVariantMap m_pendingRuntimeParams;
    int m_pendingBatchFactorCount{0};
    int m_pendingWorkerCount{0};
    int m_activeFactorIndex{0};
    
    // 结果变量
    QVariantMap m_backtestResult;
    QVariantMap m_resultMetrics;
    QVariantList m_lastPreflightFailures;
    QVariantMap m_factorSupportMapCache;
    bool m_supportMapRequestInFlight{false};
    bool m_pendingFilterAfterSupportMap{false};
    int m_supportMapRequestSeq{0};
    int m_supportMapAppliedSeq{0};
    QString m_lastSupportMapScopeKey;
    QString m_lastSupportMapCacheKey;

    // 测试钩子：仅由友元测试访问器设置，用于纯内存回归测试。
    std::function<QString(const QVariant&)> m_resolveInstanceIdOverrideForTests;
    std::function<factor::FactorInstanceInfo(const QString&)> m_instanceInfoOverrideForTests;
    std::function<std::shared_ptr<factor::BaseFactor>(const QString&)> m_factorInstanceOverrideForTests;
    std::function<QVariantMap()> m_loadAppliedRiskConfigOverrideForTests;
    std::function<std::shared_ptr<factor::FactorBacktestExecutor>(
        const std::shared_ptr<factor::FactorInstanceManager>&,
        const std::shared_ptr<foundation::thread::ThreadPoolExecutor>&,
        const std::shared_ptr<factor::FactorCacheManager>&)> m_createExecutorOverrideForTests;
    QHash<QString, int> m_requiredWarmupTradingDaysOverrideForTests;
    bool m_skipInstanceRefreshForTests{false};
    quint64 m_selectionSupportCheckSeq{0};
    quint64 m_backtestPreflightSeq{0};
};

#endif // FACTORBACKTESTCONTROLLER_H
