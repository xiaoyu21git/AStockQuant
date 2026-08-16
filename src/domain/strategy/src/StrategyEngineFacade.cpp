#include "../include/IStrategyService.h"
#include "../include/NonFactorStrategy.h"
#include "../include/RuntimeFactorSvc.h"
#include "../include/LiveViewPreparer.h"
#include "../include/SubmissionFinalizer.h"
#include "../include/EngineListenerAssembler.h"
#include "../include/PositionBook.h"
#include "../include/IPriceProvider.h"
#include "../../../infrastructure/include/database/AppStateStore.h"
#include "../../../infrastructure/include/database/DbTradingCalendar.h"
#include "../../../infrastructure/include/database/ISqlDatabase.h"
#include "../../../infrastructure/include/database/NativePgConnectionPool.h"
#include "../../../infrastructure/include/database/DatabaseConfig.h"
#include "../../../infrastructure/include/database/MarketDataRepository.h"
#include "../../../infrastructure/include/database/MarketDataService.h"
#include "../../../infrastructure/include/database/OrderRecorder.h"
#include "../../strategies/include/StrategyTypeRegistry.h"
#include "../../backtest/include/BacktestRequest.h"
#include "../../backtest/include/BacktestFillSimulator.h"
#include "../../factor/include/factor_compute/FactorEngine.h"
#include "../../factor/include/factor_compute/IMarketDataView.h"
#include "../../factor/include/factor_compute/ArrowMarketDataView.h"
#include "../../factor/include/factor_compute/FactorValuePipeline.h"
#include "../../factor/include/factor_compute/CachedMarketDataView.h"
#include "../../factor/include/FactorMetricsCalculator.h"
#include "../../factor/include/factor_enums.h"
#include "../../factor/include/FactorInstanceManager.h"
#include "../../trading/TradingTypes.h"
#include "../include/EventRiskSubscriber.h"
#include "RuleGate.h"
#include "foundation/market/AStockSymbol.h"
#include "RuleVariableProvider.h"
#include "RuleConditionEvaluator.h"
#include "RuleAttribution.h"
#include "../../attribution/include/AttributionTypes.h"
#include "../../attribution/include/AttributionAnalyzer.h"
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
#include "foundation/Utils/DateUtils.h"
#include "foundation/Utils/Uuid.h"
#include "foundation/log/logging.hpp"
#include "foundation/thread/thread_pool.hpp"
#include "foundation/thread/ThreadPoolExecutor.h"
#include "foundation/Utils/Timestamp.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
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

// ══════════════════════════════════════════════════════════════════════════════
// 参数覆写应用器 (Phase 16: 参数自动调优 — 内存覆写, 零 DB 读取)
// ══════════════════════════════════════════════════════════════════════════════

/// @brief 将 StrategyParamOverlay 应用至创建参数/引擎成员 (仅覆写有值的字段, 无状态)
class ParamOverlayApplier final {
public:
    /// @brief 应用至 StrategyCreationParams (策略构建参数)
    static void applyToCreation(StrategyCreationParams& params,
                                const domain::backtest::StrategyParamOverlay& overlay)
    {
        if (overlay.topN)                   params.topN = *overlay.topN;
        if (overlay.maxPositions)           params.maxPositions = *overlay.maxPositions;
        if (overlay.maxWeightPerStock)      params.maxWeightPerStock = *overlay.maxWeightPerStock;
        if (overlay.minWeightPerStock)      params.minWeightPerStock = *overlay.minWeightPerStock;
        if (overlay.weightSchemeIndex)
            params.weightScheme = static_cast<domain::strategies::WeightScheme>(*overlay.weightSchemeIndex);
        if (overlay.rebalanceFrequencyIndex)
            params.rebalanceFrequency = static_cast<domain::strategies::RebalanceFrequency>(*overlay.rebalanceFrequencyIndex);
        if (overlay.allowShort)             params.allowShort = *overlay.allowShort;
        if (overlay.industryNeutral)        params.industryNeutral = *overlay.industryNeutral;
        if (overlay.stopLossPercent)        params.stopLossPercent = *overlay.stopLossPercent;
        if (overlay.takeProfitPercent)      params.takeProfitPercent = *overlay.takeProfitPercent;
        if (overlay.minHoldDays)            params.minHoldDays = *overlay.minHoldDays;
        // minCompositeScore / sellThreshold / sellRankMultiplier — 引擎层/策略层应用
        if (overlay.fastPeriod)             params.fastPeriod = *overlay.fastPeriod;
        if (overlay.slowPeriod)             params.slowPeriod = *overlay.slowPeriod;
        if (overlay.signalPeriod)           params.signalPeriod = *overlay.signalPeriod;
        if (overlay.macdFast)               params.macdFast = *overlay.macdFast;
        if (overlay.macdSlow)               params.macdSlow = *overlay.macdSlow;
        if (overlay.macdSignal)             params.macdSignal = *overlay.macdSignal;
        if (overlay.bbPeriod)               params.bbPeriod = *overlay.bbPeriod;
        if (overlay.bbStdDev)               params.bbStdDev = *overlay.bbStdDev;
        // priceFieldIndex — 策略层应用
    }

    /// @brief 应用至引擎层成员 (风控/持仓天数/调仓频率)
    static void applyToEngine(RiskConfig& riskCfg,
                              int& minHoldDays,
                              int& rebalanceInterval,
                              const domain::backtest::StrategyParamOverlay& overlay)
    {
        if (overlay.stopLossPercent)        riskCfg.stopLossPercent = *overlay.stopLossPercent;
        if (overlay.takeProfitPercent)      riskCfg.takeProfitPercent = *overlay.takeProfitPercent;
        if (overlay.minHoldDays)            minHoldDays = *overlay.minHoldDays;
        if (overlay.rebalanceFrequencyIndex) {
            auto rf = static_cast<domain::strategies::RebalanceFrequency>(*overlay.rebalanceFrequencyIndex);
            rebalanceInterval = domain::strategies::rebalanceFrequencyStepInterval(rf);
        }
    }
};

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
    void setLiveMarketView(
        std::shared_ptr<const factor::compute::IMarketDataView>) override {}
    void setActivePeriod(BarPeriod) override {}
    void buildLiveView(const std::vector<astock::database::SqlQueryResultRow>&,
                       const std::vector<std::string>&) override {}
    void warmUpCache(const std::string&, BarPeriod,
                     const std::vector<std::string>&) override {}
    // ── 视图/元数据查询 — 返回安全默认值 ──
    [[nodiscard]] std::shared_ptr<const factor::compute::IMarketDataView> liveView() const override { return nullptr; }
    [[nodiscard]] std::vector<std::string> getRequiredFields() const override { return {}; }
    [[nodiscard]] int getMaxLookbackDays() const override { return 90; }
    [[nodiscard]] const std::map<std::string, double>* backtestValuesBySymbol(
        const std::string&, std::int32_t) const override { return nullptr; }
    // 非因子策略无因子依赖 → 探测恒通过 (P2 preflight)
    [[nodiscard]] bool probeFactorCrossSection(
        const std::vector<std::string>&, std::int32_t) const override { return true; }
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
                         << " has(strategyType)=" << meta.has("strategyType")
                         << " name=" << (meta.has("name") ? meta.get("name").asString() : "N/A");

    StrategyCreationParams params;
    params.strategyId     = strategyId;
    params.strategyName   = meta.has("name")        ? meta.get("name").asString()        : "";
    params.description    = meta.has("description") ? meta.get("description").asString()  : "";
    // ── 策略类型: 唯一权威来源 = metadata_json.strategyType 枚举名; 行为类型一律推导 ──
    if (!meta.has("strategyType")) {
        INTERNAL_ERROR_STREAM << "[fromDb] strategyType 缺失, 拒绝加载: " << strategyId;
        return nullptr;
    }
    const std::string strategyTypeIdStr = meta.get("strategyType").asString();
    auto parsedType = ::domain::strategies::StrategyTypeRegistry::fromTypeId(strategyTypeIdStr);
    if (!parsedType.has_value()) {
        INTERNAL_ERROR_STREAM << "[fromDb] strategyType 非法: " << strategyTypeIdStr
                              << ", 拒绝加载: " << strategyId;
        return nullptr;
    }
    params.behaviorKind = ::domain::strategies::StrategyTypeRegistry::behaviorKindOf(*parsedType);
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
        params.topN = root.has("topN") ? root.get("topN").asInt() : 0;
        params.allowShort = root.has("allowShort") && root.get("allowShort").asBool();
        params.maxPositions = root.has("maxPositions") ? root.get("maxPositions").asInt() : 20;
        // 因子池容量约束: 最大持仓不能超过因子候选池可提供的标的数
        if (factorTargetPositionCount > 0 && params.maxPositions > factorTargetPositionCount)
            params.maxPositions = factorTargetPositionCount;
        params.maxWeightPerStock = root.has("maxWeightPerStock") ? root.get("maxWeightPerStock").asDouble() : 0.1;
        params.minWeightPerStock = root.has("minWeightPerStock") ? root.get("minWeightPerStock").asDouble() : 0.0;
        params.minHoldDays = root.has("minHoldDays") ? root.get("minHoldDays").asInt() : 0;
        if (root.has("maxOrderQuantity"))
            params.maxOrderQuantity = static_cast<std::uint32_t>(root.get("maxOrderQuantity").asInt());
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
        INTERNAL_WARN_STREAM << "[fromDb] 中止: factorOverlay 已启用但 factorSvc 为空";
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

    // ── 构建调仓配置 (P4: period 缺省 Daily; 分钟策略启用不在范围 §12) ──
    RebalanceConfig rebalanceCfg;
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
        .withMaxOrderQuantity(params.maxOrderQuantity)
        .maxStrategies(params.maxPositions)
        .maxMarketDataPerBatch(kAllMarketDataBatchCapacity)
        .withFactorService(std::move(factorSvc))
        .build();

    INTERNAL_INFO_STREAM << "[fromDb] " << strategyId << " strategyType=" << strategyTypeIdStr
                         << " kind=" << static_cast<int>(params.behaviorKind)
                         << " factorIds=" << params.factorIds.size()
                         << " engine=" << static_cast<void*>(engine.get());
    if (!engine) return nullptr;

    // ── 保存原始创建参数 (供回测覆写使用，避免重复 DB 读取) ──
    engine->m_originalCreationParams = params;

    // ── 策略创建 ──
    if (params.maxOrderQuantity == 0) {
        INTERNAL_ERROR_STREAM << "[fromDb] maxOrderQuantity=0 非法, 策略创建被拒绝: " << strategyId;
        return nullptr;
    }
    constexpr StrategyInstanceId kDefaultInstanceId = 1;
    RuntimeStrategyContext ctx(kDefaultInstanceId, 1,
                                params.maxOrderQuantity, params.maxWeightPerStock, true);
    if (!ctx.isValid()) {
        INTERNAL_ERROR_STREAM << "[fromDb] RuntimeStrategyContext 校验失败: " << strategyId
                              << " maxOrderQuantity=" << params.maxOrderQuantity
                              << " maxWeightPerStock=" << params.maxWeightPerStock;
        return nullptr;
    }
    auto runtimeStrategy = StrategyBase::create(kDefaultInstanceId, params);
    if (!runtimeStrategy) {
        INTERNAL_ERROR_STREAM << "[fromDb] StrategyBase::create 返回 nullptr kind="
                             << static_cast<int>(params.behaviorKind)
                             << " factorWeights=" << params.factorWeights.size();
        return nullptr;
    }
    auto regResult = engine->registerStrategy(runtimeStrategy, ctx);
    if (!regResult.isOk()) {
        INTERNAL_ERROR_STREAM << "[fromDb] registerStrategy 失败: code="
                             << static_cast<int>(regResult.code());
        return nullptr;
    }

    engine->m_minHoldDays = params.minHoldDays;
    engine->m_strategyName = params.strategyName;
    // 初始化交易日志: logs/策略名/trade_YYYY-MM-DD.jsonl
    if (!params.strategyName.empty()) {
        engine->m_tradeJournal = std::make_unique<TradeJournal>("logs", params.strategyName);
    }
    return engine;

    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[fromDb] 异常: " << e.what();
        return nullptr;
    } catch (...) {
        INTERNAL_ERROR_STREAM << "[fromDb] 未知异常";
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
    }
    return engine;
}

