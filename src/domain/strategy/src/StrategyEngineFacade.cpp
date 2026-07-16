#include "../../strategies/include/MultiFactorSelectionStrategy.h"

#include "../include/IStrategyService.h"
#include "../include/NonFactorStrategy.h"
#include "../include/RuntimeStrategyFactory.h"
#include "../include/RuntimeFactorSvc.h"
#include "../../../infrastructure/include/database/ISqlDatabase.h"
#include "../../../infrastructure/include/database/NativePgConnectionPool.h"
#include "../../../infrastructure/include/database/DatabaseConfig.h"
#include "../../../infrastructure/include/database/MarketDataRepository.h"
#include "../../../infrastructure/include/database/OrderRecorder.h"
#include "../../backtest/include/BacktestRequest.h"
#include "../../backtest/include/BacktestFillSimulator.h"
#include "../../factor/include/factor_compute/FactorEngine.h"
#include "../../factor/include/factor_compute/IMarketDataView.h"
#include "../../factor/include/factor_compute/CachedMarketDataView.h"
#include "../../factor/include/factor_compute/MarketDataViewHistoricalAdapter.h"
#include "../../factor/include/FactorMetricsCalculator.h"
#include "../../trading/TradingTypes.h"
#include "../include/EventRiskSubscriber.h"
#include "../include/RiskEvaluator.h"
#include "../include/RiskManager.h"
#include "../../../engine/include/AccountEngine.h"
#include "../../../engine/include/GmSessionEngine.h"
#include "../../../thirdparty/gmsdk/gmapi.h"
#include <cstdio>
#include "MarketDataService.h"

#include "foundation/json/json_facade.h"
#include "foundation/market/AStockSymbol.h"
#include "foundation/log/logging.hpp"
#include "foundation/thread/thread_pool.hpp"
#include "foundation/thread/ThreadPoolExecutor.h"
#include "foundation/Utils/Timestamp.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <exception>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace domain::strategy {

namespace {

/// @brief 生成客户端幂等订单ID (纳秒时间戳 + 原子计数器)
std::string generateClOrdId() {
    static std::atomic<uint64_t> s_counter{0};
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    uint64_t seq = s_counter.fetch_add(1, std::memory_order_relaxed);
    std::ostringstream oss;
    oss << std::hex << now << "_" << seq;
    return oss.str();
}

/// @brief 非因子策略使用的空因子服务 — 所有操作均为 no-op
class NoOpFactorService final : public IRuntimeFactorService {
public:
    [[nodiscard]] StrategyServiceFlowResult updateIncremental(const MarketDataPoint&) override {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
    }
    [[nodiscard]] StrategyServiceFlowResult updateBatch(const std::vector<MarketDataPoint>&) override {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
    }
    void copySnapshots(std::vector<RuntimeFactorSnapshot>& output) const override {
        output.clear();
    }
};

std::vector<std::string> parseFactorIds(const std::string& json) {
    std::vector<std::string> ids;
    if (json.empty()) return ids;
    try {
        auto root = foundation::json::JsonFacade::parse(json);
        for (std::size_t i = 0; i < root.size(); ++i) {
            ids.push_back(root.at(i).asString());
        }
    } catch(...) {}
    return ids;
}
} // anonymous namespace

