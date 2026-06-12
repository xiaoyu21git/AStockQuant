#include "../../strategies/include/MultiFactorSelectionStrategy.h"

#include "../include/IStrategyService.h"
#include "../include/RuntimeStrategyFactory.h"
#include "../include/RuntimeFactorSvc.h"
#include "../../../infrastructure/include/database/ISqlDatabase.h"
#include "../../../infrastructure/include/database/NativeMySQLConnectionPool.h"
#include "../../backtest/include/BacktestRequest.h"
#include "../../factor/include/factor_compute/FactorEngine.h"
#include "../../factor/include/factor_compute/IMarketDataView.h"
#include "foundation/json/json_facade.h"
#include "foundation/thread/thread_pool.hpp"
#include "foundation/thread/ThreadPoolExecutor.h"

#include <cstdlib>
#include <chrono>
#include <exception>
#include <string>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace domain::strategy {

namespace {
std::vector<::domain::strategies::FactorId> parseFactorIds(const std::string& json) {
    std::vector<::domain::strategies::FactorId> ids;
    if (json.empty()) return ids;
    try {
        auto root = foundation::json::JsonFacade::parse(json);
        for (std::size_t i = 0; i < root.size(); ++i) {
            ids.push_back(static_cast<::domain::strategies::FactorId>(root.at(i).asInt()));
        }
    } catch(...) {}
    return ids;
}
} // anonymous namespace

std::unique_ptr<StrategyEngine> StrategyEngine::fromDb(const std::string& strategyId,
                                                         std::shared_ptr<IFactorSvc> factorSvc)
{
    auto& pool = astock::database::NativeMySQLConnectionPool::instance();
    if (!pool.isInitialized()) return nullptr;

    auto db = pool.getConnection();
    if (!db || !db->isOpen()) return nullptr;

    // 查询策略定义表获取参数
    auto result = db->executeQuery(
        "SELECT strategy_name, description, behavior_kind, parameters, factor_ids "
        "FROM strategy_definitions WHERE strategy_id = ?",
        {strategyId});
    if (result.isEmpty()) return nullptr;

    const auto& row = result.getRow(0);
    StrategyCreationParams params;
    params.strategyId = strategyId;
    params.strategyName = row.getString("strategy_name");
    params.description = row.getString("description");
    params.behaviorKind = static_cast<::domain::strategies::StrategyBehaviorKind>(
        row.getInt("behavior_kind", 0));

    std::string paramJson = row.getString("parameters");
    if (!paramJson.empty()) {
        auto root = foundation::json::JsonFacade::parse(paramJson);
        params.topN = root.has("topN") ? root.get("topN").asInt() : 0;
        params.allowShort = root.has("allowShort") && root.get("allowShort").asBool();
        params.maxPositions = root.has("maxPositions") ? root.get("maxPositions").asInt() : 100;
        params.maxWeightPerStock = root.has("maxWeightPerStock") ? root.get("maxWeightPerStock").asDouble() : 0.1;
        params.minWeightPerStock = root.has("minWeightPerStock") ? root.get("minWeightPerStock").asDouble() : 0.0;
        params.maxOrderQuantity = root.has("maxOrderQuantity") ? static_cast<std::uint32_t>(root.get("maxOrderQuantity").asInt()) : 100U;
    }

    params.factorIds = parseFactorIds(row.getString("factor_ids"));

    // 注入因子回调：使用 buildFactorCallbacks + RuntimeFactorSvc
    if (factorSvc && !params.factorIds.empty()) {
        auto cbs = buildFactorCallbacks(params.factorIds, std::move(factorSvc));
        params.onIncremental = std::move(cbs.updateIncremental);
        params.onBatch       = std::move(cbs.updateBatch);
        params.onCopySnapshots = std::move(cbs.copySnapshots);
    }

    auto engine = fromParams(params);
    if (!engine) return nullptr;

    // 构建并注册运行时策略实例
    if (!params.factorIds.empty()) {
        ::domain::strategies::StrategyCommonConfig commonCfg;
        commonCfg.allowShort          = params.allowShort;
        commonCfg.maxPositions         = params.maxPositions;
        commonCfg.maxWeightPerStock    = params.maxWeightPerStock;
        commonCfg.minWeightPerStock    = params.minWeightPerStock;
        commonCfg.weightScheme         = ::domain::strategies::WeightScheme::EQUAL;
        commonCfg.rebalanceFrequency   = ::domain::strategies::RebalanceFrequency::DAILY;

        ::domain::strategies::StrategyMetadata meta;
        meta.name         = params.strategyName;
        meta.description  = params.description;
        meta.behaviorKind = params.behaviorKind;
        meta.factorIds    = params.factorIds;
        meta.enabled      = true;

        ::domain::strategies::MultiFactorSelectionStrategySpec spec;
        spec.topN           = params.topN > 0 ? params.topN : params.maxPositions;
        spec.industryNeutral = false;

        auto strategyDef = std::make_shared<::domain::strategies::MultiFactorSelectionStrategy>(
            commonCfg, meta, spec);

        constexpr StrategyInstanceId kDefaultInstanceId = 1;
        RuntimeStrategyContext ctx(kDefaultInstanceId,
                                    1,                    // snapshotVersion
                                    params.maxOrderQuantity,
                                    params.maxWeightPerStock,
                                    false);               // autoExecutionEnabled

        auto runtimeStrategy = createMultiFactorSelectionRuntimeStrategy(
            strategyDef, kDefaultInstanceId, CallbackRuntimeFactorServiceAdapter::Callbacks{});

        const auto regResult = engine->registerStrategy(std::move(runtimeStrategy), ctx);
        if (!regResult.isOk()) {
            return nullptr;
        }
    }

    return engine;
}

