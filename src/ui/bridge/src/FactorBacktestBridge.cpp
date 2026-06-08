// FactorBacktestBridge.cpp — 桥接层，纯参数转换、线程调度、进度信号。
// 因子计算、模拟成交、分析统计等业务逻辑全部在域层执行。

#include "FactorBacktestBridge.h"
#include "DataServiceCache.h"

#include "factor_compute/AnalysisReportTypes.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QDir>
#include <cstdio>
#include <cmath>

#include "foundation/thread/ThreadPoolExecutor.h"
#include "FactorBacktestOrchestrator.h"
#include "BacktestScheduler.h"
#include "BacktestRunConfig.h"
#include "FactorService.h"

// ═══ 静态 QVariant 格式化方法 — 纯数据转换，无业务逻辑 ═══

QVariantList FactorBacktestBridge::buildCoreMetrics(
    const factor::compute::FactorQualityMetrics16View& metrics,
    const factor::compute::FactorQualityMetrics16DiagnosticsView& diag)
{
    QVariantList items;

    auto addMetric = [&](const QString& key, const QString& title,
                         const QString& formatStr, const QString& direction,
                         double goodThreshold,
                         const factor::compute::AnalysisScalarMetric& m,
                         factor::compute::AnalysisMetricAvailabilityReason /*reason*/)
    {
        QVariantMap item;
        item["key"] = key;
        item["title"] = title;
        item["format"] = formatStr;
        item["direction"] = direction;
        item["goodThreshold"] = goodThreshold;
        item["units"] = 1;
        item["tier"] = "core";
        if (m.available && std::isfinite(m.value)) {
            item["value"] = m.value;
            item["subtitle"] = QStringLiteral("可用");
        } else {
            item["value"] = 0.0;
            item["subtitle"] = QStringLiteral("不可用");
        }
        items.append(item);
    };

    auto addIntMetric = [&](const QString& key, const QString& title,
                            const factor::compute::AnalysisScalarMetricInt& m)
    {
        QVariantMap item;
        item["key"] = key;
        item["title"] = title;
        item["format"] = "integer";
        item["direction"] = "high";
        item["goodThreshold"] = 5;
        item["units"] = 1;
        item["tier"] = "core";
        if (m.available && m.value > 0) {
            item["value"] = (double)m.value;
            item["subtitle"] = QStringLiteral("可用");
        } else {
            item["value"] = 0;
            item["subtitle"] = QStringLiteral("不可用");
        }
        items.append(item);
    };

    addMetric("rankIcMean", "Rank IC 均值", "number3", "high", 0.05,
              metrics.rankIcMean, diag.rankIcMeanReason);
    addMetric("rankIcStd", "Rank IC 标准差", "number3", "low", 0.1,
              metrics.rankIcStd, diag.rankIcStdReason);
    addMetric("rankIcir", "Rank IR", "number2", "high", 0.5,
              metrics.rankIcir, diag.rankIcirReason);
    addMetric("icWinRate", "IC 胜率", "percent1", "high", 0.55,
              metrics.icWinRate, diag.icWinRateReason);
    addMetric("monotonicityScore", "单调性", "number3", "high", 0.85,
              metrics.monotonicityScore, diag.monotonicityScoreReason);
    addMetric("longShortSharpe", "多空夏普", "number2", "high", 1.0,
              metrics.longShortSharpe, diag.longShortSharpeReason);
    addMetric("longShortAnnualReturn", "多空年化收益", "percent2", "high", 0.1,
              metrics.longShortAnnualReturn, diag.longShortAnnualReturnReason);
    addMetric("costAdjustedSharpe", "成本后夏普", "number2", "high", 0.5,
              metrics.costAdjustedSharpe, diag.costAdjustedSharpeReason);
    addMetric("alpha", "Alpha", "percent2", "high", 0.0,
              metrics.alpha, diag.alphaReason);
    addMetric("annualTurnover", "年化换手率", "number2", "low", 1.0,
              metrics.annualTurnover, diag.annualTurnoverReason);
    addMetric("monthlyWinRate", "月胜率", "percent1", "high", 0.5,
              metrics.monthlyWinRate, diag.monthlyWinRateReason);
    addIntMetric("icHalfLife", "IC 半衰期(天)", metrics.icHalfLife);
    addIntMetric("numGroups", "分组数", metrics.numGroups);

    return items;
}

