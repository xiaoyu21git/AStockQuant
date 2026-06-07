#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>

#include <atomic>
#include <memory>

#include "foundation/thread/ThreadPoolExecutor.h"
#include "BacktestContracts.hpp"

/// @brief 策略回测桥接层
///
/// 策略回测 QML 通过 StrategyBacktestController (QML Type) 调用此桥接层。
/// 桥接层职责：参数转换 + 线程创建/管理 + 进度中继 + 结果信号转发 + 错误转发。
/// 不允许在此层写任何业务逻辑。
///
/// 与因子回测的区别：
/// - 因子回测：FactorBacktestController → FactorBacktestBridge (SignalOnly mode)
/// - 策略回测：StrategyBacktestController → StrategyBacktestBridge (FullPipeline mode)
///
/// 模块注入链：
/// - 启动时自动从 StrategyBridge 单例获取 IStrategyService
/// - 因子引擎从 FactorService 单例获取
/// - 交易模块使用 ModuleRegistryAssembler 内置默认实现
class StrategyBacktestBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isRunning READ isRunning NOTIFY isRunningChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit StrategyBacktestBridge(QObject* parent = nullptr);
    ~StrategyBacktestBridge() override;

    Q_INVOKABLE bool initialize();
    Q_INVOKABLE void runBacktest(const QString& strategyId, const QVariantMap& params);
    Q_INVOKABLE void cancelBacktest();

    /// @brief 由应用层注入域层模块，Bridge 不持有所有权
    void setBacktestModules(const application::backtest::ExistingModuleSlots& modules);

    bool isRunning() const;
    double progress() const;
    QString status() const;

signals:
    void isRunningChanged();
    void progressChanged();
    void statusChanged();
    void backtestCompleted(const QVariantMap& result);
    void backtestFailed(const QString& error);
    void backtestCancelled();

private:
    /// @brief 自动从现有单例解析回测模块
    /// @param strategyId 策略 ID，用于在 StrategyBridge 中查找对应的运行时引擎
    void resolveBacktestModules(const QString& strategyId);

    std::unique_ptr<foundation::thread::ThreadPoolExecutor> m_workerPool;
    std::atomic<bool> m_isRunning{false};
    double m_progress{0.0};
    QString m_statusText;
    application::backtest::ExistingModuleSlots m_backtestModules{};
    bool m_modulesResolved{false};
};
