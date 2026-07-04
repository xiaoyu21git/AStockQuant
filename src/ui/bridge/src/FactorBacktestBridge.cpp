// FactorBacktestBridge.cpp — 桥接层，纯参数转换、线程调度、进度信号。
// 因子计算、模拟成交、分析统计等业务逻辑全部在域层执行。

#include "FactorBacktestBridge.h"
#include "DataCacheAdapter.h"
#include "AppStoragePaths.h"

#include "factor_compute/AnalysisReportTypes.h"
#include "../../domain/factor/include/factor_compute/FactorEngine.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QDir>
#include <cstdio>
#include <cmath>

#include "foundation/thread/ThreadPoolExecutor.h"
#include "../../domain/factor/include/FactorBacktestOrchestrator.h"
#include "../../../infrastructure/include/database/NativeMySQLConnectionPool.h"
#include "../../../infrastructure/include/database/ISqlDatabase.h"
#include "foundation/Utils/Uuid.h"
#include "../../domain/factor/include/BacktestRunConfig.h"
#include "BacktestScheduler.h"
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
{ INTERNAL_DEBUG_STREAM << "[FactBacktestBridge] 实例已创建"; }

FactorBacktestBridge::~FactorBacktestBridge() = default;

bool FactorBacktestBridge::initialize()
{
    // 1) 创建四个下层域组件
    m_scheduler       = std::make_unique<domain::scheduler::BacktestScheduler>(0ULL);
    m_backtestDataSvc = std::make_unique<factor::compute::BacktestDataService>();
m_factorEngine    = std::make_unique<factor::compute::FactorEngine>(0ULL);
    m_reporter        = std::make_unique<factor::compute::BacktestReporter>();

    if (!m_scheduler || !m_backtestDataSvc || !m_factorEngine || !m_reporter) {
        INTERNAL_DEBUG_STREAM << "[FactBacktestBridge] 域组件创建失败";
        m_scheduler.reset();
        m_backtestDataSvc.reset();
        m_factorEngine.reset();
        m_reporter.reset();
        return false;
    }

    // 2) 创建 Orchestrator 并注入依赖
    m_orchestrator = std::make_unique<Factor::backtest::FactorBacktestOrchestrator>();
    if (!m_orchestrator) {
        INTERNAL_DEBUG_STREAM << "[FactBacktestBridge] 因子回测编排器初始化失败";
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
            INTERNAL_DEBUG_STREAM << "[FactBacktestBridge] 复用 FactorService 的 FactorInstanceManager";
        }
    }

    m_engineReady = true;
    m_statusText = QStringLiteral("因子回测引擎就绪");
    emit statusChanged();
    return true;
}


