#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
#include <QTimer>

#include <atomic>
#include <memory>

// 前向声明核心类型（不引入 Qt 依赖）
namespace factor::compute {
class ISignalEngine;
class IFactorComputeEngine;
class IMarketDataView;
class ParquetMarketDataView;
class SignalCache;
struct SignalSet;
struct GenerateSpec;
struct FactorError;
struct AnalysisReport;
}

namespace application::backtest {
struct ExistingModuleSlots;
struct RunSpec;
struct RunResult;
}

/**
 * @brief QML 桥接层 - 因子回测控制器
 *
 * 约束（设计文档 Section 4.2）：
 * - Qt 类型仅允许出现在此 QML 入口桥接层。
 * - 字符串仅允许在桥接层解析为核心强类型。
 * - 所有核心计算均通过 typed 接口执行，不在此层实现算法。
 * - 禁止将 Qt 类型传递到 factor::compute 命名空间内部。
 */
class FactorBacktestBridge : public QObject {
    Q_OBJECT

    // ── QML 属性（仅桥接层使用 Qt 类型）───
    Q_PROPERTY(QVariantMap backtestRuntimeParams READ backtestRuntimeParams WRITE setBacktestRuntimeParams NOTIFY backtestRuntimeParamsChanged)
    Q_PROPERTY(QVariantMap backtestResult READ backtestResult NOTIFY backtestResultChanged)
    Q_PROPERTY(bool isBacktesting READ isBacktesting NOTIFY isBacktestingChanged)
    Q_PROPERTY(double backtestProgress READ backtestProgress NOTIFY backtestProgressChanged)
    Q_PROPERTY(QString backtestStatusText READ backtestStatusText NOTIFY backtestStatusTextChanged)
    Q_PROPERTY(QVariantMap factorSupportMapCache READ factorSupportMapCache NOTIFY factorSupportMapCacheChanged)
    Q_PROPERTY(int selectedDatasetId READ selectedDatasetId WRITE setSelectedDatasetId NOTIFY selectedDatasetIdChanged)
    Q_PROPERTY(QString dataSourceMode READ dataSourceMode WRITE setDataSourceMode NOTIFY dataSourceModeChanged)
    Q_PROPERTY(QVariantMap selectedDatasetBenchmarkMetadata READ selectedDatasetBenchmarkMetadata WRITE setSelectedDatasetBenchmarkMetadata NOTIFY selectedDatasetBenchmarkMetadataChanged)

public:
    explicit FactorBacktestBridge(QObject* parent = nullptr);
    ~FactorBacktestBridge() override;

    // ── 初始化（在桥接层完成核心引擎组装）───
    Q_INVOKABLE bool initialize(const QString& marketDataPath);
    Q_INVOKABLE bool initializeWithExistingEngine(
        factor::compute::IFactorComputeEngine* existingEngine);

    // ── 回测执行 ──
    Q_INVOKABLE void startBacktest();
    Q_INVOKABLE void cancelBacktest();

    // ── 因子支持校验 ──
    Q_INVOKABLE QVariantMap buildFactorSupportMap(
        const QVariantList& factorIds,
        const QString& startDate,
        const QString& endDate,
        const QVariantMap& cacheSnapshot);
    Q_INVOKABLE int beginFactorSupportMapRefresh(
        const QVariantList& factorIds,
        const QString& startDate,
        const QString& endDate,
        const QVariantMap& cacheSnapshot);

    // ── 属性访问器 ──
    QVariantMap backtestRuntimeParams() const;
    void setBacktestRuntimeParams(const QVariantMap& params);

    QVariantMap backtestResult() const;
    bool isBacktesting() const;
    double backtestProgress() const;
    QString backtestStatusText() const;
    QVariantMap factorSupportMapCache() const;

    int selectedDatasetId() const;
    void setSelectedDatasetId(int id);
    QString dataSourceMode() const;
    void setDataSourceMode(const QString& mode);
    QVariantMap selectedDatasetBenchmarkMetadata() const;
    void setSelectedDatasetBenchmarkMetadata(const QVariantMap& metadata);

signals:
    void backtestRuntimeParamsChanged();
    void backtestResultChanged();
    void isBacktestingChanged();
    void backtestProgressChanged();
    void backtestStatusTextChanged();
    void factorSupportMapCacheChanged();
    void selectedDatasetIdChanged();
    void dataSourceModeChanged();
    void selectedDatasetBenchmarkMetadataChanged();
    void backtestCompleted(const QVariantMap& result);
    void backtestFailed(const QString& error);
    void supportMapRefreshed(int requestId, const QVariantMap& supportMap);

private slots:
    void onBacktestTimeout();

private:
    // ── 内部方法（桥接层转换：Qt 类型 → 核心强类型 → Qt 类型）───

    /// @brief 从 QVariantMap 构建核心 GenerateSpec
    factor::compute::GenerateSpec buildGenerateSpecFromParams(
        const QVariantMap& params,
        const QVariantList& factorIds) const;

    /// @brief 将核心 RunResult 转换为 QVariantMap
    static QVariantMap convertRunResult(const application::backtest::RunResult& result);

    /// @brief 将核心 SignalSet 转换为 QVariantMap
    static QVariantMap convertSignalSetSummary(const factor::compute::SignalSet& signalSet);

    /// @brief 确保核心引擎已初始化（懒加载）
    bool ensureEngineInitialized();

    // ── 核心引擎（typed-only，无 Qt 依赖）───
    std::unique_ptr<factor::compute::ParquetMarketDataView> ownedMarketDataView_;
    std::unique_ptr<factor::compute::ISignalEngine> ownedSignalEngine_;
    std::unique_ptr<factor::compute::SignalCache> ownedSignalCache_;

    // 外部注入的引擎（当使用 initializeWithExistingEngine 时）
    factor::compute::IFactorComputeEngine* existingComputeEngine_{nullptr};
    factor::compute::ISignalEngine* externalSignalEngine_{nullptr};

    // ── Qt 属性存储（仅桥接层）───
    QVariantMap m_backtestRuntimeParams;
    QVariantMap m_backtestResult;
    std::atomic<bool> m_isBacktesting{false};
    double m_backtestProgress{0.0};
    QString m_backtestStatusText;
    QVariantMap m_factorSupportMapCache;
    int m_selectedDatasetId{0};
    QString m_dataSourceMode;
    QVariantMap m_selectedDatasetBenchmarkMetadata;

    // 定时器
    QTimer* m_timeoutTimer{nullptr};
};