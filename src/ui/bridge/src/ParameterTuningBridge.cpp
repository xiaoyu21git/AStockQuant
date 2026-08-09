// ParameterTuningBridge.cpp — 纯桥接层
// 职责：QML 参数解析 → 线程调度 → 领域 IOptimizer/ParameterSpace 调用 → 结果回传 QML
// 不允许在此层写任何业务逻辑。

#include "ParameterTuningBridge.h"
#include "BacktestRequest.h"
#include "StrategyBridge.h"
#include "DataCacheAdapter.h"
#include "../../domain/cleaning/include/DataCache.h"
#include "FactorService.h"
#include "AppStoragePaths.h"
#include "../../../infrastructure/include/database/NativePgConnectionPool.h"
#include "../../domain/strategy/include/IStrategyService.h"
#include "../../domain/strategy/include/StrategyManager.h"
#include "../../domain/strategy/include/RuntimeFactorSvc.h"
#include "../../domain/factor/include/FactorInstanceManager.h"
#include "../../domain/factor/include/factor_compute/FactorEngine.h"
#include "../../domain/factor/include/factor_compute/ArrowMarketDataView.h"
#include "../../domain/optimization/include/ParamRange.h"
#include "../../domain/optimization/include/ParamDef.h"
#include "../../domain/optimization/include/ParameterSpace.h"
#include "../../domain/optimization/include/ParameterSpaceFactory.h"
#include "../../domain/optimization/include/ObjectiveSpec.h"
#include "../../domain/optimization/include/TrialResult.h"
#include "../../domain/optimization/include/IOptimizer.h"
#include "../../domain/optimization/include/GridSearchOptimizer.h"
#include "../../foundation/include/foundation/market/AStockSymbol.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>