void StrategyEngine::setContextHistoricalView(const void* view)
{
    if (strategyService_) {
        strategyService_->setContextHistoricalView(view);
    }
}

void StrategyEngine::setLiveMarketView(
    std::shared_ptr<const factor::compute::IMarketDataView> view)
{
    // P3: 引擎持有注入视图 — 外部注入 (StrategyBridge 调试/回放) 不再悬垂;
    // factorService 侧由 shared_ptr 自持, strategyService 上下文裸指针由本引擎生命周期保证
    m_injectedLiveView = std::move(view);
    factorService_->setLiveMarketView(m_injectedLiveView);
    // 因子/非因子策略都注入上下文视图:
    // 非因子策略用它取 OHLCV; 因子策略权重方案(市值加权/风险平价)用它取市值和波动率
    setContextHistoricalView(m_injectedLiveView.get());
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
        INTERNAL_ERROR_STREAM << "[Engine] prepareMarketData: PG 连接失败";
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
        INTERNAL_INFO_STREAM << "[Engine] 查询 OHLCV: " << rawRows.size()
                             << " 行, start=" << startDate << " end=" << endDate;
        if (!rawRows.empty()) {
            m_liveMarketView = factor::compute::CachedMarketDataView::fromSqlRows(rawRows, {});
        }
    }
    else{
        // ── 因子策略：MarketDataRepository → buildLiveView ──
        auto repo = std::make_unique<astock::infrastructure::database::MarketDataRepository>(db);
        auto rawRows = repo->queryAllMarketDailyBarWithFields(startDate, endDate, extraFields);
        INTERNAL_DEBUG_STREAM << "[Engine] query Factor: " << rawRows.size()<< " 行, fields=" << (5 + extraFields.size());
        if (!rawRows.empty()) {
            // 当日合成行: LiveViewPreparer 纯函数返回新行集合 (历史行+合成行), 不修改原始查询结果
            auto viewRows = LiveViewPreparer{}.prepareRows(
                *repo, rawRows, endDate, LiveViewPreparePolicy{});
            factorService_->buildLiveView(viewRows, extraFields);
        }
    }

    // ── 注入视图 (P3: shared_ptr 发布) ──
    std::shared_ptr<const factor::compute::IMarketDataView> v;
    if (m_hasFactorStrategies) {
        v = factorService_->liveView();
    } else {
        v = m_liveMarketView;
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
        // ── traceId: 跨日志关联追踪, 确保 Phase 2/3 均能访问 ──
        const std::string traceId = foundation::utils::Uuid::generate_v4().to_string();
        if (traceId.empty()) {
            INTERNAL_WARN_STREAM << "[StrategyEngine] traceId 生成失败, UUID 为空";
        }

        // ── Phase 1: 因子定池 → 策略只在池内判买点 ──
        if (m_factorSignalProcessor.enabled() && m_poolSelector) {
            auto pool = m_poolSelector->selectPool(m_factorSignalProcessor);
            strategyService_->updateCandidatePool(
                std::unordered_set<std::string>(pool.begin(), pool.end()));
            // 变化汇总: 候选池大小变化时打印一次, 不变时静默 (日终补单 5000+ 次调用不能打日志)
            static size_t s_lastPoolSize = SIZE_MAX;
            if (pool.size() != s_lastPoolSize) {
                s_lastPoolSize = pool.size();
                INTERNAL_INFO_STREAM << "[step] 因子候选池: " << pool.size() << " 标的 (targetPosition="
                                     << m_factorSignalProcessor.targetPositionCount() << ")";
            }
        } else {
            strategyService_->updateCandidatePool({});  // 因子关闭 → 策略扫全市场
        }

        // ── Phase 2: 策略在候选池内评估 → 生成买卖信号 ──
        auto rawSignals = strategyService_->onMarketDataPoint(marketDataPoint);
        auto orders = collectOrders(rawSignals);

        // ── traceId 注入: 同一次 step() 所有订单共享 ──
        if (orders.has_value()) {
            for (auto& o : *orders) o.setTraceId(traceId);
        }

        // 实盘信号日志 — 仅在有决策事件时写入, 不记录每次 tick 静默
        if (m_tradeJournal && orders.has_value() && !orders->empty()) {
            const std::int64_t today = domain::market::MarketDataService::instance()
                .activeTradingDay();
            if (today > 0) {
                std::string datePrefix = std::to_string(today);
                const std::string tid = traceId.empty() ? "N/A" : traceId;
                for (const auto& o : *orders) {
                    if (!o.isValid()) continue;
                    std::string side = o.side() == OrderSide::Buy ? "买入" : "卖出";
                    double score = o.extensionAs<double>(domain::trading::ExtKey::kSignalScore, 0.0);
                    std::ostringstream js;
                    js << datePrefix << " 信号生成 " << side << " " << o.symbol()
                       << " " << o.quantity() << "股";
                    if (score > 0.0) js << " 评分:" << std::fixed << std::setprecision(2) << score;
                    js << " trace:" << tid;
                    m_tradeJournal->log(js.str());
                }
            }
        }

        // ── Phase 3: 规则闸门审核 ──
        if (m_rulePipeline.enabled() && orders.has_value() && liveMarketView()) {
            // P3: liveMarketView() 返回 shared_ptr — 局部副本在本分支内保活
            auto view = liveMarketView();
            const std::int64_t today = domain::market::MarketDataService::instance()
                .activeTradingDay();
            if (today > 0) {
                const size_t beforeGate = orders->size();
                rules::BacktestRuleVariableProvider gateProvider;
                gateProvider.setDay(view.get(), static_cast<std::int32_t>(today), nullptr);
                auto filtered = m_rulePipeline.filterBuySignals(*orders,
                    [view, &gateProvider](rules::RuleCandidateContext& ctx, const std::string& symbol) {
                        ctx.symbol = symbol;
                        ctx.code = foundation::market::AStockSymbol::codeOnly(symbol);
                        const auto& symStrs = view->symbolStrings();
                        for (size_t cc = 0; cc < symStrs.size(); ++cc)
                            if (symStrs[cc] == symbol) { ctx.colIndex = static_cast<int>(cc); break; }
                        gateProvider.setCandidate(ctx);
                    }, gateProvider);
                // 实盘规则拒绝日志
                if (m_tradeJournal && filtered.size() < beforeGate) {
                    std::string datePrefix = std::to_string(today);
                    const std::string tid = traceId.empty() ? "N/A" : traceId;
                    // 收集被过滤掉的标的
                    for (const auto& o : *orders) {
                        bool survived = false;
                        for (const auto& f : filtered)
                            if (f.symbol() == o.symbol()) { survived = true; break; }
                        if (survived) continue;
                        std::ostringstream js;
                        js << datePrefix << " 规则拒绝 " << o.symbol()
                           << " 模板:" << m_rulePipeline.boundTemplateCount()
                           << " trace:" << tid;
                        m_tradeJournal->log(js.str());
                    }
                }
                orders = filtered.empty() ? std::nullopt : std::optional(std::move(filtered));
            }
        }
        return orders;
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[StrategyEngine] step() 异常: " << e.what()
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

    // P2: 评估核心懒装配 (此处 m_liveDataPath 已最终确定 — StrategyManager 的
    // setOrderListener 早于 setLiveDataPath, 装配须在路径定稿后; 顺带补装簿记挂钩)
    ensureEvaluationCore();

    // P3/ADR-004: 活跃周期注入因子缓存分区维度 (P4: m_period 由 Builder 从 RebalanceConfig 接线)
    factorService_->setActivePeriod(m_period);

    // ── CronEvaluationScheduler 管理 EOD 回调 + 补单 (P4: 日频分支删除, 无条件装配) ──
    INTERNAL_INFO_STREAM << "[启动] 日频策略 — CronEvaluationScheduler";

    if (!m_dailyScheduler) {
        // P6: 调度时间读配置文件 (trading_connection.json), 零兜底 —
        // 文件不可用/键缺失/非法 → ERROR + 拒绝启动 (不用硬编码时间顶替)
        auto scheduleCfg = EvalScheduleConfig::loadFromTradingConfig();
        if (!scheduleCfg) {
            INTERNAL_ERROR_STREAM << "[启动] 调度时间配置无效, CronEvaluationScheduler 拒绝启动";
            return;
        }
        // P6: 预收盘回调窗口注入市场层 — 时间由配置文件 eodCallbackStartTime/eodCallbackEndTime
        // 决定, 零硬编码零兜底 (市场层自身不读配置, 由 Facade 统一加载后注入)
        domain::market::MarketDataService::instance().setEodCallbackWindow(
            scheduleCfg->eodCallbackStartMinute, scheduleCfg->eodCallbackEndMinute);
        // 统一持久化: 所有策略共用 m_liveDataPath/app_state.json
        std::string persistPath = m_liveDataPath.empty()
            ? "app_state.json"
            : m_liveDataPath + "/app_state.json";
        m_dailyScheduler = std::make_unique<CronEvaluationScheduler>(
            [this](std::function<void()> fn) {
                if (m_dedicatedExecutor && m_loopRunning.load(std::memory_order_acquire))
                    m_dedicatedExecutor->post(std::move(fn));
            },
            persistPath,
            *scheduleCfg
        );
        m_dailyScheduler->setStrategyId(m_strategyId);
        // C6 裁定: 调度器门控+恒触发接线 — 每交易日触发, interval 调仓判定仍在引擎 checkRebalanceDay
        // (intervalDays 供 P2 管道 checkRebalance 接线, §2 轴2)
        m_dailyScheduler->setTriggerPolicy(
            std::make_shared<DailyTriggerPolicy>(m_rebalanceInterval));
        // 注入交易日查询 (P4: DbTradingCalendar 迁移 — 原 DB lambda 逻辑移至基础设施层)
        m_dailyScheduler->setTradingDayProvider([this]() -> std::int64_t {
            return m_calendar->currentTradingDay();
        });
        m_dailyScheduler->setPrevTradingDayProvider([this](const std::string& date) -> std::string {
            return m_calendar->previousTradingDay(date);
        });
        // K线缺口检测 dataSyncDay: P5 起由调度器内置 AppStateStore 读取 (与写侧同源), 不再注入
        m_dailyScheduler->setEvalCallback(
            [this](const std::string& tradingDay, bool isCompensation) -> EvalResult {
                return EvalResult{evaluateEndOfDay(tradingDay, isCompensation)};
            });
        // P3/C11: 预热由调度器 start() 显式调用 (终审 4.1, 不做引擎全局预热);
        // 无视图时直接跳过 (RFS 侧无视图/无因子/无标的亦有早退, 双保险)
        m_dailyScheduler->setWarmUpFn([this]() {
            auto view = factorService_->liveView();
            if (!view) return;
            factorService_->warmUpCache(m_strategyId, m_period, view->symbolStrings());
        });
        m_dailyScheduler->start();
    }

    // ── 金融事件风控订阅器在 AppBootstrap 已全局启动，此处无需操作 ──
}

