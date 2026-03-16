#ifndef FACTORBACKTESTCONTROLLER_H
#define FACTORBACKTESTCONTROLLER_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <memory>
#include <functional>

namespace domain::backtest {
    class FactorBacktestService;
    class FactorBacktestResult;
    struct FactorBacktestConfig;
}

/**
 * @brief 因子回测控制器 - QML桥接类
 * 
 * 提供因子回测与分组功能的QML接口
 */
class FactorBacktestController : public QObject
{
    Q_OBJECT
    
    // 回测状态属性
    Q_PROPERTY(bool isRunning READ isRunning NOTIFY isRunningChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    
    // 回测结果属性
    Q_PROPERTY(QVariantMap lastResult READ lastResult NOTIFY lastResultChanged)
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
    QString lastError() const { return m_lastError; }
    QVariantMap lastResult() const { return m_lastResult; }
    QVariantList groupResults() const { return m_groupResults; }
    QVariantMap icirResult() const { return m_icirResult; }
    QVariantMap summaryStats() const { return m_summaryStats; }
    
    // QML可调用方法
    Q_INVOKABLE bool initialize();
    Q_INVOKABLE void shutdown();
    
    /**
     * @brief 运行因子回测（同步）
     * @param factorId 因子ID
     * @param config 回测配置
     * @return 回测结果
     */
    Q_INVOKABLE QVariantMap runFactorBacktestSync(
        const QString& factorId,
        const QVariantMap& config);
    
    /**
     * @brief 运行因子回测（异步）
     * @param factorId 因子ID
     * @param config 回测配置
     */
    Q_INVOKABLE void runFactorBacktestAsync(
        const QString& factorId,
        const QVariantMap& config);
    
    /**
     * @brief 取消当前回测
     */
    Q_INVOKABLE void cancelBacktest();
    
    /**
     * @brief 保存回测结果到文件
     * @param filePath 文件路径
     * @return 是否保存成功
     */
    Q_INVOKABLE bool saveResultToFile(const QString& filePath);
    
    /**
     * @brief 从文件加载回测结果
     * @param filePath 文件路径
     * @return 加载的结果
     */
    Q_INVOKABLE QVariantMap loadResultFromFile(const QString& filePath);
    
    /**
     * @brief 获取默认回测配置
     * @return 默认配置
     */
    Q_INVOKABLE QVariantMap getDefaultConfig() const;
    
    /**
     * @brief 验证回测配置
     * @param config 配置
     * @return 验证结果
     */
    Q_INVOKABLE QVariantMap validateConfig(const QVariantMap& config) const;
    
    /**
     * @brief 获取分组方法列表
     * @return 分组方法列表
     */
    Q_INVOKABLE QVariantList getGroupingMethods() const;
    
    /**
     * @brief 获取回测策略列表
     * @return 回测策略列表
     */
    Q_INVOKABLE QVariantList getBacktestStrategies() const;
    
    /**
     * @brief 清除缓存
     */
    Q_INVOKABLE void clearCache();
    
signals:
    // 状态变化信号
    void isRunningChanged(bool isRunning);
    void progressChanged(int progress);
    void statusChanged(const QString& status);
    void lastErrorChanged(const QString& error);
    
    // 结果变化信号
    void lastResultChanged(const QVariantMap& result);
    void groupResultsChanged(const QVariantList& groups);
    void icirResultChanged(const QVariantMap& icirResult);
    void summaryStatsChanged(const QVariantMap& summaryStats);
    
    // 事件信号
    void backtestStarted(const QString& factorId);
    void backtestProgress(int progress, const QString& status);
    void backtestCompleted(const QVariantMap& result);
    void backtestFailed(const QString& error);
    void backtestCancelled();
    
private slots:
    void onBacktestProgress(int progress, const QString& status);
    void onBacktestCompleted(const QVariantMap& result);
    void onBacktestFailed(const QString& error);
    
private:
    // 初始化回测服务
    bool initializeService();
    
    // 转换C++结果到QML格式
    QVariantMap convertResultToVariantMap(const domain::backtest::FactorBacktestResult& result);
    QVariantList convertGroupsToVariantList(const domain::backtest::FactorBacktestResult& result);
    QVariantMap convertICIRToVariantMap(const domain::backtest::FactorBacktestResult& result);
    QVariantMap convertSummaryToVariantMap(const domain::backtest::FactorBacktestResult& result);
    
    // 转换配置
    domain::backtest::FactorBacktestConfig convertConfigFromVariantMap(const QVariantMap& config) const;
    QVariantMap convertConfigToVariantMap(const domain::backtest::FactorBacktestConfig& config) const;
    
private:
    std::unique_ptr<domain::backtest::FactorBacktestService> m_service;
    
    // 状态变量
    bool m_isRunning = false;
    int m_progress = 0;
    QString m_status;
    QString m_lastError;
    
    // 结果变量
    QVariantMap m_lastResult;
    QVariantList m_groupResults;
    QVariantMap m_icirResult;
    QVariantMap m_summaryStats;
    
    // 异步任务相关
    std::function<void()> m_cancelCallback;
};

#endif // FACTORBACKTESTCONTROLLER_H