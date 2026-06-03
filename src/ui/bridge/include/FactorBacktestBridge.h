#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
#include <QTimer>

#include <atomic>
#include <memory>

// 前向声明
namespace factor::compute {
class ISignalEngine;
class IFactorComputeEngine;
}

class FactorBacktestBridge : public QObject {
    Q_OBJECT

    // ── QML 属性 ──
    Q_PROPERTY(QVariantMap backtestRuntimeParams READ backtestRuntimeParams WRITE setBacktestRuntimeParams NOTIFY backtestRuntimeParamsChanged)
    Q_PROPERTY(QVariantMap backtestResult READ backtestResult NOTIFY backtestResultChanged)
    Q_PROPERTY(bool isRunning READ isRunning NOTIFY isRunningChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap factorSupportMapCache READ factorSupportMapCache NOTIFY factorSupportMapCacheChanged)
    Q_PROPERTY(int selectedDatasetId READ selectedDatasetId WRITE setSelectedDatasetId NOTIFY selectedDatasetIdChanged)
    Q_PROPERTY(QString dataSourceMode READ dataSourceMode WRITE setDataSourceMode NOTIFY dataSourceModeChanged)
    Q_PROPERTY(QVariantMap selectedDatasetBenchmarkMetadata READ selectedDatasetBenchmarkMetadata WRITE setSelectedDatasetBenchmarkMetadata NOTIFY selectedDatasetBenchmarkMetadataChanged)
    Q_PROPERTY(QVariantList lastPreflightFailures READ lastPreflightFailures NOTIFY lastPreflightFailuresChanged)
    Q_PROPERTY(bool supportMapRequestInFlight READ supportMapRequestInFlight NOTIFY supportMapRequestInFlightChanged)
    Q_PROPERTY(QVariantList selectedStockPoolSymbols READ selectedStockPoolSymbols WRITE setSelectedStockPoolSymbols NOTIFY selectedStockPoolSymbolsChanged)
    Q_PROPERTY(QVariantMap resultMetrics READ resultMetrics NOTIFY resultMetricsChanged)

public:
    explicit FactorBacktestBridge(QObject* parent = nullptr);
    ~FactorBacktestBridge() override;

    // ── QML 方法 ──
    Q_INVOKABLE bool initialize();
    Q_INVOKABLE void startBacktestWithFactors(const QVariantList& factorIds, const QString& groupText, const QString& startDate, const QString& endDate, const QVariantMap& cacheSnapshot);
    Q_INVOKABLE void startCompositeBacktest(const QVariantMap& compositeDraft, const QString& groupText, const QString& startDate, const QString& endDate, const QVariantMap& cacheSnapshot);
    Q_INVOKABLE void cancelBacktest();
    Q_INVOKABLE QVariantMap getDefaultConfig() const;
    Q_INVOKABLE QVariantList getAvailableDataSets() const;
    Q_INVOKABLE QVariantList buildBacktestDatasetOptions(const QVariantList& datasetList) const;
    Q_INVOKABLE bool datasetSelectableForBacktest(const QVariantMap& dataset) const;
    Q_INVOKABLE void runFactorBacktestAsync(const QString& factorId, const QVariantMap& config);
    Q_INVOKABLE QVariantMap buildFactorSupportMap(const QVariantList& factorIds, const QString& startDate, const QString& endDate, const QVariantMap& cacheSnapshot);
    Q_INVOKABLE int beginFactorSupportMapRefresh(const QVariantList& factorIds, const QString& startDate, const QString& endDate, const QVariantMap& cacheSnapshot);
    Q_INVOKABLE bool handleFactorSupportMapReady(int requestId, const QVariantMap& supportMap);
    Q_INVOKABLE QVariantList normalizeFactorIds(const QVariantList& factorIds) const;
    Q_INVOKABLE QVariantList displayedBacktestResults(const QVariantMap& rawResult) const;
    Q_INVOKABLE QString displayedBacktestResultName(const QVariantMap& entry) const;
    Q_INVOKABLE QVariantMap buildSingleFactorRunEntry(const QVariantMap& result, const QString& factorName) const;
    Q_INVOKABLE QVariantList pushSingleFactorRunHistory(const QVariantList& history, const QVariantMap& entry, int limit, const QString& factorName) const;
    Q_INVOKABLE QVariantMap factorValidationState(const QString& factorId, const QString& factorName, bool hasDefinition, const QVariantMap& supportInfo, const QVariantList& preflightFailures, const QVariantMap& result, const QString& error, const QVariantList& selectedIds, const QString& sourceMode, bool hasCache, int datasetId) const;
    Q_INVOKABLE QVariantMap resolveRiskConfigurationSnapshot(const QVariantMap& backtestResult, const QVariantMap& appliedConfig, const QVariantMap& snapshot) const;
    Q_INVOKABLE QVariantList riskConfigMetricCards(const QVariantMap& riskSnapshot) const;

    // ── 属性访问器 ──
    QVariantMap backtestRuntimeParams() const;
    void setBacktestRuntimeParams(const QVariantMap& params);
    QVariantMap backtestResult() const;
    bool isRunning() const;
    double progress() const;
    QString status() const;
    QVariantMap factorSupportMapCache() const;
    int selectedDatasetId() const;
    void setSelectedDatasetId(int id);
    QString dataSourceMode() const;
    void setDataSourceMode(const QString& mode);
    QVariantMap selectedDatasetBenchmarkMetadata() const;
    void setSelectedDatasetBenchmarkMetadata(const QVariantMap& metadata);
    QVariantList lastPreflightFailures() const;
    bool supportMapRequestInFlight() const;
    QVariantList selectedStockPoolSymbols() const;
    void setSelectedStockPoolSymbols(const QVariantList& symbols);
    QVariantMap resultMetrics() const;

signals:
    void backtestRuntimeParamsChanged();
    void backtestResultChanged();
    void isRunningChanged();
    void progressChanged();
    void statusChanged();
    void factorSupportMapCacheChanged();
    void selectedDatasetIdChanged();
    void dataSourceModeChanged();
    void selectedDatasetBenchmarkMetadataChanged();
    void lastPreflightFailuresChanged();
    void supportMapRequestInFlightChanged();
    void selectedStockPoolSymbolsChanged();
    void resultMetricsChanged();
    void backtestStarted(const QString& factorId);
    void backtestProgress(double progress, const QString& status);
    void backtestProgressDetailed(double progress, const QString& status, int currentGroup, int totalGroups);
    void backtestCompleted(const QVariantMap& result);
    void backtestFailed(const QString& error);
    void backtestCancelled();
    void factorSupportMapReady(int requestId, const QVariantMap& supportMap);

private:
    QVariantMap m_backtestRuntimeParams;
    QVariantMap m_backtestResult;
    QVariantMap m_resultMetrics;
    std::atomic<bool> m_isRunning{false};
    double m_progress{0.0};
    QString m_statusText;
    QVariantMap m_factorSupportMapCache;
    int m_selectedDatasetId{0};
    QString m_dataSourceMode{QStringLiteral("cache")};
    QVariantMap m_selectedDatasetBenchmarkMetadata;
    QVariantList m_selectedStockPoolSymbols;
    QVariantList m_lastPreflightFailures;
    std::atomic<bool> m_supportMapRequestInFlight{false};
    QTimer* m_timeoutTimer{nullptr};
};