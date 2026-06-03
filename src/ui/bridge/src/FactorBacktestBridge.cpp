// FactorBacktestBridge.cpp - 简化的因子回测桥接层
// 当前版本：提供 QML 框架所需的属性和方法存根
// 后续可逐步接入真实的 factor::compute 引擎

#include "FactorBacktestBridge.h"

#include <QCoreApplication>
#include <QThread>
#include <QDebug>

FactorBacktestBridge::FactorBacktestBridge(QObject* parent)
    : QObject(parent)
{
    qDebug() << "[FactorBacktestBridge] 实例已创建";
}

FactorBacktestBridge::~FactorBacktestBridge() = default;

// ═══════════════════════════════ 初始化和回测方法 ═══════════════════════════════

bool FactorBacktestBridge::initialize()
{
    m_statusText = QStringLiteral("因子回测引擎待接入");
    emit statusChanged();
    return true;
}

void FactorBacktestBridge::startBacktestWithFactors(
    const QVariantList& /*factorIds*/,
    const QString& /*groupText*/,
    const QString& /*startDate*/,
    const QString& /*endDate*/,
    const QVariantMap& /*cacheSnapshot*/)
{
    m_isRunning.store(true);
    emit isRunningChanged();
    m_progress = 0.0;
    emit progressChanged();
    m_statusText = QStringLiteral("回测启动中...");
    emit statusChanged();

    QMetaObject::invokeMethod(qApp, [this]() {
        // 模拟进度（暂时不接入真实引擎）
        m_progress = 50.0;
        emit progressChanged();
        m_statusText = QStringLiteral("因子计算中...");
        emit statusChanged();

        m_progress = 100.0;
        emit progressChanged();
        m_statusText = QStringLiteral("回测完成");
        emit statusChanged();

        QVariantMap result;
        result["status"] = QStringLiteral("SUCCESS");
        result["metrics"] = QVariantMap();
        m_backtestResult = result;
        emit backtestResultChanged();
        emit backtestCompleted(result);

        m_isRunning.store(false);
        emit isRunningChanged();
    }, Qt::QueuedConnection);
}

void FactorBacktestBridge::startCompositeBacktest(
    const QVariantMap& /*compositeDraft*/,
    const QString& /*groupText*/,
    const QString& /*startDate*/,
    const QString& /*endDate*/,
    const QVariantMap& /*cacheSnapshot*/)
{
    qDebug() << "[FactorBacktestBridge] 组合因子回测暂未实现";
}

void FactorBacktestBridge::cancelBacktest()
{
    m_isRunning.store(false);
    emit isRunningChanged();
    m_statusText = QStringLiteral("回测已取消");
    emit statusChanged();
    emit backtestCancelled();
}

QVariantMap FactorBacktestBridge::getDefaultConfig() const
{
    QVariantMap config;
    config["startDate"] = QString();
    config["endDate"] = QString();
    config["numGroups"] = 10;
    config["groupingMethod"] = QStringLiteral("quantile");
    config["strategy"] = QStringLiteral("equal_weight");
    config["initialCapital"] = 1000000;
    config["transactionCost"] = 0.001;
    config["slippage"] = 0.001;
    config["maxThreads"] = 4;
    config["enableCache"] = true;
    config["cacheTTL"] = 3600;
    return config;
}

QVariantList FactorBacktestBridge::getAvailableDataSets() const
{
    return {};
}

QVariantList FactorBacktestBridge::buildBacktestDatasetOptions(const QVariantList& list) const
{
    QVariantList opts;
    for (const QVariant& v : list) {
        QVariantMap ds = v.toMap();
        QVariantMap opt;
        opt["value"] = ds.value("id");
        opt["text"] = ds.value("displayName").toString().isEmpty()
            ? QStringLiteral("数据集 #%1").arg(ds.value("id").toInt())
            : ds.value("displayName").toString();
        opt["raw"] = ds;
        opts.append(opt);
    }
    return opts;
}

bool FactorBacktestBridge::datasetSelectableForBacktest(const QVariantMap& ds) const
{
    return ds.value("isBacktestReady").toBool();
}

void FactorBacktestBridge::runFactorBacktestAsync(
    const QString& /*factorId*/, const QVariantMap& /*config*/)
{
    // 委托给 startBacktestWithFactors
    startBacktestWithFactors({}, {}, {}, {}, {});
}

// ═══════════════════════════════ 因子支持校验 ═══════════════════════════════

QVariantMap FactorBacktestBridge::buildFactorSupportMap(
    const QVariantList& factorIds,
    const QString& /*startDate*/, const QString& /*endDate*/,
    const QVariantMap& /*cacheSnapshot*/)
{
    QVariantMap map;
    for (const QVariant& id : factorIds) {
        QVariantMap info;
        info["supported"] = true;
        info["reason"] = QStringLiteral("当前数据源支持");
        info["category"] = QStringLiteral("supported");
        map[id.toString()] = info;
    }
    m_factorSupportMapCache = map;
    return map;
}

int FactorBacktestBridge::beginFactorSupportMapRefresh(
    const QVariantList& f, const QString& s, const QString& e, const QVariantMap& c)
{
    m_supportMapRequestInFlight.store(true);
    emit supportMapRequestInFlightChanged();
    QVariantMap map = buildFactorSupportMap(f, s, e, c);
    m_supportMapRequestInFlight.store(false);
    emit supportMapRequestInFlightChanged();
    emit factorSupportMapReady(1, map);
    return 1;
}