std::unique_ptr<StrategyEngine> StrategyEngine::fromDb(const std::string& strategyId,
                                                         std::unique_ptr<IRuntimeFactorService> factorSvc)
{
    try {
    auto& pool = astock::database::NativePgConnectionPool::instance();
    if (!pool.isInitialized()) return nullptr;

    auto db = pool.getConnection();
    if (!db || !db->isOpen()) return nullptr;

    // 查询策略定义表获取参数 (数据库: strategy 表, metadata_json 列)
    auto result = db->executeQuery(
        "SELECT metadata_json, parameters FROM strategy WHERE strategy_id = ?",
        {strategyId});
    if (result.isEmpty()) return nullptr;

    const auto& row = result.getRow(0);

    // 解析 metadata_json
    std::string metaJson = row.getString("metadata_json");
    auto meta = foundation::json::JsonFacade::parse(metaJson);

    StrategyCreationParams params;
    params.strategyId     = strategyId;
    params.strategyName   = meta.has("name")        ? meta.get("name").asString()        : "";
    params.description    = meta.has("description") ? meta.get("description").asString()  : "";
    params.behaviorKind   = meta.has("behaviorKind")
        ? static_cast<::domain::strategies::StrategyBehaviorKind>(meta.get("behaviorKind").asInt())
        : ::domain::strategies::StrategyBehaviorKind::Custom;
    params.factorIds = parseFactorIds(meta.has("factorIds") ? meta.get("factorIds").toString() : "");

    // 解析 parameters JSON
    std::string paramJson = row.getString("parameters");
    if (!paramJson.empty() && paramJson != "null") {
        auto root = foundation::json::JsonFacade::parse(paramJson);
        params.topN = root.has("topN") ? root.get("topN").asInt() : 0;
        params.allowShort = root.has("allowShort") && root.get("allowShort").asBool();
        params.maxPositions = root.has("maxPositions") ? root.get("maxPositions").asInt() : 100;
        params.maxWeightPerStock = root.has("maxWeightPerStock") ? root.get("maxWeightPerStock").asDouble() : 0.1;
        params.minWeightPerStock = root.has("minWeightPerStock") ? root.get("minWeightPerStock").asDouble() : 0.0;
        params.maxOrderQuantity = root.has("maxOrderQuantity") ? static_cast<std::uint32_t>(root.get("maxOrderQuantity").asInt()) : 100U;
        params.stopLossPercent   = root.has("stopLossPercent")   ? root.get("stopLossPercent").asDouble()   : 10.0;
        params.takeProfitPercent = root.has("takeProfitPercent") ? root.get("takeProfitPercent").asDouble() : 20.0;
        params.fastPeriod   = root.has("fastPeriod")   ? root.get("fastPeriod").asInt()   : 5;
        params.slowPeriod   = root.has("slowPeriod")   ? root.get("slowPeriod").asInt()   : 20;
        params.signalPeriod = root.has("period")       ? root.get("period").asInt()       : 14;
        params.macdFast     = root.has("macdFast")     ? root.get("macdFast").asInt()     : 12;
        params.macdSlow     = root.has("macdSlow")     ? root.get("macdSlow").asInt()     : 26;
        params.macdSignal   = root.has("macdSignal")   ? root.get("macdSignal").asInt()   : 9;
        params.bbPeriod     = root.has("bbPeriod")     ? root.get("bbPeriod").asInt()     : 20;
        params.bbStdDev     = root.has("bbStdDev")     ? root.get("bbStdDev").asDouble()  : 2.0;
        if (root.has("weightScheme"))
            params.weightScheme = static_cast<::domain::strategies::WeightScheme>(root.get("weightScheme").asInt());
        if (root.has("rebalanceFrequency"))
            params.rebalanceFrequency = static_cast<::domain::strategies::RebalanceFrequency>(root.get("rebalanceFrequency").asInt());
    }

    // ── 根据策略类型判断是否需要因子 ──
    const bool isFactorType = params.behaviorKind == ::domain::strategies::StrategyBehaviorKind::MultiFactor
                           || params.behaviorKind == ::domain::strategies::StrategyBehaviorKind::MachineLearning;

    if (isFactorType && !factorSvc) {
        INTERNAL_WARN_STREAM << "[fromDb] ABORT: factor strategy but factorSvc is null";
        return nullptr;
    }

    auto engineBuilder = StrategyEngine::builder();
    if (factorSvc) {
        auto* rfsPtr = dynamic_cast<RuntimeFactorSvc*>(factorSvc.get());
        if (rfsPtr && !params.factorIds.empty()) {
            rfsPtr->setFactorIds(params.factorIds);
        }
        engineBuilder.withFactorService(std::move(factorSvc));
    }
    auto engine = engineBuilder.maxStrategies(params.maxPositions).build();
    INTERNAL_INFO_STREAM << "[fromDb] " << strategyId << " kind=" << static_cast<int>(params.behaviorKind)
                         << " factorIds=" << params.factorIds.size()
                         << " engine=" << static_cast<void*>(engine.get());
    if (!engine) return nullptr;
    engine->setStrategyId(strategyId);
    engine->m_orderBuilder.setStrategyId(strategyId);

    constexpr StrategyInstanceId kDefaultInstanceId = 1;
    RuntimeStrategyContext ctx(kDefaultInstanceId, 1,
                                params.maxOrderQuantity, params.maxWeightPerStock, true);

    if (isFactorType) {//策略中因子的开关。true 表示策略中使用了因子，false 表示策略中不使用因子
        // ── 因子策略 ──
        if (params.factorIds.empty()) {
            INTERNAL_WARN_STREAM << "[fromDb] factor strategy has no factor_ids configured";
            return nullptr;
        }

        ::domain::strategies::StrategyCommonConfig commonCfg;
        commonCfg.allowShort          = params.allowShort;
        commonCfg.maxPositions         = params.maxPositions;
        commonCfg.maxWeightPerStock    = params.maxWeightPerStock;
        commonCfg.minWeightPerStock    = params.minWeightPerStock;
        commonCfg.weightScheme         = params.weightScheme;
        commonCfg.rebalanceFrequency   = params.rebalanceFrequency;

        ::domain::strategies::StrategyMetadata meta;
        meta.name         = params.strategyName;
        meta.description  = params.description;
        meta.behaviorKind = params.behaviorKind;
        meta.factorIds    = params.factorIds;
        meta.enabled      = true;

        ::domain::strategies::MultiFactorSelectionStrategySpec spec;
        spec.topN           = params.topN > 0 ? params.topN : params.maxPositions;
        spec.industryNeutral = false;
        if (!params.factorIds.empty()) {
            double equalWeight = 1.0 / static_cast<double>(params.factorIds.size());
            for (const auto& fid : params.factorIds) {
                spec.factorWeights.push_back({fid, equalWeight});
            }
        }

        auto strategyDef = std::make_shared<::domain::strategies::MultiFactorSelectionStrategy>(
            commonCfg, meta, spec);

        auto runtimeStrategy = createMultiFactorSelectionRuntimeStrategy(
            strategyDef, kDefaultInstanceId);

        if (!engine->registerStrategy(std::move(runtimeStrategy), ctx).isOk())
            return nullptr;
    } else {
        // ── 非因子策略 — 按 behaviorKind 创建对应子类 ──
        ::domain::strategies::StrategyCommonConfig commonCfg;
        commonCfg.allowShort          = params.allowShort;
        commonCfg.maxPositions         = params.maxPositions;
        commonCfg.maxWeightPerStock    = params.maxWeightPerStock;
        commonCfg.minWeightPerStock    = params.minWeightPerStock;
        commonCfg.weightScheme         = params.weightScheme;
        commonCfg.rebalanceFrequency   = params.rebalanceFrequency;
        // 从 parameters JSON 读取策略自定义参数
        commonCfg.fastPeriod   = params.fastPeriod;
        commonCfg.slowPeriod   = params.slowPeriod;
        commonCfg.signalPeriod = params.signalPeriod;
        commonCfg.macdFast     = params.macdFast;
        commonCfg.macdSlow     = params.macdSlow;
        commonCfg.macdSignal   = params.macdSignal;
        commonCfg.bbPeriod     = params.bbPeriod;
        commonCfg.bbStdDev     = params.bbStdDev;

        std::unique_ptr<NonFactorStrategy> runtimeStrategy;
        switch (params.behaviorKind) {
        case ::domain::strategies::StrategyBehaviorKind::TrendFollowing:
            runtimeStrategy = std::make_unique<TrendFollowingStrategy>(
                kDefaultInstanceId, params.behaviorKind, commonCfg); break;
        case ::domain::strategies::StrategyBehaviorKind::MeanReversion:
            runtimeStrategy = std::make_unique<MeanReversionStrategy>(
                kDefaultInstanceId, params.behaviorKind, commonCfg); break;
        case ::domain::strategies::StrategyBehaviorKind::Momentum:
            runtimeStrategy = std::make_unique<MomentumStrategy>(
                kDefaultInstanceId, params.behaviorKind, commonCfg); break;
        case ::domain::strategies::StrategyBehaviorKind::Arbitrage:
            runtimeStrategy = std::make_unique<ArbitrageStrategy>(
                kDefaultInstanceId, params.behaviorKind, commonCfg); break;
        case ::domain::strategies::StrategyBehaviorKind::EventDriven:
            runtimeStrategy = std::make_unique<EventDrivenStrategy>(
                kDefaultInstanceId, params.behaviorKind, commonCfg); break;
        case ::domain::strategies::StrategyBehaviorKind::HighFrequency:
            runtimeStrategy = std::make_unique<HighFrequencyStrategy>(
                kDefaultInstanceId, params.behaviorKind, commonCfg); break;
        case ::domain::strategies::StrategyBehaviorKind::Custom:
            runtimeStrategy = std::make_unique<CustomStrategy>(
                kDefaultInstanceId, params.behaviorKind, commonCfg); break;
        default:
            runtimeStrategy = std::make_unique<NonFactorStrategy>(
                kDefaultInstanceId, params.behaviorKind, commonCfg); break;
        }

        if (!engine->registerStrategy(std::move(runtimeStrategy), ctx).isOk())
            return nullptr;

        INTERNAL_DEBUG_STREAM << "[fromDb] registered non-factor strategy, kind="
                              << static_cast<int>(params.behaviorKind);
    }

    // 将策略配置的风控参数同步到 TradingSystem
    domain::strategy::RiskConfig riskCfg = domain::strategy::RiskConfig::defaults();
    riskCfg.stopLossPercent   = params.stopLossPercent;
    riskCfg.takeProfitPercent = params.takeProfitPercent;
    domain::strategy::RiskManager::instance().setRiskConfig(riskCfg);

    // 日频/盘中分类: 仅 HighFrequency 走 drainQueue 持续评估,
    // 其余全部日频 → 盘中只巡检, 盘后触发一次 evaluateEndOfDay
    engine->m_isDailyFrequency = (params.behaviorKind
        != ::domain::strategies::StrategyBehaviorKind::HighFrequency);

    engine->m_rebalanceInterval =
        ::domain::strategies::rebalanceFrequencyStepInterval(params.rebalanceFrequency);

    return engine;

    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[fromDb] exception: " << e.what();
        return nullptr;
    } catch (...) {
        INTERNAL_ERROR_STREAM << "[fromDb] unknown exception";
        return nullptr;
    }
}

StrategyEngine::Builder StrategyEngine::builder()
{
    return Builder();
}

std::unique_ptr<StrategyEngine> StrategyEngine::fromParams(const StrategyCreationParams& params)
{
    // Builder 模式构建引擎 — 因子服务由调用方通过 fromDb(with factorSvc) 注入
    auto engine = builder()
        .maxStrategies(params.maxPositions)
        .build();
    if (engine) {
        engine->setStrategyId(params.strategyId);
        engine->m_orderBuilder.setStrategyId(params.strategyId);
    }
    return engine;
}

void StrategyEngine::setContextHistoricalView(const void* view)
{
    if (strategyService_) {
        strategyService_->setContextHistoricalView(view);
    }
}

void StrategyEngine::setLiveMarketView(const void* view)
{
    auto* v = static_cast<const factor::compute::IMarketDataView*>(view);
    if (auto* rfs = dynamic_cast<RuntimeFactorSvc*>(factorService_.get())) {
        rfs->setLiveMarketView(v);
    }
    if (!m_hasFactorStrategies) {
        setContextHistoricalView(view);
    }
}