QString FactorBacktestBridge::coreRatingLabel(int32_t rating)
{
    switch (rating) {
    case 3: return QStringLiteral("优秀 ★★★");
    case 2: return QStringLiteral("良好 ★★☆");
    case 1: return QStringLiteral("及格 ★☆☆");
    default: return QStringLiteral("不及格");
    }
}

QString FactorBacktestBridge::coreRatingTitle(int32_t rating)
{
    switch (rating) {
    case 3: return QStringLiteral("因子质量优秀");
    case 2: return QStringLiteral("因子质量良好");
    case 1: return QStringLiteral("因子质量及格");
    default: return QStringLiteral("因子质量不及格");
    }
}

QString FactorBacktestBridge::coreRatingSummary(const factor::compute::FactorQualityMetrics16View& metrics)
{
    if (!metrics.rankIcir.available) {
        return QStringLiteral("核心指标不足，无法完整评估因子质量。");
    }

    QStringList parts;
    if (metrics.rankIcir.value >= 0.8)
        parts << QStringLiteral("IR 优秀");
    else if (metrics.rankIcir.value >= 0.5)
        parts << QStringLiteral("IR 良好");
    else if (metrics.rankIcir.value >= 0.3)
        parts << QStringLiteral("IR 一般");
    else
        parts << QStringLiteral("IR 偏低");

    if (metrics.monotonicityScore.available) {
        double mono = std::abs(metrics.monotonicityScore.value);
        if (mono >= 0.9)
            parts << QStringLiteral("单调性极佳");
        else if (mono >= 0.7)
            parts << QStringLiteral("单调性良好");
        else
            parts << QStringLiteral("单调性一般");
    }

    if (metrics.costAdjustedSharpe.available && metrics.costAdjustedSharpe.value > 0.5)
        parts << QStringLiteral("成本后夏普可观");

    return parts.join("，");
}

QVariantList FactorBacktestBridge::buildRatingChecks(const factor::compute::FactorQualityMetrics16View& metrics)
{
    QVariantList checks;

    auto addCheck = [&](const QString& label, bool passed,
                        const QString& actualText, const QString& thresholdText)
    {
        QVariantMap check;
        check["label"] = label;
        check["passed"] = passed;
        check["actualText"] = actualText;
        check["thresholdText"] = thresholdText;
        checks.append(check);
    };

    if (metrics.rankIcir.available) {
        double ir = metrics.rankIcir.value;
        addCheck(QStringLiteral("Rank IR"),
                 ir >= 0.3,
                 QStringLiteral("实际: %1").arg(ir, 0, 'f', 2),
                 QStringLiteral("阈值: >= 0.30"));
    } else {
        addCheck(QStringLiteral("Rank IR"), false,
                 QStringLiteral("实际: N/A"), QStringLiteral("阈值: >= 0.30"));
    }

    if (metrics.icWinRate.available) {
        double winRate = metrics.icWinRate.value;
        addCheck(QStringLiteral("IC 胜率"),
                 winRate >= 0.55,
                 QStringLiteral("实际: %1%").arg(winRate * 100.0, 0, 'f', 1),
                 QStringLiteral("阈值: >= 55%"));
    } else {
        addCheck(QStringLiteral("IC 胜率"), false,
                 QStringLiteral("实际: N/A"), QStringLiteral("阈值: >= 55%"));
    }

    if (metrics.costAdjustedSharpe.available) {
        double sharpe = metrics.costAdjustedSharpe.value;
        addCheck(QStringLiteral("成本后夏普"),
                 sharpe >= 0.0,
                 QStringLiteral("实际: %1").arg(sharpe, 0, 'f', 2),
                 QStringLiteral("阈值: >= 0.00"));
    } else {
        addCheck(QStringLiteral("成本后夏普"), false,
                 QStringLiteral("实际: N/A"), QStringLiteral("阈值: >= 0.00"));
    }

    if (metrics.annualTurnover.available) {
        double turnover = metrics.annualTurnover.value;
        addCheck(QStringLiteral("年化换手"),
                 turnover <= 3.0,
                 QStringLiteral("实际: %1").arg(turnover, 0, 'f', 2),
                 QStringLiteral("阈值: <= 3.0"));
    } else {
        addCheck(QStringLiteral("年化换手"), false,
                 QStringLiteral("实际: N/A"), QStringLiteral("阈值: <= 3.0"));
    }

    if (metrics.monotonicityScore.available) {
        double mono = std::abs(metrics.monotonicityScore.value);
        addCheck(QStringLiteral("单调性"),
                 mono >= 0.5,
                 QStringLiteral("实际: %1").arg(metrics.monotonicityScore.value, 0, 'f', 3),
                 QStringLiteral("阈值: |值| >= 0.50"));
    } else {
        addCheck(QStringLiteral("单调性"), false,
                 QStringLiteral("实际: N/A"), QStringLiteral("阈值: |值| >= 0.50"));
    }

    return checks;
}