void FactorBacktestBridge::startBacktestWithFactors(
    const QVariantList& factorIds,
    const QString& groupText,
    const QString& startDate,
    const QString& endDate,
    const QVariantMap& /*cacheSnapshot*/)
{
    if (m_isRunning.load()) return;

    // 解析 QML 分组选择 ("5组"/"10组"/"20组") → 整数
    bool ok = false;
    int parsedGroups = groupText.toInt(&ok);
    if (ok && parsedGroups > 0) m_numGroups = parsedGroups;

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

    Factor::backtest::BacktestRunConfig config;
    config.dataSourceMode = Factor::backtest::DataSourceMode::Cache;
    config.selectedDatasetId = m_selectedDatasetId;

    // 根据 factorIds 数量自动推断因子模式
    if (factorIds.size() == 1) {
        config.factorMode = Factor::backtest::FactorMode::Single;
    } else if (factorIds.size() == 2) {
        config.factorMode = Factor::backtest::FactorMode::Dual;
    } else {
        config.factorMode = Factor::backtest::FactorMode::Composite;
    }

    config.numGroups = m_numGroups;
    config.forwardDays = m_backtestRuntimeParams.value("forwardDays", 30).toInt();
    config.rebalanceDays = m_backtestRuntimeParams.value("rebalanceDays", 15).toInt();
    config.commissionRate = m_backtestRuntimeParams.value("commissionRate", 0.001).toDouble();
    config.slippageRate = m_backtestRuntimeParams.value("slippageRate", 0.001).toDouble();
    config.riskFreeRate = m_backtestRuntimeParams.value("riskFreeRate", 0.02).toDouble();
    config.initialCapital = m_backtestRuntimeParams.value("initialCapital", 1000000.0).toDouble();
    config.benchmarkSymbol = m_backtestRuntimeParams.value("benchmarkSymbol", "000300.SH").toString().toUpper().toStdString();
    config.adjustPriceType = m_backtestRuntimeParams.value("adjustPriceType", "pre").toString().toStdString();
    config.marketEnvironmentProfile = m_backtestRuntimeParams.value("marketEnvironmentProfile", 0).toInt();
    for (const QVariant& id : factorIds) {
        config.factorIds.push_back(id.toString().toStdString());
    }

    // QML 日期字符串 (YYYYMMDD 或 YYYY-MM-DD) → DomainDate
    auto parseQmlDate = [](const QString& s) -> domain::DomainDate {
        if (s.isEmpty()) return {};
        QString cleaned = s;
        cleaned.remove(QLatin1Char('-'));
        bool ok = false;
        int32_t v = cleaned.toInt(&ok);
        if (ok && v >= 19000101 && v <= 29991231)
            return domain::DomainDate{v};
        return {};
    };
    config.cacheStartDate = parseQmlDate(startDate);
    config.cacheEndDate   = parseQmlDate(endDate);

    // 异步: 全部重操作移到 worker 线程，不阻塞 UI
    int capturedDatasetId = m_selectedDatasetId;
    m_workerPool->post([this, config, capturedDatasetId]() {
        // 初始化阶段进度
        QMetaObject::invokeMethod(this, [this]() {
            m_progress = 2.0; m_statusText = QStringLiteral("初始化中...");
            emit progressChanged(); emit backtestProgress(m_progress, m_statusText);
        }, Qt::QueuedConnection);

        // ① 直接注入 Arrow 列式视图，零拷贝
        if (capturedDatasetId > 0 && m_backtestDataSvc) {
            std::string arrowPath = cleaning::DataCache::instance().dataFilePath(capturedDatasetId);
            auto arrowView = std::make_unique<factor::compute::ArrowMarketDataView>(arrowPath);
            m_arrowView = std::move(arrowView);
            m_backtestDataSvc->setMarketView(m_arrowView.get());
            m_backtestDataSvc->buildViewForFields({});
            INTERNAL_DEBUG_STREAM << "[FactBacktestBridge] Arrow view injected, ID=" << capturedDatasetId;
            if (!m_arrowView || m_arrowView->instruments().empty()) {
                QMetaObject::invokeMethod(this, [this]() {
                    emit backtestFailed(QStringLiteral("缓存系统返回空数据集"));
                    m_isRunning.store(false); emit isRunningChanged();
                }, Qt::QueuedConnection);
                return;
            }
        }
        // ② 前置校验 — 将 Orchestrator 运行时检查提升到此，失败直接 emit backtestFailed
        if (!m_orchestrator) {
            QMetaObject::invokeMethod(this, [this]() {
                emit backtestFailed(QStringLiteral("回测编排器未初始化"));
                m_isRunning.store(false); emit isRunningChanged();
            }, Qt::QueuedConnection);
            return;
        }
        if (!m_scheduler) {
            QMetaObject::invokeMethod(this, [this]() {
                emit backtestFailed(QStringLiteral("回测调度器未初始化"));
                m_isRunning.store(false); emit isRunningChanged();
            }, Qt::QueuedConnection);
            return;
        }
        if (!m_factorEngine || !m_factorEngine->hasInstanceManager()) {
            QMetaObject::invokeMethod(this, [this]() {
                emit backtestFailed(QStringLiteral("FactorService 未初始化 — 因子实例不可用"));
                m_isRunning.store(false); emit isRunningChanged();
            }, Qt::QueuedConnection);
            return;
        }
        if (!m_backtestDataSvc || !m_backtestDataSvc->getView()) {
            QMetaObject::invokeMethod(this, [this]() {
                emit backtestFailed(QStringLiteral("缓存数据集未加载，请先选择数据"));
                m_isRunning.store(false); emit isRunningChanged();
            }, Qt::QueuedConnection);
            return;
        }
        {
            auto* arrowView = static_cast<factor::compute::ArrowMarketDataView*>(m_backtestDataSvc->getView());
            if (arrowView->dates().empty() || arrowView->instruments().empty()) {
                QMetaObject::invokeMethod(this, [this]() {
                    emit backtestFailed(QStringLiteral("缓存集为空 — 无交易日或股票数据"));
                    m_isRunning.store(false); emit isRunningChanged();
                }, Qt::QueuedConnection);
                return;
            }
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
            [this, config](const std::string& serializedResult) {
                QMetaObject::invokeMethod(this, [this, serializedResult, config]() {
                    QString jsonStr = QString::fromStdString(serializedResult);
                    QJsonParseError parseError;
                    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &parseError);

                    // 检查 orchestrator 是否返回了错误
                    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                        QJsonObject rootObj = doc.object();
                        if (rootObj.contains("error") && !rootObj["error"].toString().isEmpty()) {
                            emit backtestFailed(rootObj["error"].toString());
                            m_isRunning.store(false); emit isRunningChanged();
                            return;
                        }
                    }

                    // 解析失败或缺少 metrics 时使用空对象
                    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                        emit backtestFailed(QStringLiteral("回测结果解析失败: ") + parseError.errorString());
                        m_isRunning.store(false); emit isRunningChanged();
                        return;
                    }

                    QJsonObject rootObj = doc.object();
                    QJsonObject metricsObj = rootObj.value("metrics").toObject();

                    // ── 手动构建 QVariantMap 确保深层嵌套正确 ──
                    QVariantMap result;
                    result["status"]  = QStringLiteral("SUCCESS");
                    result["results"] = QVariantList(); // 单结果模式

                    // groups
                    QJsonArray groupsArr = metricsObj.value("groups").toArray();
                    QVariantList groupsList;
                    for (int i = 0; i < groupsArr.size(); ++i) {
                        groupsList.append(groupsArr[i].toObject().toVariantMap());
                    }

                    // trading
                    QVariantMap tradingMap = metricsObj.value("trading").toObject().toVariantMap();

                    // ic
                    QVariantMap icMap = metricsObj.value("ic").toObject().toVariantMap();

                    // execution
                    QVariantMap execMap = metricsObj.value("execution").toObject().toVariantMap();

                    // factorMetrics — Calculator 输出的全部因子质量指标
                    QVariantMap fmMap = metricsObj.value("factorMetrics").toObject().toVariantMap();

                    // ── 构建 factorQuality（AnalysisPage 需要的富结构）──
                    QJsonObject fqRaw = metricsObj.value("factorQuality").toObject();
                    int rating = fqRaw.value("rating").toInt(1);
                    QString ratingLabel = fqRaw.value("label").toString(QStringLiteral("合格"));

                    // tier: "core"=大卡152px, "optional"=标准114px, "auxiliary"=紧凑108px
                    auto mk = [](const QString& key, const QString& title, const QString& subtitle,
                                 double val, const QString& format, bool emphasize,
                                 const QString& tier, int units = 1) {
                        QVariantMap m;
                        bool avail = std::isfinite(val);
                        m["key"] = key; m["title"] = title; m["subtitle"] = avail ? subtitle : QStringLiteral("不可用");
                        m["label"] = title; m["value"] = avail ? val : 0.0; m["format"] = format;
                        m["emphasize"] = emphasize; m["tier"] = tier; m["units"] = units;
                        m["available"] = avail;
                        return m;
                    };

                    // ══ 核心指标：判断因子是否合格（5 张，刚好一行）══
                    QVariantList coreMetrics;
                    coreMetrics.append(mk("rankIcMean", "IC 均值", "Rank IC 均值",
                        icMap.value("value").toDouble(), "number", true, "core"));
                    coreMetrics.append(mk("rankIcir",   "ICIR", "IC 信息比率",
                        icMap.value("ir").toDouble(), "number", true, "core"));
                    coreMetrics.append(mk("icWinRate",  "IC 胜率", "IC>0 的期数占比",
                        icMap.value("winRate").toDouble(), "percent", false, "core"));
                    coreMetrics.append(mk("monotonicity","单调性", "分组收益单调变化程度",
                        fmMap.value("monotonicityScore").toDouble(), "number", false, "core"));
                    coreMetrics.append(mk("longShortSharpe","多空夏普", "多空组合风险调整收益",
                        fmMap.value("longShortSharpe").toDouble(), "ratio", true, "core"));

                    // ══ 扩展指标：辅助判断因子质量 ══
                    QVariantList optionalMetrics;
                    optionalMetrics.append(mk("rankIcStd", "IC 标准差", "IC 波动幅度",
                        icMap.value("std").toDouble(), "number", false, "optional"));
                    optionalMetrics.append(mk("icPValue", "IC P 值", "IC 显著性检验 P 值，<0.05 显著",
                        icMap.value("pValue").toDouble(), "number", false, "optional"));
                    optionalMetrics.append(mk("icTStat", "IC T 统计", "IC 显著性 T 统计量",
                        icMap.value("tStat").toDouble(), "number", false, "optional"));
                    optionalMetrics.append(mk("icHalfLife","IC 半衰期", "IC 自相关衰减至一半的天数",
                        icMap.value("halfLife").toDouble(), "number", false, "optional"));
                    optionalMetrics.append(mk("longShortRet","多空年化", "多空组合年化收益",
                        fmMap.value("longShortAnnualReturn").toDouble(), "percent", false, "optional"));
                    optionalMetrics.append(mk("costAdjSharpe","成本夏普", "扣除交易成本后的多空夏普",
                        fmMap.value("costAdjustedSharpe").toDouble(), "ratio", false, "optional"));
                    optionalMetrics.append(mk("monthlyWinRate","月度胜率", "月度正收益占比",
                        fmMap.value("monthlyWinRate").toDouble(), "percent", false, "optional"));
                    optionalMetrics.append(mk("annualTurnover","年化换手", "因子持仓的年化换手率",
                        fmMap.value("annualTurnover").toDouble(), "percent", false, "optional"));
                    optionalMetrics.append(mk("alpha","Alpha", "因子超额收益",
                        fmMap.value("alpha").toDouble(), "number", false, "optional"));

                    // ══ 辅助指标：参考信息（可折叠）══
                    QVariantList auxiliaryMetrics;
                    auxiliaryMetrics.append(mk("numGroups","分组数", "回测分组数量",
                        fmMap.value("numGroups").toDouble(), "number", false, "auxiliary"));
                    auxiliaryMetrics.append(mk("totalSignals","总信号数", "回测期总信号量",
                        execMap.value("totalSignals").toDouble(), "number", false, "auxiliary"));
                    auxiliaryMetrics.append(mk("validSamples","有效样本", "有效回测周期数",
                        execMap.value("validSampleCount").toDouble(), "number", false, "auxiliary"));

                    // groupCharts — 从 groups 构建 QML 期望的 {title, subtitle, series, isPercent} 格式
                    QVariantList groupCharts;
                    if (groupsArr.size() > 0) {
                        QVariantMap chart;
                        chart["title"]     = QStringLiteral("分组收益");
                        chart["subtitle"]  = QStringLiteral("各组年化收益与平均股票数");
                        chart["isPercent"] = true;
                        QVariantList series;
                        for (int i = 0; i < groupsArr.size(); ++i) {
                            QJsonObject g = groupsArr[i].toObject();
                            QVariantMap bar;
                            bar["label"] = g.value("groupName").toString();
                            bar["value"] = g.value("annualizedReturn").toDouble();
                            series.append(bar);
                        }
                        chart["series"] = series;
                        groupCharts.append(chart);
                    }

                    // returnSeries — 从 orchestrator JSON 提取三条分离的收益率序列
                    QJsonObject retObj = metricsObj.value("returnSeries").toObject();
                    auto jsonArrayToVariantList = [](const QJsonArray& arr) {
                        QVariantList out;
                        for (int i = 0; i < arr.size(); ++i)
                            out.append(arr[i].toDouble());
                        return out;
                    };
                    QVariantList rawReturns     = jsonArrayToVariantList(retObj.value("raw").toArray());
                    QVariantList costAdjusted   = jsonArrayToVariantList(retObj.value("costAdjusted").toArray());
                    QVariantList riskAdjusted   = jsonArrayToVariantList(retObj.value("riskAdjusted").toArray());
                    QVariantMap returnSeries;
                    returnSeries["rawReturns"]            = rawReturns;
                    returnSeries["costAdjustedReturns"]   = costAdjusted;
                    returnSeries["riskAdjustedReturns"]   = riskAdjusted;

                    // 评级检查项
                    QVariantList ratingChecks;
                    auto addCheck = [&](const QString& label, bool passed) {
                        QVariantMap c;
                        c["label"]  = label;
                        c["passed"] = passed;
                        ratingChecks.append(c);
                    };
                    bool hasGroups = groupsArr.size() >= 2;
                    bool monotonic = true;
                    if (hasGroups) {
                        for (int i = 1; i < groupsArr.size(); ++i) {
                            if (groupsArr[i].toObject().value("returnRate").toDouble() >
                                groupsArr[i-1].toObject().value("returnRate").toDouble())
                                { monotonic = false; break; }
                        }
                    }
                    addCheck(QStringLiteral("分组单调性"), monotonic);
                    addCheck(QStringLiteral("夏普比率 > 0"), tradingMap.value("sharpe").toDouble() > 0.0);
                    addCheck(QStringLiteral("IC 均值 > 0"), icMap.value("value").toDouble() > 0.0);
                    addCheck(QStringLiteral("IC 胜率 > 50%"), icMap.value("winRate").toDouble() > 0.5);

                    // 组装完整 factorQuality
                    QVariantMap fq;
                    fq["coreRating"]        = rating;
                    fq["coreRatingLabel"]   = ratingLabel;
                    fq["coreRatingTitle"]   = QStringLiteral("因子质量评级");
                    fq["coreRatingSummary"] = rating >= 3 ? QStringLiteral("因子表现优秀，分组单调且风险调整收益良好")
                                             : rating >= 2 ? QStringLiteral("因子表现良好，具备选股能力")
                                             : rating >= 1 ? QStringLiteral("因子基本合格，可考虑与其他因子复合使用")
                                             : QStringLiteral("因子表现不佳，建议重新审视因子逻辑");
                    fq["coreRatingChecks"]  = ratingChecks;
                    fq["coreMetrics"]       = coreMetrics;
                    fq["groupCharts"]       = groupCharts;
                    fq["returnSeries"]      = returnSeries;

                    // groupReturnSeries — 每组每日收益时间序列
                    QJsonArray grsArr = metricsObj.value("groupReturnSeries").toArray();
                    QVariantList groupReturnSeries;
                    for (int gi = 0; gi < grsArr.size(); ++gi) {
                        QJsonObject gObj = grsArr[gi].toObject();
                        QVariantMap gMap;
                        gMap["groupIndex"] = gObj.value("groupIndex").toInt();
                        gMap["groupName"]  = gObj.value("groupName").toString();
                        gMap["data"]       = jsonArrayToVariantList(gObj.value("data").toArray());
                        groupReturnSeries.append(gMap);
                    }
                    fq["groupReturnSeries"] = groupReturnSeries;
                    fq["optionalMetrics"]   = optionalMetrics;
                    fq["auxiliaryMetrics"]  = auxiliaryMetrics;

                    QVariantMap coreSection;
                    coreSection["title"]    = QStringLiteral("核心指标");
                    coreSection["subtitle"] = QStringLiteral("因子回测关键绩效与质量指标");
                    fq["coreSection"]       = coreSection;

                    QVariantMap optSection;
                    optSection["title"]    = QStringLiteral("扩展指标");
                    optSection["subtitle"] = QStringLiteral("补充风险与统计指标");
                    fq["optionalSection"]  = optSection;

                    QVariantMap auxSection;
                    auxSection["title"]              = QStringLiteral("辅助指标");
                    auxSection["subtitle"]           = QStringLiteral("其他参考指标");
                    auxSection["expandedSubtitle"]   = QStringLiteral("收起辅助指标");
                    auxSection["collapsedSubtitle"]  = QStringLiteral("展开辅助指标");
                    fq["auxiliarySection"]           = auxSection;

                    // 组装 metrics
                    QVariantMap metrics;
                    metrics["groups"]        = groupsList;
                    metrics["trading"]       = tradingMap;
                    metrics["ic"]            = icMap;
                    metrics["execution"]     = execMap;
                    metrics["factorQuality"] = fq;

                    m_resultMetrics = metrics;
                    result["metrics"] = metrics;

                    // config — QML 读取 config.factorId / startDate / endDate / benchmarkSymbol
                    QVariantMap cfgMap;
                    if (!config.factorIds.empty()) {
                        QVariantList allFactorIds;
                        for (const auto& fid : config.factorIds)
                            allFactorIds.append(QString::fromStdString(fid));
                        cfgMap["factorIds"] = allFactorIds;
                        cfgMap["factorId"] = QString::fromStdString(config.factorIds.front());
                        result["factorId"] = cfgMap["factorId"];
                    }
                    cfgMap["benchmarkSymbol"] = QString::fromStdString(config.benchmarkSymbol);
                    cfgMap["initialCapital"]  = config.initialCapital;
                    cfgMap["startDate"] = rootObj.value("startDate").toString();
                    cfgMap["endDate"]   = rootObj.value("endDate").toString();
                    result["config"] = cfgMap;

                    // 先结束 isRunning（让 onBacktestResultChanged 的 guard 通过）
                    m_progress = 100.0;
                    m_statusText = QStringLiteral("回测完成");
                    m_isRunning.store(false);
                    emit progressChanged(); emit statusChanged();
                    emit isRunningChanged();

                    emit resultMetricsChanged();

                    m_backtestResult = result;
                    emit backtestResultChanged();
                    emit backtestCompleted(result);

                    // ── 持久化到 alpha.factor_backtest_runs / daily ──
                    {
                        auto& pool = astock::database::NativeMySQLConnectionPool::instance();
                        auto db = pool.getConnection();
                        if (db && db->isOpen()) {
                            using P = astock::database::SqlParam;
                            std::string runId = foundation::utils::Uuid::generate().to_string_no_dashes();
                            std::string fid = config.factorIds.empty() ? "unknown" :
                                (config.factorIds.size() == 1 ? config.factorIds.front() :
                                 [&]() { std::string s; for (size_t i=0;i<config.factorIds.size();++i)
                                    { if(i>0) s+=","; s+=config.factorIds[i]; } return s; }());
                            std::string cfgJson = QJsonDocument(QJsonObject::fromVariantMap(cfgMap)).toJson(QJsonDocument::Compact).toStdString();
                            std::string summaryJson = QJsonDocument(QJsonObject::fromVariantMap(metrics)).toJson(QJsonDocument::Compact).toStdString();
                            std::string groupsJsonStr = QJsonDocument(QJsonArray::fromVariantList(groupsList)).toJson(QJsonDocument::Compact).toStdString();

                            db->executeUpdate(
                                "INSERT INTO alpha.factor_backtest_runs(id,factor_id,config_json,summary_json,groups_json) VALUES($1,$2,$3,$4,$5)",
                                {P{runId}, P{fid}, P{cfgJson}, P{summaryJson}, P{groupsJsonStr}});

                            // daily series from returnSeries
                            QJsonArray retArr = rootObj.value("returnSeries").toArray();
                            QJsonArray rawArr = retArr.size()>0 ? retArr[0].toObject().value("data").toArray() : QJsonArray();
                            for (int di = 0; di < rawArr.size(); ++di) {
                                QJsonArray dayData = rawArr[di].toArray();
                                if (dayData.size() < 2) continue;
                                std::string dateStr = dayData[0].toString().toStdString();
                                QJsonArray groupReturns;
                                for (int gi = 1; gi < dayData.size(); ++gi)
                                    groupReturns.append(dayData[gi].toDouble());
                                std::string grJson = QJsonDocument(groupReturns).toJson(QJsonDocument::Compact).toStdString();
                                db->executeUpdate(
                                    "INSERT INTO alpha.factor_backtest_daily(run_id,trade_date,group_returns_json) VALUES($1,$2,$3) ON CONFLICT(run_id,trade_date) DO NOTHING",
                                    {P{runId}, P{dateStr}, P{grJson}});
                            }
                        }
                    }

                    emit backtestProgress(100.0, m_statusText);
                }, Qt::QueuedConnection);
            }
        );
    });
}