StrategyEngine::Builder StrategyEngine::builder()
{
    return Builder();
}

std::unique_ptr<StrategyEngine> StrategyEngine::fromParams(const StrategyCreationParams& params)
{
    // 使用 Builder 模式构建完整引擎
    CallbackRuntimeFactorServiceAdapter::Callbacks cbs;
    cbs.updateIncremental = params.onIncremental;
    cbs.updateBatch      = params.onBatch;
    cbs.copySnapshots    = params.onCopySnapshots;

    return builder()
        .withFactorCallbacks(std::move(cbs))
        .maxStrategies(params.maxPositions)
        .build();
}

StrategyEngine::StrategyEngine(std::unique_ptr<IRuntimeFactorService> factorService,
                               std::unique_ptr<IRuleEvaluationService> ruleEvaluationService,
                               std::unique_ptr<IStrategyService> strategyService)
    : factorService_(std::move(factorService))
    , ruleEvaluationService_(std::move(ruleEvaluationService))
    , strategyService_(std::move(strategyService))
    , asyncExecutor_(foundation::thread::ThreadPoolFactory::create_cpu_aware())
{
}

StrategyServiceFlowResult StrategyEngine::registerStrategy(
    std::shared_ptr<IRuntimeStrategy> strategy,
    const RuntimeStrategyContext& context)
{
    return strategyService_->registerStrategy(std::move(strategy), context);
}

StrategyServiceFlowResult StrategyEngine::registerStrategies(
    const std::vector<std::shared_ptr<IRuntimeStrategy>>& strategies,
    const std::vector<RuntimeStrategyContext>& contexts)
{
    if (strategies.size() != contexts.size()) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidInput);
    }
    for (std::size_t i = 0; i < strategies.size(); ++i) {
        const StrategyServiceFlowResult result =
            strategyService_->registerStrategy(strategies[i], contexts[i]);
        if (!result.isOk()) {
            return result;
        }
    }
    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
}

StrategyServiceFlowResult StrategyEngine::start()
{
    return strategyService_->start();
}

StrategyServiceFlowResult StrategyEngine::pause()
{
    return strategyService_->pause();
}

StrategyServiceFlowResult StrategyEngine::resume()
{
    return strategyService_->resume();
}

StrategyServiceFlowResult StrategyEngine::stop()
{
    return strategyService_->stop();
}

std::optional<std::vector<OrderRequest>> StrategyEngine::step(const MarketDataPoint& marketDataPoint)
{
    return collectOrders(strategyService_->onMarketDataPoint(marketDataPoint));
}

std::optional<std::vector<OrderRequest>> StrategyEngine::stepBatch(
    const std::vector<MarketDataPoint>& batch)
{
    return collectOrders(strategyService_->onMarketDataBatch(batch));
}

std::future<std::optional<std::vector<OrderRequest>>> StrategyEngine::stepAsync(
    const MarketDataPoint& marketDataPoint)
{
    return foundation::thread::async(asyncExecutor_, [this, marketDataPoint]() {
        return step(marketDataPoint);
    });
}

std::future<std::optional<std::vector<OrderRequest>>> StrategyEngine::stepBatchAsync(
    std::vector<MarketDataPoint> batch)
{
    return foundation::thread::async(asyncExecutor_, [this, batch = std::move(batch)]() {
        return stepBatch(batch);
    });
}