// ═══ Bridge 核心方法 ═══

FactorBacktestBridge::FactorBacktestBridge(QObject* parent)
    : QObject(parent), m_timeoutTimer(new QTimer(this))
{ qDebug() << "[FactBacktestBridge] 实例已创建"; }

FactorBacktestBridge::~FactorBacktestBridge() = default;

bool FactorBacktestBridge::initialize()
{
    // 1) 创建四个下层域组件
    m_scheduler       = std::make_unique<domain::scheduler::BacktestScheduler>(0ULL);
    m_backtestDataSvc = std::make_unique<factor::compute::BacktestDataService>();
    m_factorEngine    = std::make_unique<factor::compute::BacktestFactorEngine>(0ULL);
    m_reporter        = std::make_unique<factor::compute::BacktestReporter>();

    if (!m_scheduler || !m_backtestDataSvc || !m_factorEngine || !m_reporter) {
        qDebug() << "[FactBacktestBridge] 域组件创建失败";
        m_scheduler.reset();
        m_backtestDataSvc.reset();
        m_factorEngine.reset();
        m_reporter.reset();
        return false;
    }

    // 2) 创建 Orchestrator 并注入依赖
    m_orchestrator = std::make_unique<application::backtest::FactorBacktestOrchestrator>();
    if (!m_orchestrator) {
        qDebug() << "[FactBacktestBridge] 因子回测编排器初始化失败";
        return false;
    }

    m_orchestrator->setScheduler(m_scheduler.get());
    m_orchestrator->setDataService(m_backtestDataSvc.get());
    m_orchestrator->setFactorEngine(m_factorEngine.get());
    m_orchestrator->setReporter(m_reporter.get());

    // 引擎需要 DataSvc 引用以便按需构建 MarketView
    m_factorEngine->setDataService(m_backtestDataSvc.get());

    // 3) 复用 FactorService 已有的 FactorInstanceManager（避免重复查询）
    auto* factorSvc = FactorService::instance();
    if (factorSvc && factorSvc->isInitialized()) {
        auto* instanceMgr = factorSvc->instanceManager();
        if (instanceMgr) {
            m_factorEngine->setInstanceManager(instanceMgr);
            qDebug() << "[FactBacktestBridge] 复用 FactorService 的 FactorInstanceManager";
        }
    }

    m_engineReady = true;
    m_statusText = QStringLiteral("因子回测引擎就绪");
    emit statusChanged();
    return true;
}


