#ifndef STRATEGYBACKTESTCONTROLLER_H
#define STRATEGYBACKTESTCONTROLLER_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
#include <map>
#include <memory>

#include "../../../domain/factor/include/factor_enums.h"

namespace domain::backtest {
    class DatabaseStockDataProvider;
    class DatabaseFactorDataProvider;
    class CacheManager;
    class StrategyBacktestService;
    class StrategyBacktestConfig;
    class StrategyBacktestResult;
}

/**
 * @brief 策略回测控制器 - 专门处理策略回测
 * 
 * 负责策略回测的UI逻辑控制，与StrategyBacktestService交互
 */
class StrategyBacktestController : public QObject
{
    Q_OBJECT
    
    // 回测状态属性
    Q_PROPERTY(bool isRunning READ isRunning NOTIFY isRunningChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString currentStageKey READ currentStageKey NOTIFY currentStageKeyChanged)
    Q_PROPERTY(QString currentStageLabel READ currentStageLabel NOTIFY currentStageLabelChanged)
    Q_PROPERTY(bool collectingData READ collectingData NOTIFY collectingDataChanged)
    
    // 策略配置属性
    Q_PROPERTY(QString selectedStrategyId READ selectedStrategyId WRITE setSelectedStrategyId NOTIFY selectedStrategyIdChanged)
    Q_PROPERTY(QVariantMap strategyParams READ strategyParams WRITE setStrategyParams NOTIFY strategyParamsChanged)
    Q_PROPERTY(double initialCapital READ initialCapital WRITE setInitialCapital NOTIFY initialCapitalChanged)
    Q_PROPERTY(QString startDate READ startDate WRITE setStartDate NOTIFY startDateChanged)
    Q_PROPERTY(QString endDate READ endDate WRITE setEndDate NOTIFY endDateChanged)
    Q_PROPERTY(QVariantList selectedSymbols READ selectedSymbols WRITE setSelectedSymbols NOTIFY selectedSymbolsChanged)
    Q_PROPERTY(QString dataSourceMode READ dataSourceMode WRITE setDataSourceMode NOTIFY dataSourceModeChanged)
    Q_PROPERTY(int selectedDatasetId READ selectedDatasetId WRITE setSelectedDatasetId NOTIFY selectedDatasetIdChanged)
    
    // 回测结果属性
    Q_PROPERTY(QVariantMap backtestResult READ backtestResult NOTIFY backtestResultChanged)
    Q_PROPERTY(QVariantList optimizationHistory READ optimizationHistory NOTIFY optimizationHistoryChanged)
    Q_PROPERTY(QVariantMap comparisonResult READ comparisonResult NOTIFY comparisonResultChanged)
    
public:
    explicit StrategyBacktestController(QObject *parent = nullptr);
    ~StrategyBacktestController();

    static double normalizeInitialCapitalValue(const QVariant& value, double fallback = 0.0);
    static domain::backtest::StrategyBacktestConfig resolveConfigForTesting(
        const QString& strategyId,
        const QVariantMap& strategyParams,
        const QVariantList& symbols,
        const QString& startDate,
        const QString& endDate);
    
    // 属性访问器
    bool isRunning() const { return m_isRunning; }
    int progress() const { return m_progress; }
    QString status() const { return m_status; }
    QString currentStageKey() const { return m_currentStageKey; }
    QString currentStageLabel() const { return m_currentStageLabel; }
    bool collectingData() const { return m_collectingData; }
    
    QString selectedStrategyId() const { return m_selectedStrategyId; }
    void setSelectedStrategyId(const QString& strategyId);
    
    QVariantMap strategyParams() const { return m_strategyParams; }
    void setStrategyParams(const QVariantMap& params);
    
    double initialCapital() const { return m_initialCapital; }
    void setInitialCapital(double capital);
    
    QString startDate() const { return m_startDate; }
    void setStartDate(const QString& date);
    
    QString endDate() const { return m_endDate; }
    void setEndDate(const QString& date);
    
    QVariantList selectedSymbols() const { return m_selectedSymbols; }
    void setSelectedSymbols(const QVariantList& symbols);

    QString dataSourceMode() const { return m_dataSourceMode; }
    void setDataSourceMode(const QString& dataSourceMode);

    int selectedDatasetId() const { return m_selectedDatasetId; }
    void setSelectedDatasetId(int datasetId);
    
    QVariantMap backtestResult() const { return m_backtestResult; }
    QVariantList optimizationHistory() const { return m_optimizationHistory; }
    QVariantMap comparisonResult() const { return m_comparisonResult; }
    
    /**
     * @brief 开始策略回测
     * @param strategyId 策略ID
     * @param strategyParams 策略参数
     * @param symbols 交易标的列表
     * @param startDate 开始日期
     * @param endDate 结束日期
     */
    Q_INVOKABLE void startStrategyBacktest(
        const QString& strategyId,
        const QVariantMap& strategyParams,
        const QVariantList& symbols,
        const QString& startDate,
        const QString& endDate);
    
    /**
     * @brief 批量回测多个策略
     * @param strategies 策略配置列表
     */
    Q_INVOKABLE void startBatchStrategyBacktest(const QVariantList& strategies);
    
    /**
     * @brief 参数优化
     * @param baseStrategyId 基础策略ID
     * @param paramRanges 参数范围映射 {参数名: [最小值, 最大值]}
     * @param objectiveFunction 目标函数
     * @param maxIterations 最大迭代次数
     */
    Q_INVOKABLE void optimizeStrategyParameters(
        const QString& baseStrategyId,
        const QVariantMap& paramRanges,
        const QString& objectiveFunction = "sharpe_ratio",
        int maxIterations = 100);
    