IStrategyService& StrategyEngine::service() noexcept
{
    return *strategyService_;
}

const IStrategyService& StrategyEngine::service() const noexcept
{
    return *strategyService_;
}

void StrategyEngine::setAsyncExecutor(std::shared_ptr<foundation::thread::IExecutor> executor)
{
    if (executor) {
        asyncExecutor_ = std::move(executor);
    }
}

void StrategyEngine::setFactorSvc(std::shared_ptr<IFactorSvc> svc)
{
    m_factorSvc = std::move(svc);
}

std::optional<std::vector<OrderRequest>> StrategyEngine::collectOrders(
    const StrategyServiceFlowResult& flowResult)
{
    if (!flowResult.isOk()) {
        return std::nullopt;
    }

    if (strategyService_->pendingOrderCount() == 0) {
        return std::nullopt;
    }

    std::vector<OrderRequest> output;
    strategyService_->copyPendingOrders(output);
    return output;
}

// ─── 实盘异步实现 ───

void StrategyEngine::enqueueMarketData(const MarketDataPoint& marketDataPoint)
{
    if (!m_loopRunning.load(std::memory_order_acquire)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_mdpQueue.push(marketDataPoint);
    }
    m_queueCv.notify_one();
}

void StrategyEngine::startLiveLoop()
{
    if (m_loopRunning.load(std::memory_order_acquire)) {
        return; // 已经运行
    }
    if (!m_dedicatedExecutor) {
        m_dedicatedExecutor = std::make_shared<foundation::thread::ThreadPoolExecutor>(
            1, 1, std::chrono::seconds(60), "StrategyEngineLiveLoop");
    }
    m_loopRunning.store(true, std::memory_order_release);

    // 提交 drainQueue 到专属线程（线程池只有 1 个 worker，独占该线程）
    m_dedicatedExecutor->post([this]() {
        drainQueue();
    });
}

void StrategyEngine::stopLiveLoop()
{
    if (!m_loopRunning.load(std::memory_order_acquire)) {
        return;
    }
    m_loopRunning.store(false, std::memory_order_release);
    m_queueCv.notify_one();

    if (m_dedicatedExecutor) {
        m_dedicatedExecutor->shutdown(false);
        // 等待线程池终止（最多 5 秒）
        m_dedicatedExecutor->awaitTermination(std::chrono::milliseconds(5000));
    }
}

void StrategyEngine::setOrderListener(IOrderListener* listener)
{
    m_orderListener = listener;
}

void StrategyEngine::drainQueue()
{
    while (m_loopRunning.load(std::memory_order_acquire)) {
        MarketDataPoint mdp;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCv.wait(lock, [this]() {
                return !m_mdpQueue.empty() || !m_loopRunning.load(std::memory_order_acquire);
            });
            if (!m_loopRunning.load(std::memory_order_acquire)) {
                return;
            }
            mdp = m_mdpQueue.front();
            m_mdpQueue.pop();
        }

        try {
            auto orders = step(mdp);
            if (orders.has_value() && m_orderListener) {
                m_orderListener->onOrders(*orders);
            }
        } catch (const std::exception& e) {
            // 单引擎异常不崩溃，故障隔离
            (void)e;
            // 继续处理下一条行情
        }
    }
}

StrategyEngine::Builder::Builder() = default;