void FactorBacktestBridge::startBacktestWithFactors(
    const QVariantList& factorIds,
    const QString& /*groupText*/,
    const QString& /*startDate*/,
    const QString& /*endDate*/,
    const QVariantMap& /*cacheSnapshot*/)
{
    if (m_isRunning.load()) return;

    // 懒初始化: 确保 Orchestrator 等组件已创建
    if (!m_orchestrator) {
        if (!initialize()) {
            emit backtestFailed(QStringLiteral("因子回测引擎初始化失败"));
            return;
        }
    }

    m_isRunning.store(true);
    emit isRunningChanged();
    m_progress = 0.0; emit progressChanged();
    m_statusText = QStringLiteral("启动中..."); emit statusChanged();
    emit backtestStarted(factorIds.isEmpty() ? QStringLiteral("default") : factorIds.first().toString());
    emit backtestProgress(0.0, m_statusText);

    if (!m_workerPool) {
        m_workerPool = std::make_unique<foundation::thread::ThreadPoolExecutor>(
            1, 1, std::chrono::milliseconds(60000), "FactorBacktestWorker");
    }

    // ── 新架构: Bridge 只做三件事 — 启动 / 进度 / 接收结果 ──
    // QVariantMap → BacktestRunConfig 转换, 然后委托给 Orchestrator

    application::backtest::BacktestRunConfig config;
    config.dataSourceMode = application::backtest::DataSourceMode::Cache;
    config.selectedDatasetId = m_selectedDatasetId;

    // 根据 factorIds 数量自动推断因子模式
    if (factorIds.size() == 1) {
        config.factorMode = application::backtest::FactorMode::Single;
    } else if (factorIds.size() == 2) {
        config.factorMode = application::backtest::FactorMode::Dual;
    } else {
        config.factorMode = application::backtest::FactorMode::Composite;
    }

    config.numGroups = m_numGroups;
    config.forwardDays = m_backtestRuntimeParams.value("forwardDays", 30).toInt();
    config.rebalanceDays = m_backtestRuntimeParams.value("rebalanceDays", 15).toInt();
    config.commissionRate = m_backtestRuntimeParams.value("commissionRate", 0.001).toDouble();
    config.slippageRate = m_backtestRuntimeParams.value("slippageRate", 0.001).toDouble();
    config.riskFreeRate = m_backtestRuntimeParams.value("riskFreeRate", 0.02).toDouble();
    for (const QVariant& id : factorIds) {
        config.factorIds.push_back(id.toString().toStdString());
    }

    // 异步: 全部重操作移到 worker 线程，不阻塞 UI
    int capturedDatasetId = m_selectedDatasetId;
    m_workerPool->post([this, config, capturedDatasetId]() {
        // ① 在 worker 线程中加载缓存数据集 + 构建 MarketView
        if (capturedDatasetId > 0 && m_backtestDataSvc) {
            QVariantList data = DataServiceCache::getInstance().getDataSetById(capturedDatasetId);
            if (!data.isEmpty()) {
            QJsonDocument doc(QJsonArray::fromVariantList(data));
            std::string jsonStr = doc.toJson(QJsonDocument::Compact).toStdString();
            m_backtestDataSvc->storeRawJson(jsonStr);
            qDebug() << "[FactBacktestBridge] DataSvc 存储原始 JSON, ID=" << capturedDatasetId
                     << "(" << data.size() << " 行, 延迟构建视图)";
            } else {
                QMetaObject::invokeMethod(this, [this]() {
                    emit backtestFailed(QStringLiteral("缓存系统返回空数据集"));
                    m_isRunning.store(false); emit isRunningChanged();
                }, Qt::QueuedConnection);
                return;
            }
        }
        // ② 校验 + 运行
        if (!m_orchestrator) {
            QMetaObject::invokeMethod(this, [this]() {
                emit backtestFailed(QStringLiteral("回测编排器未初始化"));
                m_isRunning.store(false); emit isRunningChanged();
            }, Qt::QueuedConnection);
            return;
        }

        m_orchestrator->run(
            config,
            // 进度回调
            [this](double progress, const std::string& status) {
                QMetaObject::invokeMethod(this, [this, progress, status]() {
                    m_progress = progress;
                    m_statusText = QString::fromStdString(status);
                    emit progressChanged(); emit statusChanged();
                    emit backtestProgress(progress, m_statusText);
                }, Qt::QueuedConnection);
            },
            // 结果回调
            [this](const std::string& serializedResult) {
                QMetaObject::invokeMethod(this, [this, serializedResult]() {
                    QVariantMap result;
                    result["status"] = QStringLiteral("SUCCESS");
                    QString jsonStr = QString::fromStdString(serializedResult);

                    // 解析 JSON 并正确填充 metrics（QML 通过 result.metrics.execution 取值）
                    QJsonParseError parseError;
                    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &parseError);
                    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                        QJsonObject rootObj = doc.object();
                        if (rootObj.contains("metrics") && rootObj["metrics"].isObject()) {
                            QVariantMap metrics = rootObj["metrics"].toVariant().toMap();
                            m_resultMetrics = metrics;
                            result["metrics"] = metrics;
                            emit resultMetricsChanged();
                        }
                    }
                    // results 保持空数组（QML 展开分析结果列表用）
                    result["results"] = QVariantList();

                    m_backtestResult = result;
                    emit backtestResultChanged();
                    emit backtestCompleted(result);
                    m_progress = 100.0;
                    m_statusText = QStringLiteral("回测完成");
                    emit progressChanged(); emit statusChanged();
                    emit backtestProgress(100.0, m_statusText);
                    m_isRunning.store(false); emit isRunningChanged();
                }, Qt::QueuedConnection);
            }
        );
    });
}

