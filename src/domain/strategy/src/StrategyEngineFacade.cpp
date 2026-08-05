#include "../include/IStrategyService.h"
#include "../include/NonFactorStrategy.h"
#include "../include/RuntimeFactorSvc.h"
#include "../../../infrastructure/include/database/ISqlDatabase.h"
#include "../../../infrastructure/include/database/NativePgConnectionPool.h"
#include "../../../infrastructure/include/database/DatabaseConfig.h"
#include "../../../infrastructure/include/database/MarketDataRepository.h"
#include "../../../infrastructure/include/database/MarketDataService.h"
#include "../../../infrastructure/include/database/OrderRecorder.h"
#include "../../backtest/include/BacktestRequest.h"
#include "../../backtest/include/BacktestFillSimulator.h"
#include "../../factor/include/factor_compute/FactorEngine.h"
#include "../../factor/include/factor_compute/IMarketDataView.h"
#include "../../factor/include/factor_compute/CachedMarketDataView.h"
#include "../../factor/include/FactorMetricsCalculator.h"
#include "../../factor/include/factor_enums.h"
#include "../../factor/include/FactorInstanceManager.h"
#include "../../trading/TradingTypes.h"
#include "../include/EventRiskSubscriber.h"
#include "RuleGate.h"
#include "RuleVariableProvider.h"
#include "RuleConditionEvaluator.h"
#include "RuleAttribution.h"
#include "RuleLibrary.h"
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
#include <numeric>
#include <string>
#include <unordered_map>
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
    // ── 因子配置/数据注入 — 全部 no-op ──
    void setFactorIds(const std::vector<std::string>&) override {}
    void setDataService(factor::compute::BacktestDataService*) override {}
    void setLiveMarketView(const factor::compute::IMarketDataView*) override {}
    void buildLiveView(const std::vector<astock::database::SqlQueryResultRow>&,
                       const std::vector<std::string>&) override {}
    // ── 视图/元数据查询 — 返回安全默认值 ──
    [[nodiscard]] const factor::compute::IMarketDataView* liveView() const override { return nullptr; }
    [[nodiscard]] std::vector<std::string> getRequiredFields() const override { return {}; }
    [[nodiscard]] int getMaxLookbackDays() const override { return 90; }
    [[nodiscard]] const std::map<std::string, double>* backtestValuesBySymbol(
        const std::string&, std::int32_t) const override { return nullptr; }
};

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
    INTERNAL_INFO_STREAM << "[fromDb] metaJson.length=" << metaJson.size()
                         << " first50=" << metaJson.substr(0, 50);
    auto meta = foundation::json::JsonFacade::parse(metaJson);
    INTERNAL_INFO_STREAM << "[fromDb] meta.has(name)=" << meta.has("name")
                         << " has(behaviorKind)=" << meta.has("behaviorKind")
                         << " name=" << (meta.has("name") ? meta.get("name").asString() : "N/A");

    StrategyCreationParams params;
    params.strategyId     = strategyId;
    params.strategyName   = meta.has("name")        ? meta.get("name").asString()        : "";
    params.description    = meta.has("description") ? meta.get("description").asString()  : "";
    params.behaviorKind   = meta.has("behaviorKind")
        ? static_cast<::domain::strategies::StrategyBehaviorKind>(meta.get("behaviorKind").asInt())
        : ::domain::strategies::StrategyBehaviorKind::Custom;
    // ── 因子存在性: 唯一权威来源 = factor_overlay.enabled (DB字段) ──
    bool factorOverlayEnabled = false;
    int  factorTargetPositionCount = 10;
    double factorMinCompositeScore = 0.0;
    FactorCombineMode factorCombineMode = FactorCombineMode::RankOnly;

    // 解析 parameters JSON
    // ── 规则模板: 策略勾选的 templateId 列表 ──
    std::vector<std::string> enabledRuleTemplates;
    std::string paramJson = row.getString("parameters");
    if (!paramJson.empty() && paramJson != "null") {
        auto root = foundation::json::JsonFacade::parse(paramJson);
        // v2.1: fallback — metadata 无 behaviorKind 但有因子配置时强制 MultiFactor
        if (params.behaviorKind == ::domain::strategies::StrategyBehaviorKind::Custom) {
            if (root.has("factor_overlay") && root.get("factor_overlay").has("enabled")
                && root.get("factor_overlay").get("enabled").asBool()) {
                params.behaviorKind = ::domain::strategies::StrategyBehaviorKind::MultiFactor;
            }
        }
        params.topN = root.has("topN") ? root.get("topN").asInt() : 0;
        params.allowShort = root.has("allowShort") && root.get("allowShort").asBool();
        params.maxPositions = root.has("maxPositions") ? root.get("maxPositions").asInt() : 20;
        // 因子池容量约束: 最大持仓不能超过因子候选池可提供的标的数
        if (factorTargetPositionCount > 0 && params.maxPositions > factorTargetPositionCount)
            params.maxPositions = factorTargetPositionCount;
        params.maxWeightPerStock = root.has("maxWeightPerStock") ? root.get("maxWeightPerStock").asDouble() : 0.1;
        params.minWeightPerStock = root.has("minWeightPerStock") ? root.get("minWeightPerStock").asDouble() : 0.0;
        params.minHoldDays = root.has("minHoldDays") ? root.get("minHoldDays").asInt() : 0;
        params.maxOrderQuantity = root.has("maxOrderQuantity") ? static_cast<std::uint32_t>(root.get("maxOrderQuantity").asInt()) : 100U;
        params.stopLossPercent   = root.has("stopLossPercent")   ? root.get("stopLossPercent").asDouble()   : 10.0;
        params.takeProfitPercent = root.has("takeProfitPercent") ? root.get("takeProfitPercent").asDouble() : 20.0;
        params.maxDrawdownLimit  = root.has("maxDrawdownLimit")  ? root.get("maxDrawdownLimit").asDouble()   : 99.0;
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

        // ── 因子覆盖层: factor_overlay.enabled 是因子存在性的唯一权威来源 ──
        if (root.has("factor_overlay")) {
            auto overlay = root.get("factor_overlay");
            factorOverlayEnabled = overlay.has("enabled") && overlay.get("enabled").asBool();
            if (factorOverlayEnabled) {
                factorTargetPositionCount = overlay.has("targetPositionCount")
                    ? overlay.get("targetPositionCount").asInt() : 50;
                factorMinCompositeScore = overlay.has("minimumCompositeScore")
                    ? overlay.get("minimumCompositeScore").asDouble() : 0.0;
                if (overlay.has("combineMode")) {
                    std::string cm = overlay.get("combineMode").asString();
                    if (cm == "intersection") factorCombineMode = FactorCombineMode::Intersection;
                    else if (cm == "union") factorCombineMode = FactorCombineMode::Union;
                    else if (cm == "quota") factorCombineMode = FactorCombineMode::Quota;
                    // else 保持默认 RankOnly
                }
                // selectionScope 解析供将来扩展(universe 全市场扫描)
                if (overlay.has("allocations")) {
                    auto allocations = overlay.get("allocations");
                    for (std::size_t i = 0; i < allocations.size(); ++i) {
                        auto allocation = allocations.at(i);
                        std::string fid = allocation.has("factor_id")
                            ? allocation.get("factor_id").asString() : "";
                        double weightPercent = allocation.has("weight_percent")
                            ? allocation.get("weight_percent").asDouble() : 0.0;
                        if (!fid.empty() && weightPercent > 0.0)
                            params.factorWeights.push_back({fid, weightPercent});
                    }
                }
            }
        }
        // 从 factorWeights 派生 factorIds
        params.factorIds.clear();
        for (const auto& fw : params.factorWeights)
            params.factorIds.push_back(fw.factorId);
        INTERNAL_INFO_STREAM << "[fromDb] factorOverlayEnabled=" << factorOverlayEnabled
                             << " 因子数=" << params.factorWeights.size()
                             << " factorIds=" << params.factorIds.size();

        // ── 自动标记: 从PG查因子类型, SUPPLY_CHAIN=18 → skipNormalizeFactorIds ──
        for (const auto& fw : params.factorWeights) {
            auto fi = db->executeQuery(
                "SELECT full_config->>'factorType' AS factor_type FROM alpha.factor_instance WHERE instance_id = ?",
                {astock::database::SqlParam{fw.factorId}});
            if (!fi.isEmpty() && fi.getRow(0).getInt("factor_type", 0) == 18) {
                params.skipNormalizeFactorIds.insert(fw.factorId);
            }
        }

        // ── 规则模板勾选: rule_composer_state.stages[].groups[].rules[].templateId ──
        if (root.has("rule_composer_state")) {
            auto composer = root.get("rule_composer_state");
            if (composer.has("stages")) {
                auto stages = composer.get("stages");
                for (std::size_t si = 0; si < stages.size(); ++si) {
                    auto stage = stages.at(si);
                    if (!stage.has("groups")) continue;
                    auto groups = stage.get("groups");
                    for (std::size_t gi = 0; gi < groups.size(); ++gi) {
                        auto group = groups.at(gi);
                        if (!group.has("rules")) continue;
                        auto boundRules = group.get("rules");
                        for (std::size_t ri = 0; ri < boundRules.size(); ++ri) {
                            auto binding = boundRules.at(ri);
                            std::string tid = binding.has("templateId")
                                ? binding.get("templateId").asString() : "";
                            if (!tid.empty()) enabledRuleTemplates.push_back(tid);
                        }
                    }
                }
            }
        }
    }

    // ── factorOverlayEnabled 是因子存在性的唯一权威来源 ──
    // 所有策略类型均支持因子; MultiFactor/MachineLearning 仅决定策略子类, 不参与因子存在性判断
    if (factorOverlayEnabled && !factorSvc) {
        INTERNAL_WARN_STREAM << "[fromDb] ABORT: factorOverlay enabled but factorSvc is null";
        return nullptr;
    }

    // ── 构建因子覆盖层配置 ──
    FactorOverlayConfig factorOverlayCfg;
    factorOverlayCfg.enabled = factorOverlayEnabled;
    if (factorOverlayEnabled) {
        double totalWeight = 0.0;
        for (const auto& fw : params.factorWeights) totalWeight += fw.weight;
        for (const auto& fw : params.factorWeights) {
            factorOverlayCfg.filters.push_back({fw.factorId, 0.1});
            factorOverlayCfg.scalers.push_back({fw.factorId, 1.0});
            double inf = totalWeight > 0.0 ? fw.weight / totalWeight : 1.0;
            factorOverlayCfg.factorInfluence[fw.factorId] = inf;
        }
        factorOverlayCfg.targetPositionCount = factorTargetPositionCount;
        factorOverlayCfg.minimumCompositeScore = factorMinCompositeScore;
        factorOverlayCfg.combineMode = factorCombineMode;
        factorOverlayCfg.needsMarketCapField =
            (params.weightScheme == ::domain::strategies::WeightScheme::MARKET_CAP);
    }

    // ── 构建规则闸门配置 ──
    RuleGateConfig ruleGateCfg;
    ruleGateCfg.templateIds = enabledRuleTemplates;

    // ── 构建风控配置 ──
    RiskConfig riskCfg;
    riskCfg.stopLossPercent        = 0.0;   // 由规则模板接管
    riskCfg.takeProfitPercent      = 0.0;   // 由规则模板接管
    riskCfg.maxDrawdownLimitPercent = params.maxDrawdownLimit;

    // ── 构建调仓配置 ──
    RebalanceConfig rebalanceCfg;
    rebalanceCfg.isDailyFrequency = true;  // 全策略日频
    rebalanceCfg.interval =
        ::domain::strategies::rebalanceFrequencyStepInterval(params.rebalanceFrequency);

    // ── 注入因子服务并构建引擎 ──
    if (factorSvc) {
        if (!params.factorIds.empty()) factorSvc->setFactorIds(params.factorIds);
    }
    auto engine = StrategyEngine::builder()
        .withStrategyId(strategyId)
        .withFactorOverlayConfig(factorOverlayCfg)
        .withRuleGateConfig(ruleGateCfg)
        .withRiskConfig(riskCfg)
        .withRebalanceConfig(rebalanceCfg)
        .maxStrategies(params.maxPositions)
        .maxMarketDataPerBatch(kAllMarketDataBatchCapacity)
        .withFactorService(std::move(factorSvc))
        .build();

    INTERNAL_INFO_STREAM << "[fromDb] " << strategyId << " kind=" << static_cast<int>(params.behaviorKind)
                         << " factorIds=" << params.factorIds.size()
                         << " engine=" << static_cast<void*>(engine.get());
    if (!engine) return nullptr;

    // ── 策略创建 ──
    constexpr StrategyInstanceId kDefaultInstanceId = 1;
    RuntimeStrategyContext ctx(kDefaultInstanceId, 1,
                                params.maxOrderQuantity, params.maxWeightPerStock, true);
    auto runtimeStrategy = StrategyBase::create(kDefaultInstanceId, params);
    if (!runtimeStrategy || !engine->registerStrategy(runtimeStrategy, ctx).isOk())
        return nullptr;

    engine->m_minHoldDays = params.minHoldDays;
    engine->m_strategyName = params.strategyName;
    // 初始化交易日志: logs/策略名/trade_YYYY-MM-DD.jsonl
    if (!params.strategyName.empty()) {
        engine->m_tradeJournal = std::make_unique<TradeJournal>("logs", params.strategyName);
    }
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
        .maxMarketDataPerBatch(kAllMarketDataBatchCapacity)
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
    factorService_->setLiveMarketView(v);
    // 因子/非因子策略都注入上下文视图:
    // 非因子策略用它取 OHLCV; 因子策略权重方案(市值加权/风险平价)用它取市值和波动率
    setContextHistoricalView(view);
}

