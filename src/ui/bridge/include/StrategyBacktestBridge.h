#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>

#include <atomic>
#include <memory>
#include "factor_compute/ArrowMarketDataView.h"

#include "foundation/thread/ThreadPoolExecutor.h"


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
    bool m_modulesResolved{false};
    std::unique_ptr<class factor::compute::ArrowMarketDataView> m_strategyDataSvc;
};