// ── 其余辅助方法 ──

void FactorBacktestBridge::startCompositeBacktest(const QVariantMap&, const QString&, const QString&, const QString&, const QVariantMap&)
{ qDebug() << "[FactBacktestBridge] 组合回测暂未实现"; }

void FactorBacktestBridge::cancelBacktest()
{ m_isRunning.store(false); emit isRunningChanged(); m_statusText = QStringLiteral("已取消"); emit statusChanged(); emit backtestCancelled(); }

QVariantMap FactorBacktestBridge::getDefaultConfig() const { QVariantMap c; c["numGroups"] = 10; return c; }
QVariantList FactorBacktestBridge::getAvailableDataSets() const { return {}; }

QVariantList FactorBacktestBridge::buildBacktestDatasetOptions(const QVariantList& list) const
{ QVariantList opts; for (const QVariant& v : list) {
      QVariantMap ds = v.toMap(); QVariantMap o;
      o["value"] = ds.value("id");
      o["text"]  = ds.value("displayName").toString().isEmpty() ? QStringLiteral("数据集 #%1").arg(ds.value("id").toInt())
                                                                 : ds.value("displayName").toString();
      o["raw"] = ds; opts.append(o);
  } return opts; }
bool FactorBacktestBridge::datasetSelectableForBacktest(const QVariantMap& ds) const { return ds.value("isBacktestReady").toBool(); }

void FactorBacktestBridge::runFactorBacktestAsync(const QString& factorId, const QVariantMap& config)
{
    QVariantList factorIds;
    if (!factorId.isEmpty()) factorIds.append(factorId);
    if (!config.isEmpty()) {
        m_factorTypeInt = config.value("factorType", 0).toInt();
        m_numGroups = config.value("numGroups", 10).toInt();
        m_backtestRuntimeParams = config;
    }
    QString startDate = config.value("startDate").toString();
    QString endDate = config.value("endDate").toString();
    startBacktestWithFactors(factorIds, QString(), startDate, endDate, config);
}

QVariantMap FactorBacktestBridge::buildFactorSupportMap(const QVariantList& factorIds, const QString&, const QString&, const QVariantMap&)
{ QVariantMap map; for (const QVariant& id : factorIds) {
      QVariantMap i; i["supported"] = true; i["reason"] = QStringLiteral("当前数据源支持");
      i["category"] = QStringLiteral("supported"); map[id.toString()] = i;
  } m_factorSupportMapCache = map; return map; }

int FactorBacktestBridge::beginFactorSupportMapRefresh(const QVariantList& f, const QString& s, const QString& e, const QVariantMap& c)
{ m_supportMapRequestInFlight.store(true); emit supportMapRequestInFlightChanged();
  QVariantMap map = buildFactorSupportMap(f, s, e, c);
  m_supportMapRequestInFlight.store(false); emit supportMapRequestInFlightChanged();
  emit factorSupportMapReady(1, map); return 1; }

bool FactorBacktestBridge::handleFactorSupportMapReady(int id, const QVariantMap& map)
{ if (id != 1) return false; m_factorSupportMapCache = map; emit factorSupportMapCacheChanged(); return true; }

