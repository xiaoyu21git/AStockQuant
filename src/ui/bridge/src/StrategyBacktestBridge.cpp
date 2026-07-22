// StrategyBacktestBridge.cpp — 纯桥接层
// 职责：参数转换 → 线程调度 → 结果转发到 QML
// 不允许在此层写任何业务逻辑。

#include "StrategyBacktestBridge.h"
#include "foundation/thread/ThreadPoolExecutor.h"
#include "foundation/Utils/Timestamp.h"
#include "foundation/Utils/Uuid.h"
#include "foundation/log/logging.hpp"

#include "BacktestRequest.h"
#include "StrategyBridge.h"
#include "DataCacheAdapter.h"
#include "../../../infrastructure/include/database/BacktestResultRepository.h"
#include "../../../infrastructure/include/database/NativePgConnectionPool.h"
#include "FactorService.h"
#include "AppStoragePaths.h"
#include "../../domain/factor/include/FactorInstanceManager.h"
#include "../../domain/strategy/include/IStrategyService.h"
#include "../../domain/strategy/include/StrategyManager.h"
#include "../../domain/strategy/include/RuntimeFactorSvc.h"
#include "../../domain/factor/include/factor_compute/FactorEngine.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>

namespace {

using domain::backtest::BacktestRequest;
using domain::backtest::CostSpec;
using domain::backtest::RiskSpec;
using domain::backtest::ExecutionSpec;
using domain::backtest::DataSourceSpec;
using domain::strategy::StrategyId;
using domain::strategy::StrategyName;
using domain::strategy::StrategyExecutionKind;
using domain::strategy::Money;
using domain::strategy::Ratio;
using domain::strategy::DatasetId;
using domain::strategy::DataSourceMode;
using domain::strategy::PositionSizingMethod;

BacktestRequest buildBacktestRequest(const QString& strategyId, const QVariantMap& params)
{
    BacktestRequest req;

    req.strategyIdentity.strategyId = StrategyId(strategyId.toStdString());
    req.strategyIdentity.strategyName = StrategyName(
        params.value("strategyName").toString().toStdString());
    req.strategyIdentity.executionKind = StrategyExecutionKind::Standard;

    const QString startDate = params.value("startDate").toString();
    const QString endDate = params.value("endDate").toString();
    req.window.startDate = foundation::utils::Timestamp::from_string(
        startDate.toStdString(), "%Y-%m-%d");
    req.window.endDate = foundation::utils::Timestamp::from_string(
        endDate.toStdString(), "%Y-%m-%d");

    req.benchmarkIndex = params.value("benchmarkIndex", "000300.SH").toString().toStdString();

    req.costSpec.initialCapital = Money{params.value("initialCapital", 1000000.0).toDouble()};
    req.costSpec.commissionRate = Ratio{params.value("commissionRate", 0.0003).toDouble()};
    req.costSpec.slippageRate = Ratio{params.value("slippageRate", 0.001).toDouble()};
    req.costSpec.taxRate = Ratio{params.value("stampTaxRate", 0.001).toDouble()};

    req.riskSpec.maxPositionRatio = Ratio{params.value("singlePositionWeight", 0.20).toDouble()};
    req.riskSpec.maxSinglePositionRatio = Ratio{params.value("singlePositionWeight", 0.20).toDouble()};
    req.riskSpec.maxDrawdownLimit = Ratio{0.30};
    req.riskSpec.stopLossRate = Ratio{params.value("stopLossPercent", 0.10).toDouble()};
    req.riskSpec.takeProfitRate = Ratio{params.value("takeProfitPercent", 0.30).toDouble()};

    req.executionSpec.executionKind = StrategyExecutionKind::Standard;
    req.executionSpec.positionSizingMethod = PositionSizingMethod::FixedFraction;
    QString rebalanceFreq = params.value("rebalanceFrequency", "daily").toString();
    if (rebalanceFreq == "weekly") req.executionSpec.rebalanceFrequencyDays = 5;
    else if (rebalanceFreq == "monthly") req.executionSpec.rebalanceFrequencyDays = 21;
    else if (rebalanceFreq == "quarterly") req.executionSpec.rebalanceFrequencyDays = 63;
    else req.executionSpec.rebalanceFrequencyDays = 1;
    req.executionSpec.useMarketOnClose =
        (params.value("executionTiming", "close").toString() == "close");

    int datasetId = params.value("datasetCacheId", -1).toInt();
    if (datasetId >= 0) {
        req.dataSourceSpec.mode = DataSourceMode::CacheDataset;
        req.dataSourceSpec.datasetId = DatasetId{datasetId};
    } else {
        req.dataSourceSpec.mode = DataSourceMode::Raw;
    }

    req.runtimeOptions.maxThreads = 1;
    req.runtimeOptions.enableCache = false;
    req.runtimeOptions.cacheTtlSeconds = 0;

    req.factorOverlaySpec.enabled = false;
    req.factorOverlaySpec.targetPositionCount = params.value("maxPositionCount", 20).toInt();

    req.strategyIdentity.strategyCode = domain::strategy::StrategyCode("default");
    req.strategyIdentity.storedType = domain::backtest::StrategyStoredType::Custom;
    req.strategyIdentity.behaviorKind = domain::backtest::StrategyBehaviorKind::Custom;

    req.strategySpec.ruleProfile.maxPositionRatio = Ratio{1.0};
    req.strategySpec.ruleProfile.maxTotalExposureRatio = Ratio{1.0};
    req.strategySpec.ruleProfile.stopLossRatio = Ratio{0.10};
    req.strategySpec.ruleProfile.takeProfitRatio = Ratio{0.30};
    req.strategySpec.ruleProfile.rebalanceDays = req.executionSpec.rebalanceFrequencyDays;
    req.strategySpec.executionPolicy.rebalanceFrequencyDays =
        domain::strategy::RebalanceFrequencyDays{req.executionSpec.rebalanceFrequencyDays};
    req.strategySpec.executionPolicy.shortSellingMode =
        domain::strategy::ShortSellingMode::Disabled;
    req.strategySpec.factorOverlay.enabled = false;

    // 从数据集提取股票池（若已加载）
    const QString symbolsJson = params.value("datasetSymbols").toString();
    if (!symbolsJson.isEmpty()) {
        const QStringList symbols = symbolsJson.split(",", Qt::SkipEmptyParts);
        for (const QString& sym : symbols) {
            const std::string s = sym.trimmed().toStdString();
            if (!s.empty()) {
                req.universeSpec.explicitSymbols.push_back(domain::strategy::SymbolCode(s));
            }
        }
    }
    if (req.universeSpec.explicitSymbols.empty()) {
        req.universeSpec.explicitSymbols.push_back(domain::strategy::SymbolCode("000001.SZ"));
    }
    req.universeSpec.universeMode = domain::strategy::UniverseMode::ExplicitSymbols;
    req.strategySpec.strategyScopeContext.universe = req.universeSpec;

    return req;
}

} // anonymous namespace

