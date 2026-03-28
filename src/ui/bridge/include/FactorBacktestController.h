#ifndef FACTORBACKTESTCONTROLLER_H
#define FACTORBACKTESTCONTROLLER_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>
#include <atomic>
#include <future>
#include <memory>

#include "../../../domain/factor/include/FactorBacktestExecutor.h"

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

/**
 * @brief 因子回测控制器 - 简化版本
 * 
 * 只提供核心的回测功能，移除所有过度设计
 */
class FactorBacktestController : public QObject
{
    Q_OBJECT
    
    // 回测状态属性
    Q_PROPERTY(bool isRunning READ isRunning NOTIFY isRunningChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    
    // 因子选择属性
    Q_PROPERTY(QVariantList selectedFactorIds READ selectedFactorIds WRITE setSelectedFactorIds NOTIFY selectedFactorIdsChanged)
    Q_PROPERTY(int selectedDatasetId READ selectedDatasetId WRITE setSelectedDatasetId NOTIFY selectedDatasetIdChanged)
    Q_PROPERTY(QString dataSourceMode READ dataSourceMode WRITE setDataSourceMode NOTIFY dataSourceModeChanged)
    
    // 回测结果属性
    Q_PROPERTY(QVariantMap backtestResult READ backtestResult NOTIFY backtestResultChanged)
    Q_PROPERTY(QVariantList groupResults READ groupResults NOTIFY groupResultsChanged)
    Q_PROPERTY(QVariantMap icirResult READ icirResult NOTIFY icirResultChanged)
    Q_PROPERTY(QVariantMap summaryStats READ summaryStats NOTIFY summaryStatsChanged)
    
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
    QVariantMap backtestResult() const { return m_backtestResult; }
    QVariantList groupResults() const { return m_groupResults; }
    QVariantMap icirResult() const { return m_icirResult; }
    QVariantMap summaryStats() const { return m_summaryStats; }
    
    /**
     * @brief 开始回测 - 简化版本（使用控制器内部存储的因子ID）
     * @param groupText 分组数量文本（如"10组"）
     * @param startDate 开始日期（可选，格式：yyyy-MM-dd）
     * @param endDate 结束日期（可选，格式：yyyy-MM-dd）
     */
    Q_INVOKABLE void startBacktest(const QString& groupText, 
                                   const QString& startDate = "", 
                                   const QString& endDate = "");
    
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
        const QString& endDate = "");
    
    /**
     * @brief 取消当前回测
     */
    Q_INVOKABLE void cancelBacktest();
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
    
    // 结果变化信号
    void backtestResultChanged(const QVariantMap& result);
    void groupResultsChanged(const QVariantList& groups);
    void icirResultChanged(const QVariantMap& icirResult);
    void summaryStatsChanged(const QVariantMap& summaryStats);
    
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
    bool initializeRuntime();
    QString resolveInstanceId(const QVariant& factorId) const;
    factor::BacktestConfig buildBacktestConfig(const QString& resolvedInstanceId,
                                               const QString& groupText,
                                               const QString& startDate,
                                               const QString& endDate) const;
    QVariantMap buildResultMap(const QString& requestedFactorId,
                               const factor::BacktestResult& result) const;
    void pollBacktestProgress();
    void finalizeBacktestSuccess(const QString& requestedFactorId,
                                 const factor::BacktestResult& result);
    void finalizeBacktestFailure(const QString& errorMessage,
                                 bool cancelled);
    void applyPersistedResult(const QVariantMap& result);
    bool persistLatestResult() const;
    QString persistedResultFilePath() const;
    void resetResults();
    
private:
    std::shared_ptr<astock::database::QtMySQLDatabase> m_database;
    std::shared_ptr<foundation::thread::ThreadPoolExecutor> m_threadPool;
    std::shared_ptr<factor::DataAvailabilityChecker> m_dataChecker;
    std::shared_ptr<factor::FactorCacheManager> m_cacheManager;
    std::shared_ptr<factor::FactorInstanceManager> m_instanceManager;
    std::unique_ptr<factor::FactorBacktestExecutor> m_executor;
    std::unique_ptr<std::future<factor::BacktestResult>> m_pendingBacktestResult;
    QTimer* m_progressTimer{nullptr};
    foundation::utils::Uuid m_activeTaskId;
    bool m_hasActiveTask{false};
    std::atomic_bool m_cancelRequested{false};
    
    // 状态变量
    bool m_isRunning = false;
    int m_progress = 0;
    QString m_status;
    
    // 因子选择变量
    QVariantList m_selectedFactorIds;
    int m_selectedDatasetId{-1};
    QString m_dataSourceMode{"cache"};
    QString m_activeRequestedFactorId;
    
    // 结果变量
    QVariantMap m_backtestResult;
    QVariantList m_groupResults;
    QVariantMap m_icirResult;
    QVariantMap m_summaryStats;
};

#endif // FACTORBACKTESTCONTROLLER_H
