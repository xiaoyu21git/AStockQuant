#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>

#include <atomic>
#include <memory>

#include "foundation/thread/ThreadPoolExecutor.h"

/// @brief 参数自动调优桥接层 — QML ↔ 领域层
/// 职责：QML 参数解析 → 线程调度 → 领域 IOptimizer/ParameterSpace 调用 → 结果回传 QML
/// 禁止业务逻辑（所有优化算法、评估逻辑均在领域层）
class ParameterTuningBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isRunning READ isRunning NOTIFY isRunningChanged)
    Q_PROPERTY(int currentTrial READ currentTrial NOTIFY currentTrialChanged)
    Q_PROPERTY(int totalTrials READ totalTrials NOTIFY totalTrialsChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit ParameterTuningBridge(QObject* parent = nullptr);
    ~ParameterTuningBridge() override;

    /// @brief 获取指定策略类型的可调参数范围列表 (供 QML 配置面板展示)
    /// @return QVariantList of QVariantMap {name, type, min, max, step, options[], candidateCount}
    Q_INVOKABLE QVariantList getTuningParamRanges(int strategyTypeIndex) const;

    /// @brief 估算参数组合总数 (供 QML 显示组合爆炸警告)
    Q_INVOKABLE int estimateCombinations(int strategyTypeIndex) const;

    /// @brief 启动参数调优
    /// @param strategyId 策略 ID (用于引擎创建)
    /// @param strategyTypeIndex StrategyType 枚举索引 (int)
    /// @param params 覆盖参数: optimizerKind(0=GridSearch), maxTrials, objectiveMetric, paramRanges(可选)
    Q_INVOKABLE void startTuning(const QString& strategyId, int strategyTypeIndex,
                                  const QVariantMap& params);

    /// @brief 取消正在运行的调优
    Q_INVOKABLE void cancelTuning();

    bool isRunning() const;
    int currentTrial() const;
    int totalTrials() const;
    double progress() const;
    QString status() const;

signals:
    void isRunningChanged();
    void currentTrialChanged();
    void totalTrialsChanged();
    void progressChanged();
    void statusChanged();
    void trialCompleted(int trialIndex, const QVariantMap& trial);
    void tuningCompleted(const QVariantMap& result);
    void tuningFailed(const QString& error);
    void tuningCancelled();

private:
    /// @brief 从 QVariantMap 构建 TunerConfig (在 worker 线程中执行)
    void executeTuning(const std::string& strategyId, int strategyTypeIndex,
                       const QVariantMap& params);

    std::unique_ptr<foundation::thread::ThreadPoolExecutor> m_workerPool;
    std::atomic<bool> m_isRunning{false};
    std::atomic<bool> m_cancelRequested{false};
    int m_currentTrial{0};
    int m_totalTrials{0};
    double m_progress{0.0};
    QString m_statusText;
};