QVariantList FactorBacktestBridge::normalizeFactorIds(const QVariantList& ids) const
{ QVariantList out; QSet<QString> seen;
  for (const QVariant& v : ids) { QString s = v.toString().trimmed();
      if (!s.isEmpty() && !seen.contains(s)) { seen.insert(s); out.append(s); } }
  std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) { return a.toString() < b.toString(); });
  return out; }

QVariantList FactorBacktestBridge::displayedBacktestResults(const QVariantMap& r) const { QVariantList l; l.append(r); return l; }
QString FactorBacktestBridge::displayedBacktestResultName(const QVariantMap& e) const { return e.value("factorId").toString(); }
QVariantMap FactorBacktestBridge::buildSingleFactorRunEntry(const QVariantMap& r, const QString& n) const { QVariantMap m = r; m["factorId"] = n; return m; }
QVariantList FactorBacktestBridge::pushSingleFactorRunHistory(const QVariantList& h, const QVariantMap& e, int limit, const QString&) const
{ QVariantList u = h; u.prepend(e); if (u.size() > limit) u = u.mid(0, limit); return u; }

QVariantMap FactorBacktestBridge::factorValidationState(
    const QString& factorId, const QString&, bool, const QVariantMap& si,
    const QVariantList&, const QVariantMap&, const QString&, const QVariantList&,
    const QString&, bool, int) const
{ QVariantMap s; s["factorId"] = factorId; s["supported"] = si.value("supported").toBool();
  s["reason"] = si.value("reason").toString(); s["statusText"] = QStringLiteral("可回测");
  s["accentColor"] = QStringLiteral("#3B82F6"); return s; }
QVariantMap FactorBacktestBridge::resolveRiskConfigurationSnapshot(const QVariantMap&, const QVariantMap&, const QVariantMap& sn) const
{ return sn.isEmpty() ? QVariantMap() : sn; }
QVariantList FactorBacktestBridge::riskConfigMetricCards(const QVariantMap&) const { return {}; }

// ── 属性 getter/setter ──

QVariantMap FactorBacktestBridge::backtestRuntimeParams() const { return m_backtestRuntimeParams; }
void FactorBacktestBridge::setBacktestRuntimeParams(const QVariantMap& p) { m_backtestRuntimeParams = p; emit backtestRuntimeParamsChanged(); }
QVariantMap FactorBacktestBridge::backtestResult() const { return m_backtestResult; }
bool   FactorBacktestBridge::isRunning() const { return m_isRunning.load(); }
double FactorBacktestBridge::progress() const { return m_progress; }
QString FactorBacktestBridge::status() const { return m_statusText; }
QVariantMap FactorBacktestBridge::factorSupportMapCache() const { return m_factorSupportMapCache; }
int    FactorBacktestBridge::selectedDatasetId() const { return m_selectedDatasetId; }
void   FactorBacktestBridge::setSelectedDatasetId(int id) { if (m_selectedDatasetId != id) { m_selectedDatasetId = id; m_engineReady = false; emit selectedDatasetIdChanged(); } }
QString FactorBacktestBridge::dataSourceMode() const { return m_dataSourceMode; }
void   FactorBacktestBridge::setDataSourceMode(const QString& m) { if (m_dataSourceMode != m) { m_dataSourceMode = m; emit dataSourceModeChanged(); } }
QVariantMap FactorBacktestBridge::selectedDatasetBenchmarkMetadata() const { return m_selectedDatasetBenchmarkMetadata; }
void   FactorBacktestBridge::setSelectedDatasetBenchmarkMetadata(const QVariantMap& md) { m_selectedDatasetBenchmarkMetadata = md; emit selectedDatasetBenchmarkMetadataChanged(); }
QVariantList FactorBacktestBridge::lastPreflightFailures() const { return m_lastPreflightFailures; }
bool   FactorBacktestBridge::supportMapRequestInFlight() const { return m_supportMapRequestInFlight.load(); }
QVariantList FactorBacktestBridge::selectedStockPoolSymbols() const { return m_selectedStockPoolSymbols; }
void   FactorBacktestBridge::setSelectedStockPoolSymbols(const QVariantList& s) { m_selectedStockPoolSymbols = s; emit selectedStockPoolSymbolsChanged(); }
QVariantMap FactorBacktestBridge::resultMetrics() const { return m_resultMetrics; }