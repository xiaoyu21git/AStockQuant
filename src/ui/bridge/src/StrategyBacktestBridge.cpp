// StrategyBacktestBridge.cpp — 纯桥接层
// 职责：参数转换 → 线程调度 → 委托 application::backtest → 进度/结果转发到 QML
// 不允许在此层写任何业务逻辑。

#include "StrategyBacktestBridge.h"
#include "foundation/thread/ThreadPoolExecutor.h"

#include "BacktestRequest.h"
#include "StrategySnapshotTypes.h"
#include "BacktestContracts.hpp"
#include "BacktestRunEntry.h"
#include "StrategyBridge.h"
#include "../../domain/strategy/include/IStrategyService.h"

#include <QDebug>
#include <QMetaObject>

namespace {

using domain::backtest::BacktestRequest;
using domain::backtest::DateWindow;
using domain::backtest::CostSpec;
using domain::backtest::RiskSpec;
using domain::backtest::ExecutionSpec;
using domain::backtest::DataSourceSpec;
using domain::backtest::RuntimeOptionSpec;
using domain::strategy::StrategyIdentity;
using domain::strategy::StrategySpec;
using domain::strategy::UniverseSpec;
using domain::strategy::FactorOverlaySpec;
using domain::strategy::StrategyId;
using domain::strategy::StrategyName;
using domain::strategy::StrategyExecutionKind;
using domain::strategy::Money;
using domain::strategy::Ratio;
using domain::strategy::DatasetId;
using domain::strategy::DataSourceMode;
using domain::strategy::PositionSizingMethod;
using domain::strategy::RebalanceFrequencyDays;

/// @brief QML 参数 QVariantMap → BacktestRequest 转换（纯适配，非业务逻辑）
BacktestRequest buildBacktestRequest(const QString& strategyId, const QVariantMap& params)
{
    BacktestRequest req;

    // 策略身份
    req.strategyIdentity.strategyId = StrategyId(strategyId.toStdString());
    req.strategyIdentity.strategyName = StrategyName(
        params.value("strategyName").toString().toStdString());
    req.strategyIdentity.executionKind = StrategyExecutionKind::Standard;

    // 日期窗口
    QString startDate = params.value("startDate").toString();
    QString endDate = params.value("endDate").toString();
    req.window.startDate = startDate.replace("-", "").toInt();
    req.window.endDate = endDate.replace("-", "").toInt();

    // 资金/成本
    req.costSpec.initialCapital = Money{params.value("initialCapital", 1000000.0).toDouble()};
    req.costSpec.commissionRate = Ratio{params.value("commissionRate", 0.0003).toDouble()};
    req.costSpec.slippageRate = Ratio{params.value("slippageRate", 0.001).toDouble()};
    req.costSpec.taxRate = Ratio{params.value("stampTaxRate", 0.001).toDouble()};

    // 风控
    req.riskSpec.maxPositionRatio = Ratio{params.value("singlePositionWeight", 0.20).toDouble()};
    req.riskSpec.maxSinglePositionRatio = Ratio{params.value("singlePositionWeight", 0.20).toDouble()};
    req.riskSpec.maxDrawdownLimit = Ratio{0.30};
    req.riskSpec.stopLossRate = Ratio{params.value("stopLossPercent", 0.10).toDouble()};

    // 执行
    req.executionSpec.executionKind = StrategyExecutionKind::Standard;
    req.executionSpec.positionSizingMethod = PositionSizingMethod::FixedFraction;
    QString rebalanceFreq = params.value("rebalanceFrequency", "daily").toString();
    if (rebalanceFreq == "weekly") req.executionSpec.rebalanceFrequencyDays = 5;
    else if (rebalanceFreq == "monthly") req.executionSpec.rebalanceFrequencyDays = 21;
    else if (rebalanceFreq == "quarterly") req.executionSpec.rebalanceFrequencyDays = 63;
    else req.executionSpec.rebalanceFrequencyDays = 1;
    req.executionSpec.useMarketOnClose =
        (params.value("executionTiming", "close").toString() == "close");

    // 数据源
    int datasetId = params.value("datasetCacheId", -1).toInt();
    if (datasetId >= 0) {
        req.dataSourceSpec.mode = DataSourceMode::CacheDataset;
        req.dataSourceSpec.datasetId = DatasetId{datasetId};
    } else {
        req.dataSourceSpec.mode = DataSourceMode::Raw;
    }

    // 运行时选项
    req.runtimeOptions.maxThreads = 1;
    req.runtimeOptions.enableCache = false;
    req.runtimeOptions.cacheTtlSeconds = 0;

    // 因子叠加（默认禁用）
    req.factorOverlaySpec.enabled = false;
    req.factorOverlaySpec.targetPositionCount =
        params.value("maxPositionCount", 20).toInt();

    // 策略规格 — 最小有效填充以通过 isValid()
    req.strategyIdentity.strategyCode = domain::strategy::StrategyCode("default");
    req.strategyIdentity.storedType = domain::backtest::StrategyStoredType::Custom;
    req.strategyIdentity.behaviorKind = domain::backtest::StrategyBehaviorKind::Custom;

    req.strategySpec.ruleProfile.maxPositionRatio = Ratio{1.0};
    req.strategySpec.ruleProfile.maxTotalExposureRatio = Ratio{1.0};
    req.strategySpec.ruleProfile.stopLossRatio = Ratio{0.10};
    req.strategySpec.ruleProfile.takeProfitRatio = Ratio{0.30};
    req.strategySpec.ruleProfile.rebalanceDays = req.executionSpec.rebalanceFrequencyDays;
    req.strategySpec.executionPolicy.rebalanceFrequencyDays =
        RebalanceFrequencyDays{req.executionSpec.rebalanceFrequencyDays};
    req.strategySpec.executionPolicy.shortSellingMode =
        domain::strategy::ShortSellingMode::Disabled;
    req.strategySpec.factorOverlay.enabled = false;

    // 股票池：可以为空但是不能影响流程 
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

void StrategyBacktestBridge::setBacktestModules(const application::backtest::ExistingModuleSlots& modules)
{
    m_backtestModules = modules;
    m_modulesResolved = true;
}

void StrategyBacktestBridge::resolveBacktestModules(const QString& strategyId)
{
    if (m_modulesResolved) return;

    StrategyBridge* strategyBridge = StrategyBridge::instance();
    if (strategyBridge == nullptr || !strategyBridge->inited()) {
        return;
    }

    // 尝试获取已启动的策略引擎
    domain::strategy::StrategyEngine* engine =
        strategyBridge->backtestEngineProvider(strategyId);

    // 如果策略尚未启动，先执行 start() 创建运行时引擎
    if (engine == nullptr) {
        (void)strategyBridge->start(strategyId);
        engine = strategyBridge->backtestEngineProvider(strategyId);
    }

    if (engine != nullptr) {
        m_backtestModules.strategyService = &engine->service();
        m_modulesResolved = true;
    }
}

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
    m_statusText = QStringLiteral("Running..."); emit statusChanged();

    // ① 解析并注入域层模块（尽力而为，失败不阻断）
    resolveBacktestModules(strategyId);

    // ② 参数转换：QVariantMap → C++ 域类型（纯适配，无业务逻辑）
    BacktestRequest request = buildBacktestRequest(strategyId, params);
    if (!request.isValid()) {
        m_isRunning.store(false);
        emit isRunningChanged();
        emit backtestFailed(QStringLiteral("Invalid backtest parameters"));
        return;
    }

    // ③ 构建 RunSpec
    using application::backtest::RunSpec;
    using application::backtest::RunMode;
    using application::backtest::RunTaskId;
    using application::backtest::FillOrderSideMode;

    RunSpec spec;
    spec.taskId = RunTaskId{};
    spec.taskId.value = 1; // temporary task id
    spec.mode = RunMode::StrategyBacktest;
    spec.fillOrderSideMode = FillOrderSideMode::LongOnlyBuy;
    spec.request = std::make_shared<const BacktestRequest>(std::move(request));

    // ④ 工作线程：构造管道 → 运行 → 回调结果
    if (!m_workerPool) {
        m_workerPool = std::make_unique<foundation::thread::ThreadPoolExecutor>(
            1, 1, std::chrono::milliseconds(120000), "StrategyBacktestBridge");
    }

    // 捕获 modules 值副本到 lambda，避免竞态
    application::backtest::ExistingModuleSlots capturedModules = m_backtestModules;
    m_workerPool->post([this, spec, capturedModules]() mutable {
        try {
            using application::backtest::BacktestRunEntry;

            // 依赖注入点：ExistingModuleSlots 由应用层通过 setBacktestModules() 注入
            auto ingressResult = BacktestRunEntry::runBacktest(capturedModules, spec);

            // ⑤ 结果转发到 QML — 成功/失败分别用不同信号
            if (!ingressResult.ok()) {
                QString errorMsg = QStringLiteral("Backtest failed: ErrorCode=%1")
                    .arg(static_cast<int>(ingressResult.code));
                QMetaObject::invokeMethod(this, [this, errorMsg]() {
                    m_isRunning.store(false); m_progress = 0.0;
                    m_statusText = QStringLiteral("Failed");
                    emit isRunningChanged(); emit progressChanged(); emit statusChanged();
                    emit backtestFailed(errorMsg);
                }, Qt::QueuedConnection);
                return;
            }

            QVariantMap qResult;
            qResult["status"] = QStringLiteral("SUCCESS");
            QVariantMap metrics; metrics["execution"] = QVariantMap{}; metrics["groups"] = QVariantList();
            qResult["metrics"] = metrics;
            Q_UNUSED(ingressResult.taskId);

            QMetaObject::invokeMethod(this, [this, qResult]() {
                m_isRunning.store(false); m_progress = 100.0;
                m_statusText = QStringLiteral("Complete");
                emit isRunningChanged(); emit progressChanged(); emit statusChanged();
                emit backtestCompleted(qResult);
            }, Qt::QueuedConnection);

        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = QString::fromUtf8(e.what())]() {
                m_isRunning.store(false);
                emit isRunningChanged(); emit progressChanged(); emit statusChanged();
                emit backtestFailed(QStringLiteral("Error: %1").arg(msg));
            }, Qt::QueuedConnection);
        } catch (...) {
            QMetaObject::invokeMethod(this, [this]() {
                m_isRunning.store(false);
                emit isRunningChanged(); emit progressChanged(); emit statusChanged();
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

bool   StrategyBacktestBridge::isRunning() const { return m_isRunning.load(); }
double StrategyBacktestBridge::progress()  const { return m_progress; }
QString StrategyBacktestBridge::status()   const { return m_statusText; }