// ── 其余辅助方法 ──

void FactorBacktestBridge::startCompositeBacktest(const QVariantMap&, const QString&, const QString&, const QString&, const QVariantMap&)
{ INTERNAL_DEBUG_STREAM << "[FactBacktestBridge] 组合回测暂未实现"; }

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

QVariantMap FactorBacktestBridge::buildFactorSupportMap(
    const QVariantList& factorIds,
    const QString& startDate,
    const QString& endDate,
    const QVariantMap& cacheSnapshot)
{
    auto buildFallbackMap = [&factorIds](const QString& reason, const QString& category) {
        QVariantMap map;
        for (const QVariant& id : factorIds) {
            QVariantMap i;
            i["supported"] = false;
            i["reason"] = reason;
            i["category"] = category;
            map[id.toString()] = i;
        }
        return map;
    };

    auto* factorSvc = FactorService::instance();
    if (factorSvc && factorSvc->isInitialized()) {
        QStringList ids;
        for (const QVariant& id : factorIds)
            ids.append(id.toString());

        try {
            QVariantMap map = factorSvc->buildFactorSupportMap(
                ids, startDate, endDate, cacheSnapshot,
                m_dataSourceMode, m_selectedDatasetId);

            m_factorSupportMapCache = map;
            return map;
        } catch (const std::exception& e) {
            QVariantMap map = buildFallbackMap(
                QStringLiteral("因子检测异常: ") + QString::fromStdString(e.what()),
                QStringLiteral("runtime-init-failed"));
            m_factorSupportMapCache = map;
            return map;
        } catch (...) {
            QVariantMap map = buildFallbackMap(
                QStringLiteral("因子检测未知异常，已跳过检测"),
                QStringLiteral("runtime-init-failed"));
            m_factorSupportMapCache = map;
            return map;
        }
    }

    // FactorService 未就绪 — 不可回测
    QVariantMap map = buildFallbackMap(
        QStringLiteral("FactorService 未初始化，请稍后重试"),
        QStringLiteral("runtime-init-failed"));
    m_factorSupportMapCache = map;
    return map;
}