    /**
     * @brief 对比多个策略
     * @param strategies 策略配置列表
     */
    Q_INVOKABLE void compareStrategies(const QVariantList& strategies);
    
    /**
     * @brief 取消当前回测
     */
    Q_INVOKABLE void cancelBacktest();
    
    /**
     * @brief 获取支持的策略列表
     * @return 策略列表
     */
    Q_INVOKABLE QVariantList getAvailableStrategies() const;
    
    /**
     * @brief 获取默认策略参数
     * @param strategyId 策略ID
     * @return 默认参数映射
     */
    Q_INVOKABLE QVariantMap getDefaultStrategyParams(const QString& strategyId) const;
    
    /**
     * @brief 获取默认日期范围
     * @return 日期范围映射 {startDate, endDate}
     */
    Q_INVOKABLE QVariantMap getDefaultDateRange() const;
    
    /**
     * @brief 获取可交易的标的列表
     * @param universeId 股票池ID
     * @return 标的列表
     */
    Q_INVOKABLE QVariantList getAvailableSymbols(const QString& universeId = "") const;

    Q_INVOKABLE QVariantList getAvailableIndustries() const;

    Q_INVOKABLE QVariantList getIndexConstituentSymbols(const QString& indexSymbol,
                                                        const QString& snapshotDate = "") const;
    
    /**
     * @brief 保存回测结果到文件
     * @param filePath 文件路径
     * @return 是否保存成功
     */
    Q_INVOKABLE bool saveResultToFile(const QString& filePath) const;
    
    /**
     * @brief 从文件加载回测结果
     * @param filePath 文件路径
     * @return 是否加载成功
     */
    Q_INVOKABLE bool loadResultFromFile(const QString& filePath);

    Q_INVOKABLE void prepareForNextRun(bool clearResult = true);
    
signals:
    // 状态变化信号
    void isRunningChanged(bool isRunning);
    void progressChanged(int progress);
    void statusChanged(const QString& status);
    void currentStageKeyChanged(const QString& currentStageKey);
    void currentStageLabelChanged(const QString& currentStageLabel);
    void collectingDataChanged(bool collectingData);
    
    // 配置变化信号
    void selectedStrategyIdChanged(const QString& strategyId);
    void strategyParamsChanged(const QVariantMap& params);
    void initialCapitalChanged(double capital);
    void startDateChanged(const QString& date);
    void endDateChanged(const QString& date);
    void selectedSymbolsChanged(const QVariantList& symbols);
    void dataSourceModeChanged(const QString& dataSourceMode);
    void selectedDatasetIdChanged(int datasetId);
    
    // 结果变化信号
    void backtestResultChanged(const QVariantMap& result);
    void optimizationHistoryChanged(const QVariantList& history);
    void comparisonResultChanged(const QVariantMap& result);
    
    // 事件信号
    void backtestStarted(const QString& strategyId);
    void backtestProgress(int progress, const QString& status);
    void backtestCompleted(const QVariantMap& result);
    void backtestFailed(const QString& error);
    void backtestCancelled();
    
    void optimizationStarted();
    void optimizationProgress(int progress, const QString& status);
    void optimizationCompleted(const QVariantMap& result);
    void optimizationFailed(const QString& error);
    
    void comparisonStarted();
    void comparisonCompleted(const QVariantMap& result);
    void comparisonFailed(const QString& error);
    
private:
    // 解析参数范围
    std::map<std::string, std::pair<double, double>> parseParamRanges(const QVariantMap& qmlRanges) const;
    
    domain::backtest::StrategyBacktestConfig createConfig(
        const QString& strategyId,
        const QVariantMap& strategyParams,
        const QVariantList& symbols,
        const QString& startDate,
        const QString& endDate) const;
    
    QVariantMap convertResultToQml(const domain::backtest::StrategyBacktestResult& result) const;
    QVariantList convertHistoryToQml(const QVariantList& history) const;
    void updateBacktestState(int progress, const QString& status);
    void resetTransientRunState(bool clearResult);
    
private:
    std::unique_ptr<domain::backtest::StrategyBacktestService> m_service;
    std::shared_ptr<domain::backtest::DatabaseStockDataProvider> m_stockDataProvider;
    std::shared_ptr<domain::backtest::DatabaseFactorDataProvider> m_factorDataProvider;
    std::shared_ptr<domain::backtest::CacheManager> m_cacheManager;
    
    // 状态变量
    bool m_isRunning = false;
    int m_progress = 0;
    QString m_status;
    QString m_currentStageKey{QStringLiteral("idle")};
    QString m_currentStageLabel{QStringLiteral("等待开始")};
    bool m_collectingData = false;
    
    // 配置变量
    QString m_selectedStrategyId;
    QVariantMap m_strategyParams;
    double m_initialCapital = 1000000.0;
    QString m_startDate;
    QString m_endDate;
    QVariantList m_selectedSymbols;
    QString m_dataSourceMode{"raw"};
    int m_selectedDatasetId{-1};
    
    // 结果变量
    QVariantMap m_backtestResult;
    QVariantList m_optimizationHistory;
    QVariantMap m_comparisonResult;
    
    // 任务ID
    QString m_currentTaskId;
};

#endif // STRATEGYBACKTESTCONTROLLER_H