namespace {

using domain::backtest::BacktestRequest;
using domain::backtest::CostSpec;
using domain::backtest::DateWindow;
using domain::backtest::RiskSpec;
using domain::backtest::ExecutionSpec;
using domain::backtest::DataSourceSpec;
using domain::backtest::StrategyParamOverlay;
using domain::strategy::StrategyId;
using domain::strategy::StrategyName;
using domain::strategy::StrategyExecutionKind;
using domain::strategy::Money;
using domain::strategy::Ratio;
using domain::strategy::DatasetId;
using domain::strategy::DataSourceMode;
using domain::strategy::PositionSizingMethod;
using domain::optimization::ParamRange;
using domain::optimization::ParamSet;
using domain::optimization::TrialResult;
using domain::optimization::ObjectiveSpec;
using domain::optimization::OptimizationResult;
using domain::optimization::ObjectiveMetric;

/// @brief 将 ParamSet 映射为 StrategyParamOverlay
StrategyParamOverlay paramSetToOverlay(const ParamSet& ps,
                                       const std::vector<ParamRange>& ranges)
{
    StrategyParamOverlay overlay;

    for (const auto& range : ranges) {
        const std::string& name = range.name();
        auto it = ps.find(name);
        if (it == ps.end()) continue;

        double value = it->second;

        if (name == "topN")                          overlay.topN = static_cast<int>(value);
        else if (name == "maxPositions")             overlay.maxPositions = static_cast<int>(value);
        else if (name == "maxWeightPerStock")        overlay.maxWeightPerStock = value;
        else if (name == "minWeightPerStock")        overlay.minWeightPerStock = value;
        else if (name == "weightScheme")             overlay.weightSchemeIndex = static_cast<int>(value);
        else if (name == "rebalanceFrequency")       overlay.rebalanceFrequencyIndex = static_cast<int>(value);
        else if (name == "allowShort")               overlay.allowShort = (value > 0.5);
        else if (name == "industryNeutral")          overlay.industryNeutral = (value > 0.5);
        else if (name == "stopLossPercent")          overlay.stopLossPercent = value;
        else if (name == "takeProfitPercent")        overlay.takeProfitPercent = value;
        else if (name == "minHoldDays")              overlay.minHoldDays = static_cast<int>(value);
        else if (name == "minCompositeScore")        overlay.minCompositeScore = value;
        else if (name == "sellThreshold")            overlay.sellThreshold = value;
        else if (name == "sellRankMultiplier")       overlay.sellRankMultiplier = value;
        else if (name == "fastPeriod")               overlay.fastPeriod = static_cast<int>(value);
        else if (name == "slowPeriod")               overlay.slowPeriod = static_cast<int>(value);
        else if (name == "signalPeriod")             overlay.signalPeriod = static_cast<int>(value);
        else if (name == "period")                   overlay.signalPeriod = static_cast<int>(value);
        else if (name == "macdFast")                 overlay.macdFast = static_cast<int>(value);
        else if (name == "macdSlow")                 overlay.macdSlow = static_cast<int>(value);
        else if (name == "macdSignal")               overlay.macdSignal = static_cast<int>(value);
        else if (name == "bbPeriod")                 overlay.bbPeriod = static_cast<int>(value);
        else if (name == "bbStdDev")                 overlay.bbStdDev = value;
        else if (name == "priceField")               overlay.priceFieldIndex = static_cast<int>(value);
        // 以下参数名由各策略类型定制
        else if (name == "channelPeriod")            overlay.signalPeriod = static_cast<int>(value);
        else if (name == "breakoutMultiplier")       overlay.bbStdDev = value;  // 复用浮点槽
        else if (name == "atrPeriod")                overlay.fastPeriod = static_cast<int>(value);
        else if (name == "standardDeviationMultiplier") overlay.bbStdDev = value;
        else if (name == "entryThreshold")           overlay.sellThreshold = value;
        else if (name == "exitThreshold")            overlay.takeProfitPercent = value;
        else if (name == "oversoldLevel")            overlay.sellThreshold = value;
        else if (name == "overboughtLevel")          overlay.takeProfitPercent = value;
        else if (name == "lookback")                 overlay.signalPeriod = static_cast<int>(value);
        else if (name == "entryZScore")              overlay.sellThreshold = value;
        else if (name == "exitZScore")               overlay.takeProfitPercent = value;
        else if (name == "volatilityLookback")       overlay.signalPeriod = static_cast<int>(value);
        else if (name == "targetVolatility")         overlay.maxWeightPerStock = value;
    }

    return overlay;
}

/// @brief 构建基础 BacktestRequest (不含 overlay 部分)
BacktestRequest buildBaseRequest(const std::string& strategyId, const QVariantMap& params)
{
    BacktestRequest req;

    req.strategyIdentity.strategyId = StrategyId(strategyId);
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
    req.executionSpec.rebalanceFrequencyDays = 1;
    req.executionSpec.useMarketOnClose = true;

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
    req.strategySpec.ruleProfile.rebalanceDays = 1;
    req.strategySpec.executionPolicy.rebalanceFrequencyDays =
        domain::strategy::RebalanceFrequencyDays{1};
    req.strategySpec.executionPolicy.shortSellingMode =
        domain::strategy::ShortSellingMode::Disabled;
    req.strategySpec.factorOverlay.enabled = false;

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

/// @brief 从回测结果提取目标值
double extractObjectiveValue(const domain::strategy::StrategyBacktestResult& result,
                              ObjectiveMetric metric)
{
    switch (metric) {
    case ObjectiveMetric::SharpeRatio:      return result.metrics.sharpeRatio;
    case ObjectiveMetric::AnnualizedReturn: return result.metrics.annualizedReturn;
    case ObjectiveMetric::CalmarRatio:
        return (result.metrics.maxDrawdown > 0.001)
            ? result.metrics.annualizedReturn / result.metrics.maxDrawdown
            : 0.0;
    case ObjectiveMetric::SortinoRatio:     return result.metrics.sortinoRatio;
    case ObjectiveMetric::ProfitFactor:     return result.metrics.profitFactor;
    case ObjectiveMetric::Custom:           return result.metrics.sharpeRatio; // fallback
    }
    return 0.0;
}

/// @brief 将 TrialResult 序列化到 QVariantMap
QVariantMap trialToMap(const TrialResult& trial)
{
    QVariantMap m;
    m["trialIndex"] = trial.trialIndex;
    m["objectiveValue"] = trial.objectiveValue;
    m["constraintViolated"] = trial.constraintViolated;
    m["isViable"] = trial.isViable();
    if (!trial.errorMessage.empty())
        m["errorMessage"] = QString::fromStdString(trial.errorMessage);

    QVariantMap paramsMap;
    for (const auto& [key, value] : trial.params)
        paramsMap[QString::fromStdString(key)] = value;
    m["params"] = paramsMap;

    return m;
}

/// @brief 将 OptimizationResult 序列化到 QVariantMap
QVariantMap optimizationResultToMap(const OptimizationResult& optResult)
{
    QVariantMap m;
    m["totalTrials"] = optResult.totalTrials;
    m["failedTrials"] = optResult.failedTrials;
    m["constraintViolations"] = optResult.constraintViolations;
    m["elapsedSeconds"] = optResult.elapsedSeconds;

    const auto* best = optResult.bestTrial();
    if (best) {
        m["bestObjectiveValue"] = best->objectiveValue;
        QVariantMap bestParams;
        for (const auto& [key, value] : best->params)
            bestParams[QString::fromStdString(key)] = value;
        m["bestParams"] = bestParams;
    }

    QVariantList trialsList;
    for (const auto& t : optResult.trials)
        trialsList.append(trialToMap(t));
    m["trials"] = trialsList;

    return m;
}

} // anonymous namespace


ParameterTuningBridge::ParameterTuningBridge(QObject* parent) : QObject(parent) {}

ParameterTuningBridge::~ParameterTuningBridge()
{
    cancelTuning();
}

QVariantList ParameterTuningBridge::getTuningParamRanges(int strategyTypeIndex) const
{
    using domain::optimization::ParamRange;
    using domain::optimization::ParameterSpaceFactory;

    auto strategyType = static_cast<domain::strategies::StrategyType>(strategyTypeIndex);
    auto ranges = ParameterSpaceFactory::defaultRanges(strategyType);

    QVariantList result;
    for (const auto& r : ranges) {
        QVariantMap m;
        m["name"] = QString::fromStdString(r.name());
        m["type"] = static_cast<int>(r.type());
        m["candidateCount"] = r.candidateCount();

        switch (r.type()) {
        case domain::optimization::ParamType::Int:
            m["min"] = static_cast<int>(r.intMin());
            m["max"] = static_cast<int>(r.intMax());
            m["step"] = static_cast<int>(r.intStep());
            m["default"] = static_cast<int>(r.defaultValue());
            break;
        case domain::optimization::ParamType::Double:
            m["min"] = r.doubleMin();
            m["max"] = r.doubleMax();
            m["step"] = r.doubleStep();
            m["default"] = r.defaultValue();
            break;
        case domain::optimization::ParamType::Bool:
            m["default"] = r.defaultValue() > 0.5;
            break;
        case domain::optimization::ParamType::Enum: {
            QVariantList opts;
            for (const auto& opt : r.enumOptions()) {
                QVariantMap om;
                om["value"] = opt.value;
                om["label"] = QString::fromStdString(opt.label);
                opts.append(om);
            }
            m["options"] = opts;
            m["default"] = static_cast<int>(r.defaultValue());
            break;
        }
        }
        result.append(m);
    }
    return result;
}

int ParameterTuningBridge::estimateCombinations(int strategyTypeIndex) const
{
    auto strategyType = static_cast<domain::strategies::StrategyType>(strategyTypeIndex);
    auto ranges = domain::optimization::ParameterSpaceFactory::defaultRanges(strategyType);
    return static_cast<int>(domain::optimization::ParameterSpaceFactory::estimateSearchSpace(ranges));
}

bool ParameterTuningBridge::isRunning() const { return m_isRunning.load(); }
int ParameterTuningBridge::currentTrial() const { return m_currentTrial; }
int ParameterTuningBridge::totalTrials() const { return m_totalTrials; }
double ParameterTuningBridge::progress() const { return m_progress; }
QString ParameterTuningBridge::status() const { return m_statusText; }

void ParameterTuningBridge::startTuning(const QString& strategyId, int strategyTypeIndex,
                                         const QVariantMap& params)
{
    if (m_isRunning.load()) return;

    if (strategyId.isEmpty()) {
        emit tuningFailed(QStringLiteral("Strategy ID is empty"));
        return;
    }

    m_isRunning.store(true);
    m_cancelRequested.store(false);
    emit isRunningChanged();

    m_currentTrial = 0;
    m_totalTrials = 0;
    m_progress = 0.0;
    m_statusText = QStringLiteral("Preparing...");
    emit currentTrialChanged();
    emit totalTrialsChanged();
    emit progressChanged();
    emit statusChanged();

    const std::string capturedStrategyId = strategyId.toStdString();

    if (!m_workerPool) {
        m_workerPool = std::make_unique<foundation::thread::ThreadPoolExecutor>(
            1, 1, std::chrono::milliseconds(120000), "ParameterTuningBridge");
    }

    m_workerPool->post([this, capturedStrategyId, strategyTypeIndex, params]() {
        executeTuning(capturedStrategyId, strategyTypeIndex, params);
    });
}

void ParameterTuningBridge::cancelTuning()
{
    if (!m_isRunning.load()) return;
    m_cancelRequested.store(true, std::memory_order_release);
}

void ParameterTuningBridge::executeTuning(const std::string& strategyId,
                                           int strategyTypeIndex,
                                           const QVariantMap& params)
{
    try {
        // ── 0. 检查取消 ──
        if (m_cancelRequested.load(std::memory_order_acquire)) {
            QMetaObject::invokeMethod(this, [this]() {
                m_isRunning.store(false); emit isRunningChanged();
                emit tuningCancelled();
            }, Qt::QueuedConnection);
            return;
        }

        // ── 1. 解析策略类型 ──
        auto strategyType = static_cast<domain::strategies::StrategyType>(strategyTypeIndex);

        // ── 2. 构建参数空间 ──
        auto ranges = domain::optimization::ParameterSpaceFactory::defaultRanges(strategyType);
        if (ranges.empty()) {
            QMetaObject::invokeMethod(this, [this]() {
                m_isRunning.store(false); emit isRunningChanged();
                emit tuningFailed(QStringLiteral("No tunable parameters for this strategy type"));
            }, Qt::QueuedConnection);
            return;
        }

        domain::optimization::ParameterSpace space(ranges);
        std::size_t totalCombos = space.totalCombinations();
        if (totalCombos == 0) {
            QMetaObject::invokeMethod(this, [this]() {
                m_isRunning.store(false); emit isRunningChanged();
                emit tuningFailed(QStringLiteral("Empty parameter space"));
            }, Qt::QueuedConnection);
            return;
        }

        // ── 3. 检查组合爆炸 ──
        if (totalCombos > 100000) {
            QMetaObject::invokeMethod(this, [this, n = static_cast<int>(totalCombos)]() {
                m_isRunning.store(false); emit isRunningChanged();
                emit tuningFailed(
                    QString("Parameter space too large (%1 combinations). Narrow ranges or use Bayesian optimizer.")
                        .arg(n));
            }, Qt::QueuedConnection);
            return;
        }

        int maxTrials = params.value("maxTrials", 0).toInt();
        if (maxTrials <= 0 || maxTrials > static_cast<int>(totalCombos))
            maxTrials = static_cast<int>(totalCombos);

        m_totalTrials = maxTrials;
        QMetaObject::invokeMethod(this, [this]() { emit totalTrialsChanged(); }, Qt::QueuedConnection);

        // ── 4. 解析优化目标 ──
        QString metricName = params.value("objectiveMetric", "sharpe").toString();
        ObjectiveMetric metric = ObjectiveMetric::SharpeRatio;
        if (metricName == "annualizedReturn")      metric = ObjectiveMetric::AnnualizedReturn;
        else if (metricName == "calmar")           metric = ObjectiveMetric::CalmarRatio;
        else if (metricName == "sortino")          metric = ObjectiveMetric::SortinoRatio;
        else if (metricName == "profitFactor")     metric = ObjectiveMetric::ProfitFactor;
        else if (metricName == "custom")           metric = ObjectiveMetric::Custom;

        ObjectiveSpec objective = ObjectiveSpec::maximize(metric);

        // ── 5. 创建引擎一次 (fromDb) ──
        auto& mgr = domain::strategy::StrategyManager::instance();
        std::unique_ptr<domain::strategy::RuntimeFactorSvc> factorSvc;
        auto* factorSvcBridge = FactorService::instance();
        if (factorSvcBridge && factorSvcBridge->isInitialized()) {
            auto* instanceMgr = factorSvcBridge->instanceManager();
            if (instanceMgr) {
                auto symbolResolver = [](std::uint32_t id) -> std::string {
                    return foundation::market::AStockSymbol::fromInstrumentId(id);
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
        auto* engine = mgr.createEngine(strategyId, std::move(factorSvc));
        if (!engine) {
            QMetaObject::invokeMethod(this, [this]() {
                m_isRunning.store(false); emit isRunningChanged();
                emit tuningFailed(QStringLiteral("Failed to create strategy engine"));
            }, Qt::QueuedConnection);
            return;
        }

        // ── 6. 准备数据服务 (一次加载，所有 trial 复用) ──
        auto dataSvc = std::make_unique<factor::compute::BacktestDataService>();
        int capturedDatasetId = params.value("datasetCacheId", -1).toInt();
        std::unique_ptr<factor::compute::ArrowMarketDataView> arrowView;
        if (capturedDatasetId >= 0) {
            std::string arrowPath = cleaning::DataCache::instance().dataFilePath(capturedDatasetId);
            arrowView = std::make_unique<factor::compute::ArrowMarketDataView>(arrowPath);
            if (arrowView->instruments().empty()) {
                QMetaObject::invokeMethod(this, [this]() {
                    m_isRunning.store(false); emit isRunningChanged();
                    emit tuningFailed(QStringLiteral("数据集为空"));
                }, Qt::QueuedConnection);
                return;
            }
            dataSvc->setMarketView(arrowView.get());
            dataSvc->buildViewForFields({});
        }

        // ── 7. 构建基础 BacktestRequest ──
        BacktestRequest baseRequest = buildBaseRequest(strategyId, params);

        m_statusText = QStringLiteral("Tuning...");
        QMetaObject::invokeMethod(this, [this]() { emit statusChanged(); }, Qt::QueuedConnection);

        // ── 8. 创建 TrialEvaluator (每次 backtest 回调) ──
        auto evaluator = [&](const ParamSet& ps, std::int32_t trialIndex) -> TrialResult {
            TrialResult trial;
            trial.trialIndex = trialIndex;
            trial.params = ps;

            // 检查取消
            if (m_cancelRequested.load(std::memory_order_acquire)) {
                trial.errorMessage = "Cancelled by user";
                return trial;
            }

            // 构建覆写
            StrategyParamOverlay overlay = paramSetToOverlay(ps, ranges);
            BacktestRequest trialRequest = baseRequest;
            trialRequest.strategyParamOverlay = std::move(overlay);

            // 执行回测
            auto btResult = engine->backtest(trialRequest, dataSvc.get(), {},
                                              &m_cancelRequested);

            if (!btResult.success) {
                trial.errorMessage = btResult.errorMessage;
                return trial;
            }

            // 提取目标值
            trial.objectiveValue = extractObjectiveValue(btResult, metric);

            // 约束检查
            if (!objective.checkConstraints(btResult)) {
                trial.constraintViolated = true;
                trial.objectiveValue = -std::numeric_limits<double>::infinity();
            }

            return trial;
        };

        // ── 9. 进度回调 (每个 trial 完成后更新 QML) ──
        auto onProgress = [this](int completed, int total) {
            m_currentTrial = completed;
            m_progress = total > 0 ? static_cast<double>(completed) / total * 100.0 : 0.0;
            QMetaObject::invokeMethod(this, [this]() {
                emit currentTrialChanged();
                emit progressChanged();
            }, Qt::QueuedConnection);
        };

        // ── 10. 运行优化器 ──
        domain::optimization::GridSearchOptimizer optimizer;
        auto startTime = std::chrono::steady_clock::now();
        OptimizationResult optResult = optimizer.optimize(
            space, objective, evaluator, maxTrials, onProgress);
        auto endTime = std::chrono::steady_clock::now();
        optResult.elapsedSeconds =
            std::chrono::duration<double>(endTime - startTime).count();

        // ── 11. 检查是否被取消 ──
        if (m_cancelRequested.load(std::memory_order_acquire)) {
            QMetaObject::invokeMethod(this, [this]() {
                m_isRunning.store(false); emit isRunningChanged();
                emit tuningCancelled();
            }, Qt::QueuedConnection);
            return;
        }

        // ── 12. 返回结果 ──
        QVariantMap qResult = optimizationResultToMap(optResult);
        qResult["strategyId"] = QString::fromStdString(strategyId);
        m_statusText = QStringLiteral("Complete");
        m_progress = 100.0;

        QMetaObject::invokeMethod(this, [this, qResult]() {
            m_isRunning.store(false);
            emit isRunningChanged();
            emit statusChanged();
            emit progressChanged();
            emit tuningCompleted(qResult);
        }, Qt::QueuedConnection);

    } catch (const std::exception& e) {
        QMetaObject::invokeMethod(this, [this, msg = QString::fromStdString(e.what())]() {
            m_isRunning.store(false); emit isRunningChanged();
            emit tuningFailed(msg);
        }, Qt::QueuedConnection);
    } catch (...) {
        QMetaObject::invokeMethod(this, [this]() {
            m_isRunning.store(false); emit isRunningChanged();
            emit tuningFailed(QStringLiteral("Unknown error during tuning"));
        }, Qt::QueuedConnection);
    }
}