bool StrategyEngine::prepareMarketData()
{
    // ── 根据策略因子开关决定字段需求与回溯窗口 ──
    std::vector<std::string> extraFields;
    int lookbackDays = 90;
    if (m_hasFactorStrategies) {
        auto* rfs = dynamic_cast<RuntimeFactorSvc*>(factorService_.get());
        if (rfs) {
            extraFields = rfs->getRequiredFields();
            lookbackDays = (std::max)(90, rfs->getMaxLookbackDays());
        }
    }

    // ── 计算日期范围 ──
    const auto now = foundation::utils::Timestamp::now();
    const std::string endDate = now.to_string("%Y-%m-%d");
    const auto start = now - foundation::utils::Duration::days(lookbackDays);
    const std::string startDate = start.to_string("%Y-%m-%d");

    // ── 从连接池获取 PG 连接（线程缓存复用）──
    auto& pool = astock::database::NativePgConnectionPool::instance();
    auto db = pool.getConnection();
    if (!db || !db->isOpen()) {
        INTERNAL_ERROR_STREAM << "[Engine] prepareMarketData: PG connection failed";
        return false;
    }

    if (!m_hasFactorStrategies) {
        // ── 非因子策略：fromSqlRows 直接构建，零 Qt/JSON 中转 ──
        std::ostringstream sql;
        sql << "SELECT si.symbol, d.trade_date, d.open, d.high, d.low, d.close, d.volume"
            << " FROM mkt.daily_bar d"
            << " JOIN ref.symbol_info si ON d.symbol_id = si.id"
            << " WHERE d.trade_date >= '" << startDate << "'"
            << " AND d.trade_date <= '" << endDate << "'"
            << " ORDER BY si.symbol, d.trade_date ASC";
        auto result = db->executeQuery(sql.str());
        auto rawRows = result.getRows();
        INTERNAL_INFO_STREAM << "[Engine] query OHLCV: " << rawRows.size()
                             << " rows, start=" << startDate << " end=" << endDate;
        if (!rawRows.empty()) {
            m_liveMarketView = factor::compute::CachedMarketDataView::fromSqlRows(rawRows, {});
        }
    }
    else{
        // ── 因子策略：MarketDataRepository → buildLiveView ──
        auto* rfs = dynamic_cast<RuntimeFactorSvc*>(factorService_.get());
        auto repo = std::make_unique<astock::infrastructure::database::MarketDataRepository>(db);
        auto rawRows = repo->queryAllMarketDailyBarWithFields(startDate, endDate, extraFields);
        INTERNAL_DEBUG_STREAM << "[Engine] query Factor: " << rawRows.size()<< " rows, fields=" << (5 + extraFields.size());
        if (!rawRows.empty() && rfs) {
            rfs->buildLiveView(rawRows, extraFields);
        }
    }

    // ── 注入视图 ──
    const factor::compute::IMarketDataView* v = nullptr;
    if (m_hasFactorStrategies) {
        auto* rfs = dynamic_cast<RuntimeFactorSvc*>(factorService_.get());
        if (rfs) v = rfs->liveView();
    } else {
        v = m_liveMarketView.get();
    }
    if (v) {
        setLiveMarketView(v);
        INTERNAL_INFO_STREAM << "[Engine] 历史数据就绪: " << v->dates().size()
                             << "天 " << v->instruments().size()
                             << "标的 fields=" << (extraFields.empty() ? 5 : 5 + extraFields.size());
        return true;
    }

    INTERNAL_WARN_STREAM << "[Engine] 历史数据为空: start=" << startDate << " end=" << endDate;
    return false;
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

StrategyEngine::~StrategyEngine()
{
    // 析构前必须停止实盘循环，避免后台线程访问已销毁的 this
    stopLiveLoop();
}

StrategyServiceFlowResult StrategyEngine::registerStrategy(
    std::shared_ptr<IRuntimeStrategy> strategy,
    const RuntimeStrategyContext& context)
{
    if (strategy && strategy->usesFactors()) {
        m_hasFactorStrategies = true;
    }
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
        if (strategies[i] && strategies[i]->usesFactors()) {
            m_hasFactorStrategies = true;
        }
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
    try {
        return collectOrders(strategyService_->onMarketDataPoint(marketDataPoint));
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[StrategyEngine] step() exception: " << e.what()
                             << " instId=" << marketDataPoint.instrumentId().value
                             << " price=" << marketDataPoint.lastPrice();
        throw;
    }
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

    if (m_isDailyFrequency) {
        // ── 日频: DailyEodScheduler 管理 EOD 回调 + 补单 ──
        INTERNAL_INFO_STREAM << "[启动] 日频策略 — DailyEodScheduler";

        if (!m_dailyScheduler) {
            // 持久化路径: 策略数据目录下 strategy_<id>_last_eval.txt
            std::string persistPath = "strategy_" + m_strategyId + "_last_eval.txt";
            m_dailyScheduler = std::make_unique<DailyEodScheduler>(
                [this](std::function<void()> fn) {
                    if (m_dedicatedExecutor && m_loopRunning.load(std::memory_order_acquire))
                        m_dedicatedExecutor->post(std::move(fn));
                },
                persistPath
            );
            // 从 TradingConnectionConfig 读取下单窗口配置(默认 15:00-15:30)
            {
                auto& cfgMgr = foundation::config::ConfigManager::instance();
                auto cfg = cfgMgr.loadConfigFile(foundation::config::ConfigFile::TradingConnection);
                std::string start = "15:00", end = "15:30";
                if (cfg && !cfg->isNull()) {
                    if (cfg->has("preCloseOrderStart")) start = cfg->get("preCloseOrderStart").asString();
                    if (cfg->has("preCloseOrderEnd"))   end   = cfg->get("preCloseOrderEnd").asString();
                }
                m_dailyScheduler->setPreCloseWindow(start, end);
            }
            m_dailyScheduler->setEvalCallback(
                [this](const std::string& tradingDay, bool isCompensation) -> EodEvaluationStatus {
                    return evaluateEndOfDay(tradingDay, isCompensation);
                });
            m_dailyScheduler->start();
        }
    } else {
        // ── 分钟频/高频: 启动完整 drainQueue ──
        INTERNAL_INFO_STREAM << "[启动] 盘中策略 — 启动 drainQueue 事件循环";

        m_dedicatedExecutor->post([this]() {
            drainQueue();
        });
    }

    // ── 金融事件风控订阅器在 AppBootstrap 已全局启动，此处无需操作 ──
}

void StrategyEngine::stopLiveLoop()
{
    if (!m_loopRunning.load(std::memory_order_acquire)) {
        return;
    }
    m_loopRunning.store(false, std::memory_order_release);
    m_queueCv.notify_one();

    // 先停调度器, 防止回调在 executor 关闭后投递任务
    if (m_dailyScheduler) {
        m_dailyScheduler->stop();
    }
    // 风控订阅器全局单例，不在此停止

    if (m_dedicatedExecutor) {
        INTERNAL_DEBUG_STREAM << "[StrategyEngine] 等待专用线程退出...";
        m_dedicatedExecutor->shutdown(false);
        m_dedicatedExecutor->awaitTermination(std::chrono::milliseconds(5000));
        INTERNAL_DEBUG_STREAM << "[StrategyEngine] 专用线程已退出";
    }
}

bool StrategyEngine::isLiveLoopRunning() const noexcept {
    return m_loopRunning.load(std::memory_order_acquire);
}

int StrategyEngine::liquidateAll()
{
    if (!m_orderListener) {
        INTERNAL_WARN_STREAM << "[StrategyEngine] liquidateAll: 无订单监听器";
        return -1;
    }

    auto& accEng = engine::AccountEngine::instance();
    auto positions = accEng.positions();

    if (positions.empty()) {
        INTERNAL_INFO_STREAM << "[StrategyEngine] liquidateAll: 无持仓";
        return 0;
    }

    std::vector<OrderRequest> orders;
    for (const auto& pos : positions) {
        if (pos.quantity <= 0) continue;
        OrderRequest order;
        order.setSymbol(pos.symbol);
        order.setSide(domain::trading::OrderSide::Sell);
        order.setQuantity(static_cast<double>(pos.quantity));
        order.setPrice(0);  // 市价
        order.setOrderType(domain::trading::OrderType::Market);
        order.setPositionEffect(domain::trading::PositionEffect::Close);
        order.setStrategyId(m_strategyId);
        m_orderBuilder.setAccountId(accEng.account().accountId);
        order.setAccountId(accEng.account().accountId);

        orders.push_back(std::move(order));

        // 去后缀加入清仓名单
        auto dot = pos.symbol.find('.');
        std::string code = (dot != std::string::npos)
            ? pos.symbol.substr(0, dot) : pos.symbol;
        m_liquidationBlocklist.insert(code);
    }

    // 篮子ID
    static std::atomic<uint64_t> s_liqBasketSeq{0};
    uint64_t basketId = s_liqBasketSeq.fetch_add(1);
    for (auto& o : orders)
        o.setExtension(domain::trading::ExtKey::kBasketId, basketId);

    m_orderListener->onOrders(orders);
    INTERNAL_WARN_STREAM << "[StrategyEngine] 一键清仓: basketId=" << basketId
                         << " orders=" << orders.size()
                         << " 笔订单, 持仓已提交";
    return static_cast<int>(orders.size());
}

void StrategyEngine::setOrderListener(IOrderListener* listener)
{
    m_orderListener = listener;
}

// ═════════════════════════════════════════════════════════════════════════
// buildPositionAwareOrders — 持仓感知建单（EOD 和 drainQueue 共用）
// ═════════════════════════════════════════════════════════════════════════

std::vector<OrderRequest> StrategyEngine::buildPositionAwareOrders(
    const std::vector<OrderRequest>& rawOrders,
    const std::unordered_map<std::string, int64_t>& posQtyMap,
    const engine::AccountInfo& account,
    double priceForWeight)
{
    std::vector<OrderRequest> result;
    constexpr int64_t kMinLot = 100;
    std::unordered_set<std::string> seenKeys;  // 同标的+方向去重，防止重复下单

    auto stripExchange = [](const std::string& sym) -> std::string {
        auto dot = sym.find('.');
        return (dot != std::string::npos) ? sym.substr(0, dot) : sym;
    };

    for (const auto& raw : rawOrders) {
        if (!raw.isValid()) continue;

        // 同标的+方向去重（多策略或多轮评估可能产生重复信号）
        std::string dedupKey = stripExchange(raw.symbol())
            + (raw.side() == OrderSide::Buy ? "_B" : "_S");
        if (seenKeys.count(dedupKey)) continue;
        seenKeys.insert(dedupKey);

        double targetWeight = raw.extensionAs<double>(domain::trading::ExtKey::kTargetWeight, 0.0);
        double signalScore  = raw.extensionAs<double>(domain::trading::ExtKey::kSignalScore, 0.5);
        std::string code = stripExchange(raw.symbol());

        auto it = posQtyMap.find(code);
        int64_t currentQty = (it != posQtyMap.end()) ? it->second : 0;

        if (priceForWeight <= 0 || account.totalAsset <= 0) {
            INTERNAL_WARN_STREAM << "[buildPAO] price或totalAsset无效: " << code
                << " price=" << priceForWeight << " asset=" << account.totalAsset;
            continue;
        }

        double currentW = static_cast<double>(currentQty) * priceForWeight / account.totalAsset;
        int64_t targetQty = static_cast<int64_t>(
            targetWeight * account.totalAsset / priceForWeight / 100.0) * 100;

        int64_t deltaQty = 0;
        SignalIntent intent = SignalIntent::KEEP;
        OrderSide side = raw.side();

        if (side == OrderSide::Buy) {
            if (currentW < 0.001) {
                intent = SignalIntent::OPEN; deltaQty = targetQty;
            } else if (targetWeight > currentW) {
                intent = SignalIntent::ADD; deltaQty = targetQty - currentQty;
            } else {
                INTERNAL_WARN_STREAM << "[buildPAO] Buy矛盾丢弃: " << code
                    << " target=" << targetWeight << " currentW=" << currentW;
                continue;
            }
        } else { // Sell
            if (currentQty <= 0) {
                INTERNAL_WARN_STREAM << "[buildPAO] Sell无持仓丢弃: " << code;
                continue;
            }
            if (targetWeight >= currentW) {
                INTERNAL_WARN_STREAM << "[buildPAO] Sell矛盾丢弃: " << code
                    << " target=" << targetWeight << " currentW=" << currentW;
                continue;
            }
            if (targetWeight > 0.0) {
                if (targetQty < kMinLot) {
                    intent = SignalIntent::CLOSE;
                    deltaQty = currentQty;
                } else {
                    intent = SignalIntent::REDUCE;
                    deltaQty = currentQty - targetQty;
                }
            } else {
                intent = SignalIntent::CLOSE; deltaQty = currentQty;
            }
        }

        if (deltaQty < kMinLot) {
            INTERNAL_DEBUG_STREAM << "[buildPAO] delta不足一手: " << code
                << " delta=" << deltaQty << " intent=" << static_cast<int>(intent);
            continue;
        }

        OrderRequest order = m_orderBuilder.buildSignalOrder(
            raw.symbol(), side, 0, deltaQty, signalScore);
        order.setExtension(domain::trading::ExtKey::kSignalIntent, static_cast<uint64_t>(intent));
        result.push_back(std::move(order));
    }
    return result;
}

void StrategyEngine::drainQueue()
{
    while (m_loopRunning.load(std::memory_order_acquire)) {
        // ── 收集本轮需要评估的 MarketDataPoint ──
        std::vector<MarketDataPoint> batch;

        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            // 500ms 超时 → 从 LiveData 生成 MDP
            m_queueCv.wait_for(lock, std::chrono::milliseconds(500), [this]() {
                return !m_mdpQueue.empty() || !m_loopRunning.load(std::memory_order_acquire);
            });
            if (!m_loopRunning.load(std::memory_order_acquire)) return;

            // 处理积压的队列数据（回测等场景）
            while (!m_mdpQueue.empty()) {
                batch.push_back(m_mdpQueue.front());
                m_mdpQueue.pop();
            }
        }

        // 队列为空 → 从 LiveData 构造当日实时行情
        if (batch.empty()) {
            auto symbols = domain::market::MarketDataService::instance().symbols();
            for (const auto& sym : symbols) {
                auto& d = domain::market::MarketDataService::instance().liveData(sym);
                if (!d.valid()) continue;
                double price = d.dailyBar().close();
                if (price <= 0) continue;
                auto aSym = foundation::market::AStockSymbol::fromString(sym);
                if (!aSym.isValid()) continue;
                batch.emplace_back(
                    domain::strategy::InstrumentId{aSym.instrumentId()},
                    price,
                    d.dailyBar().volume(),
                    0);
            }
        }

        for (const auto& mdp : batch) {
        try {
            auto orders = step(mdp);
            if (orders.has_value() && m_orderListener
                && !m_isBacktestMode.load(std::memory_order_acquire)) {

                auto& accEng = engine::AccountEngine::instance();
                auto account = accEng.account();
                auto positions = accEng.positions();

                if (account.totalAsset <= 0) continue;

                std::unordered_map<std::string, int64_t> posQtyMap;
                for (const auto& p : positions) {
                    auto dot = p.symbol.find('.');
                    std::string code = (dot != std::string::npos)
                        ? p.symbol.substr(0, dot) : p.symbol;
                    posQtyMap[code] = p.quantity;
                }

                auto finalOrders = buildPositionAwareOrders(*orders, posQtyMap, account, mdp.lastPrice());
                if (!finalOrders.empty()) {
                    static std::atomic<uint64_t> s_basketSeq{0};
                    auto basketId = std::to_string(
                        std::chrono::steady_clock::now().time_since_epoch().count())
                        + "_" + std::to_string(s_basketSeq.fetch_add(1));
                    uint64_t basketHash = std::hash<std::string>{}(basketId);
                    for (auto& o : finalOrders)
                        o.setExtension(domain::trading::ExtKey::kBasketId, basketHash);

                    try {
                        m_orderListener->onOrders(finalOrders);
                        INTERNAL_INFO_STREAM << "[StrategyEngine] 篮子提交: basketId=" << basketId
                                             << " orders=" << finalOrders.size();
                    } catch (const std::exception& e) {
                        INTERNAL_ERROR_STREAM << "[StrategyEngine] drainQueue onOrders 异常: basketId="
                                              << basketId << " " << e.what();
                    }
                }
            }
        } catch (const std::exception& e) {
            INTERNAL_WARN_STREAM << "[StrategyEngine] tick processing failed: "
                                 << e.what() << " — skipping";
        }
        } // for each MDP in batch

        // 心跳 — 记录最后处理时间（风控巡检由 JMC 全局线程处理）
        m_lastProcessedAt.store(
            std::chrono::steady_clock::now().time_since_epoch().count(),
            std::memory_order_release);
    }
    // 循环退出时报告丢 tick 统计
    auto dropped = m_droppedTicks.exchange(0, std::memory_order_relaxed);
    if (dropped > 0) {
        INTERNAL_WARN_STREAM << "[StrategyEngine] drainQueue stopped: " << static_cast<unsigned long long>(dropped) << " ticks dropped during session";
    }
}