StrategyEngine::Builder& StrategyEngine::Builder::withFactorService(
    std::unique_ptr<IRuntimeFactorService> factorService)
{
    factorService_ = std::move(factorService);
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::withRuleEvaluationService(
    std::unique_ptr<IRuleEvaluationService> ruleEvaluationService)
{
    ruleEvaluationService_ = std::move(ruleEvaluationService);
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::withFactorCallbacks(
    CallbackRuntimeFactorServiceAdapter::Callbacks callbacks)
{
    factorService_ = std::make_unique<CallbackRuntimeFactorServiceAdapter>(std::move(callbacks));
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::withOrderSink(IRuntimeOrderSink& orderSink)
{
    orderSink_ = &orderSink;
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::withDiagnosticsSink(IDiagnosticsSink& diagnosticsSink)
{
    diagnosticsSink_ = &diagnosticsSink;
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::withOrderBuilder(const IOrderBuilder& orderBuilder)
{
    orderBuilder_ = &orderBuilder;
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::withAsyncExecutor(
    std::shared_ptr<foundation::thread::IExecutor> executor)
{
    asyncExecutor_ = std::move(executor);
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::maxStrategies(StrategyCount value)
{
    plan_ = StrategyServiceExecutionPlan(
        value,
        plan_.maxMarketDataPerBatch(),
        plan_.maxSignalPerBatch(),
        plan_.maxRuleResultPerBatch());
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::maxSignalsPerBatch(StrategyCount value)
{
    plan_ = StrategyServiceExecutionPlan(
        plan_.maxStrategyCount(),
        plan_.maxMarketDataPerBatch(),
        value,
        plan_.maxRuleResultPerBatch());
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::maxRuleResultsPerBatch(StrategyCount value)
{
    plan_ = StrategyServiceExecutionPlan(
        plan_.maxStrategyCount(),
        plan_.maxMarketDataPerBatch(),
        plan_.maxSignalPerBatch(),
        value);
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::maxMarketDataPerBatch(StrategyCount value)
{
    plan_ = StrategyServiceExecutionPlan(
        plan_.maxStrategyCount(),
        value,
        plan_.maxSignalPerBatch(),
        plan_.maxRuleResultPerBatch());
    return *this;
}

std::unique_ptr<StrategyEngine> StrategyEngine::Builder::build()
{
    if (!factorService_) {
        // 运行时主链必须显式注入真实因子服务，不允许默认伪服务回退。
        std::abort();
    }
    std::unique_ptr<IRuntimeFactorService> factorService = std::move(factorService_);

    std::unique_ptr<IRuleEvaluationService> ruleEvaluationService;
    if (ruleEvaluationService_) {
        ruleEvaluationService = std::move(ruleEvaluationService_);
    } else {
        ruleEvaluationService = std::make_unique<LocalRuleEvaluationService>();
    }

    std::unique_ptr<IStrategyService> strategyService;
    if (orderSink_) {
        strategyService = std::make_unique<StrategyService>(
            *factorService,
            *ruleEvaluationService,
            *orderSink_);
    } else {
        strategyService = std::make_unique<StrategyService>(*factorService, *ruleEvaluationService);
    }

    const StrategyServiceFlowResult configureResult = strategyService->configureExecutionPlan(plan_);
    if (!configureResult.isOk()) {
        std::abort();
    }
    strategyService->setDiagnosticsSink(diagnosticsSink_);
    if (orderBuilder_) {
        strategyService->setOrderBuilder(orderBuilder_);
    }
    const StrategyServiceFlowResult startResult = strategyService->start();
    if (!startResult.isOk()) {
        std::abort();
    }

    auto engine = std::make_unique<StrategyEngine>(
        std::move(factorService),
        std::move(ruleEvaluationService),
        std::move(strategyService));
    if (asyncExecutor_) {
        engine->setAsyncExecutor(std::move(asyncExecutor_));
    }
    return engine;
}

StrategyBacktestResult StrategyEngine::backtest(
    const domain::backtest::BacktestRequest& req,
    const std::string& datasetJson,
    const std::function<void(double)>& onProgress)
{
    StrategyBacktestResult result;

    // 1. 加载数据
    factor::compute::BacktestDataService dataSvc;
    dataSvc.storeRawJson(datasetJson);
    dataSvc.buildViewForFields({});
    auto batch = dataSvc.loadBatch(0);
    const auto* view = batch.marketView;
    if (!view) {
        result.errorMessage = "Failed to load market data view";
        return result;
    }

    const auto& dates = view->dates();
    const auto& instruments = view->instruments();
    const auto closeMat = view->close();
    const auto volumeMat = view->volume();
    const int totalDays = static_cast<int>(dates.size());
    const int colCount = static_cast<int>(instruments.size());

    if (totalDays == 0 || colCount == 0) {
        result.errorMessage = "Empty market data";
        return result;
    }

    // 2. 账户初始化
    double cash = req.costSpec.initialCapital.value;
    std::unordered_map<std::uint32_t, int> positions;  // instrumentId → 持仓数量
    std::vector<double> equityCurve;
    equityCurve.reserve(totalDays);

    const double commissionRate = req.costSpec.commissionRate.value;
    const double taxRate = req.costSpec.taxRate.value;
    const double slippageRate = req.costSpec.slippageRate.value;

    // 3. 逐日驱动
    for (int r = 0; r < totalDays; ++r) {
        // 构建当天的 MarketDataPoint 批
        std::vector<MarketDataPoint> mdpBatch;
        mdpBatch.reserve(colCount);
        const std::size_t rowOffset = static_cast<std::size_t>(r) * static_cast<std::size_t>(colCount);
        for (int c = 0; c < colCount; ++c) {
            const std::uint32_t instrumentId = instruments[static_cast<std::size_t>(c)].value;
            const double price = static_cast<double>(closeMat.data[rowOffset + static_cast<std::size_t>(c)]);
            const double volume = static_cast<double>(volumeMat.data[rowOffset + static_cast<std::size_t>(c)]);
            if (price > 0.0) {
                mdpBatch.emplace_back(
                    InstrumentId(instrumentId),
                    price,
                    volume,
                    dates[static_cast<std::size_t>(r)].value);
            }
        }

        // 调引擎管线
        auto orders = stepBatch(mdpBatch);

        // 模拟成交
        if (orders.has_value()) {
            for (const auto& order : *orders) {
                const std::uint32_t instrumentId = order.instrumentId().value();
                const std::uint32_t quantity = order.quantity();
                // 找该标的收盘价
                double closePrice = 0.0;
                for (const auto& mdp : mdpBatch) {
                    if (mdp.instrumentId().value() == instrumentId) {
                        closePrice = mdp.lastPrice();
                        break;
                    }
                }
                if (closePrice <= 0.0) continue;

                const double notional = closePrice * static_cast<double>(quantity);
                const double commission = notional * commissionRate;
                const double slippage = notional * slippageRate;

                if (order.side() == RuntimeOrderSide::Buy) {
                    const double cost = notional + commission + slippage;
                    if (cash >= cost) {
                        cash -= cost;
                        positions[instrumentId] += static_cast<int>(quantity);
                    }
                } else {
                    auto it = positions.find(instrumentId);
                    const int held = (it != positions.end()) ? it->second : 0;
                    const int sellQty = std::min(static_cast<int>(quantity), held);
                    if (sellQty > 0) {
                        const double sellNotional = closePrice * static_cast<double>(sellQty);
                        const double tax = sellNotional * taxRate;
                        const double income = sellNotional - commission - slippage - tax;
                        cash += income;
                        positions[instrumentId] = held - sellQty;
                        if (positions[instrumentId] <= 0) {
                            positions.erase(instrumentId);
                        }
                    }
                }
            }
        }

        // 计算当日权益
        double equity = cash;
        for (const auto& [instId, qty] : positions) {
            if (qty > 0) {
                double closePrice = 0.0;
                for (const auto& mdp : mdpBatch) {
                    if (mdp.instrumentId().value() == instId) {
                        closePrice = mdp.lastPrice();
                        break;
                    }
                }
                equity += closePrice * static_cast<double>(qty);
            }
        }
        equityCurve.push_back(equity);

        // 进度
        if (onProgress) {
            const double progress = 100.0 * static_cast<double>(r + 1) / static_cast<double>(totalDays);
            onProgress(progress);
        }
    }

    // 4. 指标计算
    if (!equityCurve.empty()) {
        const double initialCapital = req.costSpec.initialCapital.value;
        result.metrics.totalReturn = (equityCurve.back() - initialCapital) / initialCapital;

        // 年化收益
        const double years = static_cast<double>(totalDays) / 250.0;
        if (years > 0.0 && initialCapital > 0.0) {
            result.metrics.annualizedReturn =
                std::pow(equityCurve.back() / initialCapital, 1.0 / years) - 1.0;
        }

        // 日收益率序列
        std::vector<double> dailyReturns;
        dailyReturns.reserve(equityCurve.size() - 1);
        for (std::size_t i = 1; i < equityCurve.size(); ++i) {
            if (equityCurve[i - 1] > 0.0) {
                dailyReturns.push_back(equityCurve[i] / equityCurve[i - 1] - 1.0);
            }
        }

        // 波动率
        if (!dailyReturns.empty()) {
            double sum = 0.0;
            for (double ret : dailyReturns) sum += ret;
            const double mean = sum / static_cast<double>(dailyReturns.size());
            double sqSum = 0.0;
            for (double ret : dailyReturns) sqSum += (ret - mean) * (ret - mean);
            const double stdDev = std::sqrt(sqSum / static_cast<double>(dailyReturns.size()));
            result.metrics.volatility = stdDev * std::sqrt(250.0);
            if (result.metrics.volatility > 0.0) {
                result.metrics.sharpeRatio = result.metrics.annualizedReturn / result.metrics.volatility;
            }
        }

        // 最大回撤
        double peak = equityCurve[0];
        double maxDD = 0.0;
        for (double eq : equityCurve) {
            if (eq > peak) peak = eq;
            const double dd = (peak > 0.0) ? (peak - eq) / peak : 0.0;
            if (dd > maxDD) maxDD = dd;
        }
        result.metrics.maxDrawdown = maxDD;
    }

    result.success = true;
    return result;
}

} // namespace domain::strategy