bool FactorBacktestBridge::handleFactorSupportMapReady(int id, const QVariantMap& map)
{
    if (id != 1) return false;
    m_factorSupportMapCache = map;
    emit factorSupportMapCacheChanged();
    return true;
}

QVariantList FactorBacktestBridge::normalizeFactorIds(const QVariantList& ids) const
{
    QVariantList out;
    QSet<QString> seen;
    for (const QVariant& v : ids) {
        QString s = v.toString().trimmed();
        if (!s.isEmpty() && !seen.contains(s)) {
            seen.insert(s);
            out.append(s);
        }
    }
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b)
              { return a.toString() < b.toString(); });
    return out;
}

QVariantList FactorBacktestBridge::displayedBacktestResults(const QVariantMap& r) const
{
    QVariantList list;
    list.append(r);
    return list;
}

QString FactorBacktestBridge::displayedBacktestResultName(const QVariantMap& e) const
{
    return e.value("factorId").toString();
}

QVariantMap FactorBacktestBridge::buildSingleFactorRunEntry(
    const QVariantMap& result, const QString& factorName) const
{
    QVariantMap m = result;
    m["factorId"] = factorName;
    return m;
}

QVariantList FactorBacktestBridge::pushSingleFactorRunHistory(
    const QVariantList& history, const QVariantMap& entry, int limit, const QString& factorName) const
{
    Q_UNUSED(factorName);
    QVariantList u = history;
    u.prepend(entry);
    if (u.size() > limit) u = u.mid(0, limit);
    return u;
}

QVariantMap FactorBacktestBridge::factorValidationState(
    const QString& factorId, const QString& factorName, bool hasDefinition,
    const QVariantMap& supportInfo, const QVariantList& preflightFailures,
    const QVariantMap& result, const QString& error, const QVariantList& selectedIds,
    const QString& sourceMode, bool hasCache, int datasetId) const
{
    Q_UNUSED(factorName); Q_UNUSED(hasDefinition); Q_UNUSED(preflightFailures);
    Q_UNUSED(result); Q_UNUSED(error); Q_UNUSED(selectedIds);
    Q_UNUSED(sourceMode); Q_UNUSED(hasCache); Q_UNUSED(datasetId);

    QVariantMap s;
    s["factorId"] = factorId;
    s["supported"] = supportInfo.value("supported").toBool();
    s["reason"] = supportInfo.value("reason").toString();
    s["statusText"] = QStringLiteral("可回测");
    s["accentColor"] = QStringLiteral("#3B82F6");
    return s;
}

QVariantMap FactorBacktestBridge::resolveRiskConfigurationSnapshot(
    const QVariantMap& backtestResult, const QVariantMap& appliedConfig,
    const QVariantMap& snapshot) const
{
    Q_UNUSED(backtestResult); Q_UNUSED(appliedConfig);
    return snapshot.isEmpty() ? QVariantMap() : snapshot;
}

QVariantList FactorBacktestBridge::riskConfigMetricCards(const QVariantMap& riskSnapshot) const
{
    Q_UNUSED(riskSnapshot);
    return {};
}

// ═══════════════════════════════ 属性访问器 ═══════════════════════════════

QVariantMap FactorBacktestBridge::backtestRuntimeParams() const
{ return m_backtestRuntimeParams; }

void FactorBacktestBridge::setBacktestRuntimeParams(const QVariantMap& p)
{ m_backtestRuntimeParams = p; emit backtestRuntimeParamsChanged(); }

QVariantMap FactorBacktestBridge::backtestResult() const
{ return m_backtestResult; }

bool FactorBacktestBridge::isRunning() const
{ return m_isRunning.load(); }

double FactorBacktestBridge::progress() const
{ return m_progress; }

QString FactorBacktestBridge::status() const
{ return m_statusText; }

QVariantMap FactorBacktestBridge::factorSupportMapCache() const
{ return m_factorSupportMapCache; }

int FactorBacktestBridge::selectedDatasetId() const
{ return m_selectedDatasetId; }

void FactorBacktestBridge::setSelectedDatasetId(int id)
{ if (m_selectedDatasetId != id) { m_selectedDatasetId = id; emit selectedDatasetIdChanged(); } }

QString FactorBacktestBridge::dataSourceMode() const
{ return m_dataSourceMode; }

void FactorBacktestBridge::setDataSourceMode(const QString& m)
{ if (m_dataSourceMode != m) { m_dataSourceMode = m; emit dataSourceModeChanged(); } }

QVariantMap FactorBacktestBridge::selectedDatasetBenchmarkMetadata() const
{ return m_selectedDatasetBenchmarkMetadata; }

void FactorBacktestBridge::setSelectedDatasetBenchmarkMetadata(const QVariantMap& md)
{ m_selectedDatasetBenchmarkMetadata = md; emit selectedDatasetBenchmarkMetadataChanged(); }

QVariantList FactorBacktestBridge::lastPreflightFailures() const
{ return m_lastPreflightFailures; }

bool FactorBacktestBridge::supportMapRequestInFlight() const
{ return m_supportMapRequestInFlight.load(); }

QVariantList FactorBacktestBridge::selectedStockPoolSymbols() const
{ return m_selectedStockPoolSymbols; }

void FactorBacktestBridge::setSelectedStockPoolSymbols(const QVariantList& symbols)
{ m_selectedStockPoolSymbols = symbols; emit selectedStockPoolSymbolsChanged(); }

QVariantMap FactorBacktestBridge::resultMetrics() const
{ return m_resultMetrics; }