// ═════════════════════════════════════════════════════════════════════════
// evaluateEndOfDay — 日频策略盘后评估: 当日 Bar 已封口, 跑一次完整策略
// ═════════════════════════════════════════════════════════════════════════

EodEvaluationStatus StrategyEngine::evaluateEndOfDay(const std::string& tradingDay, bool isCompensation)
{
    INTERNAL_INFO_STREAM << "[StrategyEngine] 日终评估 tradingDay=" << tradingDay
                         << " isCompensation=" << isCompensation;

    // 调仓周期检查: 非调仓日跳过, 避免每日重复下单
    if (m_rebalanceInterval > 1 && !m_lastRebalanceDate.empty()) {
        std::string date = tradingDay;
        int tradingDaysSince = 0;
        for (int i = 0; i < m_rebalanceInterval && !date.empty(); ++i) {
            char prevOut[32] = {};
            if (::get_previous_trading_date("SZSE", date.c_str(), prevOut) != 0)
                ::get_previous_trading_date("SHSE", date.c_str(), prevOut);
            date = prevOut;
            if (date.empty()) break;
            ++tradingDaysSince;
            if (date == m_lastRebalanceDate) break;
        }
        if (tradingDaysSince < m_rebalanceInterval) {
            INTERNAL_INFO_STREAM << "[StrategyEngine] 非调仓日: 距上次调仓 "
                << tradingDaysSince << "/" << m_rebalanceInterval << " 交易日, 跳过";
            return EodEvaluationStatus::Skipped;
        }
    }

    if (m_isBacktestMode.load(std::memory_order_acquire)) {
        INTERNAL_INFO_STREAM << "[StrategyEngine] 回测模式, 跳过日终评估";
        return EodEvaluationStatus::Skipped;
    }
    if (!m_orderListener) {
        INTERNAL_WARN_STREAM << "[StrategyEngine] 无订单监听器, 日终评估跳过";
        return EodEvaluationStatus::Skipped;
    }

    // ── 从 MarketDataService 取所有已订阅标的 ──
    auto symbols = domain::market::MarketDataService::instance().symbols();
    if (symbols.empty()) {
        INTERNAL_WARN_STREAM << "[StrategyEngine] 日终评估: 无订阅标的, 跳过";
        return EodEvaluationStatus::Skipped;
    }

    // 补单: tradingDay 转 "YYYY-MM-DD" 供 history_bars_n 用
    std::string endDateStr;
    if (isCompensation) {
        auto dayInt = std::stoll(tradingDay);
        int y = static_cast<int>(dayInt / 10000);
        int m = static_cast<int>((dayInt % 10000) / 100);
        int d = static_cast<int>(dayInt % 100);
        char buf[32];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
        endDateStr = buf;
    }

    INTERNAL_INFO_STREAM << "[StrategyEngine] 日终评估 " << symbols.size() << " 只标的";

    // ── 获取账户和持仓快照（一次，循环内不复查）──
    auto& accEng = engine::AccountEngine::instance();
    auto account   = accEng.account();     // 返回值拷贝
    auto positions = accEng.positions();   // 返回值拷贝
    m_orderBuilder.setAccountId(account.accountId);

    if (account.totalAsset <= 0) {
        INTERNAL_WARN_STREAM << "[StrategyEngine] EOD account.totalAsset=0, skip";
        return EodEvaluationStatus::Skipped;
    }

    auto stripExchange = [](const std::string& sym) -> std::string {
        auto dot = sym.find('.');
        return (dot != std::string::npos) ? sym.substr(0, dot) : sym;
    };

    std::unordered_map<std::string, int64_t> posQtyMap;
    for (const auto& p : positions) {
        posQtyMap[stripExchange(p.symbol)] = p.quantity;
        // quantity 非 availableQty: EOD 下单次日成交, 隔夜无冻结
    }

    if (EventRiskSubscriber::instance().isStarted())
        EventRiskSubscriber::instance().clearBlockedSymbols();  // T+1 事件封禁解禁
    // 买单资金按 pendingOrders 中顺序分配(先到先得),
    // 策略信号已按信号强度排序, 核心标的优先获得资金

    // ═══════════════════════════════════════════════════════════
    // Phase 1: 收集 — 遍历所有标的, 生成订单并规范化
    // ═══════════════════════════════════════════════════════════
    struct PendingOrder {
        OrderRequest order;
        double tickPrice;
        double targetWeight;
        double signalScore;
    };
    std::vector<PendingOrder> pendingOrders;

    for (const auto& sym : symbols) {
        auto& d = domain::market::MarketDataService::instance().liveData(sym);
        if (!d.valid()) continue;

        double price = 0;
        if (isCompensation) {
            std::string gm = engine::GmSessionEngine::toGmSymbol(sym);
            if (gm.empty()) continue;
            auto* bars = ::history_bars_n(gm.c_str(), "1d", 1, endDateStr.c_str(),
                                           0, nullptr, true, nullptr);
            if (!bars || bars->status() || bars->count() <= 0) {
                if (bars) bars->release();
                continue;
            }
            price = bars->at(0).close;
            bars->release();
        } else {
            price = d.dailyBar().close();
        }
        if (price <= 0) continue;

        auto aSym = foundation::market::AStockSymbol::fromString(sym);
        if (!aSym.isValid()) continue;

        MarketDataPoint mdp(
            domain::strategy::InstrumentId{aSym.instrumentId()},
            price,
            d.dailyBar().volume(),
            0);

        try {
            // Phase 1 预过滤: 跳过事件风控封禁的标的
            if (EventRiskSubscriber::instance().isStarted() &&
                EventRiskSubscriber::instance().blockedSymbols().count(
                    stripExchange(sym))) {
                INTERNAL_INFO_STREAM
                    << "[StrategyEngine] EOD Phase1 skip blocked: " << sym;
                continue;
            }

            auto orders = step(mdp);
            if (orders.has_value()) {
                for (auto& order : *orders) {
                    if (!order.isValid()) continue;

                    double signalScore = order.extensionAs<double>(
                        domain::trading::ExtKey::kSignalScore, 0.5);
                    double targetWeight = order.extensionAs<double>(
                        domain::trading::ExtKey::kTargetWeight, 0.0);

                    order = m_orderBuilder.buildSignalOrder(
                        order.symbol(), order.side(),
                        0,
                        static_cast<int64_t>(order.quantity()), signalScore);
                    // 保留 kTargetWeight — buildPositionAwareOrders 依赖此字段计算 delta
                    if (targetWeight > 0.0)
                        order.setExtension(domain::trading::ExtKey::kTargetWeight, targetWeight);

                    auto& ld = domain::market::MarketDataService::instance()
                        .liveData(order.symbol());
                    double tickPrice = ld.valid() ? ld.dailyBar().close() : mdp.lastPrice();
                    if (!std::isfinite(tickPrice) || tickPrice <= 0) continue;

                    if (order.orderType() == OrderType::Market)
                        order.setPrice(tickPrice);

                    pendingOrders.push_back(
                        {std::move(order), tickPrice, targetWeight, signalScore});
                }
            }
        } catch (const std::exception& e) {
            INTERNAL_WARN_STREAM << "[StrategyEngine] EOD collect 异常: " << sym
                                 << " " << e.what();
        }
    }

    // ═══════════════════════════════════════════════════════════
    // Phase 2+3: 持仓感知建单 (buildPositionAwareOrders)
    // ═══════════════════════════════════════════════════════════

    int totalGenerated = static_cast<int>(pendingOrders.size());

    // 计算用于权重估算的参考价格
    double priceForWeight = 0.0;
    {
        double sum = 0.0;
        int cnt = 0;
        for (const auto& sym : symbols) {
            auto& d = domain::market::MarketDataService::instance().liveData(sym);
            if (d.valid() && d.dailyBar().close() > 0) {
                sum += d.dailyBar().close();
                ++cnt;
            }
        }
        if (cnt > 0) {
            priceForWeight = sum / cnt;
        } else {
            // fallback: 使用 pendingOrder 的 tick price
            for (const auto& po : pendingOrders) {
                if (po.tickPrice > 0) { sum += po.tickPrice; ++cnt; }
            }
            if (cnt > 0) priceForWeight = sum / cnt;
        }
    }

    std::vector<OrderRequest> rawOrders;
    rawOrders.reserve(pendingOrders.size());
    for (auto& po : pendingOrders) {
        rawOrders.push_back(std::move(po.order));
    }

    auto finalOrders = buildPositionAwareOrders(rawOrders, posQtyMap, account, priceForWeight);

    int totalSubmitted = 0;
    if (!finalOrders.empty() && m_orderListener) {
        static std::atomic<uint64_t> s_eodBasketSeq{0};
        auto basketId = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count())
            + "_" + std::to_string(s_eodBasketSeq.fetch_add(1));
        uint64_t basketHash = std::hash<std::string>{}(basketId);
        for (auto& o : finalOrders)
            o.setExtension(domain::trading::ExtKey::kBasketId, basketHash);

        try {
            m_orderListener->onOrders(finalOrders);
            totalSubmitted = static_cast<int>(finalOrders.size());
            INTERNAL_INFO_STREAM << "[StrategyEngine] EOD 篮子提交: basketId=" << basketId
                                 << " orders=" << totalSubmitted;
        } catch (const std::exception& e) {
            INTERNAL_ERROR_STREAM << "[StrategyEngine] EOD onOrders 异常: basketId="
                                  << basketId << " " << e.what();
        }
    }

    int totalRejected = totalGenerated - totalSubmitted;

    m_lastProcessedAt.store(
        std::chrono::steady_clock::now().time_since_epoch().count(),
        std::memory_order_release);

    INTERNAL_INFO_STREAM << "[StrategyEngine] 日终评估完成"
                         << " 信号=" << totalGenerated
                         << " 提交=" << totalSubmitted
                         << " 拒绝=" << totalRejected;

    if (totalSubmitted > 0) {
        m_lastRebalanceDate = tradingDay;
    }

    // ── 每日账户快照 ──
    {
        auto acc = engine::AccountEngine::instance().account();
        int td = static_cast<int>(domain::market::MarketDataService::instance().activeTradingDay());
        if (td > 0 && acc.totalAsset > 0) {
            astock::infrastructure::database::OrderRecorder::instance().insertAccountSnapshot(
                td, acc.totalAsset, acc.availableCash, acc.marketValue, acc.frozenCash,
                acc.realizedPnl, acc.unrealizedPnl);
        }
    }

    // ── 返回篮子状态 ──
    if (totalGenerated == 0) return EodEvaluationStatus::NoSignal;
    if (totalSubmitted == 0) return EodEvaluationStatus::AllRejected;
    return EodEvaluationStatus::Submitted;
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
        // 非因子策略回退：空因子服务，所有因子操作均为 no-op
        factorService_ = std::make_unique<NoOpFactorService>();
    }
    std::unique_ptr<IRuntimeFactorService> factorService = std::move(factorService_);

    std::unique_ptr<IRuleEvaluationService> ruleEvaluationService;
    if (ruleEvaluationService_) {
        ruleEvaluationService = std::move(ruleEvaluationService_);
    } else {
        ruleEvaluationService = std::make_unique<LocalRuleEvaluationService>();
    }

    std::unique_ptr<IStrategyService> strategyService;
    strategyService = std::make_unique<StrategyService>(*factorService, *ruleEvaluationService);

    const StrategyServiceFlowResult configureResult = strategyService->configureExecutionPlan(plan_);
    if (!configureResult.isOk()) {
        std::abort();
    }
    strategyService->setDiagnosticsSink(diagnosticsSink_);
    if (orderBuilder_) {
        strategyService->setOrderBuilder(orderBuilder_);
    }
    // 不在此处自动启动——回测由 backtest() 内部启动，实盘由 StrategyBridge::start() 手动启动

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
    void* dataSvcPtr,
    const std::function<void(double)>& onProgress)
{
    StrategyBacktestResult result;

    // 防御：回测期间 drainQueue() 不得触发 IOrderListener
    struct BacktestGuard {
        std::atomic<bool>& flag;
        explicit BacktestGuard(std::atomic<bool>& f) : flag(f) {
            flag.store(true, std::memory_order_release);
        }
        ~BacktestGuard() { flag.store(false, std::memory_order_release); }
    };
    BacktestGuard backtestGuard(m_isBacktestMode);

    auto* dataSvc = static_cast<factor::compute::BacktestDataService*>(dataSvcPtr);
    if (!dataSvc) {
        result.errorMessage = "Null data service";
        return result;
    }

    // 1. 从已加载的 DataSvc 获取视图（不再自行解析 JSON）
    if (onProgress) onProgress(1.0);
    dataSvc->buildViewForFields({});  // 确保视图已构建
    auto batch = dataSvc->loadBatch(0);
    const auto* view = batch.marketView;
    if (!view) {
        result.errorMessage = "Failed to load market data view";
        return result;
    }

    // ── 将 DataSvc 注入 RuntimeFactorSvc，使因子计算能访问市场数据 ──
    if (factorService_) {
        auto* rfs = dynamic_cast<RuntimeFactorSvc*>(factorService_.get());
        if (rfs) {
            rfs->setDataService(dataSvc);
        }
    }

    if (onProgress) onProgress(2.0);

    // 确保引擎服务处于运行状态（复用引擎时可能未启动）
    if (strategyService_) {
        strategyService_->start();
    }

    const int totalDays = static_cast<int>(view->dates().size());
    const int colCount = static_cast<int>(view->instruments().size());

    if (totalDays == 0 || colCount == 0) {
        result.errorMessage = "Empty market data";
        return result;
    }

    // 2. 构建 symbol 映射 + 账户初始化
    std::unordered_map<std::uint32_t, std::string> idToSymbol;
    {
        auto batch0 = dataSvc->loadBatch(0);
        const auto* v0 = batch0.marketView;
        if (v0) {
            factor::compute::CachedMarketDataViewHistoricalAdapter adapter(*v0);
            auto symbols = adapter.getAvailableSymbols("");
            const auto& instrs = v0->instruments();
            for (size_t i = 0; i < instrs.size() && i < symbols.size(); ++i) {
                idToSymbol[instrs[i].value] = symbols[i];
            }
        }
    }

    double backtestCash = req.costSpec.initialCapital.value;
    std::unordered_map<std::string, domain::trading::Position> backtestPositions;
    auto btAccount = [&]() {
        domain::trading::AccountSnapshot a;
        a.setTotalAsset(backtestCash);
        a.setAvailableCash(backtestCash);
        return a;
    };
    domain::trading::AccountSnapshot acc;
    acc.setAvailableCash(req.costSpec.initialCapital.value);
    acc.setTotalAsset(req.costSpec.initialCapital.value);
    acc.setAccountId(req.strategyIdentity.strategyId.text());
    // removed

    domain::backtest::FillSimulatorParams fillParams;
    fillParams.commissionRate = req.costSpec.commissionRate.value;
    fillParams.taxRate        = req.costSpec.taxRate.value;
    fillParams.slippageRate   = req.costSpec.slippageRate.value;
    domain::backtest::BacktestFillSimulator fillSim(fillParams);

    double cash = req.costSpec.initialCapital.value;
    int riskRejectedCount = 0;
    int totalFills = 0, winningFills = 0, losingFills = 0;
    double totalProfit = 0.0, totalLoss = 0.0, largestWin = 0.0, largestLoss = 0.0;
    std::unordered_map<std::string, double> buyPriceMap;
    std::unordered_map<std::string, double> symbolPnl;  // 逐标的累计盈亏
    std::vector<double> equityCurve;
    equityCurve.reserve(totalDays);

    // 数据准备完成 → 0%
    if (onProgress) onProgress(0.0);

    // 3. 逐日驱动
    // 第一步会触发惰性因子计算（FactorEngine::compute），我们不知道它占总时间的比例
    // 但它是真实工作，后续逐日循环也是真实工作
    const double kSetupFrac  = 0.0;
    const double kLoopStart  = 0.0;
    const double kLoopEnd    = 90.0;
    const double kMetricsEnd = 100.0;
    for (int r = 0; r < totalDays; ++r) {
        // 每次迭代重新获取 view（因子计算可能重建了 m_ownedView）
        auto batch = dataSvc->loadBatch(0);
        const auto* view = batch.marketView;
        if (!view) { result.errorMessage = "View lost during backtest"; return result; }
        const auto& dates = view->dates();
        const auto& instruments = view->instruments();
        auto closeMat = view->close();
        auto volumeMat = view->volume();

        if (r == 0 || r == totalDays-1 || r % 100 == 0) {
            INTERNAL_INFO_STREAM << "[backtest] day " << r << "/" << totalDays << " equity=" << btAccount().totalAsset() << " positions=" << backtestPositions.size();
        }

        std::vector<MarketDataPoint> mdpBatch;
        mdpBatch.reserve(colCount);
        const std::size_t rowOffset = static_cast<std::size_t>(r) * static_cast<std::size_t>(colCount);
        for (int c = 0; c < colCount; ++c) {
            const std::uint32_t instrumentId = instruments[static_cast<std::size_t>(c)].value;
            const double price = static_cast<double>(closeMat.data[rowOffset + static_cast<std::size_t>(c)]);
            const double volume = static_cast<double>(volumeMat.data[rowOffset + static_cast<std::size_t>(c)]);
            if (price > 0.0) {
                mdpBatch.emplace_back(InstrumentId(instrumentId), price, volume,
                    dates[static_cast<std::size_t>(r)].value);
            }
        }

        auto ordersOpt = stepBatch(mdpBatch);

        if (ordersOpt.has_value()) {
            auto& orderList = ordersOpt.value();
            if (r == 0 || r % 100 == 0) {
                INTERNAL_INFO_STREAM << "[backtest] day " << r << " orders=" << orderList.size();
            }
            for (auto& order : orderList) {
                // symbol 在策略层已填为纯数字码 (如 "600000")
                const std::string& symStr = order.symbol();
                const std::uint32_t instrumentId = static_cast<std::uint32_t>(
                    std::stoul(symStr.empty() ? "0" : symStr));
                const std::string symbol = idToSymbol.count(instrumentId)
                    ? idToSymbol.at(instrumentId) : symStr;

                // 补齐 symbol 后缀
                char codeBuf[16];
                std::snprintf(codeBuf, sizeof(codeBuf), "%06u", instrumentId);
                order.setSymbol(foundation::market::AStockSymbol::fromCode(codeBuf).fullSymbol());
                double closePrice = 0.0;
                for (const auto& mdp : mdpBatch) {
                    if (mdp.instrumentId().value == instrumentId) {
                        closePrice = mdp.lastPrice(); break;
                    }
                }
                if (closePrice <= 0.0) continue;

                // ── 风控检查 (RiskEvaluator — 公共类) ──
                domain::strategy::RiskInput riskInput;
                riskInput.setStrategyId(req.strategyIdentity.strategyId.text());
                riskInput.setSymbol(symbol);
                riskInput.setBuyOrder(order.side() == OrderSide::Buy);
                riskInput.setPrice(closePrice);
                riskInput.setQuantity(static_cast<std::int64_t>(order.quantity()));
                riskInput.setStrategyBound(true);
                riskInput.setStrategyActive(true);
                riskInput.setSignalStrength(0.5);         // 回测默认信号强度
                riskInput.setPositionSnapshotReady(true);  // 回测持仓快照可用
                auto accSnap = btAccount();
                riskInput.setCurrentTotalAsset(accSnap.totalAsset());
                riskInput.setCurrentMarketValue(accSnap.marketValue());
                riskInput.setTradingSessionOpen(true);
                // 卖出单：填充可卖数量
                if (!riskInput.isBuyOrder()) {
                    const auto& posMap = backtestPositions;
                    auto pit = posMap.find(symbol);
                    riskInput.setCloseableQuantity(pit != posMap.end()
                        ? pit->second.quantity() : 0);
                }

                auto riskResult = domain::strategy::RiskEvaluator::evaluateOrder(riskInput);
                if (!riskResult.approved()) {
                    ++riskRejectedCount;
                    continue;
                }

                // ── 成交模拟 (BacktestFillSimulator — 公共类) ──
                if (order.side() == OrderSide::Buy) {
                    double remaining = fillSim.cashAfterBuy(cash, closePrice,
                        static_cast<std::int64_t>(order.quantity()));
                    if (remaining >= 0.0) {
                        cash = remaining;
                        domain::trading::Position pos;
                        pos.setSymbol(symbol);
                        pos.setSide(domain::trading::PositionSide::Long);
                        const auto& buyPosMap = backtestPositions;
                        std::int64_t existingQty = buyPosMap.count(symbol)
                            ? buyPosMap.at(symbol).quantity() : 0LL;
                        if (existingQty == 0) buyPriceMap[symbol] = closePrice;
                        pos.setQuantity(existingQty + static_cast<std::int64_t>(order.quantity()));
                        pos.setLastPrice(closePrice);
                        backtestPositions[symbol] = pos;
                    }
                } else {
                    const auto& posMap = backtestPositions;
                    auto it = posMap.find(symbol);
                    const std::int64_t held = (it != posMap.end()) ? it->second.quantity() : 0LL;
                    const std::int64_t qty = static_cast<std::int64_t>(order.quantity());
                    const std::int64_t sellQty = qty < held ? qty : held;
                    if (sellQty > 0) {
                        auto fr = fillSim.simulateSell(closePrice, sellQty);
                        cash += fr.income;
                        ++totalFills;
                        double bp = closePrice;
                        auto bpIt = buyPriceMap.find(symbol);
                        if (bpIt != buyPriceMap.end()) { bp = bpIt->second; buyPriceMap.erase(bpIt); }
                        double pnl = (fr.income / sellQty - bp) * sellQty;
                        if (pnl > 0) { ++winningFills; totalProfit += pnl; if (pnl > largestWin) largestWin = pnl; }
                        else { ++losingFills; totalLoss += -pnl; if (-pnl > largestLoss) largestLoss = -pnl; }
                        symbolPnl[symbol] += pnl;
                        domain::trading::Position pos;
                        pos.setSymbol(symbol);
                        pos.setSide(domain::trading::PositionSide::Long);
                        pos.setQuantity(held - sellQty);
                        pos.setLastPrice(closePrice);
                        if (pos.quantity() > 0) backtestPositions[symbol] = pos;
                        else {
                            domain::trading::Position empty;
                            empty.setSymbol(symbol); empty.setQuantity(0);
                            backtestPositions.erase(symbol);
                        }
                    }
                }
            }
        }

        // 更新账户
        double marketValue = 0.0;
        for (const auto& [sym, pos] : backtestPositions) {
            if (pos.quantity() > 0) {
                for (const auto& mdp : mdpBatch) {
                    auto symIt = idToSymbol.find(mdp.instrumentId().value);
                    if (symIt != idToSymbol.end() && symIt->second == sym) {
                        marketValue += mdp.lastPrice() * static_cast<double>(pos.quantity());
                        break;
                    }
                }
            }
        }
        double equity = cash + marketValue;
        domain::trading::AccountSnapshot newAcc;
        newAcc.setAvailableCash(cash);
        newAcc.setMarketValue(marketValue);
        newAcc.setTotalAsset(equity);
        backtestCash = newAcc.availableCash();
        equityCurve.push_back(equity);

        if (onProgress && totalDays > 0) {
            double loopFrac = static_cast<double>(r + 1) / static_cast<double>(totalDays);
            double pct = kLoopStart + loopFrac * (kLoopEnd - kLoopStart);
            onProgress(pct);
        }
    }

    INTERNAL_INFO_STREAM << "[backtest] loop done: days=" << totalDays << " finalEquity=" << btAccount().totalAsset() << " fills=" << totalFills << " riskRejected=" << riskRejectedCount;
    // 逐标的盈亏 top5
    std::vector<std::pair<std::string, double>> topStocks(symbolPnl.begin(), symbolPnl.end());
    std::sort(topStocks.begin(), topStocks.end(), [](auto& a, auto& b){ return a.second > b.second; });
    int showN = (std::min)(5, static_cast<int>(topStocks.size()));
    if (showN > 0) {
        std::ostringstream topOss;
        topOss << "[backtest] top" << showN << " winners: ";
        for (int i = 0; i < showN; ++i) topOss << topStocks[i].first << "(" << static_cast<int>(topStocks[i].second) << ") ";
        topOss << "\n[backtest] top" << showN << " losers:  ";
        for (int i = 0; i < showN; ++i) topOss << topStocks[topStocks.size()-1-i].first << "(" << static_cast<int>(topStocks[topStocks.size()-1-i].second) << ") ";
        INTERNAL_INFO_STREAM << topOss.str();
    }

    if (onProgress) onProgress(kLoopEnd);

    // 4. 指标计算
    std::vector<double> dailyReturns;
    if (!equityCurve.empty()) {
        const double initialCapital = req.costSpec.initialCapital.value;
        result.metrics.totalReturn = (equityCurve.back() - initialCapital) / initialCapital;
        dailyReturns.reserve(equityCurve.size() - 1);
        for (std::size_t i = 1; i < equityCurve.size(); ++i) {
            if (equityCurve[i - 1] > 0.0)
                dailyReturns.push_back(equityCurve[i] / equityCurve[i - 1] - 1.0);
        }

        using Metrics = ::factor::FactorBacktestMetricsCalculator;
        result.metrics.maxDrawdown   = Metrics::calculateMaxDrawdown(dailyReturns);
        result.metrics.winRate       = Metrics::calculateWinRate(dailyReturns);
        result.metrics.profitFactor  = Metrics::calculateProfitFactor(dailyReturns);
        result.metrics.volatility       = Metrics::calculateVolatility(dailyReturns);
        result.metrics.annualizedReturn = Metrics::calculateAnnualizedReturn(
            equityCurve.back(), initialCapital, totalDays);
        double downsideDev = Metrics::calculateDownsideDeviation(dailyReturns);
        result.metrics.sortinoRatio = Metrics::calculateSortinoRatio(
            result.metrics.annualizedReturn, downsideDev);
        result.metrics.calmarRatio  = Metrics::calculateCalmarRatio(
            result.metrics.annualizedReturn, result.metrics.maxDrawdown);
        result.metrics.sharpeRatio  = Metrics::calculateSharpeRatio(
            result.metrics.annualizedReturn, result.metrics.volatility);
    }

    // 交易统计
    result.tradeStats.totalTrades   = static_cast<int>(totalFills);
    result.tradeStats.winningTrades = static_cast<int>(winningFills);
    result.tradeStats.losingTrades  = static_cast<int>(losingFills);
    result.tradeStats.totalProfit   = domain::strategy::Money{totalProfit};
    result.tradeStats.totalLoss     = domain::strategy::Money{totalLoss};
    result.tradeStats.largestWin    = domain::strategy::Money{largestWin};
    result.tradeStats.largestLoss   = domain::strategy::Money{largestLoss};

    // 时间序列
    for (const auto& dk : view->dates()) result.timeSeries.dates.push_back(domain::DomainDate{dk.value});
    result.timeSeries.portfolioValues = equityCurve;
    result.timeSeries.returns         = dailyReturns;
    {
        std::vector<double> dds; dds.reserve(equityCurve.size()); double pk = equityCurve.empty()?0:equityCurve[0];
        for (double e : equityCurve) { if (e > pk) pk = e; dds.push_back(pk > 0 ? (e-pk)/pk : 0); }
        result.timeSeries.drawdowns = dds;
    }

    // 基准对比 (沪深300)，从 View 中提取基准价格序列
    {
        std::vector<double> bmRet;
        // 从 idToSymbol 反查基准标的的 instrumentId，再从 View 的 close 矩阵提取价格
        std::string bmSym = req.benchmarkIndex.empty() ? "000300.SH" : req.benchmarkIndex;
        std::uint32_t bmId = 0;
        for (const auto& [id, sym] : idToSymbol) {
            if (sym == bmSym) { bmId = id; break; }
        }
        // fallback: 尝试 instrumentId == 300 (沪深300 指数代码)
        if (bmId == 0) {
            for (const auto& instr : view->instruments()) {
                if (instr.value == 300) { bmId = instr.value; break; }
            }
        }
        if (bmId != 0) {
            auto closeMat = view->close();
            const auto& instrs = view->instruments();
            int benchCol = -1;
            for (size_t c = 0; c < instrs.size(); ++c) {
                if (instrs[c].value == bmId) { benchCol = static_cast<int>(c); break; }
            }
            if (benchCol >= 0) {
                const std::size_t colCount = instrs.size();
                bmRet.reserve(static_cast<size_t>(totalDays) - 1);
                for (int r = 1; r < totalDays; ++r) {
                    const std::size_t prevOff = static_cast<std::size_t>(r - 1) * colCount + static_cast<std::size_t>(benchCol);
                    const std::size_t currOff = static_cast<std::size_t>(r)     * colCount + static_cast<std::size_t>(benchCol);
                    double prev = static_cast<double>(closeMat.data[prevOff]);
                    double curr = static_cast<double>(closeMat.data[currOff]);
                    if (prev > 0.0) bmRet.push_back(curr / prev - 1.0);
                }
            }
        }
        if (!bmRet.empty()) {
            auto benchMetrics = ::factor::FactorBacktestMetricsCalculator::calculateBenchmarkMetrics(
                dailyReturns, bmRet);
            result.metrics.beta             = benchMetrics.beta;
            result.metrics.alpha            = benchMetrics.alpha;
            result.metrics.trackingError    = benchMetrics.trackingError;
            result.metrics.informationRatio = benchMetrics.informationRatio;
        }
    }

    if (riskRejectedCount > 0) {
        INTERNAL_DEBUG_STREAM << "[backtest] risk-rejected orders: " << riskRejectedCount;
    }
    if (onProgress) onProgress(100.0);
    INTERNAL_INFO_STREAM << "[backtest] success, returning result";
    result.success = true;
    return result;
}

} // namespace domain::strategy