bool StrategyEngine::prepareMarketData()
{
    // ── 根据策略因子开关决定字段需求与回溯窗口 ──
    std::vector<std::string> extraFields;
    int lookbackDays = 90;
    if (m_hasFactorStrategies) {
        extraFields = factorService_->getRequiredFields();
        lookbackDays = (std::max)(90, factorService_->getMaxLookbackDays());
        // 市值加权方案需要 market_cap, 因子需求未覆盖时追加
        if (m_needsMarketCapField) {
            const char* kMarketCapField = astock::infrastructure::database::field::MARKET_CAP;
            if (std::find(extraFields.begin(), extraFields.end(), kMarketCapField) == extraFields.end()) {
                extraFields.push_back(kMarketCapField);
            }
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
        auto repo = std::make_unique<astock::infrastructure::database::MarketDataRepository>(db);
        auto rawRows = repo->queryAllMarketDailyBarWithFields(startDate, endDate, extraFields);
        INTERNAL_DEBUG_STREAM << "[Engine] query Factor: " << rawRows.size()<< " rows, fields=" << (5 + extraFields.size());
        if (!rawRows.empty()) {
            factorService_->buildLiveView(rawRows, extraFields);
        }
    }

    // ── 注入视图 ──
    const factor::compute::IMarketDataView* v = nullptr;
    if (m_hasFactorStrategies) {
        v = factorService_->liveView();
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
        // ── Phase 1: 因子定池 → 策略只在池内判买点 ──
        if (m_factorSignalProcessor.enabled() && m_poolSelector) {
            auto pool = m_poolSelector->selectPool(m_factorSignalProcessor);
            strategyService_->updateCandidatePool(
                std::unordered_set<std::string>(pool.begin(), pool.end()));
            INTERNAL_INFO_STREAM << "[step] 因子候选池: " << pool.size() << " 标的 (targetPosition="
                                 << m_factorSignalProcessor.targetPositionCount() << ")";
        } else {
            strategyService_->updateCandidatePool({});  // 因子关闭 → 策略扫全市场
        }

        // ── Phase 2: 策略在候选池内评估 → 生成买卖信号 ──
        auto orders = collectOrders(strategyService_->onMarketDataPoint(marketDataPoint));

        // ── Phase 3: 规则闸门审核 ──
        if (m_rulePipeline.enabled() && orders.has_value() && liveMarketView()) {
            const auto* view = liveMarketView();
            const std::int64_t today = domain::market::MarketDataService::instance()
                .activeTradingDay();
            if (today > 0) {
                rules::BacktestRuleVariableProvider gateProvider;
                gateProvider.setDay(view, static_cast<std::int32_t>(today), nullptr);
                auto filtered = m_rulePipeline.filterBuySignals(*orders,
                    [view, &gateProvider](rules::RuleCandidateContext& ctx, const std::string& symbol) {
                        ctx.symbol = symbol;
                        auto dot = symbol.find('.');
                        ctx.code = dot != std::string::npos
                            ? symbol.substr(0, dot) : symbol;
                        const auto& symStrs = view->symbolStrings();
                        for (size_t cc = 0; cc < symStrs.size(); ++cc)
                            if (symStrs[cc] == symbol) { ctx.colIndex = static_cast<int>(cc); break; }
                        gateProvider.setCandidate(ctx);
                    }, gateProvider);
                orders = filtered.empty() ? std::nullopt : std::optional(std::move(filtered));
            }
        }
        return orders;
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
            // 统一持久化: 所有策略共用 m_liveDataPath/app_state.json
            std::string persistPath = m_liveDataPath.empty()
                ? "app_state.json"
                : m_liveDataPath + "/app_state.json";
            m_dailyScheduler = std::make_unique<DailyEodScheduler>(
                [this](std::function<void()> fn) {
                    if (m_dedicatedExecutor && m_loopRunning.load(std::memory_order_acquire))
                        m_dedicatedExecutor->post(std::move(fn));
                },
                persistPath
            );
            m_dailyScheduler->setStrategyId(m_strategyId);
            // 从 TradingConnectionConfig 读取 EOD 触发时间(默认 15:00)
            {
                auto& cfgMgr = foundation::config::ConfigManager::instance();
                auto cfg = cfgMgr.loadConfigFile(foundation::config::ConfigFile::TradingConnection);
                std::string triggerTime = "15:00";
                if (cfg && !cfg->isNull() && cfg->has("eodTriggerTime"))
                    triggerTime = cfg->get("eodTriggerTime").asString();
                m_dailyScheduler->setEodTriggerTime(triggerTime);
                INTERNAL_INFO_STREAM << "[启动] DailyEod 触发时间 " << triggerTime;
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

                MapPositionProvider posProvider(posQtyMap);
                auto finalOrders = m_orderGenerator.generate(*orders, posProvider, account, mdp.lastPrice());
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

    // ── 规则闸门: 市场审核(每日冻结) ──
    bool ruleAllowEntriesEod = true;
    std::unordered_map<std::string, int> eodSymToCol;
    if (m_ruleGate.enabled() && liveMarketView()) {
        const auto* view = liveMarketView();
        const auto& instrs = view->instruments();
        const auto& symStrs = view->symbolStrings();
        for (size_t c = 0; c < symStrs.size() && c < instrs.size(); ++c)
            eodSymToCol[stripExchange(symStrs[c])] = static_cast<int>(c);
        rules::BacktestRuleVariableProvider eodProvider;
        eodProvider.setDay(view, std::stoi(tradingDay), nullptr);
        ruleAllowEntriesEod = m_ruleGate.allowNewEntriesToday(eodProvider);
        if (!ruleAllowEntriesEod)
            INTERNAL_INFO_STREAM << "[StrategyEngine] EOD 规则闸门: 市场冻结, 当日不产生新买单";
    }

    // ── 择时闸门 (v0.13): 与回测一致, 每日盘前判断大盘状态 ──
    TimingResult eodTiming;
    if (liveMarketView()) {
        const auto* view = liveMarketView();
        auto closeMat = view->close();
        const auto& viewInstrs = view->instruments();
        int eodCols = static_cast<int>(viewInstrs.size());
        int eodRows = static_cast<int>(view->dates().size());
        // 查找沪深300列 (与回测一致)
        int eodBmCol = -1;
        const auto& eodSyms = view->symbolStrings();
        for (int c = 0; c < eodCols && c < static_cast<int>(eodSyms.size()); ++c) {
            if (eodSyms[c] == "000300.SH") { eodBmCol = c; break; }
        }
        if (eodBmCol >= 0 && eodRows > 60) {
            int lastRow = eodRows - 1;
            MarketTimingSnapshot ts;
            const double idxClose = static_cast<double>(closeMat.data[
                static_cast<size_t>(lastRow) * static_cast<size_t>(eodCols) + static_cast<size_t>(eodBmCol)]);
            if (idxClose > 0.0) {
                ts.indexClose = idxClose;
                double sum20=0, sum60=0; int cnt20=0, cnt60=0;
                for (int back=0; back<60 && (lastRow-back)>=0; ++back) {
                    double c = static_cast<double>(closeMat.data[
                        static_cast<size_t>(lastRow-back)*static_cast<size_t>(eodCols)+static_cast<size_t>(eodBmCol)]);
                    if (c>0) { if (back<20) { sum20+=c; ++cnt20; } sum60+=c; ++cnt60; }
                }
                ts.ma20 = cnt20>0 ? sum20/cnt20 : idxClose;
                ts.ma60 = cnt60>0 ? sum60/cnt60 : idxClose;
                ts.ma20AboveMa60 = ts.ma20 > ts.ma60;
                double sum20_5=0; int cnt20_5=0;
                for (int back=5; back<25 && (lastRow-back)>=0; ++back) {
                    double c = static_cast<double>(closeMat.data[
                        static_cast<size_t>(lastRow-back)*static_cast<size_t>(eodCols)+static_cast<size_t>(eodBmCol)]);
                    if (c>0) { sum20_5+=c; ++cnt20_5; }
                }
                ts.ma20Rising = cnt20_5>0 ? ts.ma20 > sum20_5/cnt20_5 : false;
                if (eodCols>0) { int up=0,tot=0;
                    for (int c=0; c<eodCols; ++c) {
                        double t = static_cast<double>(closeMat.data[lastRow*static_cast<size_t>(eodCols)+c]);
                        double p = static_cast<double>(closeMat.data[(lastRow-1)*static_cast<size_t>(eodCols)+c]);
                        if (t>1e-9&&p>1e-9) { if(t>p)++up; ++tot; }
                    }
                    ts.advanceRatio = tot>0?static_cast<double>(up)/tot:0.5;
                }
                ts.atrPercent = 0.02;
                eodTiming = m_timingGate.evaluate(ts);
                INTERNAL_INFO_STREAM << "[StrategyEngine] EOD 择时: exposure=" << eodTiming.targetExposure
                    << " allowNew=" << eodTiming.allowNewEntries
                    << " liquidate=" << eodTiming.forceLiquidate
                    << " reason=" << eodTiming.reason;
            }
        }
    }

    // 择时空仓: 一键清仓
    if (eodTiming.forceLiquidate && !m_circuitBreaker.isHalted()) {
        liquidateAll();
        return EodEvaluationStatus::Submitted;
    }

    for (const auto& sym : symbols) {
        // 规则闸门冻结 或 择时不允新开仓 → 跳过
        if (!ruleAllowEntriesEod || !eodTiming.allowNewEntries) {
            INTERNAL_INFO_STREAM << "[StrategyEngine] EOD 冻结:"
                << " ruleGate=" << (ruleAllowEntriesEod ? "允许" : "冻结")
                << " timingGate=" << (eodTiming.allowNewEntries ? "允许" : "冻结")
                << " timingReason=" << eodTiming.reason
                << " liquidate=" << eodTiming.forceLiquidate;
            break;
        }
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
                    // 保留 kTargetWeight — OrderGenerator 依赖此字段计算 delta
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
    // 规则闸门: 信号审核(实盘EOD, Phase1后/Phase2前)
    // ═══════════════════════════════════════════════════════════
    int eodGateRejected = 0;
    if (m_ruleGate.enabled() && liveMarketView()) {
        const auto* view = liveMarketView();
        const int dayValue = std::stoi(tradingDay);
        // 构造当日 symbol → column 映射(用于 Provider 查均线/量比)
        std::unordered_map<std::string, int> symToCol;
        const auto& instrs = view->instruments();
        const auto& symStrs = view->symbolStrings();
        for (size_t c = 0; c < symStrs.size() && c < instrs.size(); ++c)
            symToCol[stripExchange(symStrs[c])] = static_cast<int>(c);

        rules::BacktestRuleVariableProvider eodProvider;
        eodProvider.setDay(view, dayValue, nullptr);
        std::vector<PendingOrder> filtered;
        filtered.reserve(pendingOrders.size());
        for (auto& po : pendingOrders) {
            if (po.order.side() == OrderSide::Buy) {
                const std::string sym6 = stripExchange(po.order.symbol());
                rules::RuleCandidateContext ctx;
                auto cite = symToCol.find(sym6);
                ctx.colIndex = cite != symToCol.end() ? cite->second : -1;
                ctx.symbol = po.order.symbol();
                ctx.code = sym6;
                eodProvider.setCandidate(ctx);
                if (!m_ruleGate.allowSignal(eodProvider)) {
                    ++eodGateRejected; continue;
                }
            }
            filtered.push_back(std::move(po));
        }
        pendingOrders = std::move(filtered);
    }

    // ═══════════════════════════════════════════════════════════
    // Phase 2+3: 持仓感知建单 (OrderGenerator)
    // ═══════════════════════════════════════════════════════════

    // ── 规则闸门: 持仓出场审核 ──
    int eodPositionExits = 0;
    if (m_ruleGate.enabled() && liveMarketView() && !positions.empty()) {
        const auto* view = liveMarketView();
        const int dayValue = std::stoi(tradingDay);
        rules::BacktestRuleVariableProvider exitProvider;
        exitProvider.setDay(view, dayValue, nullptr);
        for (const auto& pos : positions) {
            if (pos.quantity <= 0 || pos.costPrice <= 0.0) continue;
            const std::string sym6 = stripExchange(pos.symbol);
            rules::RuleCandidateContext posCtx;
            posCtx.symbol = pos.symbol;
            posCtx.code = sym6;
            auto cite = eodSymToCol.find(sym6);
            posCtx.colIndex = cite != eodSymToCol.end() ? cite->second : -1;
            posCtx.isHolding = true;
            posCtx.entryPrice = pos.costPrice;
            const double currentPrice = pos.lastPrice;
            if (currentPrice > 0.0 && posCtx.entryPrice > 0.0)
                posCtx.pnlPercent = (currentPrice - posCtx.entryPrice) / posCtx.entryPrice * 100.0;
            exitProvider.setCandidate(posCtx);
            const rules::RuleAction action = m_ruleGate.positionAction(exitProvider);
            if (action == rules::RuleAction::Exit || action == rules::RuleAction::Reduce) {
                OrderRequest exitOrderReq;
                exitOrderReq.setSymbol(pos.symbol);
                exitOrderReq.setSide(OrderSide::Sell);
                exitOrderReq.setQuantity(action == rules::RuleAction::Exit
                    ? pos.quantity : (std::max)(static_cast<std::int64_t>(1), pos.quantity / 2));
                exitOrderReq.setOrderType(domain::trading::OrderType::Market);
                exitOrderReq.setPrice(currentPrice);
                PendingOrder exitPo;
                exitPo.order = std::move(exitOrderReq);
                exitPo.tickPrice = currentPrice;
                exitPo.signalScore = 1.0;
                exitPo.targetWeight = 0.0;
                pendingOrders.push_back(std::move(exitPo));
                ++eodPositionExits;
            }
        }
        if (eodPositionExits > 0)
            INTERNAL_INFO_STREAM << "[StrategyEngine] EOD 规则闸门: 持仓出场=" << eodPositionExits;
    }

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

    MapPositionProvider posProvider(posQtyMap);
    auto finalOrders = m_orderGenerator.generate(rawOrders, posProvider, account, priceForWeight);

    // ── 规则闸门输出(当日) ──
    if (m_ruleGate.enabled()) {
        INTERNAL_INFO_STREAM << "[StrategyEngine] EOD 规则闸门: 冻结=" << (!ruleAllowEntriesEod)
                             << " 信号审核拒绝=" << eodGateRejected
                             << "/" << (totalGenerated + eodGateRejected)
                             << " 持仓出场=" << eodPositionExits
                             << " (绑定模板=" << m_ruleGate.boundTemplateCount() << ")";
    }

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

StrategyEngine::Builder& StrategyEngine::Builder::withStrategyId(std::string id)
{
    strategyId_ = std::move(id);
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::withFactorOverlayConfig(const FactorOverlayConfig& cfg)
{
    factorOverlayCfg_ = cfg;
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::withRuleGateConfig(const RuleGateConfig& cfg)
{
    ruleGateCfg_ = cfg;
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::withRiskConfig(const RiskConfig& cfg)
{
    riskCfg_ = cfg;
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::withRebalanceConfig(const RebalanceConfig& cfg)
{
    rebalanceCfg_ = cfg;
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::withTimingGate(const MarketTimingGate& gate)
{
    timingGate_ = gate;
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::withCircuitBreaker(const TimedCircuitBreaker& breaker)
{
    circuitBreaker_ = breaker;
    return *this;
}

std::string StrategyEngine::Builder::validate() const
{
    if (factorOverlayCfg_.enabled && !factorOverlayCfg_.isValid())
        return "FactorOverlay enabled but filters empty";
    if (!rebalanceCfg_.isValid())
        return "RebalanceConfig invalid: interval=" + std::to_string(rebalanceCfg_.interval);
    if (factorOverlayCfg_.enabled && !factorService_)
        return "FactorOverlay enabled but factorService is null";
    return {};  // 空字符串 = 通过
}

std::unique_ptr<StrategyEngine> StrategyEngine::Builder::build()
{
    // ── 校验 ──
    if (auto err = validate(); !err.empty()) {
        INTERNAL_ERROR_STREAM << "[Builder] validate failed: " << err;
        return nullptr;
    }

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

    // ── 应用配置（值类型移动，构建后 Builder 失效）──
    if (!strategyId_.empty()) {
        engine->setStrategyId(strategyId_);
        engine->m_orderBuilder.setStrategyId(strategyId_);
    }

    // ── 因子覆盖层配置 ──
    if (factorOverlayCfg_.enabled) {
        engine->m_factorSignalProcessor.setFilters(factorOverlayCfg_.filters);
        engine->m_factorSignalProcessor.setScalers(factorOverlayCfg_.scalers);
        engine->m_factorSignalProcessor.setTargetPositionCount(factorOverlayCfg_.targetPositionCount);
        engine->m_factorSignalProcessor.setMinimumCompositeScore(factorOverlayCfg_.minimumCompositeScore);
        engine->m_factorSignalProcessor.setCombineMode(factorOverlayCfg_.combineMode);
        engine->m_needsMarketCapField = factorOverlayCfg_.needsMarketCapField;
        // v2.1: 规则形态分自动注入因子权重 (因子50% + 规则50%)
        if (ruleGateCfg_.enabled()) {
            auto influence = factorOverlayCfg_.factorInfluence;
            double totalWeight = 0.0;
            for (const auto& [fid, w] : influence) totalWeight += w;
            // 归一化: 因子权重总和缩放到 50%, rule_score 占 50%
            if (totalWeight > 0.0) {
                double scale = 0.5 / totalWeight;
                for (auto& [fid, w] : influence) w *= scale;
            }
            influence["rule_score"] = 0.5;
            engine->m_factorSignalProcessor.setFactorInfluence(influence);
        } else {
            engine->m_factorSignalProcessor.setFactorInfluence(factorOverlayCfg_.factorInfluence);
        }
        engine->m_poolSelector = createPoolSelector(factorOverlayCfg_.combineMode);
    } else {
        engine->m_poolSelector = std::make_unique<NullPoolSelector>();
    }

    // ── 规则闸门配置 ──
    engine->m_enableCandlePatterns = ruleGateCfg_.enableCandlePatterns;
    if (ruleGateCfg_.enabled()) {
        const auto* ruleLibrary = rules::sharedRuleLibrary();
        if (ruleLibrary) {
            const auto& ablated = ruleGateCfg_.ablationEnabled
                ? ruleGateCfg_.ablatedTemplateIds
                : std::vector<std::string>{};
            const int bound = engine->m_ruleGate.configure(
                ruleGateCfg_.templateIds, *ruleLibrary, ablated);
            INTERNAL_INFO_STREAM << "[Builder] 规则闸门: 勾选 " << ruleGateCfg_.templateIds.size()
                                 << " 个模板, 绑定 " << bound
                                 << " 消融=" << ablated.size()
                                 << " candle=" << ruleGateCfg_.enableCandlePatterns;
        } else {
            INTERNAL_WARN_STREAM << "[Builder] 规则库不可用, 策略勾选的 "
                                 << ruleGateCfg_.templateIds.size() << " 个规则模板不生效";
        }
    }

    // ── 风控配置 ──
    engine->m_riskConfig = riskCfg_;
    RiskManager::instance().setRiskConfig(riskCfg_);

    // ── 择时闸门 + 风控熔断器 (v0.13) ──
    engine->m_timingGate = timingGate_;
    engine->m_circuitBreaker = circuitBreaker_;

    // ── 调仓频率配置 ──
    engine->m_isDailyFrequency = rebalanceCfg_.isDailyFrequency;
    engine->m_rebalanceInterval = rebalanceCfg_.interval;

    INTERNAL_INFO_STREAM << "[Builder] 风控: stopLoss=" << riskCfg_.stopLossPercent
                         << " takeProfit=" << riskCfg_.takeProfitPercent
                         << " maxDrawdown=" << riskCfg_.maxDrawdownLimitPercent
                         << " isDaily=" << rebalanceCfg_.isDailyFrequency
                         << " rebalanceInterval=" << rebalanceCfg_.interval;

    return engine;
}

StrategyBacktestResult StrategyEngine::backtest(
    const domain::backtest::BacktestRequest& req,
    factor::compute::BacktestDataService* dataSvc,
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

    // ── 将 DataSvc 注入因子服务，使因子计算能访问市场数据 ──
    if (factorService_) {
        factorService_->setDataService(dataSvc);
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

    // 2. 构建 symbol→列号映射 + 账户初始化
    // 契约: 全链路 symbol 均为真实完整代码 (如 "300767.SZ"), 与视图 symbolStrings/数据列逐位对齐
    std::unordered_map<std::string, int> symbolToCol;
    {
        const auto& symStrs = view->symbolStrings();
        if (symStrs.size() != view->instruments().size()) {
            result.errorMessage = "Symbol/instrument column mismatch";
            return result;
        }
        symbolToCol.reserve(symStrs.size());
        for (std::size_t c = 0; c < symStrs.size(); ++c)
            symbolToCol[symStrs[c]] = static_cast<int>(c);
    }

    double latestEquity = req.costSpec.initialCapital.value;  // 最新总资产(现金+持仓市值)
    double cash = req.costSpec.initialCapital.value;          // 可用现金
    std::unordered_map<std::string, domain::trading::Position> backtestPositions;
    auto btAccount = [&]() {
        domain::trading::AccountSnapshot a;
        a.setTotalAsset(latestEquity);
        a.setAvailableCash(cash);
        a.setMarketValue(latestEquity - cash);
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

    int riskRejectedCount = 0;
    int totalFills = 0, winningFills = 0, losingFills = 0;
    int stopLossExitCount = 0, ruleExitCount = 0, drawdownBlockCount = 0;
    int stopLossFilled = 0, ruleExitFilled = 0, normalSellFilled = 0;
    int stopLossSkippedNoHeld = 0, totalStopLossOrders = 0;
    std::unordered_set<std::string> todayStopLossSyms, todayRuleExitSyms;
    std::unordered_set<std::string> boughtToday;  // T+1: 当日买入的标的禁止同日卖出
    double totalProfit = 0.0, totalLoss = 0.0, largestWin = 0.0, largestLoss = 0.0;
    std::unordered_map<std::string, double> buyPriceMap;
    std::unordered_map<std::string, double> buySignalScoreMap;  // 买入时因子信号强度
    std::unordered_map<std::string, double> symbolPnl;  // 逐标的累计盈亏
    std::vector<double> equityCurve;
    equityCurve.reserve(totalDays);
    double peakEquity = static_cast<double>(req.costSpec.initialCapital.value);
    // 混合模式各因子实际参与天数(喂入快照成功计数), 回测结束输出
    std::unordered_map<std::string, int> hybridFactorCoveredDays;
    // ── 诊断: 持仓/因子绩效跟踪 ──
    std::unordered_map<std::string, double> buyDateMap;      // symbol → entryDate(YYYYMMDD)
    std::unordered_map<std::string, double> buyFactorScoreMap2; // symbol → entry时因子compositeScore
    std::vector<double> holdingDaysVec;                       // 每笔持仓天数
    std::vector<double> tradePnlVec;                          // 每笔盈亏(与holdingDays对齐)
    std::vector<double> entryFactorScores;                    // 每笔入场的因子分
    int dailyPositionSum{0};                                  // Σ每日持仓数 → avg = sum/days
    int daysWithTrades{0};                                    // 有交易的交易日数
    double deployedCapitalSum{0.0};                           // Σ每日已部署资金
    size_t totalBuySignals{0};                                // 策略产生的总买入信号数
    size_t totalPoolCandidates{0};                             // 因子池累计候选数
    int poolSelectionDays{0};                                  // 因子池选择的天数
    // 规则闸门: 变量提供者 + 当日新开仓许可
    rules::BacktestRuleVariableProvider ruleProvider;
    ruleProvider.setConceptQueriesEnabled(false);  // 默认关闭: 无规则引用 concept.* 变量
    bool ruleAllowEntriesToday = true;

    // 查找基准指数列（用于大盘解冻判断）— 不在视图内则保持 -1, 下游跳过解冻判断
    int bmColIdx = -1;
    {
        const std::string bmSym = req.benchmarkIndex.empty() ? "000300.SH" : req.benchmarkIndex;
        const auto bmIt = symbolToCol.find(bmSym);
        if (bmIt != symbolToCol.end()) bmColIdx = bmIt->second;
    }

    // 数据准备完成 → 0%
    if (onProgress) onProgress(0.0);

    // 3. 逐日驱动
    // 回测结束(含早退)复位策略上下文: 视图归 dataSvc 所有, 回测返回后即失效, 防悬垂;
    // 行号复位 -1 (实盘语义=最后一行), 避免复用引擎时残留回测状态
    struct ContextResetGuard {
        StrategyEngine& eng;
        explicit ContextResetGuard(StrategyEngine& e) : eng(e) {}
        ~ContextResetGuard() {
            eng.setContextHistoricalView(nullptr);
            if (eng.strategyService_) eng.strategyService_->setContextEvaluationRow(-1);
        }
    };
    ContextResetGuard contextResetGuard(*this);

    // 第一步会触发惰性因子计算（FactorEngine::compute），我们不知道它占总时间的比例
    // 但它是真实工作，后续逐日循环也是真实工作
    // ── 规则归因收集器 ──
    rules::AttributionCollector attributionCollector;
    ruleProvider.setCandlePatternsEnabled(m_enableCandlePatterns);

    // 回测日期窗口: 截断到请求的 startDate ~ endDate
    const int kWindowStartDay = req.window.startDate.to_yyyymmdd();
    const int kWindowEndDay   = req.window.endDate.to_yyyymmdd();

    const double kSetupFrac  = 0.0;
    const double kLoopStart  = 0.0;
    const double kLoopEnd    = 90.0;
    const double kMetricsEnd = 100.0;
    for (int r = 0; r < totalDays; ++r) {
        // 每次迭代重新获取 view（因子计算可能重建了 m_ownedView）
        auto batch = dataSvc->loadBatch(0);
        const auto* view = batch.marketView;
        if (!view) { result.errorMessage = "View lost during backtest"; return result; }

        // 逐日注入时点上下文: 策略(非因子 OHLCV / 因子权重方案市值·波动率)
        // 只读到第 r 行为止的数据, 避免前视偏差
        setContextHistoricalView(view);
        if (strategyService_) strategyService_->setContextEvaluationRow(r);

        const auto& dates = view->dates();
        const int currentDay = dates[static_cast<std::size_t>(r)].value;
        // 跳过不在回测窗口内的日期 (用于样本内/样本外切分)
        if (currentDay < kWindowStartDay || currentDay > kWindowEndDay) {
            equityCurve.push_back(latestEquity);
            continue;
        }

        // ── 调仓间隔检查 (与实盘 EOD 一致): 非调仓日跳过策略评估 ──
        bool isRebalanceDay = true;
        if (m_rebalanceInterval > 1 && !m_lastRebalanceDate.empty()) {
            isRebalanceDay = false;
            std::string dateStr = std::to_string(currentDay);
            int since = 0;
            for (int i = 0; i < m_rebalanceInterval && !dateStr.empty(); ++i) {
                char prevOut[32] = {};
                if (::get_previous_trading_date("SZSE", dateStr.c_str(), prevOut) != 0)
                    ::get_previous_trading_date("SHSE", dateStr.c_str(), prevOut);
                dateStr = prevOut;
                if (dateStr.empty()) break;
                ++since;
                if (dateStr == m_lastRebalanceDate) { isRebalanceDay = (since >= m_rebalanceInterval); break; }
            }
        }

        const auto& instruments = view->instruments();
        auto closeMat = view->close();
        auto volumeMat = view->volume();

        if (r == 0 || r == totalDays-1 || r % 100 == 0) {
            INTERNAL_INFO_STREAM << "[backtest] day " << r << "/" << totalDays << " equity=" << btAccount().totalAsset() << " cash=" << cash << " positions=" << backtestPositions.size();
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

        // ── 规则闸门: 每日市场快照 + 新开仓许可 ──
        if (m_ruleGate.enabled()) {
            ruleProvider.setDay(view, dates[static_cast<std::size_t>(r)].value, &backtestPositions);
            ruleAllowEntriesToday = m_ruleGate.allowNewEntriesToday(ruleProvider);
        } else {
            ruleAllowEntriesToday = true;
        }

        // ── 择时闸门 (v0.13): 每日盘前判断大盘状态 → 决定仓位比例 ──
        TimingResult timing;
        {
            MarketTimingSnapshot ts;
            // 从 Benchmark 指数列提取快照
            if (bmColIdx >= 0 && r > 60) {
                const double idxClose = static_cast<double>(closeMat.data[
                    static_cast<size_t>(r) * static_cast<size_t>(colCount) + static_cast<size_t>(bmColIdx)]);
                if (idxClose > 0.0) {
                    ts.indexClose = idxClose;
                    // MA20
                    double sum20 = 0.0; int cnt20 = 0;
                    for (int back = 0; back < 20 && (r - back) >= 0; ++back) {
                        double c = static_cast<double>(closeMat.data[
                            static_cast<size_t>(r - back) * static_cast<size_t>(colCount)
                            + static_cast<size_t>(bmColIdx)]);
                        if (c > 0.0) { sum20 += c; ++cnt20; }
                    }
                    ts.ma20 = cnt20 > 0 ? sum20 / cnt20 : idxClose;
                    // MA60
                    double sum60 = 0.0; int cnt60 = 0;
                    for (int back = 0; back < 60 && (r - back) >= 0; ++back) {
                        double c = static_cast<double>(closeMat.data[
                            static_cast<size_t>(r - back) * static_cast<size_t>(colCount)
                            + static_cast<size_t>(bmColIdx)]);
                        if (c > 0.0) { sum60 += c; ++cnt60; }
                    }
                    ts.ma60 = cnt60 > 0 ? sum60 / cnt60 : idxClose;
                    ts.ma20AboveMa60 = ts.ma20 > ts.ma60;
                    // MA20 斜率: 比较当前MA20 vs 5日前MA20
                    double sum20_5 = 0.0; int cnt20_5 = 0;
                    for (int back = 5; back < 25 && (r - back) >= 0; ++back) {
                        double c = static_cast<double>(closeMat.data[
                            static_cast<size_t>(r - back) * static_cast<size_t>(colCount)
                            + static_cast<size_t>(bmColIdx)]);
                        if (c > 0.0) { sum20_5 += c; ++cnt20_5; }
                    }
                    double ma20_5dAgo = cnt20_5 > 0 ? sum20_5 / cnt20_5 : ts.ma20;
                    ts.ma20Rising = ts.ma20 > ma20_5dAgo;
                    // 宽度: 统计当日上涨股票占比 (从全市场close推算)
                    if (colCount > 0) {
                        int upCnt = 0, totalCnt = 0;
                        for (int c = 0; c < colCount; ++c) {
                            double todayC = static_cast<double>(closeMat.data[rowOffset + static_cast<size_t>(c)]);
                            if (r > 0) {
                                double prevC = static_cast<double>(closeMat.data[
                                    static_cast<size_t>(r - 1) * static_cast<size_t>(colCount) + static_cast<size_t>(c)]);
                                if (todayC > 1e-9 && prevC > 1e-9) {
                                    if (todayC > prevC) ++upCnt;
                                    ++totalCnt;
                                }
                            }
                        }
                        ts.advanceRatio = totalCnt > 0 ? static_cast<double>(upCnt) / totalCnt : 0.5;
                    }
                    // 波动: 默认值 (ATR单独计算成本高, 用中性值)
                    ts.atrPercent = 0.02;
                }
            }
            timing = m_timingGate.evaluate(ts);
            if (r == 0 || r % 100 == 0) {
                INTERNAL_INFO_STREAM << "[backtest] day " << r
                    << " 择时: exposure=" << timing.targetExposure
                    << " allowNew=" << timing.allowNewEntries
                    << " liquidate=" << timing.forceLiquidate
                    << " reason=" << timing.reason;
            }
        }

        // ── 风控熔断检查 (v0.13) ──
        bool circuitHalted = m_circuitBreaker.isHalted();
        bool forceLiquidate = timing.forceLiquidate && !circuitHalted;
        std::optional<std::vector<OrderRequest>> ordersOpt;
        double equity = 0.0;

        if (circuitHalted || forceLiquidate) {
            // 熔断或择时空仓: 只平仓不开仓
            std::vector<OrderRequest> exitOrders;
            for (const auto& kvPos : backtestPositions) {
                if (kvPos.second.quantity() > 0) {
                    OrderRequest exitOrder;
                    exitOrder.setSymbol(kvPos.first);
                    exitOrder.setSide(OrderSide::Sell);
                    exitOrder.setQuantity(kvPos.second.quantity());
                    exitOrder.setOrderType(domain::trading::OrderType::Market);
                    exitOrders.push_back(std::move(exitOrder));
                }
            }
            if (!exitOrders.empty()) ordersOpt = std::move(exitOrders);
            strategyService_->updateCandidatePool({});  // 禁止新品种进入候选池
            double mv = 0.0;
            for (const auto& kvPos : backtestPositions) {
                const auto& sym = kvPos.first;
                if (kvPos.second.quantity() <= 0) continue;
                const double px = static_cast<double>(closeMat.data[
                    rowOffset + static_cast<size_t>(symbolToCol.at(sym))]);
                if (px > 0.0) mv += px * static_cast<double>(kvPos.second.quantity());
            }
            equity = cash + mv;
            equityCurve.push_back(equity);  // 熔断日也记录净值, 保证与 dates 对齐
        } else {
        // ── 非调仓日: 只更新净值, 不评估策略 ──
        if (!isRebalanceDay) {
            double mv = 0.0;
            for (const auto& [sym, pos] : backtestPositions) {
                if (pos.quantity() <= 0) continue;
                const double px = static_cast<double>(closeMat.data[
                    rowOffset + static_cast<size_t>(symbolToCol.at(sym))]);
                if (px > 0.0) mv += px * static_cast<double>(pos.quantity());
            }
            equity = cash + mv;
            equityCurve.push_back(equity);
            latestEquity = equity;
            if (equity > peakEquity) peakEquity = equity;
            m_circuitBreaker.updateEndOfDay(equity);
            dailyPositionSum += static_cast<int>(backtestPositions.size());
            deployedCapitalSum += mv;
            if (!backtestPositions.empty()) ++daysWithTrades;
            continue;  // 跳过策略评估和订单处理, 进入下一天
        }

        // ── 因子值注入 (所有策略共用: 填充缓存 + 更新快照) ──
        {
            const std::int32_t dayValue = dates[static_cast<std::size_t>(r)].value;
            for (const auto& fid : m_factorSignalProcessor.factorIds()) {
                const auto* factorVals = factorService_->backtestValuesBySymbol(fid, dayValue);
                if (!factorVals) continue;
                ++hybridFactorCoveredDays[fid];
                std::unordered_map<std::string, double> bySymbol;
                bySymbol.reserve(factorVals->size());
                for (const auto& [code, value] : *factorVals)
                    bySymbol[foundation::market::AStockSymbol::fromCode(code).fullSymbol()] = value;
                m_factorSignalProcessor.updateSnapshot(fid, bySymbol);
            }
        }

        // ── 因子定池/候选池 (因子策略跳过: 自行全量排名) ──
        if (m_factorSignalProcessor.enabled() && m_poolSelector && !m_hasFactorStrategies) {
            // v2.1: 规则形态分注入 (合成因子 "rule_score")
            if (m_ruleGate.enabled()) {
                std::unordered_map<std::string, double> ruleScoreMap;
                const auto& allSyms = m_factorSignalProcessor.allSymbols();
                for (const auto& sym : allSyms) {
                    rules::RuleCandidateContext candidateCtx;
                    candidateCtx.symbol = sym;
                    auto dot = sym.find('.');
                    candidateCtx.code = dot != std::string::npos ? sym.substr(0, dot) : sym;
                    candidateCtx.isHolding = false;
                    ruleProvider.setCandidate(candidateCtx);
                    ruleScoreMap[sym] = m_ruleGate.entryScore(ruleProvider);
                }
                m_factorSignalProcessor.updateSnapshot("rule_score", ruleScoreMap);
            }
            auto pool = m_poolSelector->selectPool(m_factorSignalProcessor);
            if (!pool.empty()) { totalPoolCandidates += pool.size(); ++poolSelectionDays; }
            strategyService_->updateCandidatePool(
                std::unordered_set<std::string>(pool.begin(), pool.end()));
            // 注入因子复合评分到上下文 (非因子策略用, 按策略配置的因子权重加权)
            {
                std::unordered_map<std::string, double> factorScoreMap;
                for (const auto& sym : pool)
                    factorScoreMap[sym] = m_factorSignalProcessor.compositeScore(sym);
                strategyService_->updateFactorScores(std::move(factorScoreMap));
            }
            if (r == 0 || r % 100 == 0)
                INTERNAL_INFO_STREAM << "[backtest] day " << r << " 因子候选池: " << pool.size()
                                     << " 标的 (targetPosition=" << m_factorSignalProcessor.targetPositionCount() << ")";
        } else {
            strategyService_->updateCandidatePool({});
        }

        // 注入当前持仓权重到策略上下文 (出池卖出评估需要)
        {
            std::unordered_map<std::string, double> wmap;
            for (const auto& [sym, pos] : backtestPositions)
                if (pos.quantity() > 0) wmap[sym] = static_cast<double>(pos.quantity());
            strategyService_->updateCurrentWeights(wmap);
        }

        ordersOpt = stepBatch(mdpBatch);

        // ── 规则闸门: 命中退出的持仓生成卖出订单(stepBatch 后注入) ──
        if (m_ruleGate.enabled() && !backtestPositions.empty()) {
            std::vector<OrderRequest> generatedExits;
            for (const auto& kvPos : backtestPositions) {
                const std::string& fullSymbol = kvPos.first;
                const auto& pos = kvPos.second;
                if (pos.quantity() <= 0) continue;
                rules::RuleCandidateContext posCtx;
                posCtx.symbol = fullSymbol;
                auto dot = fullSymbol.find('.');
                posCtx.code = dot != std::string::npos ? fullSymbol.substr(0, dot) : fullSymbol;
                posCtx.isHolding = true;
                posCtx.holdDays = 0.0;
                auto bpIt = buyPriceMap.find(fullSymbol);
                posCtx.entryPrice = bpIt != buyPriceMap.end() ? bpIt->second : 0.0;
                posCtx.colIndex = symbolToCol.at(fullSymbol);
                const double currentPrice = static_cast<double>(closeMat.data[
                    rowOffset + static_cast<std::size_t>(posCtx.colIndex)]);
                if (posCtx.entryPrice > 0.0 && currentPrice > 0.0)
                    posCtx.pnlPercent = (currentPrice - posCtx.entryPrice) / posCtx.entryPrice * 100.0;  // 百分数口径, 与规则阈值(如 ≥12)一致
                ruleProvider.setCandidate(posCtx);
                const rules::RuleAction exitAction = m_ruleGate.positionAction(ruleProvider);

                // ── 最少持有期检查(v0.15): 非硬止损规则触发的卖出需满足 minHoldDays ──
                bool isHardStop = false;
                if (m_minHoldDays > 0 && (exitAction == rules::RuleAction::Exit || exitAction == rules::RuleAction::Reduce)) {
                    // 优先检查规则级 tags, 回退到模板级 tags
                    const auto& ruleTags = m_ruleGate.lastHitRuleTags();
                    const auto& tmplTags = m_ruleGate.lastHitTemplateTags();
                    isHardStop = std::find(ruleTags.begin(), ruleTags.end(), "hard-stop") != ruleTags.end()
                              || std::find(tmplTags.begin(), tmplTags.end(), "hard-stop") != tmplTags.end();
                    if (!isHardStop) {
                        auto bdIt = buyDateMap.find(fullSymbol);
                        if (bdIt != buyDateMap.end()) {
                            int entryRow = static_cast<int>(bdIt->second);
                            int daysHeld = r - entryRow;  // 行号差 = 交易日数
                            if (daysHeld < m_minHoldDays) {
                                continue;  // 持有期不足, 跳过此卖出(硬止损除外)
                            }
                        }
                    }
                }

                if (exitAction == rules::RuleAction::Exit || exitAction == rules::RuleAction::Reduce) {
                    // 归因记录: 规则触发的出场
                    const double exitPrice = static_cast<double>(closeMat.data[
                        rowOffset + static_cast<std::size_t>(posCtx.colIndex)]);
                    attributionCollector.recordExit({
                        fullSymbol,
                        m_ruleGate.lastHitTemplateId(),
                        m_ruleGate.lastHitRuleId(),
                        exitPrice, posCtx.entryPrice,
                        (exitPrice - posCtx.entryPrice) / posCtx.entryPrice * 100.0,
                        -1, r
                    });
                    OrderRequest exitOrder;
                    exitOrder.setSymbol(fullSymbol);
                    exitOrder.setSide(OrderSide::Sell);
                    exitOrder.setQuantity(exitAction == rules::RuleAction::Exit
                        ? pos.quantity() : (std::max)(static_cast<std::int64_t>(1), pos.quantity() / 2));
                    ++ruleExitCount;
                    todayRuleExitSyms.insert(fullSymbol);
                    generatedExits.push_back(std::move(exitOrder));
                }
            }
            if (!generatedExits.empty()) {
                if (!ordersOpt.has_value()) ordersOpt = std::move(generatedExits);
                else {
                    auto& list = ordersOpt.value();
                    list.insert(list.end(),
                                std::make_move_iterator(generatedExits.begin()),
                                std::make_move_iterator(generatedExits.end()));
                }
            }
        }

        // ── 止损止盈扫描（由规则闸门 positionAction 接管，此处保留引擎层兜底）──
        // 规则模板中绑定止盈止损模板后，positionAction 会触发对应出场。

        if (ordersOpt.has_value()) {
            auto& orderList = ordersOpt.value();
            if (r == 0 || r % 100 == 0) {
                INTERNAL_INFO_STREAM << "[backtest] day " << r << " orders=" << orderList.size();
            }
            // 当日真实成交统计 (换算后的最终下单量, 采样日输出)
            int dayBuys = 0, daySells = 0, dayCashShort = 0, dayBudgetSmall = 0;
            double dayBuyAmount = 0.0;
            std::int64_t dayMinQty = 0, dayMaxQty = 0;
            for (auto& order : orderList) {
                // 契约: order.symbol() 为真实完整代码 — 策略单由信号携带, 止损/规则出场单生成即完整
                const std::string& symbol = order.symbol();
                const int col = symbolToCol.at(symbol);
                const double closePrice = static_cast<double>(closeMat.data[
                    rowOffset + static_cast<std::size_t>(col)]);
                if (!std::isfinite(closePrice) || closePrice <= 0.0) continue;  // 停牌/无价/NaN, 无法成交

                // ── 规则闸门: 买入候选审核 ──
                if (m_ruleGate.enabled() && order.side() == OrderSide::Buy) {
                    if (!ruleAllowEntriesToday) continue;  // 市场冻结
                    rules::RuleCandidateContext signalCtx;
                    signalCtx.symbol = symbol;
                    auto dot = signalCtx.symbol.find('.');
                    signalCtx.code = dot != std::string::npos
                        ? signalCtx.symbol.substr(0, dot) : signalCtx.symbol;
                    signalCtx.colIndex = col;
                    ruleProvider.setCandidate(signalCtx);
                    if (!m_ruleGate.allowSignal(ruleProvider)) {
                        // 归因记录: 被封堵的买入信号
                        attributionCollector.recordBlocked({
                            symbol,
                            m_ruleGate.lastHitTemplateId(),
                            m_ruleGate.lastHitRuleId(),
                            closePrice, r
                        });
                        continue;
                    }
                }

                // ── 下单量换算: 昨日总资产 × 最终目标权重, 整百股 ──
                // targetWeight 已由策略按信号强度在 [minWeight, maxWeight] 插值,
                // 混合模式因子缩放亦已作用于权重 — 不再重复乘 score
                const double sizingBase = equityCurve.empty()
                    ? req.costSpec.initialCapital.value : equityCurve.back();
                const double signalScore = std::clamp(order.extensionAs<double>(
                    domain::trading::ExtKey::kSignalScore, 0.5), 0.0, 1.0);
                if (order.side() == OrderSide::Buy) {
                    constexpr std::int64_t kSharesPerLot = 100;
                    const double targetWeight = order.extensionAs<double>(
                        domain::trading::ExtKey::kTargetWeight, 0.0);
                    // 已持仓部分市值: 只买差额, 避免重复计算导致资金超100%
                    auto existIt = backtestPositions.find(symbol);
                    const std::int64_t existingQty = (existIt != backtestPositions.end())
                        ? existIt->second.quantity() : 0LL;
                    const double existingValue = (existingQty > 0)
                        ? closePrice * static_cast<double>(existingQty) : 0.0;
                    const double targetValue = sizingBase * targetWeight;
                    const double neededValue = (std::max)(0.0, targetValue - existingValue);
                    const double budget = (std::min)(neededValue, cash);
                    const std::int64_t lots = static_cast<std::int64_t>(
                        budget / (closePrice * static_cast<double>(kSharesPerLot)));
                    if (lots <= 0) {
                        // 区分跳过原因: 现金买不起一手 vs 权重预算本身不足一手
                        if (cash < closePrice * static_cast<double>(kSharesPerLot)) ++dayCashShort;
                        else ++dayBudgetSmall;
                        continue;
                    }
                    order.setQuantity(lots * kSharesPerLot);
                } else {
                    // 卖出信号 = 离场: 全平该标的持仓
                    const auto sizingPosIt = backtestPositions.find(symbol);
                    const std::int64_t held = (sizingPosIt != backtestPositions.end())
                        ? sizingPosIt->second.quantity() : 0;
                    if (held <= 0) {
                        if (todayStopLossSyms.count(symbol)) ++stopLossSkippedNoHeld;
                        continue;
                    }
                    order.setQuantity(held);
                }

                // ── 风控检查 (RiskEvaluator — 公共类) ──
                domain::strategy::RiskInput riskInput;
                riskInput.setStrategyId(req.strategyIdentity.strategyId.text());
                riskInput.setSymbol(symbol);
                riskInput.setBuyOrder(order.side() == OrderSide::Buy);
                riskInput.setPrice(closePrice);
                riskInput.setQuantity(static_cast<std::int64_t>(order.quantity()));
                riskInput.setStrategyBound(true);
                riskInput.setStrategyActive(true);
                riskInput.setSignalStrength(signalScore);  // 真实信号强度进风控
                riskInput.setPositionSnapshotReady(true);  // 回测持仓快照可用
                auto accSnap = btAccount();
                riskInput.setCurrentTotalAsset(accSnap.totalAsset());
                riskInput.setCurrentMarketValue(accSnap.marketValue());
                riskInput.setTradingSessionOpen(true);
                // 当前回撤（用于 maxDrawdownLimit 检查）
                if (peakEquity > 0.0) {
                    double drawdownPct = (peakEquity - accSnap.totalAsset()) / peakEquity * 100.0;
                    riskInput.setCurrentDrawdownPercent(-drawdownPct);
                }
                // 卖出单：填充可卖数量
                const auto& posMap = backtestPositions;
                auto pit = posMap.find(symbol);
                if (!riskInput.isBuyOrder()) {
                    riskInput.setCloseableQuantity(pit != posMap.end()
                        ? pit->second.quantity() : 0);
                }
                // 持仓盈亏%（用于止盈止损检查）
                auto bpit = buyPriceMap.find(symbol);
                if (bpit != buyPriceMap.end() && bpit->second > 0.0 && closePrice > 0.0) {
                    double retPct = (closePrice - bpit->second) / bpit->second * 100.0;
                    riskInput.setSymbolPositionReturnPercent(retPct);
                    riskInput.setSymbolMarketValue(
                        closePrice * (pit != posMap.end() ? pit->second.quantity() : 0));
                }

                domain::strategy::RiskEvaluator::applyConfig(riskInput, m_riskConfig);
                auto riskResult = domain::strategy::RiskEvaluator::evaluateOrder(riskInput);
                if (!riskResult.approved()) {
                    ++riskRejectedCount;
                    continue;
                }

                // ── 成交模拟 (BacktestFillSimulator — 公共类) ──
                if (order.side() == OrderSide::Buy) {
                    double remaining = fillSim.cashAfterBuy(cash, closePrice,
                        static_cast<std::int64_t>(order.quantity()));
                    if (remaining >= 0.0 && std::isfinite(remaining)) {
                        cash = remaining;
                        ++dayBuys;
                        const std::int64_t filledQty = static_cast<std::int64_t>(order.quantity());
                        dayBuyAmount += closePrice * static_cast<double>(filledQty);
                        if (dayMinQty == 0 || filledQty < dayMinQty) dayMinQty = filledQty;
                        if (filledQty > dayMaxQty) dayMaxQty = filledQty;
                        result.tradeLog.push_back({dates[static_cast<std::size_t>(r)].value,
                                                   symbol, true, filledQty, closePrice, 0.0});
                        domain::trading::Position pos;
                        pos.setSymbol(symbol);
                        pos.setSide(domain::trading::PositionSide::Long);
                        std::int64_t heldQty = 0LL;
                        { auto it = backtestPositions.find(symbol);
                          if (it != backtestPositions.end()) heldQty = it->second.quantity(); }
                        if (heldQty == 0) {
                            buyPriceMap[symbol] = closePrice;
                            buySignalScoreMap[symbol] = signalScore;
                            buyDateMap[symbol] = static_cast<double>(r);  // entry row
                            buyFactorScoreMap2[symbol] = m_factorSignalProcessor.enabled()
                                ? m_factorSignalProcessor.compositeScore(symbol) : 0.0;
                        }
                        pos.setQuantity(heldQty + static_cast<std::int64_t>(order.quantity()));
                        pos.setLastPrice(closePrice);
                        backtestPositions[symbol] = pos;
                        boughtToday.insert(symbol);  // T+1: 当日买入, 禁止同日卖出
                        // 交易日志: 买入成交
                        if (m_tradeJournal) {
                            m_tradeJournal->log(
                                "{\"ts\":\"" + std::to_string(dates[static_cast<std::size_t>(r)].value)
                                + "\",\"type\":\"fill\",\"side\":\"buy\""
                                + ",\"symbol\":\"" + symbol + "\""
                                + ",\"qty\":" + std::to_string(filledQty)
                                + ",\"price\":" + std::to_string(closePrice) + "}");
                        }
                    }
                } else {
                    // T+1: 当日买入的标的禁止同日卖出
                    if (boughtToday.count(symbol)) continue;
                    if (todayStopLossSyms.count(symbol)) ++totalStopLossOrders;
                    const auto& posMap = backtestPositions;
                    auto it = posMap.find(symbol);
                    const std::int64_t held = (it != posMap.end()) ? it->second.quantity() : 0LL;
                    const std::int64_t qty = static_cast<std::int64_t>(order.quantity());
                    const std::int64_t sellQty = qty < held ? qty : held;
                    if (sellQty <= 0 && todayStopLossSyms.count(symbol))
                        ++stopLossSkippedNoHeld;
                    if (sellQty > 0) {
                        auto fr = fillSim.simulateSell(closePrice, sellQty);
                        if (!std::isfinite(fr.income)) {
                            INTERNAL_WARN_STREAM << "[backtest] NaN income day="
                                << dates[static_cast<std::size_t>(r)].value
                                << " sym=" << symbol << " price=" << closePrice
                                << " qty=" << sellQty << " cash=" << cash;
                            continue;
                        }
                        cash += fr.income;
                        ++totalFills;
                        ++daySells;
                        double bp = closePrice;
                        auto bpIt = buyPriceMap.find(symbol);
                        if (bpIt != buyPriceMap.end()) { bp = bpIt->second; buyPriceMap.erase(bpIt); }
                        double pnl = (fr.income / sellQty - bp) * sellQty;
                        result.tradeLog.push_back({dates[static_cast<std::size_t>(r)].value,
                                                   symbol, false, sellQty, closePrice, pnl});
                        if (todayStopLossSyms.count(symbol)) ++stopLossFilled;
                        else if (todayRuleExitSyms.count(symbol)) ++ruleExitFilled;
                        else ++normalSellFilled;
                        if (pnl > 0) { ++winningFills; totalProfit += pnl; if (pnl > largestWin) largestWin = pnl; }
                        else { ++losingFills; totalLoss += -pnl; if (-pnl > largestLoss) largestLoss = -pnl; }
                        symbolPnl[symbol] += pnl;
                        // 交易日志: 卖出成交
                        if (m_tradeJournal) {
                            m_tradeJournal->log(
                                "{\"ts\":\"" + std::to_string(dates[static_cast<std::size_t>(r)].value)
                                + "\",\"type\":\"fill\",\"side\":\"sell\""
                                + ",\"symbol\":\"" + symbol + "\""
                                + ",\"qty\":" + std::to_string(sellQty)
                                + ",\"price\":" + std::to_string(closePrice)
                                + ",\"pnl\":" + std::to_string(static_cast<int>(pnl)) + "}");
                        }
                        // ── 持仓诊断 ──
                        {
                            auto bdIt = buyDateMap.find(symbol);
                            if (bdIt != buyDateMap.end()) {
                                holdingDaysVec.push_back(static_cast<double>(r) - bdIt->second);
                                tradePnlVec.push_back(pnl);
                                auto bfsIt = buyFactorScoreMap2.find(symbol);
                                if (bfsIt != buyFactorScoreMap2.end())
                                    entryFactorScores.push_back(bfsIt->second);
                            }
                        }
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

            // 记录调仓日期 (与实盘 EOD 一致)
            if (dayBuys > 0 || daySells > 0)
                m_lastRebalanceDate = std::to_string(dates[static_cast<std::size_t>(r)].value);

            // 采样日输出真实成交统计 (换算后的最终下单量)
            if ((r == 0 || r % 100 == 0)
                && (dayBuys > 0 || daySells > 0 || dayCashShort > 0 || dayBudgetSmall > 0)) {
                std::ostringstream fillLog;
                fillLog << "[backtest] day " << r << " fills: buy=" << dayBuys;
                if (dayBuys > 0)
                    fillLog << " (qty " << dayMinQty << "~" << dayMaxQty
                            << ", 金额" << static_cast<std::int64_t>(dayBuyAmount) << ")";
                fillLog << " sell=" << daySells
                        << " 现金不足跳过=" << dayCashShort
                        << " 权重预算不足一手=" << dayBudgetSmall;
                INTERNAL_INFO_STREAM << fillLog.str();
            }
        }

        // 更新账户: 持仓按当日收盘价估值, 停牌无价日不计入市值
        double marketValue = 0.0;
        for (const auto& [sym, pos] : backtestPositions) {
            if (pos.quantity() <= 0) continue;
            const double px = static_cast<double>(closeMat.data[
                rowOffset + static_cast<std::size_t>(symbolToCol.at(sym))]);
            if (px > 0.0) marketValue += px * static_cast<double>(pos.quantity());
        }
        equity = cash + marketValue;
        if (!std::isfinite(equity) && r > 0) {
            INTERNAL_ERROR_STREAM << "[backtest] NaN equity at day="
                << dates[static_cast<std::size_t>(r)].value
                << " row=" << r << " cash=" << cash
                << " marketValue=" << marketValue
                << " positions=" << backtestPositions.size()
                << " — stopping backtest";
            result.errorMessage = "NaN equity at day " + std::to_string(dates[static_cast<std::size_t>(r)].value);
            return result;
        }
        domain::trading::AccountSnapshot newAcc;
        newAcc.setAvailableCash(cash);
        newAcc.setMarketValue(marketValue);
        newAcc.setTotalAsset(equity);
        latestEquity = newAcc.totalAsset();
        equityCurve.push_back(equity);

        // ── 择时过滤 (v0.13): 不允许新开仓时剔除买单 ──
        if (!timing.allowNewEntries && ordersOpt.has_value()) {
            auto& list = ordersOpt.value();
            list.erase(std::remove_if(list.begin(), list.end(),
                [](const OrderRequest& o) { return o.side() == OrderSide::Buy; }), list.end());
        }

        // ── 风控熔断器每日更新 (v0.13) ──
        m_circuitBreaker.updateEndOfDay(equity);

        // ── 每日持仓/资金诊断 ──
        {
            int posCount = static_cast<int>(backtestPositions.size());
            dailyPositionSum += posCount;
            deployedCapitalSum += marketValue;
            if (posCount > 0) ++daysWithTrades;
        }
        todayStopLossSyms.clear();
        todayRuleExitSyms.clear();
        boughtToday.clear();  // T+1 次日解禁
        }  // else (非熔断/清仓路径)
        if (equity > peakEquity) peakEquity = equity;

        // 大盘回暖解冻: 基准指数站上 20 日均线 → 重置回撤峰值
        if (bmColIdx >= 0 && r >= 20) {
            auto closeMat = view->close();
            const size_t colCount = view->instruments().size();
            double bmClose = static_cast<double>(closeMat.data[
                static_cast<size_t>(r) * colCount + static_cast<size_t>(bmColIdx)]);
            double bmSum20 = 0.0;
            int bmCnt = 0;
            for (int back = 1; back <= 20; ++back) {
                double c = static_cast<double>(closeMat.data[
                    static_cast<size_t>(r - back) * colCount + static_cast<size_t>(bmColIdx)]);
                if (c > 0) { bmSum20 += c; ++bmCnt; }
            }
            if (bmCnt >= 15 && bmClose > 0 && bmSum20 > 0) {
                double bmMA20 = bmSum20 / bmCnt;
                if (bmClose > bmMA20) {
                    // 指数回暖 → 解冻
                    peakEquity = equity;
                }
            }
        }

        if (onProgress && totalDays > 0) {
            double loopFrac = static_cast<double>(r + 1) / static_cast<double>(totalDays);
            double pct = kLoopStart + loopFrac * (kLoopEnd - kLoopStart);
            onProgress(pct);
        }
    }

    INTERNAL_INFO_STREAM << "[backtest] loop done: days=" << totalDays << " finalEquity=" << btAccount().totalAsset() << " fills=" << totalFills << " riskRejected=" << riskRejectedCount;

    // ── 混合模式因子参与统计: 明确回答"跑了几个因子、各参与多少天" ──
    if (m_factorSignalProcessor.enabled()) {
        std::ostringstream coverageLog;
        for (const auto& fid : m_factorSignalProcessor.factorIds()) {
            const auto coveredIt = hybridFactorCoveredDays.find(fid);
            const int coveredDays = coveredIt != hybridFactorCoveredDays.end() ? coveredIt->second : 0;
            result.hybridFactorCoverage.push_back({fid, coveredDays});
            coverageLog << " " << fid << "=" << coveredDays << "/" << totalDays << "天";
        }
        INTERNAL_INFO_STREAM << "[backtest] 混合因子参与: " << result.hybridFactorCoverage.size()
                             << " 个:" << coverageLog.str();
    } else {
        INTERNAL_INFO_STREAM << "[backtest] 混合因子参与: 0 个 (纯策略信号)";
    }

    // ── 规则闸门统计: 明确回答"规则是否生效、拦了什么、缺了什么数据" ──
    if (m_ruleGate.enabled()) {
        const auto& gateStats = m_ruleGate.stats();
        INTERNAL_INFO_STREAM << "[backtest] 规则闸门: 绑定模板=" << m_ruleGate.boundTemplateCount()
                             << " 市场冻结天数=" << gateStats.frozenDays
                             << " 信号被拒=" << gateStats.signalsBlocked
                             << " 规则出场=" << gateStats.positionExits;
        for (const auto& [templateId, templateStats] : gateStats.byTemplate) {
            INTERNAL_INFO_STREAM << "[backtest]   规则模板 " << templateId
                                 << ": 评估=" << templateStats.evaluated
                                 << " 命中=" << templateStats.hits
                                 << " 数据未就绪=" << templateStats.dataMissing;
        }
    } else {
        INTERNAL_INFO_STREAM << "[backtest] 规则闸门: 未启用 (策略无勾选模板或规则库不可用)";
    }
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
        // 胜率/盈亏比为按笔口径: 盈利笔数/总卖出笔数, 总盈利/总亏损
        result.metrics.winRate       = totalFills > 0
            ? static_cast<double>(winningFills) / static_cast<double>(totalFills) : 0.0;
        result.metrics.profitFactor  = totalLoss > 0.0 ? totalProfit / totalLoss : 0.0;
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

    // 基准对比 (沪深300)，从 PG 查询指数日K线，按回测日期对齐
    {
        std::string bmSym = req.benchmarkIndex.empty() ? "000300.SH" : req.benchmarkIndex;
        const auto& dates = view->dates();
        if (!dates.empty()) {
            auto& pool = astock::database::NativePgConnectionPool::instance();
            auto db = pool.getConnection();
            auto repo = std::make_unique<astock::infrastructure::database::MarketDataRepository>(db);
            std::string startStr = std::to_string(dates.front().value);
            std::string endStr   = std::to_string(dates.back().value);
            auto rows = repo->queryDailyBar(bmSym, startStr, endStr);
            if (!rows.empty()) {
                // date → close 映射（tradeDate 是 YYYY-MM-DD 格式 → YYYYMMDD int）
                std::unordered_map<int, double> dateClose;
                for (const auto& r : rows) {
                    std::string ds = r.tradeDate;
                    ds.erase(std::remove(ds.begin(), ds.end(), '-'), ds.end());
                    int d = 0;
                    try { d = std::stoi(ds); } catch (...) { continue; }
                    if (d > 0 && r.close > 0) dateClose[d] = r.close;
                }
                // 逐回测日计算基准收益 — 与 dailyReturns 逐下标对齐:
                // bmRet[k] 与 dailyReturns[k] 同为第 k+1 个交易日相对前一交易日的收益
                std::vector<double> bmRet;
                bmRet.reserve(dates.size() - 1);
                double prevClose = 0.0;
                {
                    auto it0 = dateClose.find(dates.front().value);
                    if (it0 != dateClose.end()) prevClose = it0->second;
                }
                for (size_t i = 1; i < dates.size(); ++i) {
                    auto it = dateClose.find(dates[i].value);
                    const double currClose = (it != dateClose.end()) ? it->second : 0.0;
                    bmRet.push_back((prevClose > 0.0 && currClose > 0.0)
                        ? currClose / prevClose - 1.0 : 0.0);  // 缺数据日记 0 收益
                    if (currClose > 0.0) prevClose = currClose;
                }
                // 指标计算
                auto benchMetrics = ::factor::FactorBacktestMetricsCalculator::calculateBenchmarkMetrics(
                    dailyReturns, bmRet);
                result.metrics.beta             = benchMetrics.beta;
                result.metrics.alpha            = benchMetrics.alpha;
                result.metrics.trackingError    = benchMetrics.trackingError;
                result.metrics.informationRatio = benchMetrics.informationRatio;
                // 净值曲线 + 回撤曲线 — 与 portfolioValues 同长: 首点为初始资金
                const double initialCapital = req.costSpec.initialCapital.value > 0
                    ? static_cast<double>(req.costSpec.initialCapital.value) : 1.0;
                result.timeSeries.benchmarkValues.reserve(bmRet.size() + 1);
                result.timeSeries.benchmarkDrawdowns.reserve(bmRet.size() + 1);
                double bmEquity = initialCapital;
                double bmPeak = bmEquity;
                result.timeSeries.benchmarkValues.push_back(bmEquity);
                result.timeSeries.benchmarkDrawdowns.push_back(0.0);
                for (double r : bmRet) {
                    bmEquity *= (1.0 + r);
                    if (bmEquity > bmPeak) bmPeak = bmEquity;
                    double dd = bmPeak > 0.0 ? (bmPeak - bmEquity) / bmPeak : 0.0;
                    result.timeSeries.benchmarkValues.push_back(bmEquity);
                    result.timeSeries.benchmarkDrawdowns.push_back(dd);
                }
            }
        }
    }

    // ── 回测指标全量打印 ──
    INTERNAL_INFO_STREAM << "═══════════════════════════════════════════";
    INTERNAL_INFO_STREAM << "[回测结果] 策略: " << req.strategyIdentity.strategyCode.text();
    INTERNAL_INFO_STREAM << "[回测结果] 区间: " << (view->dates().empty() ? 0 : view->dates().front().value)
                         << " → " << (view->dates().empty() ? 0 : view->dates().back().value)
                         << "  交易日: " << totalDays;
    INTERNAL_INFO_STREAM << "[回测结果] 初始资金: " << req.costSpec.initialCapital.value
                         << "  最终净值: " << (equityCurve.empty() ? 0 : static_cast<int64_t>(equityCurve.back()));
    INTERNAL_INFO_STREAM << "[交易明细] 止损扫描: " << stopLossExitCount << "次"
                         << "  到达sell段: " << totalStopLossOrders << "次"
                         << "  实际卖出: " << stopLossFilled << "笔"
                         << "  跳过(无持仓): " << stopLossSkippedNoHeld << "次";
    // 卖单按盈亏排序，打印 top20
    std::vector<const BacktestTradeRecord*> sells;
    for (const auto& t : result.tradeLog)
        if (!t.isBuy) sells.push_back(&t);
    std::sort(sells.begin(), sells.end(),
              [](const auto* a, const auto* b) { return a->realizedPnl > b->realizedPnl; });
    int showNTrades = (std::min)(20, static_cast<int>(sells.size()));
    INTERNAL_INFO_STREAM << "[交易明细] === 最佳" << showNTrades << "笔 ===";
    for (int i = 0; i < showNTrades; ++i)
        INTERNAL_INFO_STREAM << "[交易明细] " << sells[i]->tradeDate << " " << sells[i]->symbol
                             << " 盈亏:" << static_cast<int>(sells[i]->realizedPnl);
    INTERNAL_INFO_STREAM << "[交易明细] === 最差" << showNTrades << "笔 ===";
    for (int i = 0; i < showNTrades; ++i)
        INTERNAL_INFO_STREAM << "[交易明细] " << sells[sells.size()-1-i]->tradeDate << " "
                             << sells[sells.size()-1-i]->symbol
                             << " 盈亏:" << static_cast<int>(sells[sells.size()-1-i]->realizedPnl);
    INTERNAL_INFO_STREAM << "[回测指标] 总收益率: " << (result.metrics.totalReturn * 100.0) << "%";
    INTERNAL_INFO_STREAM << "[回测指标] 年化收益: " << (result.metrics.annualizedReturn * 100.0) << "%";
    INTERNAL_INFO_STREAM << "[回测指标] 最大回撤: " << (result.metrics.maxDrawdown * 100.0) << "%";
    INTERNAL_INFO_STREAM << "[回测指标] 胜率: " << (result.metrics.winRate * 100.0) << "%";
    INTERNAL_INFO_STREAM << "[回测指标] 盈亏比: " << result.metrics.profitFactor;
    INTERNAL_INFO_STREAM << "[回测指标] 夏普比率: " << result.metrics.sharpeRatio;
    INTERNAL_INFO_STREAM << "[回测指标] 索提诺比率: " << result.metrics.sortinoRatio;
    INTERNAL_INFO_STREAM << "[回测指标] 卡玛比率: " << result.metrics.calmarRatio;
    INTERNAL_INFO_STREAM << "[回测指标] 年化波动率: " << (result.metrics.volatility * 100.0) << "%";
    INTERNAL_INFO_STREAM << "[回测指标] Alpha: " << result.metrics.alpha;
    INTERNAL_INFO_STREAM << "[回测指标] Beta: " << result.metrics.beta;
    INTERNAL_INFO_STREAM << "[回测指标] 跟踪误差: " << result.metrics.trackingError;
    INTERNAL_INFO_STREAM << "[回测指标] 信息比率: " << result.metrics.informationRatio;
    INTERNAL_INFO_STREAM << "[交易统计] 总成交: " << result.tradeStats.totalTrades
                         << "  盈利: " << result.tradeStats.winningTrades
                         << "  亏损: " << result.tradeStats.losingTrades;
    INTERNAL_INFO_STREAM << "[交易统计] 总盈利: " << result.tradeStats.totalProfit.value
                         << "  总亏损: " << result.tradeStats.totalLoss.value;
    INTERNAL_INFO_STREAM << "[交易统计] 最大单笔盈利: " << result.tradeStats.largestWin.value
                         << "  最大单笔亏损: " << result.tradeStats.largestLoss.value;
    // ── 凯利公式: f* = p - (1-p)/b, b = avgWin/avgLoss ──
    if (result.tradeStats.winningTrades > 0 && result.tradeStats.losingTrades > 0
        && result.tradeStats.totalLoss.value > 0.0) {
        double winRate = static_cast<double>(result.tradeStats.winningTrades)
            / static_cast<double>(result.tradeStats.totalTrades);
        double avgWin  = result.tradeStats.totalProfit.value
            / static_cast<double>(result.tradeStats.winningTrades);
        double avgLoss = result.tradeStats.totalLoss.value
            / static_cast<double>(result.tradeStats.losingTrades);
        double odds = avgWin / avgLoss;
        double fullKelly = winRate - (1.0 - winRate) / odds;
        double halfKelly = fullKelly * 0.5;
        INTERNAL_INFO_STREAM << "[仓位建议] 胜率=" << (winRate * 100.0)
                             << "% 均盈=" << avgWin
                             << " 均亏=" << avgLoss
                             << " 赔率=" << odds;
        INTERNAL_INFO_STREAM << "[仓位建议] 全凯=" << (fullKelly * 100.0)
                             << "% 半凯(建议)=" << (halfKelly * 100.0) << "%";
        result.fullKelly = fullKelly;
        result.halfKelly = halfKelly;
    }
    // ── 规则归因: 计算后输出 + 存到 engine ──
    attributionCollector.compute(view);
    m_ruleAttribution = attributionCollector.results();
    // 存储回测日期区间
    {
        const auto& d = view->dates();
        if (!d.empty())
            m_backtestDateRange = std::to_string(d.front().value) + "-" + std::to_string(d.back().value);
    }
    const auto& attrResults = m_ruleAttribution;
    for (const auto& [tid, attr] : attrResults) {
        INTERNAL_INFO_STREAM << "[规则归因] 模板=" << tid
                             << " 封堵=" << attr.preventedTrades
                             << " 假设盈亏=" << attr.preventedHypotheticalPnL << "%"
                             << " 封堵胜率=" << (attr.preventedWinRate * 100.0) << "%"
                             << " 出场=" << attr.triggeredExits
                             << " 已实现盈亏=" << attr.exitRealizedPnL << "%";
    }
    INTERNAL_INFO_STREAM << "[规则闸门] 冻结天数: " << m_ruleGate.stats().frozenDays
                         << "  信号拒绝: " << m_ruleGate.stats().signalsBlocked
                         << "  规则出场: " << m_ruleGate.stats().positionExits;
    INTERNAL_INFO_STREAM << "[基准对比] 基准净值点数: " << result.timeSeries.benchmarkValues.size()
                         << "  策略净值点数: " << result.timeSeries.portfolioValues.size();
    INTERNAL_INFO_STREAM << "───────────────────────────────────────────";
    // ── 诊断: 持仓结构 ──
    {
        double avgPositions = totalDays > 0
            ? static_cast<double>(dailyPositionSum) / static_cast<double>(totalDays) : 0.0;
        double avgDeployed = totalDays > 0
            ? deployedCapitalSum / static_cast<double>(totalDays) : 0.0;
        // 用日均净值做分母, 避免盈利放大后利用率虚高
        double avgEquity = equityCurve.empty() ? 0.0
            : std::accumulate(equityCurve.begin(), equityCurve.end(), 0.0)
                / static_cast<double>(equityCurve.size());
        double utilizationPct = avgEquity > 0.0 ? (avgDeployed / avgEquity * 100.0) : 0.0;
        double activeDayPct = totalDays > 0
            ? static_cast<double>(daysWithTrades) / static_cast<double>(totalDays) * 100.0 : 0.0;

        INTERNAL_INFO_STREAM << "[诊断-持仓] 日均持仓数: " << avgPositions
                             << "  资金利用率: " << utilizationPct << "%"
                             << "  有持仓天数: " << daysWithTrades << "/" << totalDays
                             << " (" << activeDayPct << "%)";
    }
    // ── 诊断: 持仓周期 ──
    if (!holdingDaysVec.empty()) {
        std::sort(holdingDaysVec.begin(), holdingDaysVec.end());
        double avgHold = 0.0;
        for (double h : holdingDaysVec) avgHold += h;
        avgHold /= static_cast<double>(holdingDaysVec.size());
        double medHold = holdingDaysVec[holdingDaysVec.size() / 2];
        double minHold = holdingDaysVec.front();
        double maxHold = holdingDaysVec.back();
        // 分段分布
        int shortTerm=0, midTerm=0, longTerm=0; // <5 / 5-20 / >20
        for (double h : holdingDaysVec) {
            if (h < 5) ++shortTerm; else if (h <= 20) ++midTerm; else ++longTerm;
        }
        INTERNAL_INFO_STREAM << "[诊断-持仓周期] 平均: " << avgHold << "天  中位数: " << medHold
                             << "天  最短: " << minHold << "天  最长: " << maxHold << "天";
        INTERNAL_INFO_STREAM << "[诊断-持仓周期] 分布: <5天=" << shortTerm
                             << " (占" << (100.0*shortTerm/holdingDaysVec.size()) << "%)"
                             << "  5~20天=" << midTerm
                             << " (占" << (100.0*midTerm/holdingDaysVec.size()) << "%)"
                             << "  >20天=" << longTerm
                             << " (占" << (100.0*longTerm/holdingDaysVec.size()) << "%)";
        // 分层盈亏: 按持仓天数分组
        double pnlShort=0, pnlMid=0, pnlLong=0; int cntS=0, cntM=0, cntL=0;
        for (size_t i=0; i<holdingDaysVec.size() && i<tradePnlVec.size(); ++i) {
            if (holdingDaysVec[i] < 5)       { pnlShort+=tradePnlVec[i]; ++cntS; }
            else if (holdingDaysVec[i]<=20)  { pnlMid+=tradePnlVec[i]; ++cntM; }
            else                              { pnlLong+=tradePnlVec[i]; ++cntL; }
        }
        INTERNAL_INFO_STREAM << "[诊断-分层盈亏] <5天: " << (cntS>0?pnlShort/cntS:0)
                             << "/笔 (" << cntS << "笔)"
                             << "  5~20天: " << (cntM>0?pnlMid/cntM:0)
                             << "/笔 (" << cntM << "笔)"
                             << "  >20天: " << (cntL>0?pnlLong/cntL:0)
                             << "/笔 (" << cntL << "笔)";
    }
    // ── 诊断: 卖出分类 ──
    {
        int totalSells = stopLossFilled + ruleExitFilled + normalSellFilled;
        if (totalSells > 0) {
            INTERNAL_INFO_STREAM << "[诊断-卖出分类] 止损: " << stopLossFilled
                                 << "  规则出场: " << ruleExitFilled
                                 << "  策略卖出: " << normalSellFilled
                                 << "  合计: " << totalSells;
        }
    }
    // ── 诊断: 因子池 ──
    if (poolSelectionDays > 0) {
        double avgPool = static_cast<double>(totalPoolCandidates)
            / static_cast<double>(poolSelectionDays);
        INTERNAL_INFO_STREAM << "[诊断-因子池] 选池天数: " << poolSelectionDays
                             << "/" << totalDays
                             << "  日均候选: " << avgPool
                             << "  目标持仓: " << m_factorSignalProcessor.targetPositionCount();
    }
    // ── 诊断: 因子分与盈亏相关性 (Rank IC) ──
    if (entryFactorScores.size() >= 30) {
        // Spearman rank correlation between entryFactorScore and pnl
        std::vector<size_t> idx(entryFactorScores.size());
        for (size_t i=0; i<idx.size(); ++i) idx[i]=i;
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
            return entryFactorScores[a] < entryFactorScores[b]; });
        std::vector<double> rankS(idx.size()), rankP(idx.size());
        for (size_t i=0; i<idx.size(); ++i) rankS[idx[i]] = static_cast<double>(i);
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
            return tradePnlVec[a] < tradePnlVec[b]; });
        for (size_t i=0; i<idx.size(); ++i) rankP[idx[i]] = static_cast<double>(i);
        double meanR=(idx.size()-1)/2.0, cov=0, varS=0, varP=0;
        for (size_t i=0; i<idx.size(); ++i) {
            double ds=rankS[i]-meanR, dp=rankP[i]-meanR;
            cov+=ds*dp; varS+=ds*ds; varP+=dp*dp;
        }
        double rankIC = (varS>0&&varP>0) ? cov/std::sqrt(varS*varP) : 0.0;
        INTERNAL_INFO_STREAM << "[诊断-因子IC] Rank_IC: " << rankIC
                             << "  样本: " << entryFactorScores.size() << "笔"
                             << "  (正=因子分与盈亏正相关)";
    }
    INTERNAL_INFO_STREAM << "═══════════════════════════════════════════";

    if (riskRejectedCount > 0) {
        INTERNAL_DEBUG_STREAM << "[backtest] risk-rejected orders: " << riskRejectedCount;
    }
    if (onProgress) onProgress(100.0);
    INTERNAL_INFO_STREAM << "[backtest] success, returning result";
    // ── 诊断持久化 ──
    result.stopLossFills   = stopLossFilled;
    result.ruleExitFills   = ruleExitFilled;
    result.normalSellFills = normalSellFilled;
    {
        double sumHold = 0; for (auto h : holdingDaysVec) sumHold += h;
        result.avgHoldingDays = holdingDaysVec.empty() ? 0 : sumHold / holdingDaysVec.size();
        result.avgPositions = totalDays > 0 ? static_cast<double>(dailyPositionSum) / totalDays : 0;
        result.avgPoolSize  = poolSelectionDays > 0 ? static_cast<double>(totalPoolCandidates) / poolSelectionDays : 0;
        // Rank IC
        if (entryFactorScores.size() >= 30 && tradePnlVec.size() >= 30) {
            std::vector<size_t> idx(entryFactorScores.size());
            for (size_t i=0; i<idx.size(); ++i) idx[i]=i;
            std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return entryFactorScores[a] < entryFactorScores[b]; });
            std::vector<double> rankS(idx.size()), rankP(idx.size());
            for (size_t i=0; i<idx.size(); ++i) rankS[idx[i]] = static_cast<double>(i);
            std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return tradePnlVec[a] < tradePnlVec[b]; });
            for (size_t i=0; i<idx.size(); ++i) rankP[idx[i]] = static_cast<double>(i);
            double meanR=(idx.size()-1)/2.0, cov=0, varS=0, varP=0;
            for (size_t i=0; i<idx.size(); ++i) {
                double ds=rankS[i]-meanR, dp=rankP[i]-meanR;
                cov+=ds*dp; varS+=ds*ds; varP+=dp*dp;
            }
            result.rankIC = (varS>0&&varP>0) ? cov/std::sqrt(varS*varP) : 0;
        }
    }

    result.success = true;
    return result;
}

} // namespace domain::strategy