int FactorBacktestBridge::beginFactorSupportMapRefresh(const QVariantList& factorIds, const QString& startDate, const QString& endDate, const QVariantMap& cacheSnapshot)
{
    // 懒初始化线程池
    if (!m_workerPool) {
        m_workerPool = std::make_unique<foundation::thread::ThreadPoolExecutor>(
            1, 1, std::chrono::milliseconds(60000), "FactorSupportCheck");
    }

    static std::atomic<int> s_nextRequestId{1};
    int requestId = s_nextRequestId.fetch_add(1);

    m_supportMapRequestInFlight.store(true);
    emit supportMapRequestInFlightChanged();

    // 捕获必要参数到 worker 线程
    QStringList ids;
    for (const QVariant& id : factorIds) ids.append(id.toString());
    QString capturedMode = m_dataSourceMode;
    int capturedDatasetId = m_selectedDatasetId;

    INTERNAL_INFO_STREAM << "[FactorSupportCheck] worker started, requestId=" << requestId << ", factors=" << (int)ids.size();
    m_workerPool->post([this, requestId, ids, startDate, endDate, cacheSnapshot, capturedMode, capturedDatasetId]() {
        INTERNAL_INFO_STREAM << "[FactorSupportCheck] worker running, requestId=" << requestId;
        QVariantMap map;
        try {
            auto* factorSvc = FactorService::instance();
            INTERNAL_INFO_STREAM << "[FactorSupportCheck] FactorService=" << (void*)factorSvc << ", initialized=" << (factorSvc ? (int)factorSvc->isInitialized() : -1);
            if (factorSvc && factorSvc->isInitialized()) {
                map = factorSvc->buildFactorSupportMap(ids, startDate, endDate, cacheSnapshot,
                                                        capturedMode, capturedDatasetId);
            } else {
                for (const QString& id : ids) {
                    QVariantMap i;
                    i["supported"] = false;
                    i["reason"] = QStringLiteral("FactorService 未初始化");
                    i["category"] = QStringLiteral("runtime-init-failed");
                    map[id] = i;
                }
            }
        } catch (const std::exception& e) {
            for (const QString& id : ids) {
                QVariantMap i;
                i["supported"] = false;
                i["reason"] = QStringLiteral("因子检测异常: ") + QString::fromStdString(e.what());
                i["category"] = QStringLiteral("runtime-init-failed");
                map[id] = i;
            }
        } catch (...) {
            for (const QString& id : ids) {
                QVariantMap i;
                i["supported"] = false;
                i["reason"] = QStringLiteral("因子检测未知异常，已跳过检测");
                i["category"] = QStringLiteral("runtime-init-failed");
                map[id] = i;
            }
        }

        INTERNAL_INFO_STREAM << "[FactorSupportCheck] worker done, requestId=" << requestId << ", mapSize=" << (int)map.size();
        QMetaObject::invokeMethod(this, [this, requestId, map]() {
            INTERNAL_INFO_STREAM << "[FactorSupportCheck] main thread callback, requestId=" << requestId << ", mapSize=" << (int)map.size();
            m_supportMapRequestInFlight.store(false);
            emit supportMapRequestInFlightChanged();
            emit factorSupportMapReady(requestId, map);
        }, Qt::QueuedConnection);
    });

    return requestId;
}

bool FactorBacktestBridge::handleFactorSupportMapReady(int id, const QVariantMap& map)
{
    if (id <= 0 || map.isEmpty()) return false;
    m_factorSupportMapCache = map;
    emit factorSupportMapCacheChanged();
    return true;
}

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