void StrategyEngine::stopLiveLoop()
{
    if (!m_loopRunning.load(std::memory_order_acquire)) {
        return;
    }
    m_loopRunning.store(false, std::memory_order_release);

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

    // P2: 账本优雅关闭落盘 (ADR-005 持久化频率: flush 必调)
    if (m_positionBook) {
        m_positionBook->flush();
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

    ensureEvaluationCore();

    auto snap = engine::AccountEngine::instance().snapshot();
    auto& positions = snap.positions;

    if (positions.empty()) {
        INTERNAL_INFO_STREAM << "[StrategyEngine] liquidateAll: 无持仓";
        return 0;
    }

    // 订单构造保留 (不经过规则闸门 — 清仓是强制性指令)
    std::vector<OrderRequest> orders;
    std::unordered_map<std::string, int64_t> liqPosMap;
    for (const auto& pos : positions) {
        if (pos.quantity <= 0) continue;
        orders.push_back(m_orderBuilder.buildLiquidationExit(
            pos.symbol, pos.quantity, m_strategyId, snap.account.accountId));
        liqPosMap[pos.symbol] = pos.quantity;

        // 去后缀加入清仓名单
        std::string code = foundation::market::AStockSymbol::codeOnly(pos.symbol);
        m_liquidationBlocklist.insert(code);
    }

    // ── P2: 提交段收敛至共享 Finalizer (mandatory=true 跳过去重, journalPrefix="清仓") ──
    SubmissionRequest liqReq;
    liqReq.rawOrders = std::move(orders);
    liqReq.positionQtyMap = std::move(liqPosMap);
    liqReq.strategyId = m_strategyId;
    liqReq.accountId = snap.account.accountId;
    liqReq.tradingDay = std::to_string(domain::market::MarketDataService::instance().activeTradingDay());
    liqReq.journalPrefix = "清仓";
    liqReq.mandatory = true;

    SubmissionResult liqResult = m_finalizer->submit(liqReq);
    if (liqResult.totalSubmitted == 0) {
        INTERNAL_INFO_STREAM << "[StrategyEngine] liquidateAll: OrderGenerator 过滤后无有效订单";
        return 0;
    }

    INTERNAL_WARN_STREAM << "[StrategyEngine] 一键清仓: basketId=" << liqResult.basketId
                         << " orders=" << liqResult.totalSubmitted
                         << " 笔订单, 持仓已提交";
    return static_cast<int>(liqResult.totalSubmitted);
}

void StrategyEngine::setOrderListener(IOrderListener* listener)
{
    // P2 簿记挂钩装配 (ADR-009①): 装配器已就绪则包装; 未就绪由 ensureEvaluationCore 补装
    m_orderListener = m_listenerAssembler ? m_listenerAssembler->install(listener) : listener;
}

// ═══════════════════════════════════════════════════════════════
// 半自动篮子确认 (v0.16.0)
// ═══════════════════════════════════════════════════════════════

void StrategyEngine::setBasketInterceptor(IBasketInterceptor* interceptor) noexcept
{
    m_basketInterceptor = interceptor;
}

void StrategyEngine::confirmBasket(std::uint64_t basketId,
                                   const std::vector<BasketEdit>& edits)
{
    std::lock_guard<std::mutex> lock(m_basketMutex);
    if (!m_pendingBasket.pending || m_pendingBasket.basketId != basketId) {
        INTERNAL_WARN_STREAM << "[SemiAuto] confirmBasket: basketId 不匹配"
                             << " expected=" << m_pendingBasket.basketId
                             << " got=" << basketId;
        return;
    }
    m_pendingBasket.pending = false;

    if (edits.empty() || !m_orderListener) {
        m_pendingBasket.orders.clear();
        return;
    }

    // Step 1: 读 QML 编辑列表 → kept 索引集合 + qty 覆盖
    std::unordered_set<int> kept;
    std::unordered_map<int, double> qtyOverrides;
    for (const auto& e : edits) {
        if (e.orderIndex < 0 || static_cast<size_t>(e.orderIndex) >= m_pendingBasket.orders.size()) continue;
        kept.insert(e.orderIndex);
        if (e.quantity > 0) {
            qtyOverrides[e.orderIndex] = e.quantity;
        }
    }

    // Step 2: 原地删除 QML 未保留的 + 应用数量修改
    auto& orders = m_pendingBasket.orders;
    size_t w = 0;
    for (size_t i = 0; i < orders.size(); ++i) {
        if (kept.find(static_cast<int>(i)) == kept.end()) continue;  // QML 删掉的
        auto it = qtyOverrides.find(static_cast<int>(i));
        if (it != qtyOverrides.end()) {
            orders[i].setQuantity(it->second);
        }
        if (w != i) orders[w] = std::move(orders[i]);
        ++w;
    }
    orders.resize(w);

    if (!orders.empty()) {
        m_orderListener->onOrders(orders);

        // 半自动确认日志 (持久化到策略交易日志)
        if (m_tradeJournal) {
            const std::int64_t today = domain::market::MarketDataService::instance()
                .activeTradingDay();
            std::string datePrefix = today > 0 ? std::to_string(today) : "";
            for (const auto& o : orders) {
                if (!o.isValid()) continue;
                std::string side = o.side() == OrderSide::Buy ? "买入" : "卖出";
                double score = o.extensionAs<double>(domain::trading::ExtKey::kSignalScore, 0.0);
                std::string tid = o.traceId();
                std::ostringstream js;
                js << datePrefix << " 确认提交 " << side << " " << o.symbol()
                   << " " << o.quantity() << "股";
                if (score > 0.0) js << " 评分:" << std::fixed << std::setprecision(2) << score;
                if (!tid.empty()) js << " trace:" << tid;
                m_tradeJournal->log(js.str());
            }
        }

        INTERNAL_INFO_STREAM << "[SemiAuto] 篮子确认: basketId=" << basketId
                             << " orders=" << orders.size();
    }
}

void StrategyEngine::rejectBasket(std::uint64_t basketId)
{
    std::lock_guard<std::mutex> lock(m_basketMutex);
    if (!m_pendingBasket.pending || m_pendingBasket.basketId != basketId) {
        return;
    }
    m_pendingBasket.pending = false;

    // 半自动拒绝日志 (持久化到策略交易日志)
    if (m_tradeJournal) {
        const std::int64_t today = domain::market::MarketDataService::instance()
            .activeTradingDay();
        std::string datePrefix = today > 0 ? std::to_string(today) : "";
        std::ostringstream js;
        js << datePrefix << " 用户拒绝篮子 " << basketId
           << " 订单数:" << m_pendingBasket.orders.size();
        m_tradeJournal->log(js.str());
    }

    INTERNAL_INFO_STREAM << "[SemiAuto] 篮子拒绝: basketId=" << basketId
                         << " 已丢弃=" << m_pendingBasket.orders.size() << " 笔订单";
}

void StrategyEngine::dispatchOrders(const std::vector<OrderRequest>& orders)
{
    if (orders.empty()) return;

    try {
        if (m_executionMode == EngineExecutionMode::SemiAuto && m_basketInterceptor) {
            // ── 半自动模式: 暂存篮子 → 通知 Bridge 展示确认窗口 ──
            std::uint64_t basketId = 0;
            {
                std::lock_guard<std::mutex> lock(m_basketMutex);
                if (m_pendingBasket.pending) {
                    INTERNAL_WARN_STREAM << "[SemiAuto] 篮子 " << m_pendingBasket.basketId
                        << " 未确认 — 新篮子被丢弃 (" << orders.size() << " 笔订单)";
                    return;
                }
                basketId = SubmissionFinalizer::generateBasketId();
                // 复制订单列表 (不修改 const 入参)
                m_pendingBasket.orders = orders;
                m_pendingBasket.basketId = basketId;
                m_pendingBasket.pending = true;
            }

            // 打篮子ID (在副本上操作)
            auto basketOrders = orders;  // 拷贝
            for (auto& o : basketOrders)
                o.setExtension(domain::trading::ExtKey::kBasketId, basketId);

            bool accepted = m_basketInterceptor->onBasketReady(
                basketId, basketOrders, m_strategyId, m_strategyName, "盘中信号");
            if (!accepted) {
                std::lock_guard<std::mutex> lock(m_basketMutex);
                m_pendingBasket.pending = false;
                INTERNAL_WARN_STREAM << "[SemiAuto] Bridge 拒收篮子 " << basketId;
            }
            return;
        }

        // Live / Backtest 模式: 直接执行
        if (m_orderListener) {
            m_orderListener->onOrders(orders);
        }
    } catch (const std::domain_error& e) {
        INTERNAL_ERROR_STREAM << "[SemiAuto] dispatchOrders 配置异常: " << e.what()
                              << " strategyId=" << m_strategyId
                              << " orders=" << orders.size()
                              << " — 该篮子已被拒绝, 引擎继续运行";
        // domain_error 表示配置缺陷 (如 baseQty=0), 拒绝该篮子但不崩溃引擎
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[SemiAuto] dispatchOrders 运行时异常: " << e.what()
                              << " strategyId=" << m_strategyId
                              << " orders=" << orders.size()
                              << " — 该篮子已被拒绝, 引擎继续运行";
    }
}

// ═════════════════════════════════════════════════════════════════════════
// evaluateEndOfDay — 薄壳 (P2): 早退保持 + 价格源装配 + 管道依赖注入 + 主链
// 原 13 环迁出至 SignalEvaluationPipeline (§7 迁移检查表)
// ═════════════════════════════════════════════════════════════════════════


// ─── P2: 评估核心装配 (ADR-009④: PipelineDeps 仅 buildPipelineDeps 构造) ───

void StrategyEngine::ensureEvaluationCore()
{
    if (m_pipeline) return;  // 已装配

    // AppStateStore 路径与调度器完全一致 (setLiveDataPath 之后首次调用)
    const std::string persistPath = m_liveDataPath.empty()
        ? "app_state.json"
        : m_liveDataPath + "/app_state.json";
    m_appStateStore = astock::infrastructure::database::AppStateStore::forPath(persistPath);

    // 交易日历 (P4: 实盘路径 DB 日历替换 gmsdk; 回测路径 gmsdk 不动)
    m_calendar = std::make_unique<astock::infrastructure::database::DbTradingCalendar>();

    // 内部持仓账本 (ADR-005 P5 簿记校验数据源)
    m_positionBook = std::make_unique<PositionBook>(m_strategyId, m_period, m_appStateStore);

    // 簿记挂钩一次性装配 (ADR-009①; onOrders 调用点零改动)
    m_listenerAssembler = std::make_unique<EngineListenerAssembler>(m_positionBook.get());

    // 共享提交核心 (幂等去重 ADR-006 + 生成 + 篮子 + journal + 投递)
    m_finalizer = std::make_unique<SubmissionFinalizer>(
        m_strategyId, m_period, m_orderGenerator, m_tradeJournal.get(), m_appStateStore,
        [this](const std::vector<OrderRequest>& orders) { dispatchOrders(orders); });

    m_pipeline = std::make_unique<SignalEvaluationPipeline>();

    // 装配器就绪后补装簿记挂钩 (StrategyManager 的 setOrderListener 可能早于装配)
    if (m_orderListener)
        m_orderListener = m_listenerAssembler->install(m_orderListener);

    INTERNAL_INFO_STREAM << "[P2] 评估核心装配: strategyId=" << m_strategyId
                         << " period=" << BarPeriodNaming::suffix(m_period)
                         << " persistPath=" << persistPath;
}

PipelineDeps StrategyEngine::buildPipelineDeps()
{
    ensureEvaluationCore();

    PipelineDeps deps;

    // ── 行为注入 (Facade 包装) ──
    deps.stepFn = [this](const MarketDataPoint& mdp) { return step(mdp); };
    deps.liveViewFn = [this]() { return liveMarketView(); };
    // C8/P4: P2 经 Deps 注入 gmsdk 包装, P4 换 DbTradingCalendar (实盘路径 DB 日历; 回测 gmsdk 不动)
    deps.prevTradingDayFn = [this](const std::string& date) {
        return m_calendar->previousTradingDay(date);
    };
    deps.accountSnapshotFn = [this]() { return buildAccountState(); };
    deps.onForceLiquidate = [this]() { liquidateAll(); };

    // ── 引擎成员引用 (观察者) ──
    deps.orderGenerator = &m_orderGenerator;
    deps.orderBuilder = &m_orderBuilder;
    deps.orderListener = m_orderListener;
    deps.tradeJournal = m_tradeJournal.get();
    deps.ruleGate = &m_ruleGate;
    deps.timingGate = &m_timingGate;
    deps.circuitBreaker = &m_circuitBreaker;
    deps.factorService = factorService_.get();
    deps.positionBook = m_positionBook.get();
    deps.finalizer = m_finalizer.get();
    deps.positionEntryDates = &m_positionEntryDates;
    deps.lastProcessedAt = &m_lastProcessedAt;
    deps.rebalanceInterval = &m_rebalanceInterval;
    deps.lastRebalanceDate = &m_lastRebalanceDate;

    // ── 配置值 ──
    deps.minHoldDays = m_minHoldDays;
    deps.maxOrderQuantity = m_maxOrderQuantity;
    deps.strategyId = m_strategyId;
    deps.accountId = m_accountId;

    return deps;
}

AccountState StrategyEngine::buildAccountState() const
{
    auto snap = engine::AccountEngine::instance().snapshot();
    AccountState out;
    out.accountId = snap.account.accountId;
    out.totalAsset = snap.account.totalAsset;
    out.availableCash = snap.account.availableCash;
    out.marketValue = snap.account.marketValue;
    out.frozenCash = snap.account.frozenCash;
    out.realizedPnl = snap.account.realizedPnl;
    out.unrealizedPnl = snap.account.unrealizedPnl;
    out.positions.reserve(snap.positions.size());
    for (const auto& p : snap.positions)
        out.positions.push_back({p.symbol, p.quantity, p.costPrice, p.lastPrice});
    return out;
}

// ═════════════════════════════════════════════════════════════════════════
// evaluateEndOfDay — 日频策略盘后评估 (薄壳, §6.1)
// ═════════════════════════════════════════════════════════════════════════

EvalStatus StrategyEngine::evaluateEndOfDay(const std::string& tradingDay, bool isCompensation)
{
    // ── 早退保持 (原 L1814-1821): 回测 / 无监听器 ──
    if (m_isBacktestMode.load(std::memory_order_acquire)) {
        INTERNAL_INFO_STREAM << "[StrategyEngine] 回测模式, 跳过日终评估";
        return EvalStatus::Skipped;
    }
    if (!m_orderListener) {
        INTERNAL_WARN_STREAM << "[StrategyEngine] 无订单监听器, 日终评估跳过";
        return EvalStatus::Skipped;
    }

    // ── 价格源装配 (ADR-009②: 工厂是唯一周期/模式分支点) ──
    auto provider = PriceProviderFactory::createProvider(
        m_period, isCompensation ? EvalMode::Compensation : EvalMode::Intraday);

    EvalRequest req;
    req.tradingDay = tradingDay;
    req.isCompensation = isCompensation;
    req.period = m_period;
    req.priceProvider = provider.get();

    PipelineDeps deps = buildPipelineDeps();
    EvalResult result = m_pipeline->run(req, deps);
    return result.status;
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

StrategyEngine::Builder& StrategyEngine::Builder::withMaxOrderQuantity(std::uint32_t value)
{
    maxOrderQuantity_ = value;
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
        return "因子覆盖已启用但过滤器为空";
    if (!rebalanceCfg_.isValid())
        return "调仓配置无效: interval=" + std::to_string(rebalanceCfg_.interval);
    if (factorOverlayCfg_.enabled && !factorService_)
        return "因子覆盖已启用但factorService为空";
    return {};  // 空字符串 = 通过
}

std::unique_ptr<StrategyEngine> StrategyEngine::Builder::build()
{
    // ── 校验 ──
    if (auto err = validate(); !err.empty()) {
        INTERNAL_ERROR_STREAM << "[Builder] 校验失败: " << err;
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

    // ── 调仓频率配置 (P4: period 接线引擎) ──
    engine->m_period = rebalanceCfg_.period;
    engine->m_rebalanceInterval = rebalanceCfg_.interval;
    if (maxOrderQuantity_ == 0) {
        INTERNAL_ERROR_STREAM << "[Builder] maxOrderQuantity=0 非法, 引擎创建被拒绝";
        return nullptr;
    }
    engine->m_maxOrderQuantity = maxOrderQuantity_;
    engine->m_positionSizer.setBaseQty(maxOrderQuantity_);

    INTERNAL_INFO_STREAM << "[Builder] 风控: stopLoss=" << riskCfg_.stopLossPercent
                         << " takeProfit=" << riskCfg_.takeProfitPercent
                         << " maxDrawdown=" << riskCfg_.maxDrawdownLimitPercent
                         << " period=" << BarPeriodNaming::suffix(rebalanceCfg_.period)
                         << " rebalanceInterval=" << rebalanceCfg_.interval;

    return engine;
}

StrategyBacktestResult StrategyEngine::backtest(
    const domain::backtest::BacktestRequest& req,
    factor::compute::BacktestDataService* dataSvc,
    const std::function<void(double)>& onProgress,
    const std::atomic<bool>* cancelFlag)
{
    StrategyBacktestResult result;

    // 防御：回测期间不得触发 IOrderListener (P4: 死代码清理后置位服务于 evaluateEndOfDay 早退)
    struct BacktestGuard {
        std::atomic<bool>& flag;
        explicit BacktestGuard(std::atomic<bool>& f) : flag(f) {
            flag.store(true, std::memory_order_release);
        }
        ~BacktestGuard() { flag.store(false, std::memory_order_release); }
    };
    BacktestGuard backtestGuard(m_isBacktestMode);
    if (!dataSvc) {
        result.errorMessage = "数据服务为空";
        return result;
    }

    // 1. 从已加载的 DataSvc 获取视图（不再自行解析 JSON）
    if (onProgress) onProgress(1.0);
    dataSvc->buildViewForFields({});  // 确保视图已构建
    auto batch = dataSvc->loadBatch(0);
    const auto* view = batch.marketView;
    if (!view) {
        result.errorMessage = "加载行情数据视图失败";
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

    // ── 参数覆写: 调优时为每次试运行注入参数而不重读 DB ──
    const auto& overlay = req.strategyParamOverlay;
    if (!overlay.isEmpty()) {
        // 0. 重置引擎层成员到原始 DB 值 (上一轮覆写可能残留)
        m_minHoldDays = m_originalCreationParams.minHoldDays;
        m_rebalanceInterval = domain::strategies::rebalanceFrequencyStepInterval(
            m_originalCreationParams.rebalanceFrequency);
        m_riskConfig.stopLossPercent = m_originalCreationParams.stopLossPercent;
        m_riskConfig.takeProfitPercent = m_originalCreationParams.takeProfitPercent;
        m_riskConfig.maxDrawdownLimitPercent = m_originalCreationParams.maxDrawdownLimit;

        // 1. 覆写引擎层成员 (风控/持仓天数/调仓频率)
        ParamOverlayApplier::applyToEngine(m_riskConfig, m_minHoldDays, m_rebalanceInterval, overlay);

        // 2. 从原始参数 + 覆写重建策略 (内存覆写，零 DB 读取)
        if (strategyService_) {
            strategyService_->clearStrategies();
        }
        auto modifiedParams = m_originalCreationParams;
        ParamOverlayApplier::applyToCreation(modifiedParams, overlay);
        constexpr StrategyInstanceId kDefaultInstanceId = 1;
        RuntimeStrategyContext ctx(kDefaultInstanceId, 1,
                                    modifiedParams.maxOrderQuantity,
                                    modifiedParams.maxWeightPerStock, true);
        auto newStrategy = StrategyBase::create(kDefaultInstanceId, modifiedParams);
        if (!newStrategy || !registerStrategy(newStrategy, ctx).isOk()) {
            result.errorMessage = "策略覆写重建失败";
            return result;
        }
    }

    const int totalDays = static_cast<int>(view->dates().size());
    const int colCount = static_cast<int>(view->instruments().size());

    if (totalDays == 0 || colCount == 0) {
        result.errorMessage = "行情数据为空";
        return result;
    }

    // 2. 构建 symbol→列号映射 + 账户初始化
    // 契约: 全链路 symbol 均为真实完整代码 (如 "300767.SZ"), 与视图 symbolStrings/数据列逐位对齐
    std::unordered_map<std::string, int> symbolToCol;
    {
        const auto& symStrs = view->symbolStrings();
        if (symStrs.size() != view->instruments().size()) {
            result.errorMessage = "标的/合约列数不匹配";
            return result;
        }
        symbolToCol.reserve(symStrs.size());
        for (std::size_t c = 0; c < symStrs.size(); ++c)
            symbolToCol[symStrs[c]] = static_cast<int>(c);
    }

    // ── Phase 30b: BacktestDayContext 封装循环体全部可变状态 ──
    BacktestDayContext ctx;
    ctx.cash = req.costSpec.initialCapital.value;
    ctx.latestEquity = req.costSpec.initialCapital.value;
    ctx.peakEquity = static_cast<double>(req.costSpec.initialCapital.value);
    ctx.equityCurve.reserve(totalDays);

    // ── 引用别名: ctx 成员映射为原局部变量名, 后处理代码零改动 ──
    double& latestEquity = ctx.latestEquity;
    double& cash = ctx.cash;
    double& peakEquity = ctx.peakEquity;
    auto& backtestPositions = ctx.backtestPositions;

    int& riskRejectedCount = ctx.riskRejectedCount;
    int& totalFills = ctx.totalFills;
    int& winningFills = ctx.winningFills;
    int& losingFills = ctx.losingFills;
    int& stopLossExitCount = ctx.stopLossExitCount;
    int& ruleExitCount = ctx.ruleExitCount;
    int& stopLossFilled = ctx.stopLossFilled;
    int& ruleExitFilled = ctx.ruleExitFilled;
    int& normalSellFilled = ctx.normalSellFilled;
    int& stopLossSkippedNoHeld = ctx.stopLossSkippedNoHeld;
    int& totalStopLossOrders = ctx.totalStopLossOrders;

    auto& todayStopLossSyms = ctx.todayStopLossSyms;
    auto& todayRuleExitSyms = ctx.todayRuleExitSyms;
    auto& boughtToday = ctx.boughtToday;

    double& totalProfit = ctx.totalProfit;
    double& totalLoss = ctx.totalLoss;
    double& largestWin = ctx.largestWin;
    double& largestLoss = ctx.largestLoss;

    auto& buyPriceMap = ctx.buyPriceMap;
    auto& buySignalScoreMap = ctx.buySignalScoreMap;
    auto& symbolPnl = ctx.symbolPnl;

    auto& equityCurve = ctx.equityCurve;

    auto& hybridFactorCoveredDays = ctx.hybridFactorCoveredDays;
    auto& buyDateMap = ctx.buyDateMap;
    auto& buyFactorScoreMap2 = ctx.buyFactorScoreMap2;
    auto& holdingDaysVec = ctx.holdingDaysVec;
    auto& tradePnlVec = ctx.tradePnlVec;
    auto& entryFactorScores = ctx.entryFactorScores;

    int& dailyPositionSum = ctx.dailyPositionSum;
    int& daysWithTrades = ctx.daysWithTrades;
    double& deployedCapitalSum = ctx.deployedCapitalSum;
    size_t& totalBuySignals = ctx.totalBuySignals;
    size_t& totalPoolCandidates = ctx.totalPoolCandidates;
    int& poolSelectionDays = ctx.poolSelectionDays;

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

    domain::backtest::FillSimulatorParams fillParams;
    fillParams.commissionRate = req.costSpec.commissionRate.value;
    fillParams.taxRate        = req.costSpec.taxRate.value;
    fillParams.slippageRate   = req.costSpec.slippageRate.value;
    domain::backtest::BacktestFillSimulator fillSim(fillParams);

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

    const double kLoopEnd = 90.0;  // 后处理 onProgress 用

    // ── 构建日循环扩展视图: [回看90交易日] + [数据集全部交易日] ──
    // 与因子回测共用同一套回看构建器 (WarmupViewBuilder), 回看行由 PG 补全;
    // 日循环从回看结束处起步, 首日即有完整指标/规则窗口
    constexpr int kLoopWarmupDays = 90;
    const auto* arrowView = static_cast<const factor::compute::ArrowMarketDataView*>(view);
    auto loopLoader = std::make_shared<factor::compute::WarmupDataLoader>(
        view->dates().front().value, kLoopWarmupDays);
    factor::compute::WarmupViewBuilder loopViewBuilder(*arrowView, loopLoader);
    std::size_t loopWarmupRows = 0;
    auto loopView = loopViewBuilder.build(
        view->dates(), {}, {"open", "high", "low", "close", "volume"},
        kLoopWarmupDays, {}, loopWarmupRows);
    if (!loopView) {
        result.errorMessage = "回看扩展视图构建失败";
        return result;
    }
    INTERNAL_INFO_STREAM << "[backtest] 日循环视图: warmup=" << loopWarmupRows
                         << " total=" << loopView->dates().size();

    // ── 主循环 (Phase 30b: 提取到 runBacktestLoop) ──
    runBacktestLoop(ctx, result, loopView.get(), static_cast<int>(loopWarmupRows),
                    req, fillSim, symbolToCol, bmColIdx, onProgress,
                    attributionCollector, cancelFlag);

    INTERNAL_INFO_STREAM << "[backtest] 循环完成: days=" << totalDays << " finalEquity=" << btAccount().totalAsset() << " fills=" << totalFills << " riskRejected=" << riskRejectedCount;

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
        topOss << "[backtest] top" << showN << " 盈利: ";
        for (int i = 0; i < showN; ++i) topOss << topStocks[i].first << "(" << static_cast<int>(topStocks[i].second) << ") ";
        topOss << "\n[backtest] top" << showN << " 亏损:  ";
        for (int i = 0; i < showN; ++i) topOss << topStocks[topStocks.size()-1-i].first << "(" << static_cast<int>(topStocks[topStocks.size()-1-i].second) << ") ";
        INTERNAL_INFO_STREAM << topOss.str();
    }

    if (onProgress) onProgress(kLoopEnd);

    // 4. 指标计算 (Phase 30c: 提取到 computeBacktestMetrics)
    computeBacktestMetrics(result, ctx, req, view, totalDays);

    // ── 诊断输出 (Phase 30c: 提取到 buildBacktestDiagnostics) ──
    buildBacktestDiagnostics(result, ctx, req, view, totalDays, onProgress, attributionCollector);

    // ── 绩效归因 (v0.16.0: 板块/因子/择时三维拆解) ──
    {
        domain::attribution::AttributionAnalyzer::Config attrConfig;
        // sectorLookup: 引擎层无 DB 查询能力, 暂不注入 (Bridge 层可二次增强)
        attrConfig.sectorLookup = nullptr;
        // factorWeightLookup: 从当前回测请求的 FactorOverlaySpec 获取
        attrConfig.factorWeightLookup = [&](const std::string& fid) -> double {
            if (req.factorOverlaySpec.enabled) {
                for (const auto& alloc : req.factorOverlaySpec.allocations) {
                    if (alloc.factorId.text() == fid)
                        return alloc.weightPercent / 100.0;
                }
            }
            return 0.0;
        };
        // benchmarkLookup: Phase 2 (待基准行业数据就绪)
        attrConfig.benchmarkLookup = nullptr;

        domain::attribution::AttributionAnalyzer analyzer(std::move(attrConfig));
        m_lastAttribution = analyzer.analyze(result);
    }

    result.success = true;
    return result;
}

// ══════════════════════════════════════════════════════════════════════════════
// Phase 30b: runBacktestLoop — 回测主循环 (从 backtest() 提取, 引用别名策略)
// ══════════════════════════════════════════════════════════════════════════════

void StrategyEngine::runBacktestLoop(
    BacktestDayContext& ctx,
    StrategyBacktestResult& result,
    const factor::compute::IMarketDataView* view,
    int warmupDayCount,
    const domain::backtest::BacktestRequest& req,
    domain::backtest::BacktestFillSimulator& fillSim,
    const std::unordered_map<std::string, int>& symbolToCol,
    int bmColIdx,
    const std::function<void(double)>& onProgress,
    rules::AttributionCollector& attributionCollector,
    const std::atomic<bool>* cancelFlag)
{
    // ── 引用别名: ctx 成员映射为原局部变量名, 循环体零改动 ──
    double& latestEquity = ctx.latestEquity;
    double& cash = ctx.cash;
    double& peakEquity = ctx.peakEquity;
    auto& backtestPositions = ctx.backtestPositions;

    int& riskRejectedCount = ctx.riskRejectedCount;
    int& totalFills = ctx.totalFills;
    int& winningFills = ctx.winningFills;
    int& losingFills = ctx.losingFills;
    int& stopLossExitCount = ctx.stopLossExitCount;
    int& ruleExitCount = ctx.ruleExitCount;
    int& stopLossFilled = ctx.stopLossFilled;
    int& ruleExitFilled = ctx.ruleExitFilled;
    int& normalSellFilled = ctx.normalSellFilled;
    int& stopLossSkippedNoHeld = ctx.stopLossSkippedNoHeld;
    int& totalStopLossOrders = ctx.totalStopLossOrders;

    auto& todayStopLossSyms = ctx.todayStopLossSyms;
    auto& todayRuleExitSyms = ctx.todayRuleExitSyms;
    auto& boughtToday = ctx.boughtToday;

    double& totalProfit = ctx.totalProfit;
    double& totalLoss = ctx.totalLoss;
    double& largestWin = ctx.largestWin;
    double& largestLoss = ctx.largestLoss;

    auto& buyPriceMap = ctx.buyPriceMap;
    auto& buySignalScoreMap = ctx.buySignalScoreMap;
    auto& symbolPnl = ctx.symbolPnl;

    auto& equityCurve = ctx.equityCurve;

    auto& hybridFactorCoveredDays = ctx.hybridFactorCoveredDays;
    auto& buyDateMap = ctx.buyDateMap;
    auto& buyFactorScoreMap2 = ctx.buyFactorScoreMap2;
    auto& holdingDaysVec = ctx.holdingDaysVec;
    auto& tradePnlVec = ctx.tradePnlVec;
    auto& entryFactorScores = ctx.entryFactorScores;
    int& dailyPositionSum = ctx.dailyPositionSum;
    int& daysWithTrades = ctx.daysWithTrades;
    double& deployedCapitalSum = ctx.deployedCapitalSum;
    size_t& totalBuySignals = ctx.totalBuySignals;
    size_t& totalPoolCandidates = ctx.totalPoolCandidates;
    int& poolSelectionDays = ctx.poolSelectionDays;

    auto btAccount = [&]() {
        domain::trading::AccountSnapshot a;
        a.setTotalAsset(latestEquity);
        a.setAvailableCash(cash);
        a.setMarketValue(latestEquity - cash);
        return a;
    };

    // ── 规则变量提供者 (仅循环内使用) ──
    rules::BacktestRuleVariableProvider ruleProvider;
    ruleProvider.setConceptQueriesEnabled(false);
    ruleProvider.setCandlePatternsEnabled(m_enableCandlePatterns);
    bool ruleAllowEntriesToday = true;

    // ═══════════════════════════════════════════════════════════════
    // 循环体: 与原 backtest() L1927-2606 逐字一致
    // ═══════════════════════════════════════════════════════════════

    if (!view) { result.errorMessage = "回测期间视图丢失"; return; }

    const int totalDays = static_cast<int>(view->dates().size());
    const int colCount = static_cast<int>(view->instruments().size());

    const int kWindowStartDay = req.window.startDate.to_yyyymmdd();
    const int kWindowEndDay   = req.window.endDate.to_yyyymmdd();

    const double kLoopStart  = 0.0;
    const double kLoopEnd    = 90.0;

    // 从回看结束处起步: r < warmupDayCount 的行仅作为指标/规则窗口存在, 不驱动交易
    for (int r = warmupDayCount; r < totalDays; ++r) {
        // ── 取消检测: 每个交易日开始时检查，支持中途取消调优 ──
        if (cancelFlag && cancelFlag->load(std::memory_order_relaxed)) {
            result.errorMessage = "用户取消";
            return;
        }

        setContextHistoricalView(view);
        if (strategyService_) strategyService_->setContextEvaluationRow(r);

        const auto& dates = view->dates();
        const int currentDay = dates[static_cast<std::size_t>(r)].value;
        if (currentDay < kWindowStartDay || currentDay > kWindowEndDay) {
            equityCurve.push_back(latestEquity);
            continue;
        }

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

        if (r == warmupDayCount || r == totalDays-1 || r % 100 == 0) {
            INTERNAL_INFO_STREAM << "[backtest] 第" << r << "/" << totalDays
                << " equity=" << btAccount().totalAsset() << " cash=" << cash
                << " positions=" << backtestPositions.size();
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

        if (m_ruleGate.enabled()) {
            ruleProvider.setDay(view, dates[static_cast<std::size_t>(r)].value, &backtestPositions);
            ruleAllowEntriesToday = m_ruleGate.allowNewEntriesToday(ruleProvider);
            if (!ruleAllowEntriesToday && m_tradeJournal) {
                m_tradeJournal->log(
                    std::to_string(dates[static_cast<std::size_t>(r)].value)
                    + " 冻结 模板:" + m_ruleGate.lastHitTemplateId()
                    + " 规则:" + m_ruleGate.lastHitRuleId());
            }
        } else {
            ruleAllowEntriesToday = true;
        }

        TimingResult timing;
        {
            MarketTimingSnapshot ts;
            if (bmColIdx >= 0 && r > 60) {
                const double idxClose = static_cast<double>(closeMat.data[
                    static_cast<size_t>(r) * static_cast<size_t>(colCount) + static_cast<size_t>(bmColIdx)]);
                if (idxClose > 0.0) {
                    ts.indexClose = idxClose;
                    double sum20 = 0.0; int cnt20 = 0;
                    for (int back = 0; back < 20 && (r - back) >= 0; ++back) {
                        double c = static_cast<double>(closeMat.data[
                            static_cast<size_t>(r - back) * static_cast<size_t>(colCount)
                            + static_cast<size_t>(bmColIdx)]);
                        if (c > 0.0) { sum20 += c; ++cnt20; }
                    }
                    ts.ma20 = cnt20 > 0 ? sum20 / cnt20 : idxClose;
                    double sum60 = 0.0; int cnt60 = 0;
                    for (int back = 0; back < 60 && (r - back) >= 0; ++back) {
                        double c = static_cast<double>(closeMat.data[
                            static_cast<size_t>(r - back) * static_cast<size_t>(colCount)
                            + static_cast<size_t>(bmColIdx)]);
                        if (c > 0.0) { sum60 += c; ++cnt60; }
                    }
                    ts.ma60 = cnt60 > 0 ? sum60 / cnt60 : idxClose;
                    ts.ma20AboveMa60 = ts.ma20 > ts.ma60;
                    double sum20_5 = 0.0; int cnt20_5 = 0;
                    for (int back = 5; back < 25 && (r - back) >= 0; ++back) {
                        double c = static_cast<double>(closeMat.data[
                            static_cast<size_t>(r - back) * static_cast<size_t>(colCount)
                            + static_cast<size_t>(bmColIdx)]);
                        if (c > 0.0) { sum20_5 += c; ++cnt20_5; }
                    }
                    double ma20_5dAgo = cnt20_5 > 0 ? sum20_5 / cnt20_5 : ts.ma20;
                    ts.ma20Rising = ts.ma20 > ma20_5dAgo;
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
                    ts.atrPercent = 0.02;
                }
            }
            timing = m_timingGate.evaluate(ts);
            if (r == warmupDayCount || r % 100 == 0) {
                INTERNAL_INFO_STREAM << "[backtest] 第" << r
                    << " 择时: exposure=" << timing.targetExposure
                    << " allowNew=" << timing.allowNewEntries
                    << " liquidate=" << timing.forceLiquidate
                    << " reason=" << timing.reason;
            }
        }

        bool circuitHalted = m_circuitBreaker.isHalted();
        bool forceLiquidate = timing.forceLiquidate && !circuitHalted;
        std::optional<std::vector<OrderRequest>> ordersOpt;
        double equity = 0.0;

        if (circuitHalted || forceLiquidate) {
            std::vector<OrderRequest> exitOrders;
            for (const auto& kvPos : backtestPositions) {
                if (kvPos.second.quantity() > 0) {
                    exitOrders.push_back(m_orderBuilder.buildLiquidationExit(
                        kvPos.first, kvPos.second.quantity(),
                        m_strategyId, m_accountId));
                }
            }
            if (!exitOrders.empty()) ordersOpt = std::move(exitOrders);
            strategyService_->updateCandidatePool({});
            double mv = 0.0;
            for (const auto& kvPos : backtestPositions) {
                const auto& sym = kvPos.first;
                if (kvPos.second.quantity() <= 0) continue;
                const double px = static_cast<double>(closeMat.data[
                    rowOffset + static_cast<size_t>(symbolToCol.at(sym))]);
                if (px > 0.0) mv += px * static_cast<double>(kvPos.second.quantity());
            }
            equity = cash + mv;
            equityCurve.push_back(equity);
        } else {
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
            continue;
        }

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

        if (m_factorSignalProcessor.enabled() && m_poolSelector && !m_hasFactorStrategies) {
            if (m_ruleGate.enabled()) {
                std::unordered_map<std::string, double> ruleScoreMap;
                const auto& allSyms = m_factorSignalProcessor.allSymbols();
                for (const auto& sym : allSyms) {
                    rules::RuleCandidateContext candidateCtx;
                    candidateCtx.symbol = sym;
                    candidateCtx.code = foundation::market::AStockSymbol::codeOnly(sym);
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
            {
                std::unordered_map<std::string, double> factorScoreMap;
                for (const auto& sym : pool)
                    factorScoreMap[sym] = m_factorSignalProcessor.compositeScore(sym);
                strategyService_->updateFactorScores(std::move(factorScoreMap));
            }
            if (r == warmupDayCount || r % 100 == 0)
                INTERNAL_INFO_STREAM << "[backtest] 第" << r << " 因子候选池: " << pool.size()
                                     << " 标的 (targetPosition=" << m_factorSignalProcessor.targetPositionCount() << ")";
        } else {
            strategyService_->updateCandidatePool({});
        }

        {
            std::unordered_map<std::string, double> wmap;
            for (const auto& [sym, pos] : backtestPositions)
                if (pos.quantity() > 0) wmap[sym] = static_cast<double>(pos.quantity());
            strategyService_->updateCurrentWeights(wmap);
        }

        ordersOpt = stepBatch(mdpBatch);

        if (m_ruleGate.enabled() && !backtestPositions.empty()) {
            std::vector<OrderRequest> generatedExits;
            for (const auto& kvPos : backtestPositions) {
                const std::string& fullSymbol = kvPos.first;
                const auto& pos = kvPos.second;
                if (pos.quantity() <= 0) continue;
                rules::RuleCandidateContext posCtx;
                posCtx.symbol = fullSymbol;
                posCtx.code = foundation::market::AStockSymbol::codeOnly(fullSymbol);
                posCtx.isHolding = true;
                posCtx.holdDays = 0.0;
                auto bpIt = buyPriceMap.find(fullSymbol);
                posCtx.entryPrice = bpIt != buyPriceMap.end() ? bpIt->second : 0.0;
                posCtx.colIndex = symbolToCol.at(fullSymbol);
                const double currentPrice = static_cast<double>(closeMat.data[
                    rowOffset + static_cast<std::size_t>(posCtx.colIndex)]);
                if (posCtx.entryPrice > 0.0 && currentPrice > 0.0)
                    posCtx.pnlPercent = (currentPrice - posCtx.entryPrice) / posCtx.entryPrice * 100.0;
                ruleProvider.setCandidate(posCtx);
                const rules::RuleAction exitAction = m_ruleGate.positionAction(ruleProvider);

                bool isHardStop = false;
                if (m_minHoldDays > 0 && (exitAction == rules::RuleAction::Exit || exitAction == rules::RuleAction::Reduce)) {
                    const auto& ruleTags = m_ruleGate.lastHitRuleTags();
                    const auto& tmplTags = m_ruleGate.lastHitTemplateTags();
                    isHardStop = std::find(ruleTags.begin(), ruleTags.end(), "hard-stop") != ruleTags.end()
                              || std::find(tmplTags.begin(), tmplTags.end(), "hard-stop") != tmplTags.end();
                    if (!isHardStop) {
                        auto bdIt = buyDateMap.find(fullSymbol);
                        if (bdIt != buyDateMap.end()) {
                            int entryRow = static_cast<int>(bdIt->second);
                            int daysHeld = r - entryRow;
                            if (daysHeld < m_minHoldDays) continue;
                        }
                    }
                }

                if (exitAction == rules::RuleAction::Exit || exitAction == rules::RuleAction::Reduce) {
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
                    OrderRequest exitOrder = m_orderBuilder.buildRuleExit(
                        fullSymbol, pos.quantity(),
                        exitAction == rules::RuleAction::Exit,
                        m_strategyId, m_accountId, exitPrice);
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

        if (ordersOpt.has_value()) {
            auto& orderList = ordersOpt.value();
            if (r == warmupDayCount || r % 100 == 0) {
                INTERNAL_INFO_STREAM << "[backtest] 第" << r << " orders=" << orderList.size();
            }
            int dayBuys = 0, daySells = 0, dayCashShort = 0, dayBudgetSmall = 0;
            double dayBuyAmount = 0.0;
            std::int64_t dayMinQty = 0, dayMaxQty = 0;
            for (auto& order : orderList) {
                const std::string& symbol = order.symbol();
                const int col = symbolToCol.at(symbol);
                const double closePrice = static_cast<double>(closeMat.data[
                    rowOffset + static_cast<std::size_t>(col)]);
                if (!std::isfinite(closePrice) || closePrice <= 0.0) continue;

                if (m_ruleGate.enabled() && order.side() == OrderSide::Buy) {
                    if (!ruleAllowEntriesToday) continue;
                    rules::RuleCandidateContext signalCtx;
                    signalCtx.symbol = symbol;
                    signalCtx.code = foundation::market::AStockSymbol::codeOnly(signalCtx.symbol);
                    signalCtx.colIndex = col;
                    ruleProvider.setCandidate(signalCtx);
                    if (!m_ruleGate.allowSignal(ruleProvider)) {
                        attributionCollector.recordBlocked({
                            symbol,
                            m_ruleGate.lastHitTemplateId(),
                            m_ruleGate.lastHitRuleId(),
                            closePrice, r
                        });
                        if (m_tradeJournal) {
                            m_tradeJournal->log(
                                std::to_string(dates[static_cast<std::size_t>(r)].value)
                                + " 拒绝 " + symbol
                                + " 规则:" + m_ruleGate.lastHitRuleId()
                                + " 模板:" + m_ruleGate.lastHitTemplateId());
                        }
                        continue;
                    }
                }

                const double sizingBase = equityCurve.empty()
                    ? req.costSpec.initialCapital.value : equityCurve.back();
                const double signalScore = std::clamp(order.extensionAs<double>(
                    domain::trading::ExtKey::kSignalScore, 0.5), 0.0, 1.0);
                if (order.side() == OrderSide::Buy) {
                    constexpr std::int64_t kSharesPerLot = 100;
                    const double targetWeight = order.extensionAs<double>(
                        domain::trading::ExtKey::kTargetWeight, 0.0);
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
                        if (cash < closePrice * static_cast<double>(kSharesPerLot)) ++dayCashShort;
                        else ++dayBudgetSmall;
                        continue;
                    }
                    order.setQuantity(lots * kSharesPerLot);
                } else {
                    const auto sizingPosIt = backtestPositions.find(symbol);
                    const std::int64_t held = (sizingPosIt != backtestPositions.end())
                        ? sizingPosIt->second.quantity() : 0;
                    if (held <= 0) {
                        if (todayStopLossSyms.count(symbol)) ++stopLossSkippedNoHeld;
                        continue;
                    }
                    order.setQuantity(held);
                }

                domain::strategy::RiskInput riskInput;
                riskInput.setStrategyId(req.strategyIdentity.strategyId.text());
                riskInput.setSymbol(symbol);
                riskInput.setBuyOrder(order.side() == OrderSide::Buy);
                riskInput.setPrice(closePrice);
                riskInput.setQuantity(static_cast<std::int64_t>(order.quantity()));
                riskInput.setStrategyBound(true);
                riskInput.setStrategyActive(true);
                riskInput.setSignalStrength(signalScore);
                riskInput.setPositionSnapshotReady(true);
                auto accSnap = btAccount();
                riskInput.setCurrentTotalAsset(accSnap.totalAsset());
                riskInput.setCurrentMarketValue(accSnap.marketValue());
                riskInput.setTradingSessionOpen(true);
                if (peakEquity > 0.0) {
                    double drawdownPct = (peakEquity - accSnap.totalAsset()) / peakEquity * 100.0;
                    riskInput.setCurrentDrawdownPercent(-drawdownPct);
                }
                const auto& posMap = backtestPositions;
                auto pit = posMap.find(symbol);
                if (!riskInput.isBuyOrder()) {
                    riskInput.setCloseableQuantity(pit != posMap.end()
                        ? pit->second.quantity() : 0);
                }
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
                    if (m_tradeJournal) {
                        m_tradeJournal->log(
                            std::to_string(dates[static_cast<std::size_t>(r)].value)
                            + " 风控拒绝 " + symbol
                            + " " + riskResult.description());
                    }
                    continue;
                }

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
                            buyDateMap[symbol] = static_cast<double>(r);
                            buyFactorScoreMap2[symbol] = m_factorSignalProcessor.enabled()
                                ? m_factorSignalProcessor.compositeScore(symbol) : 0.0;
                        }
                        pos.setQuantity(heldQty + static_cast<std::int64_t>(order.quantity()));
                        pos.setLastPrice(closePrice);
                        backtestPositions[symbol] = pos;
                        boughtToday.insert(symbol);
                        if (m_tradeJournal) {
                            auto dt = dates[static_cast<std::size_t>(r)].value;
                            std::ostringstream js;
                            js << dt << " 买入 " << symbol
                               << " " << filledQty << "股 "
                               << closePrice;
                            auto bs = buySignalScoreMap.find(symbol);
                            if (bs != buySignalScoreMap.end() && bs->second > 0.0)
                                js << " 评分:" << std::fixed << std::setprecision(2) << bs->second;
                            m_tradeJournal->log(js.str());
                        }
                    }
                } else {
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
                            INTERNAL_WARN_STREAM << "[backtest] NaN 收益 day="
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
                        if (m_tradeJournal) {
                            auto dt = dates[static_cast<std::size_t>(r)].value;
                            auto pnlInt = static_cast<int>(pnl);
                            m_tradeJournal->log(
                                std::to_string(dt) + " 卖出 " + symbol
                                + "  " + std::to_string(sellQty) + "股  "
                                + std::to_string(closePrice)
                                + "  盈亏:" + (pnlInt >= 0 ? "+" : "") + std::to_string(pnlInt));
                        }
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
                        else backtestPositions.erase(symbol);
                    }
                }
            }

            if (dayBuys > 0 || daySells > 0)
                m_lastRebalanceDate = std::to_string(dates[static_cast<std::size_t>(r)].value);

            if ((r == warmupDayCount || r % 100 == 0)
                && (dayBuys > 0 || daySells > 0 || dayCashShort > 0 || dayBudgetSmall > 0)) {
                std::ostringstream fillLog;
                fillLog << "[backtest] 第" << r << " fills: buy=" << dayBuys;
                if (dayBuys > 0)
                    fillLog << " (qty " << dayMinQty << "~" << dayMaxQty
                            << ", 金额" << static_cast<std::int64_t>(dayBuyAmount) << ")";
                fillLog << " sell=" << daySells
                        << " 现金不足跳过=" << dayCashShort
                        << " 权重预算不足一手=" << dayBudgetSmall;
                INTERNAL_INFO_STREAM << fillLog.str();
            }
        }

        double marketValue = 0.0;
        for (const auto& [sym, pos] : backtestPositions) {
            if (pos.quantity() <= 0) continue;
            const double px = static_cast<double>(closeMat.data[
                rowOffset + static_cast<std::size_t>(symbolToCol.at(sym))]);
            if (px > 0.0) marketValue += px * static_cast<double>(pos.quantity());
        }
        equity = cash + marketValue;
        if (!std::isfinite(equity) && r > 0) {
            INTERNAL_ERROR_STREAM << "[backtest] NaN 权益 day="
                << dates[static_cast<std::size_t>(r)].value
                << " row=" << r << " cash=" << cash
                << " marketValue=" << marketValue
                << " positions=" << backtestPositions.size()
                << " — 停止回测";
            result.errorMessage = "NaN 净值, 日期 " + std::to_string(dates[static_cast<std::size_t>(r)].value);
            return;
        }
        domain::trading::AccountSnapshot newAcc;
        newAcc.setAvailableCash(cash);
        newAcc.setMarketValue(marketValue);
        newAcc.setTotalAsset(equity);
        latestEquity = newAcc.totalAsset();
        equityCurve.push_back(equity);

        if (!timing.allowNewEntries && ordersOpt.has_value()) {
            auto& list = ordersOpt.value();
            list.erase(std::remove_if(list.begin(), list.end(),
                [](const OrderRequest& o) { return o.side() == OrderSide::Buy; }), list.end());
        }

        m_circuitBreaker.updateEndOfDay(equity);

        {
            int posCount = static_cast<int>(backtestPositions.size());
            dailyPositionSum += posCount;
            deployedCapitalSum += marketValue;
            if (posCount > 0) ++daysWithTrades;
        }
        todayStopLossSyms.clear();
        todayRuleExitSyms.clear();
        boughtToday.clear();
        if (m_tradeJournal) {
            int posCount = static_cast<int>(backtestPositions.size());
            m_tradeJournal->log(
                std::to_string(dates[static_cast<std::size_t>(r)].value)
                + " 日终 持仓:" + std::to_string(posCount)
                + " 净值:" + std::to_string(static_cast<int>(equity))
                + " 现金:" + std::to_string(static_cast<int>(cash)));
        }
        }  // else (非熔断/清仓路径)
        if (equity > peakEquity) peakEquity = equity;

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
                    peakEquity = equity;
                }
            }
        }

        if (onProgress && totalDays > warmupDayCount) {
            double loopFrac = static_cast<double>(r - warmupDayCount + 1)
                / static_cast<double>(totalDays - warmupDayCount);
            double pct = kLoopStart + loopFrac * (kLoopEnd - kLoopStart);
            onProgress(pct);
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// Phase 30c: computeBacktestMetrics — 回测后处理指标计算 (从 backtest() 提取)
// ══════════════════════════════════════════════════════════════════════════════

void StrategyEngine::computeBacktestMetrics(
    StrategyBacktestResult& result,
    const BacktestDayContext& ctx,
    const domain::backtest::BacktestRequest& req,
    const factor::compute::IMarketDataView* view,
    int totalDays)
{
    // ── 引用别名: ctx 成员映射为原局部变量名, 函数体零改动 ──
    const auto& equityCurve = ctx.equityCurve;
    const auto& totalFills = ctx.totalFills;
    const auto& winningFills = ctx.winningFills;
    const auto& losingFills = ctx.losingFills;
    const auto& totalProfit = ctx.totalProfit;
    const auto& totalLoss = ctx.totalLoss;
    const auto& largestWin = ctx.largestWin;
    const auto& largestLoss = ctx.largestLoss;

    // ═══════════════════════════════════════════════════════════════
    // 函数体: 与原 backtest() L1989-2099 逐字一致
    // ═══════════════════════════════════════════════════════════════

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
}

// ══════════════════════════════════════════════════════════════════════════════
// Phase 30c: buildBacktestDiagnostics — 回测后处理诊断输出 (从 backtest() 提取)
// ══════════════════════════════════════════════════════════════════════════════

void StrategyEngine::buildBacktestDiagnostics(
    StrategyBacktestResult& result,
    BacktestDayContext& ctx,
    const domain::backtest::BacktestRequest& req,
    const factor::compute::IMarketDataView* view,
    int totalDays,
    const std::function<void(double)>& onProgress,
    rules::AttributionCollector& attributionCollector)
{
    // ── 引用别名: ctx 成员映射为原局部变量名, 函数体零改动 ──
    const auto& equityCurve = ctx.equityCurve;
    const auto& totalFills = ctx.totalFills;
    const auto& winningFills = ctx.winningFills;
    const auto& losingFills = ctx.losingFills;
    const auto& stopLossExitCount = ctx.stopLossExitCount;
    const auto& totalStopLossOrders = ctx.totalStopLossOrders;
    const auto& stopLossFilled = ctx.stopLossFilled;
    const auto& stopLossSkippedNoHeld = ctx.stopLossSkippedNoHeld;
    const auto& ruleExitFilled = ctx.ruleExitFilled;
    const auto& normalSellFilled = ctx.normalSellFilled;
    const auto& totalProfit = ctx.totalProfit;
    const auto& totalLoss = ctx.totalLoss;
    const auto& largestWin = ctx.largestWin;
    const auto& largestLoss = ctx.largestLoss;
    const auto& symbolPnl = ctx.symbolPnl;
    const auto& dailyPositionSum = ctx.dailyPositionSum;
    const auto& deployedCapitalSum = ctx.deployedCapitalSum;
    const auto& daysWithTrades = ctx.daysWithTrades;
    auto& holdingDaysVec = ctx.holdingDaysVec;
    const auto& tradePnlVec = ctx.tradePnlVec;
    auto& entryFactorScores = ctx.entryFactorScores;
    const auto& poolSelectionDays = ctx.poolSelectionDays;
    const auto& totalPoolCandidates = ctx.totalPoolCandidates;
    const auto& riskRejectedCount = ctx.riskRejectedCount;

    // ═══════════════════════════════════════════════════════════════
    // 函数体: 与原 backtest() L2101-2322 逐字一致
    // ═══════════════════════════════════════════════════════════════

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
        INTERNAL_DEBUG_STREAM << "[backtest] 风控拒绝订单: " << riskRejectedCount;
    }
    if (onProgress) onProgress(100.0);
    INTERNAL_INFO_STREAM << "[backtest] 成功, 返回结果";
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
}

void StrategyEngine::logExecutionFill(const std::string& symbol, const std::string& side,
                                      double price, std::int64_t quantity, double commission,
                                      const std::string& fillTime, const std::string& brokerOrderId,
                                      const std::string& traceId)
{
    if (!m_tradeJournal) return;
    std::ostringstream js;
    js << fillTime << " 成交确认 " << side << " " << symbol << " "
       << quantity << "股 " << std::fixed << std::setprecision(2) << price
       << " 佣金:" << commission;
    if (!brokerOrderId.empty())
        js << " BrokerOrderId:" << brokerOrderId;
    if (!traceId.empty())
        js << " trace:" << traceId;
    m_tradeJournal->log(js.str());
}

} // namespace domain::strategy
