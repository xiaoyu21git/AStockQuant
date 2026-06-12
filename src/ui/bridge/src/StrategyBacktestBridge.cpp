// StrategyBacktestBridge.cpp — 纯桥接层
// 职责：参数转换 → 线程调度 → 结果转发到 QML
// 不允许在此层写任何业务逻辑。

#include "StrategyBacktestBridge.h"
#include "foundation/thread/ThreadPoolExecutor.h"
#include "foundation/Utils/Timestamp.h"

#include "BacktestRequest.h"
#include "StrategyBridge.h"
#include "DataServiceCache.h"
#include "FactorService.h"
#include "../../domain/factor/include/FactorInstanceManager.h"
#include "../../domain/strategy/include/IStrategyService.h"
#include "../../domain/strategy/include/StrategyManager.h"
#include "../../domain/strategy/include/RuntimeFactorSvc.h"

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

    req.costSpec.initialCapital = Money{params.value("initialCapital", 1000000.0).toDouble()};
    req.costSpec.commissionRate = Ratio{params.value("commissionRate", 0.0003).toDouble()};
    req.costSpec.slippageRate = Ratio{params.value("slippageRate", 0.001).toDouble()};
    req.costSpec.taxRate = Ratio{params.value("stampTaxRate", 0.001).toDouble()};

    req.riskSpec.maxPositionRatio = Ratio{params.value("singlePositionWeight", 0.20).toDouble()};
    req.riskSpec.maxSinglePositionRatio = Ratio{params.value("singlePositionWeight", 0.20).toDouble()};
    req.riskSpec.maxDrawdownLimit = Ratio{0.30};
    req.riskSpec.stopLossRate = Ratio{params.value("stopLossPercent", 0.10).toDouble()};

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

    req.universeSpec.universeMode = domain::strategy::UniverseMode::ExplicitSymbols;
    req.universeSpec.explicitSymbols.push_back(domain::strategy::SymbolCode("000001"));
    req.strategySpec.strategyScopeContext.universe.universeMode =
        domain::strategy::UniverseMode::ExplicitSymbols;
    req.strategySpec.strategyScopeContext.universe.explicitSymbols.push_back(
        domain::strategy::SymbolCode("000001"));

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
    m_progress = 0.0; emit progressChanged();
    m_statusText = QStringLiteral("Running..."); emit statusChanged();

    BacktestRequest request = buildBacktestRequest(strategyId, params);
    if (!request.isValid()) {
        m_isRunning.store(false);
        emit backtestFailed(QStringLiteral("Invalid backtest parameters"));
        return;
    }

    // 获取或创建引擎（复用 StrategyBridge::start() 的注入逻辑）
    auto& mgr = domain::strategy::StrategyManager::instance();
    auto* engine = mgr.get(strategyId.toStdString());
    if (!engine) {
        std::shared_ptr<domain::strategy::IFactorSvc> factorSvc;
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
                factorSvc = std::make_shared<domain::strategy::RuntimeFactorSvc>(
                    *instanceMgr,
                    std::move(symbolResolver),
                    std::move(factorNameResolver));
            }
        }
        engine = mgr.createEngine(strategyId.toStdString(), std::move(factorSvc));
    }
    if (!engine) {
        m_isRunning.store(false);
        emit backtestFailed(QStringLiteral("Strategy engine not available"));
        return;
    }

    // 获取数据集 JSON
    const int datasetId = params.value("datasetCacheId", -1).toInt();
    std::string datasetJson;
    if (datasetId >= 0) {
        QVariantList data = DataServiceCache::getInstance().getDataSetById(datasetId);
        if (!data.isEmpty()) {
            QJsonDocument doc(QJsonArray::fromVariantList(data));
            datasetJson = doc.toJson(QJsonDocument::Compact).toStdString();
        }
    }
    if (datasetJson.empty()) {
        m_isRunning.store(false);
        emit backtestFailed(QStringLiteral("No backtest dataset selected"));
        return;
    }

    if (!m_workerPool) {
        m_workerPool = std::make_unique<foundation::thread::ThreadPoolExecutor>(
            1, 1, std::chrono::milliseconds(120000), "StrategyBacktestBridge");
    }

    m_workerPool->post([this, engine, request = std::move(request), datasetJson = std::move(datasetJson)]() {
        try {
            auto result = engine->backtest(request, datasetJson,
                [this](double progress) {
                    QMetaObject::invokeMethod(this, [this, progress]() {
                        m_progress = progress;
                        emit progressChanged();
                    }, Qt::QueuedConnection);
                });

            if (!result.success) {
                QMetaObject::invokeMethod(this, [this, msg = QString::fromStdString(result.errorMessage)]() {
                    m_isRunning.store(false);
                    emit backtestFailed(msg);
                }, Qt::QueuedConnection);
                return;
            }

            // 序列化结果到 QVariantMap
            QVariantMap qResult;
            qResult["status"] = QStringLiteral("SUCCESS");
            QVariantMap metricsMap;
            metricsMap["totalReturn"] = result.metrics.totalReturn;
            metricsMap["annualizedReturn"] = result.metrics.annualizedReturn;
            metricsMap["volatility"] = result.metrics.volatility;
            metricsMap["sharpeRatio"] = result.metrics.sharpeRatio;
            metricsMap["maxDrawdown"] = result.metrics.maxDrawdown;
            metricsMap["sortinoRatio"] = result.metrics.sortinoRatio;
            metricsMap["calmarRatio"] = result.metrics.calmarRatio;
            metricsMap["winRate"] = result.metrics.winRate;
            metricsMap["profitFactor"] = result.metrics.profitFactor;
            metricsMap["alpha"] = result.metrics.alpha;
            metricsMap["beta"] = result.metrics.beta;
            metricsMap["informationRatio"] = result.metrics.informationRatio;
            metricsMap["trackingError"] = result.metrics.trackingError;
            qResult["metrics"] = metricsMap;

            QMetaObject::invokeMethod(this, [this, qResult]() {
                m_isRunning.store(false); m_progress = 100.0;
                m_statusText = QStringLiteral("Complete");
                emit isRunningChanged(); emit progressChanged(); emit statusChanged();
                emit backtestCompleted(qResult);
            }, Qt::QueuedConnection);

        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = QString::fromUtf8(e.what())]() {
                m_isRunning.store(false);
                emit backtestFailed(QStringLiteral("Error: %1").arg(msg));
            }, Qt::QueuedConnection);
        } catch (...) {
            QMetaObject::invokeMethod(this, [this]() {
                m_isRunning.store(false);
                emit backtestFailed(QStringLiteral("Unknown error"));
            }, Qt::QueuedConnection);
        }
    });
}

void StrategyBacktestBridge::cancelBacktest()
{
    m_isRunning.store(false);
    emit backtestCancelled();
}

bool StrategyBacktestBridge::isRunning() const { return m_isRunning.load(); }
double StrategyBacktestBridge::progress() const { return m_progress; }
QString StrategyBacktestBridge::status() const { return m_statusText; }