StrategyBacktestBridge::StrategyBacktestBridge(QObject* parent) : QObject(parent) {}

StrategyBacktestBridge::~StrategyBacktestBridge() { cancelBacktest(); }

bool StrategyBacktestBridge::initialize()
{
    m_statusText = QStringLiteral("Ready");
    emit statusChanged();
    return true;
}

void StrategyBacktestBridge::runBacktest(const QString& strategyId, const QVariantMap& params)
{
    if (m_isRunning.load()) return;
    m_isRunning.store(true);
    emit isRunningChanged();
    m_progress = 0.0; emit progressChanged();
    m_statusText = QStringLiteral("Preparing..."); emit statusChanged();

    // 快速校验（UI 线程，无 I/O）
    if (strategyId.isEmpty()) {
        m_isRunning.store(false);
        emit isRunningChanged();
        emit backtestFailed(QStringLiteral("Strategy ID is empty"));
        return;
    }

    // 捕获所有参数到值类型，后续全部在线程池中执行
    const std::string capturedStrategyId = strategyId.toStdString();
    const int capturedDatasetId = params.value("datasetCacheId", -1).toInt();

    if (!m_workerPool) {
        m_workerPool = std::make_unique<foundation::thread::ThreadPoolExecutor>(
            1, 1, std::chrono::milliseconds(120000), "StrategyBacktestBridge");
    }

    m_workerPool->post([this, capturedStrategyId, capturedDatasetId, params]() {
        try {
            // ─── 1. 构建回测请求（纯 CPU，无 I/O）───
            BacktestRequest request = buildBacktestRequest(
                QString::fromStdString(capturedStrategyId), params);
            if (!request.isValid()) {
                QMetaObject::invokeMethod(this, [this]() {
                    m_isRunning.store(false);
                    emit isRunningChanged();
                    emit backtestFailed(QStringLiteral("Invalid backtest parameters"));
                }, Qt::QueuedConnection);
                return;
            }

            // ─── 2. 重建引擎（每次回测重新读 DB，参数变更即刻生效）───
            auto& mgr = domain::strategy::StrategyManager::instance();
            std::unique_ptr<domain::strategy::RuntimeFactorSvc> factorSvc;
            auto* factorSvcBridge = FactorService::instance();
            if (factorSvcBridge && factorSvcBridge->isInitialized()) {
                auto* instanceMgr = factorSvcBridge->instanceManager();
                if (instanceMgr) {
                    auto symbolResolver = [](std::uint32_t id) -> std::string {
                        char buf[16];
                        std::snprintf(buf, sizeof(buf), "%06u.SZ", id);
                        return buf;
                    };
                    auto factorNameResolver = [](std::uint64_t fid) -> std::string {
                        return std::to_string(fid);
                    };
                    factorSvc = std::make_unique<domain::strategy::RuntimeFactorSvc>(
                        *instanceMgr,
                        std::move(symbolResolver),
                        std::move(factorNameResolver));
                }
            }
            auto* engine = mgr.createEngine(capturedStrategyId, std::move(factorSvc));
            if (!engine) {
                QMetaObject::invokeMethod(this, [this]() {
                    m_isRunning.store(false);
                    emit isRunningChanged();
                    emit backtestFailed(QStringLiteral("Strategy engine not available"));
                }, Qt::QueuedConnection);
                return;
            }

            // ─── 3. 直接注入 Arrow 列式视图 ───
            auto dataSvc = std::make_unique<factor::compute::BacktestDataService>();
            if (capturedDatasetId >= 0) {
                std::string arrowPath = cleaning::DataCache::instance().dataFilePath(capturedDatasetId);
                auto arrowView = std::make_unique<factor::compute::ArrowMarketDataView>(arrowPath);
                if (arrowView->instruments().empty()) {
                    QMetaObject::invokeMethod(this, [this]() {
                        m_isRunning.store(false); emit isRunningChanged();
                        emit backtestFailed(QStringLiteral("数据集为空"));
                    }, Qt::QueuedConnection);
                    return;
                }
                dataSvc->setMarketView(arrowView.get());
                dataSvc->buildViewForFields({});
                m_strategyDataSvc = std::move(arrowView);
            }

            // ─── 4. 执行回测 ───
            auto result = engine->backtest(request, dataSvc.get(),
                [this](double progress) {
                    QMetaObject::invokeMethod(this, [this, progress]() {
                        m_progress = progress;
                        emit progressChanged();
                    }, Qt::QueuedConnection);
                });

            if (!result.success) {
                QMetaObject::invokeMethod(this, [this, msg = QString::fromStdString(result.errorMessage)]() {
                    m_isRunning.store(false);
                    emit isRunningChanged();
                    emit backtestFailed(msg);
                }, Qt::QueuedConnection);
                return;
            }

            // ─── 5. 序列化结果到 QVariantMap ───
            QVariantMap qResult;
            qResult["status"] = QStringLiteral("SUCCESS");

            QVariantMap metricsMap;
            metricsMap["totalReturn"]      = result.metrics.totalReturn;
            metricsMap["annualizedReturn"] = result.metrics.annualizedReturn;
            metricsMap["volatility"]       = result.metrics.volatility;
            metricsMap["sharpeRatio"]      = result.metrics.sharpeRatio;
            metricsMap["maxDrawdown"]      = result.metrics.maxDrawdown;
            metricsMap["sortinoRatio"]     = result.metrics.sortinoRatio;
            metricsMap["calmarRatio"]      = result.metrics.calmarRatio;
            metricsMap["winRate"]          = result.metrics.winRate;
            metricsMap["profitFactor"]     = result.metrics.profitFactor;
            metricsMap["alpha"]            = result.metrics.alpha;
            metricsMap["beta"]             = result.metrics.beta;
            metricsMap["informationRatio"] = result.metrics.informationRatio;
            metricsMap["trackingError"]    = result.metrics.trackingError;
            metricsMap["fullKelly"]        = result.fullKelly;
            metricsMap["halfKelly"]        = result.halfKelly;
            // 混合模式因子参与统计: 落库后可精确回答"该次回测跑了几个因子"
            QVariantList hybridFactorList;
            for (const auto& coverage : result.hybridFactorCoverage) {
                QVariantMap coverageRow;
                coverageRow["factorId"]    = QString::fromStdString(coverage.factorId);
                coverageRow["coveredDays"] = coverage.coveredDays;
                hybridFactorList.append(coverageRow);
            }
            metricsMap["hybridFactors"] = hybridFactorList;
            qResult["performance"] = metricsMap;

            QVariantMap tradeStats;
            tradeStats["totalTrades"]   = static_cast<int>(result.tradeStats.totalTrades);
            tradeStats["winningTrades"] = static_cast<int>(result.tradeStats.winningTrades);
            tradeStats["losingTrades"]  = static_cast<int>(result.tradeStats.losingTrades);
            tradeStats["totalProfit"]   = result.tradeStats.totalProfit.value;
            tradeStats["totalLoss"]     = result.tradeStats.totalLoss.value;
            tradeStats["largestWin"]    = result.tradeStats.largestWin.value;
            tradeStats["largestLoss"]   = result.tradeStats.largestLoss.value;
            qResult["trades"] = tradeStats;

            QVariantMap timeSeries;
            QVariantList equityList, dateList, returnList, drawdownList,
                         bmEquityList, bmDrawdownList;
            for (size_t i = 0; i < result.timeSeries.portfolioValues.size(); ++i) {
                equityList.append(result.timeSeries.portfolioValues[i]);
                if (i < result.timeSeries.returns.size()) returnList.append(result.timeSeries.returns[i]);
                if (i < result.timeSeries.drawdowns.size()) drawdownList.append(result.timeSeries.drawdowns[i]);
            }
            for (size_t i = 0; i < result.timeSeries.benchmarkValues.size(); ++i) {
                bmEquityList.append(result.timeSeries.benchmarkValues[i]);
                if (i < result.timeSeries.benchmarkDrawdowns.size())
                    bmDrawdownList.append(result.timeSeries.benchmarkDrawdowns[i]);
            }
            for (const auto& d : result.timeSeries.dates) {
                dateList.append(d.value);
            }
            timeSeries["portfolioValues"]     = equityList;
            timeSeries["dates"]               = dateList;
            timeSeries["returns"]             = returnList;
            timeSeries["drawdowns"]           = drawdownList;
            timeSeries["benchmarkValues"]     = bmEquityList;
            timeSeries["benchmarkDrawdowns"]  = bmDrawdownList;
            qResult["timeSeries"] = timeSeries;

            // 回传参数供面板展示
            QVariantMap qParams;
            qParams["startDate"]       = params.value("startDate");
            qParams["endDate"]         = params.value("endDate");
            qParams["benchmarkIndex"]  = params.value("benchmarkIndex");
            qParams["priceAdjustment"] = params.value("priceAdjustment");
            qParams["initialCapital"]  = params.value("initialCapital");
            qParams["commissionRate"]  = params.value("commissionRate");
            qParams["slippageRate"]    = params.value("slippageRate");
            qParams["dataFrequency"]   = params.value("dataFrequency");
            // ── 策略参数快照(因子/模式/仓位) — 回答"哪次回测用的什么配置" ──
            QVariantMap strategyParamsSnapshot;
            {
                auto& pool = astock::database::NativePgConnectionPool::instance();
                if (pool.isInitialized()) {
                    auto db = pool.getConnection();
                    if (db && db->isOpen()) {
                        auto paramResult = db->executeQuery(
                            "SELECT parameters FROM live.strategy WHERE strategy_id = ?",
                            {astock::database::SqlParam{capturedStrategyId}});
                        if (!paramResult.isEmpty()) {
                            auto paramJson = QJsonDocument::fromJson(
                                QString::fromStdString(paramResult.getRow(0).getString("parameters")).toUtf8());
                            if (paramJson.isObject()) strategyParamsSnapshot = paramJson.object().toVariantMap();
                        }
                    }
                }
            }
            // 合并: 回测请求参数 + 策略配置快照
            QVariantMap mergedParams = strategyParamsSnapshot;
            for (auto it = qParams.begin(); it != qParams.end(); ++it)
                mergedParams[it.key()] = it.value();
            qResult["parameters"] = mergedParams;

            // 风险指标 + 拒绝统计
            QVariantMap riskMap;
            riskMap["var95"] = 0.0; riskMap["cvar95"] = 0.0;
            riskMap["downsideDeviation"] = 0.0; riskMap["maxConsecutiveLosses"] = 0;
            riskMap["totalRejected"] = result.riskRejectedCount;
            QVariantMap rejectionMap;
            for (const auto& [code, count] : result.riskRejectionStats) {
                rejectionMap[QString::number(code)] = count;
            }
            riskMap["rejectionDetails"] = rejectionMap;
            qResult["risk"] = riskMap;

            // 持久化到 DB
            {
                auto& pool = astock::database::NativePgConnectionPool::instance();
                auto db = pool.getConnection();
                if (db) {
                    domain::backtest::BacktestResultRepository repo(*db);
                    repo.ensureTables();
                        domain::backtest::StoredStrategyBacktest record;
                        record.id = foundation::utils::Uuid::generate_v4().to_string();
                        record.strategyId = capturedStrategyId;
                        auto sd = params.value("startDate").toString();
                        auto ed = params.value("endDate").toString();
                        if (sd.length() == 8 && !sd.contains("-")) sd = sd.left(4) + "-" + sd.mid(4,2) + "-" + sd.right(2);
                        if (ed.length() == 8 && !ed.contains("-")) ed = ed.left(4) + "-" + ed.mid(4,2) + "-" + ed.right(2);
                        record.dataStartDate = sd.toStdString();
                        record.dataEndDate   = ed.toStdString();
                        // 策略参数
                        auto po = strategyParamsSnapshot;
                        auto fo = po.value("factor_overlay").toMap();
                        record.combineMode       = fo.value("combineMode").toString().toStdString();
                        record.targetPositionCount = fo.value("targetPositionCount").toInt();
                        record.maxPositions      = po.value("maxPositions").toInt();
                        record.fastPeriod        = po.value("fastPeriod").toInt();
                        record.slowPeriod        = po.value("slowPeriod").toInt();
                        record.signalPeriod      = po.value("signalPeriod").toInt();
                        auto allocs = fo.value("allocations").toList();
                        record.factorCount = allocs.size();
                        QStringList fids, fws;
                        for (const auto& a : allocs) {
                            auto am = a.toMap();
                            fids << am.value("factor_id").toString();
                            fws << QString("%1:%2").arg(am.value("factor_id").toString()).arg(am.value("weight_percent").toDouble());
                        }
                        record.factorIds = fids.join(",").toStdString();
                        record.factorWeights = fws.join(";").toStdString();
                        // 绩效
                        auto perf = qResult["performance"].toMap();
                        record.totalReturn      = perf.value("totalReturn").toDouble();
                        record.annualizedReturn = perf.value("annualizedReturn").toDouble();
                        record.sharpeRatio      = perf.value("sharpeRatio").toDouble();
                        record.maxDrawdown      = perf.value("maxDrawdown").toDouble();
                        record.winRate          = perf.value("winRate").toDouble();
                        record.profitFactor     = perf.value("profitFactor").toDouble();
                        record.sortinoRatio     = perf.value("sortinoRatio").toDouble();
                        record.calmarRatio      = perf.value("calmarRatio").toDouble();
                        record.volatility       = perf.value("volatility").toDouble();
                        record.alpha            = perf.value("alpha").toDouble();
                        record.beta             = perf.value("beta").toDouble();
                        auto ts = qResult["trades"].toMap();
                        record.totalTrades      = ts.value("totalTrades").toInt();
                        record.winningTrades    = ts.value("winningTrades").toInt();
                        record.losingTrades     = ts.value("losingTrades").toInt();
                        record.totalProfit      = ts.value("totalProfit").toDouble();
                        record.totalLoss        = ts.value("totalLoss").toDouble();
                        record.maxWin           = ts.value("largestWin").toDouble();
                        record.maxLoss          = ts.value("largestLoss").toDouble();
                        // 诊断
                        // 诊断
                        record.stopLossFills   = result.stopLossFills;
                        record.ruleExitFills   = result.ruleExitFills;
                        record.normalSellFills = result.normalSellFills;
                        record.riskRejected    = result.riskRejectedCount;
                        record.avgHoldingDays  = result.avgHoldingDays;
                        record.avgPositions    = result.avgPositions;
                        record.equityCurveJson = QJsonDocument(QJsonObject::fromVariantMap(
                            qResult["timeSeries"].toMap())).toJson(QJsonDocument::Compact).toStdString();
                        INTERNAL_INFO_STREAM << "[StrategyBacktest] 准备写入回测结果 id=" << record.id << " strategy=" << record.strategyId;
                        if (repo.saveStrategyBacktest(record)) {
                            std::vector<domain::backtest::StoredStrategyTrade> storedTrades;
                            storedTrades.reserve(result.tradeLog.size());
                            for (const auto& trade : result.tradeLog) {
                                char dateBuf[16];
                                std::snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d",
                                              trade.tradeDate / 10000, (trade.tradeDate / 100) % 100,
                                              trade.tradeDate % 100);
                                storedTrades.push_back({record.id, dateBuf, trade.symbol,
                                                        trade.isBuy, trade.quantity,
                                                        trade.price, trade.realizedPnl});
                            }
                            repo.saveStrategyTrades(storedTrades);
                            INTERNAL_INFO_STREAM << "[StrategyBacktest] 已持久化 run=" << record.id
                                                 << " trades=" << storedTrades.size();
                        }
                    }
            }

            // ── 回测指标写入策略卡片 ──
            {
                try {
                    auto& pool = astock::database::NativePgConnectionPool::instance();
                    if (pool.isInitialized()) {
                        auto db = pool.getConnection();
                        if (db && db->isOpen()) {
                            std::ostringstream js;
                            js << "{\"returns\":\"" << (result.metrics.totalReturn * 100.0)
                               << "%\",\"sharpeRatio\":\"" << result.metrics.sharpeRatio
                               << "\",\"maxDrawdown\":\"-" << (result.metrics.maxDrawdown * 100.0)
                               << "%\",\"winRate\":\"" << (result.metrics.winRate * 100.0)
                               << "%\",\"annualizedReturn\":\"" << (result.metrics.annualizedReturn * 100.0) << "%\"}";
                            db->executeUpdate(
                                "UPDATE strategy SET metadata_json = COALESCE(metadata_json,'{}'::jsonb) || ?::jsonb WHERE strategy_id = ?",
                                {astock::database::SqlParam{js.str()}, astock::database::SqlParam{capturedStrategyId}});
                            INTERNAL_INFO_STREAM << "[StrategyBacktest] 回测指标已写入策略卡片";
                        }
                    }
                } catch (const std::exception& e) {
                    INTERNAL_WARN_STREAM << "[StrategyBacktest] 指标写入失败: " << e.what();
                }
            }

            // ── 凯利公式更新策略单票上限(负值写0, 禁止使用) ──
            {
                double fk = result.fullKelly > 0.0 ? result.fullKelly : 0.0;
                double hk = result.halfKelly > 0.0 ? result.halfKelly : 0.0;
                try {
                    auto& pool = astock::database::NativePgConnectionPool::instance();
                    if (pool.isInitialized()) {
                        auto db = pool.getConnection();
                        if (db && db->isOpen()) {
                            std::string kellyJson = "{\"fullKelly\":" + std::to_string(fk)
                                + ",\"halfKelly\":" + std::to_string(hk) + "}";
                            db->executeUpdate(
                                "UPDATE strategy SET parameters = COALESCE(parameters,'{}'::jsonb) || ?::jsonb WHERE strategy_id = ?",
                                {astock::database::SqlParam{kellyJson}, astock::database::SqlParam{capturedStrategyId}});
                            INTERNAL_INFO_STREAM << "[StrategyBacktest] 凯利仓位已更新: fullKelly="
                                                 << fk << " halfKelly=" << hk;
                        }
                    }
                } catch (const std::exception& e) {
                    INTERNAL_WARN_STREAM << "[StrategyBacktest] 凯利仓位更新失败: " << e.what();
                }
            }

            QMetaObject::invokeMethod(this, [this, qResult]() {
                m_isRunning.store(false); m_progress = 100.0;
                m_statusText = QStringLiteral("Complete");
                emit isRunningChanged(); emit progressChanged(); emit statusChanged();
                emit backtestCompleted(qResult);
            }, Qt::QueuedConnection);

        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = QString::fromUtf8(e.what())]() {
                m_isRunning.store(false);
                emit isRunningChanged();
                emit backtestFailed(QStringLiteral("Error: %1").arg(msg));
            }, Qt::QueuedConnection);
        } catch (...) {
            QMetaObject::invokeMethod(this, [this]() {
                m_isRunning.store(false);
                emit isRunningChanged();
                emit backtestFailed(QStringLiteral("Unknown error"));
            }, Qt::QueuedConnection);
        }
    });
}

void StrategyBacktestBridge::cancelBacktest()
{
    m_isRunning.store(false);
    emit isRunningChanged();
    emit backtestCancelled();
}

bool StrategyBacktestBridge::isRunning() const { return m_isRunning.load(); }
double StrategyBacktestBridge::progress() const { return m_progress; }
QString StrategyBacktestBridge::status() const { return m_statusText; }