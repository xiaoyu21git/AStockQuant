#include "StrategyBacktestRequestAdapter.h"

#include "RiskConfigService.h"

#include "../../../application/backtest/include/StrategyBacktestEntryService.h"

#include <atomic>

namespace bridge::config {

namespace {

namespace rawkeys {

constexpr const char* kParameters = "parameters";
constexpr const char* kBacktestRuntime = "backtest_runtime";
constexpr const char* kBacktestSettings = "backtest_settings";
constexpr const char* kStrategyId = "strategyId";
constexpr const char* kUniverseId = "universeId";
constexpr const char* kLayerId = "layerId";
constexpr const char* kLayerType = "layerType";
constexpr const char* kTargetPositionCount = "targetPositionCount";
constexpr const char* kWindowStartDay = "windowStartDay";
constexpr const char* kWindowEndDay = "windowEndDay";
constexpr const char* kExecutionMode = "executionMode";
constexpr const char* kResolvedExplicitSymbolIds = "resolvedExplicitSymbolIds";
constexpr const char* kResolvedOverlayFactorIds = "resolvedOverlayFactorIds";
constexpr const char* kUniverseDatasetId = "universeDatasetId";
constexpr const char* kDataSourceDatasetId = "dataSourceDatasetId";
constexpr const char* kBenchmarkSymbolId = "benchmarkSymbolId";
constexpr const char* kDataSourceMode = "dataSourceMode";
constexpr const char* kMaxThreads = "maxThreads";
constexpr const char* kEnableCache = "enableCache";
constexpr const char* kCacheTtl = "cacheTTL";
constexpr const char* kHandleRunId = "handleRunId";
constexpr const char* kState = "state";
constexpr const char* kCurrentTradingDay = "currentTradingDay";
constexpr const char* kCompletedTradingDays = "completedTradingDays";
constexpr const char* kTotalTradingDays = "totalTradingDays";
constexpr const char* kCompletionRatio = "completionRatio";
constexpr const char* kFailureCode = "failureCode";
constexpr const char* kFailureDiagnostics = "failureDiagnostics";
constexpr const char* kAssumptions = "assumptions";
constexpr const char* kValidationIssues = "validationIssues";
constexpr const char* kRecords = "records";
constexpr const char* kElapsedNs = "elapsedNs";
constexpr const char* kPeakMemoryBytes = "peakMemoryBytes";
constexpr const char* kAssumptionCode = "assumptionCode";
constexpr const char* kValidationCode = "validationCode";
constexpr const char* kSeverity = "severity";
constexpr const char* kLayerIdValue = "layerId";
constexpr const char* kSymbolIdValue = "symbolId";
constexpr const char* kTradingDayValue = "tradingDay";
constexpr const char* kHasResult = "hasResult";
constexpr const char* kResult = "result";
constexpr const char* kRunMetadata = "runMetadata";
constexpr const char* kRunState = "runState";
constexpr const char* kStartedAtNs = "startedAtNs";
constexpr const char* kFinishedAtNs = "finishedAtNs";
constexpr const char* kConfigSnapshot = "configSnapshot";
constexpr const char* kResultStrategyId = "strategyId";
constexpr const char* kBehaviorKind = "behaviorKind";
constexpr const char* kResultExecutionMode = "executionMode";
constexpr const char* kResultUniverseId = "universeId";
constexpr const char* kExplicitSymbolIds = "explicitSymbolIds";
constexpr const char* kResultUniverseDatasetId = "universeDatasetId";
constexpr const char* kMarketProfile = "marketProfile";
constexpr const char* kInitialCapital = "initialCapital";
constexpr const char* kCommissionRate = "commissionRate";
constexpr const char* kSlippageRate = "slippageRate";
constexpr const char* kTaxRate = "taxRate";
constexpr const char* kMaxPositionRatio = "maxPositionRatio";
constexpr const char* kMaxSinglePositionRatio = "maxSinglePositionRatio";
constexpr const char* kMaxDrawdown = "maxDrawdown";
constexpr const char* kMaxDrawdownLimit = "maxDrawdownLimit";
constexpr const char* kStopLossRate = "stopLossRate";
constexpr const char* kPositionSizingMethod = "positionSizingMethod";
constexpr const char* kEnableShortSelling = "enableShortSelling";
constexpr const char* kRebalanceFrequencyDays = "rebalanceFrequencyDays";
constexpr const char* kDefaultOrderType = "defaultOrderType";
constexpr const char* kResultDataSourceDatasetId = "dataSourceDatasetId";
constexpr const char* kCacheTtlNs = "cacheTtlNs";
constexpr const char* kResultWindowStartDay = "windowStartDay";
constexpr const char* kResultWindowEndDay = "windowEndDay";
constexpr const char* kResultBenchmarkSymbolId = "benchmarkSymbolId";
constexpr const char* kLayers = "layers";
constexpr const char* kResultLayerType = "layerType";
constexpr const char* kInputUniverseId = "inputUniverseId";
constexpr const char* kResultTargetPositionCount = "targetPositionCount";
constexpr const char* kEvaluationIntervalDays = "evaluationIntervalDays";
constexpr const char* kOverlayEnabled = "overlayEnabled";
constexpr const char* kOverlayFactorIds = "overlayFactorIds";
constexpr const char* kOverlayMinimumCompositeScore = "overlayMinimumCompositeScore";
constexpr const char* kOverlayTargetPositionCount = "overlayTargetPositionCount";
constexpr const char* kPerformance = "performance";
constexpr const char* kStartingEquity = "startingEquity";
constexpr const char* kEndingEquity = "endingEquity";
constexpr const char* kTotalReturn = "totalReturn";
constexpr const char* kAnnualizedReturn = "annualizedReturn";
constexpr const char* kTradeStatistics = "tradeStatistics";
constexpr const char* kTradeCount = "tradeCount";
constexpr const char* kWinCount = "winCount";
constexpr const char* kWinRate = "winRate";
constexpr const char* kRiskMetrics = "riskMetrics";
constexpr const char* kAverageExposure = "averageExposure";
constexpr const char* kVolatility = "volatility";
constexpr const char* kTimeSeries = "timeSeries";
constexpr const char* kEquity = "equity";
constexpr const char* kPeriodReturn = "periodReturn";
constexpr const char* kTradeRecords = "tradeRecords";
constexpr const char* kOrderId = "orderId";
constexpr const char* kOrderSide = "orderSide";
constexpr const char* kQuantity = "quantity";
constexpr const char* kExecutionPrice = "executionPrice";
constexpr const char* kUniverseResolution = "universeResolution";
constexpr const char* kRequestedSymbolCount = "requestedSymbolCount";
constexpr const char* kResolvedSymbolCount = "resolvedSymbolCount";
constexpr const char* kRuleSummary = "ruleSummary";
constexpr const char* kBoundTemplateCount = "boundTemplateCount";
constexpr const char* kMatchedTemplateCount = "matchedTemplateCount";
constexpr const char* kBlockedTemplateCount = "blockedTemplateCount";
constexpr const char* kForcedExitTemplateCount = "forcedExitTemplateCount";
constexpr const char* kRecentDecisions = "recentDecisions";
constexpr const char* kCode = "code";
constexpr const char* kFactorIdValue = "factorId";
constexpr const char* kLayerAttribution = "layerAttribution";
constexpr const char* kContributions = "contributions";
constexpr const char* kContributionReturn = "contributionReturn";
constexpr const char* kHitRate = "hitRate";
constexpr const char* kBenchmark = "benchmark";
constexpr const char* kDiagnostics = "diagnostics";
constexpr const char* kEnabled = "enabled";
constexpr const char* kBenchmarkReturn = "benchmarkReturn";
constexpr const char* kExcessReturn = "excessReturn";

} // namespace rawkeys

using application::backtest::BacktestRunOverrides;
using application::backtest::CanonicalBacktestRequestFactory;
using application::backtest::StrategyBacktestEntryService;
using application::backtest::StrategyBacktestEntryServiceError;
using application::backtest::StrategyBacktestEntryServiceErrorCode;
using application::backtest::StrategyBacktestEntrySpec;
using domain::backtest::strategy_engine::AsyncBacktestHandle;
using domain::backtest::strategy_engine::CancellationRequest;
using domain::backtest::strategy_engine::CandidateCount;
using domain::backtest::strategy_engine::DataSourceMode;
using domain::backtest::strategy_engine::DurationNs;
using domain::backtest::strategy_engine::FactorId;
using domain::backtest::strategy_engine::OverlayBindingScopeId;
using domain::backtest::strategy_engine::RunId;
using domain::backtest::strategy_engine::RuntimeOptions;
using domain::backtest::strategy_engine::SymbolId;

[[noreturn]] void fail(const StrategyBacktestRequestAdapterErrorCode code)
{
    throw StrategyBacktestRequestAdapterError{code};
}

OverlayBindingScopeId createOverlayBindingScopeId()
{
    static std::atomic<std::uint64_t> nextScopeId{1ULL};
    return OverlayBindingScopeId(nextScopeId.fetch_add(1ULL, std::memory_order_relaxed));
}

QString rawKeyText(const char* key)
{
    return QString::fromLatin1(key);
}

QVariant rawMapValue(const QVariantMap& map, const char* key)
{
    return map.value(rawKeyText(key));
}

bool hasRawMapValue(const QVariantMap& map, const char* key)
{
    const QVariant value = rawMapValue(map, key);
    return value.isValid() && !value.isNull();
}

QVariantMap variantMapValue(const QVariant& value)
{
    return value.canConvert<QVariantMap>() ? value.toMap() : QVariantMap();
}

template <typename IdType>
QVariant unsignedIdVariant(const IdType& id)
{
    return QVariant::fromValue<qulonglong>(id.value());
}

template <typename ListType>
QVariantList unsignedIdList(const ListType& ids)
{
    QVariantList result;
    for (const auto& id : ids.values()) {
        result.append(unsignedIdVariant(id));
    }
    return result;
}

void insertIfValid(QVariantMap& map, const char* key, const domain::backtest::strategy_engine::LayerId layerId)
{
    if (layerId.isValid()) {
        map.insert(rawKeyText(key), QVariant::fromValue<qulonglong>(layerId.value()));
    }
}

void insertIfValid(QVariantMap& map, const char* key, const domain::backtest::strategy_engine::SymbolId symbolId)
{
    if (symbolId.isValid()) {
        map.insert(rawKeyText(key), QVariant::fromValue<qulonglong>(symbolId.value()));
    }
}

void insertIfValid(QVariantMap& map,
                   const char* key,
                   const domain::backtest::strategy_engine::TradingDayIndex tradingDay)
{
    if (tradingDay.isValid()) {
        map.insert(rawKeyText(key), tradingDay.value());
    }
}

QVariantMap toVariantMap(const domain::backtest::strategy_engine::EngineAssumption& assumption)
{
    QVariantMap map;
    map.insert(rawKeyText(rawkeys::kAssumptionCode), static_cast<int>(assumption.code));
    insertIfValid(map, rawkeys::kLayerIdValue, assumption.layerId);
    return map;
}

QVariantMap toVariantMap(const domain::backtest::strategy_engine::ValidationIssue& issue)
{
    QVariantMap map;
    map.insert(rawKeyText(rawkeys::kValidationCode), static_cast<int>(issue.code));
    insertIfValid(map, rawkeys::kLayerIdValue, issue.layerId);
    insertIfValid(map, rawkeys::kSymbolIdValue, issue.symbolId);
    return map;
}

QVariantMap toVariantMap(const domain::backtest::strategy_engine::DiagnosticRecord& record)
{
    QVariantMap map;
    map.insert(rawKeyText(rawkeys::kSeverity), static_cast<int>(record.severity));
    map.insert(rawKeyText(rawkeys::kAssumptionCode), static_cast<int>(record.assumptionCode));
    map.insert(rawKeyText(rawkeys::kValidationCode), static_cast<int>(record.validationCode));
    insertIfValid(map, rawkeys::kTradingDayValue, record.tradingDay);
    insertIfValid(map, rawkeys::kLayerIdValue, record.layerId);
    insertIfValid(map, rawkeys::kSymbolIdValue, record.symbolId);
    return map;
}

QVariantMap toVariantMap(const domain::backtest::strategy_engine::RuleDecision& decision)
{
    QVariantMap map;
    map.insert(rawKeyText(rawkeys::kCode), static_cast<int>(decision.code));
    insertIfValid(map, rawkeys::kLayerIdValue, decision.layerId);
    insertIfValid(map, rawkeys::kSymbolIdValue, decision.symbolId);
    if (decision.factorId.isValid()) {
        map.insert(rawKeyText(rawkeys::kFactorIdValue), unsignedIdVariant(decision.factorId));
    }
    return map;
}

QVariantMap toVariantMap(const domain::backtest::strategy_engine::DecisionLayer& layer)
{
    QVariantMap map;
    map.insert(rawKeyText(rawkeys::kLayerIdValue), unsignedIdVariant(layer.id));
    map.insert(rawKeyText(rawkeys::kResultLayerType), static_cast<int>(layer.type));
    map.insert(rawKeyText(rawkeys::kInputUniverseId), unsignedIdVariant(layer.inputUniverseId));
    map.insert(rawKeyText(rawkeys::kResultTargetPositionCount), QVariant::fromValue<uint>(layer.targetPositionCount.value()));
    map.insert(rawKeyText(rawkeys::kEvaluationIntervalDays), QVariant::fromValue<uint>(layer.evaluationIntervalDays.value()));
    map.insert(rawKeyText(rawkeys::kOverlayEnabled), layer.overlay.enabled);
    map.insert(rawKeyText(rawkeys::kOverlayFactorIds), unsignedIdList(layer.overlay.factorIds));
    map.insert(rawKeyText(rawkeys::kOverlayMinimumCompositeScore), layer.overlay.minimumCompositeScore.value());
    map.insert(rawKeyText(rawkeys::kOverlayTargetPositionCount), QVariant::fromValue<uint>(layer.overlay.targetPositionCount.value()));
    return map;
}

QVariantMap toVariantMap(const domain::backtest::strategy_engine::BacktestRequest& request)
{
    QVariantMap map;
    map.insert(rawKeyText(rawkeys::kResultStrategyId), unsignedIdVariant(request.identity.strategyId));
    map.insert(rawKeyText(rawkeys::kBehaviorKind), static_cast<int>(request.identity.behaviorKind));
    map.insert(rawKeyText(rawkeys::kResultExecutionMode), static_cast<int>(request.identity.executionMode));

    QVariantList layers;
    for (const auto& layer : request.spec.layers.values()) {
        layers.append(toVariantMap(layer));
    }
    map.insert(rawKeyText(rawkeys::kLayers), layers);

    map.insert(rawKeyText(rawkeys::kResultUniverseId), unsignedIdVariant(request.universeSpec.universeId));
    map.insert(rawKeyText(rawkeys::kExplicitSymbolIds), unsignedIdList(request.universeSpec.explicitSymbols));
    if (request.universeSpec.datasetId.isValid()) {
        map.insert(rawKeyText(rawkeys::kResultUniverseDatasetId), unsignedIdVariant(request.universeSpec.datasetId));
    }

    map.insert(rawKeyText(rawkeys::kMarketProfile), static_cast<int>(request.marketEnvironmentSpec.profile));
    map.insert(rawKeyText(rawkeys::kInitialCapital), request.costSpec.initialCapital.value());
    map.insert(rawKeyText(rawkeys::kCommissionRate), request.costSpec.commissionRate.value());
    map.insert(rawKeyText(rawkeys::kSlippageRate), request.costSpec.slippageRate.value());
    map.insert(rawKeyText(rawkeys::kTaxRate), request.costSpec.taxRate.value());
    map.insert(rawKeyText(rawkeys::kMaxPositionRatio), request.riskSpec.maxPositionRatio.value());
    map.insert(rawKeyText(rawkeys::kMaxSinglePositionRatio), request.riskSpec.maxSinglePositionRatio.value());
    map.insert(rawKeyText(rawkeys::kMaxDrawdownLimit), request.riskSpec.maxDrawdownLimit.value());
    map.insert(rawKeyText(rawkeys::kStopLossRate), request.riskSpec.stopLossRate.value());
    map.insert(rawKeyText(rawkeys::kPositionSizingMethod), static_cast<int>(request.executionSpec.positionSizingMethod));
    map.insert(rawKeyText(rawkeys::kEnableShortSelling), request.executionSpec.enableShortSelling);
    map.insert(rawKeyText(rawkeys::kRebalanceFrequencyDays), QVariant::fromValue<uint>(request.executionSpec.rebalanceFrequencyDays.value()));
    map.insert(rawKeyText(rawkeys::kDefaultOrderType), static_cast<int>(request.executionSpec.defaultOrderType));
    map.insert(rawKeyText(rawkeys::kDataSourceMode), static_cast<int>(request.dataSourceSpec.mode));
    if (request.dataSourceSpec.datasetId.isValid()) {
        map.insert(rawKeyText(rawkeys::kResultDataSourceDatasetId), unsignedIdVariant(request.dataSourceSpec.datasetId));
    }
    map.insert(rawKeyText(rawkeys::kMaxThreads), QVariant::fromValue<uint>(request.runtimeOptions.maxThreads.value()));
    map.insert(rawKeyText(rawkeys::kEnableCache), request.runtimeOptions.enableCache);
    map.insert(rawKeyText(rawkeys::kCacheTtlNs), request.runtimeOptions.cacheTtl.value());
    map.insert(rawKeyText(rawkeys::kResultWindowStartDay), request.window.startDay.value());
    map.insert(rawKeyText(rawkeys::kResultWindowEndDay), request.window.endDay.value());
    if (request.benchmarkSymbol.has_value()) {
        map.insert(rawKeyText(rawkeys::kResultBenchmarkSymbolId), unsignedIdVariant(*request.benchmarkSymbol));
    }
    return map;
}

QVariantMap toVariantMap(const domain::backtest::strategy_engine::RunMetadata& runMetadata)
{
    QVariantMap map;
    map.insert(rawKeyText(rawkeys::kHandleRunId), unsignedIdVariant(runMetadata.runId));
    map.insert(rawKeyText(rawkeys::kRunState), static_cast<int>(runMetadata.state));
    map.insert(rawKeyText(rawkeys::kStartedAtNs), runMetadata.startedAt.value());
    map.insert(rawKeyText(rawkeys::kFinishedAtNs), runMetadata.finishedAt.value());
    map.insert(rawKeyText(rawkeys::kElapsedNs), runMetadata.elapsed.value());
    return map;
}

QVariantMap toVariantMap(const domain::backtest::strategy_engine::PerformanceSummary& performance)
{
    QVariantMap map;
    map.insert(rawKeyText(rawkeys::kStartingEquity), performance.startingEquity.value());
    map.insert(rawKeyText(rawkeys::kEndingEquity), performance.endingEquity.value());
    map.insert(rawKeyText(rawkeys::kTotalReturn), performance.totalReturn.value());
    map.insert(rawKeyText(rawkeys::kAnnualizedReturn), performance.annualizedReturn.value());
    map.insert(rawKeyText(rawkeys::kMaxDrawdown), performance.maxDrawdown.value());
    return map;
}

QVariantMap toVariantMap(const domain::backtest::strategy_engine::TradeStatistics& tradeStatistics)
{
    QVariantMap map;
    map.insert(rawKeyText(rawkeys::kTradeCount), QVariant::fromValue<uint>(tradeStatistics.tradeCount.value()));
    map.insert(rawKeyText(rawkeys::kWinCount), QVariant::fromValue<uint>(tradeStatistics.winCount.value()));
    map.insert(rawKeyText(rawkeys::kWinRate), tradeStatistics.winRate.value());
    return map;
}

QVariantMap toVariantMap(const domain::backtest::strategy_engine::RiskMetrics& riskMetrics)
{
    QVariantMap map;
    map.insert(rawKeyText(rawkeys::kMaxDrawdown), riskMetrics.maxDrawdown.value());
    map.insert(rawKeyText(rawkeys::kAverageExposure), riskMetrics.averageExposure.value());
    map.insert(rawKeyText(rawkeys::kVolatility), riskMetrics.volatility.value());
    return map;
}

QVariantMap toVariantMap(const domain::backtest::strategy_engine::TimeSeriesPoint& point)
{
    QVariantMap map;
    map.insert(rawKeyText(rawkeys::kTradingDayValue), point.tradingDay.value());
    map.insert(rawKeyText(rawkeys::kEquity), point.equity.value());
    map.insert(rawKeyText(rawkeys::kPeriodReturn), point.periodReturn.value());
    return map;
}

QVariantMap toVariantMap(const domain::backtest::strategy_engine::TradeRecord& tradeRecord)
{
    QVariantMap map;
    map.insert(rawKeyText(rawkeys::kOrderId), unsignedIdVariant(tradeRecord.orderId));
    map.insert(rawKeyText(rawkeys::kSymbolIdValue), unsignedIdVariant(tradeRecord.symbolId));
    map.insert(rawKeyText(rawkeys::kOrderSide), static_cast<int>(tradeRecord.side));
    map.insert(rawKeyText(rawkeys::kQuantity), unsignedIdVariant(tradeRecord.quantity));
    map.insert(rawKeyText(rawkeys::kExecutionPrice), tradeRecord.executionPrice.value());
    map.insert(rawKeyText(rawkeys::kTradingDayValue), tradeRecord.tradingDay.value());
    return map;
}

QVariantMap toVariantMap(const domain::backtest::strategy_engine::UniverseResolutionSummary& universeResolution)
{
    QVariantMap map;
    map.insert(rawKeyText(rawkeys::kResultUniverseId), unsignedIdVariant(universeResolution.universeId));
    map.insert(rawKeyText(rawkeys::kRequestedSymbolCount), QVariant::fromValue<uint>(universeResolution.requestedSymbolCount.value()));
    map.insert(rawKeyText(rawkeys::kResolvedSymbolCount), QVariant::fromValue<uint>(universeResolution.resolvedSymbolCount.value()));
    return map;
}

QVariantMap toVariantMap(const domain::backtest::strategy_engine::RuleTemplateSummary& ruleSummary)
{
    QVariantMap map;
    map.insert(rawKeyText(rawkeys::kBoundTemplateCount), QVariant::fromValue<uint>(ruleSummary.boundTemplateCount.value()));
    map.insert(rawKeyText(rawkeys::kMatchedTemplateCount), QVariant::fromValue<uint>(ruleSummary.matchedTemplateCount.value()));
    map.insert(rawKeyText(rawkeys::kBlockedTemplateCount), QVariant::fromValue<uint>(ruleSummary.blockedTemplateCount.value()));
    map.insert(rawKeyText(rawkeys::kForcedExitTemplateCount), QVariant::fromValue<uint>(ruleSummary.forcedExitTemplateCount.value()));

    QVariantList recentDecisions;
    for (const auto& decision : ruleSummary.recentDecisions.values()) {
        recentDecisions.append(toVariantMap(decision));
    }
    map.insert(rawKeyText(rawkeys::kRecentDecisions), recentDecisions);
    return map;
}

QVariantMap toVariantMap(const domain::backtest::strategy_engine::LayerContribution& contribution)
{
    QVariantMap map;
    map.insert(rawKeyText(rawkeys::kLayerIdValue), unsignedIdVariant(contribution.layerId));
    map.insert(rawKeyText(rawkeys::kContributionReturn), contribution.contributionReturn.value());
    map.insert(rawKeyText(rawkeys::kHitRate), contribution.hitRate.value());
    return map;
}

QVariantMap toVariantMap(const domain::backtest::strategy_engine::BenchmarkComparison& benchmark)
{
    QVariantMap map;
    map.insert(rawKeyText(rawkeys::kEnabled), benchmark.enabled);
    if (benchmark.enabled) {
        map.insert(rawKeyText(rawkeys::kResultBenchmarkSymbolId), unsignedIdVariant(benchmark.benchmarkSymbol));
        map.insert(rawKeyText(rawkeys::kBenchmarkReturn), benchmark.benchmarkReturn.value());
        map.insert(rawKeyText(rawkeys::kExcessReturn), benchmark.excessReturn.value());
    }
    return map;
}

QVariantMap toVariantMap(const domain::backtest::strategy_engine::Diagnostics& diagnostics)
{
    QVariantMap map;
    QVariantList assumptions;
    for (const domain::backtest::strategy_engine::EngineAssumption& assumption : diagnostics.assumptions) {
        assumptions.append(toVariantMap(assumption));
    }

    QVariantList validationIssues;
    for (const domain::backtest::strategy_engine::ValidationIssue& issue : diagnostics.validationIssues) {
        validationIssues.append(toVariantMap(issue));
    }

    QVariantList records;
    for (const domain::backtest::strategy_engine::DiagnosticRecord& record : diagnostics.records) {
        records.append(toVariantMap(record));
    }

    map.insert(rawKeyText(rawkeys::kAssumptions), assumptions);
    map.insert(rawKeyText(rawkeys::kValidationIssues), validationIssues);
    map.insert(rawKeyText(rawkeys::kRecords), records);
    map.insert(rawKeyText(rawkeys::kElapsedNs), diagnostics.elapsed.value());
    map.insert(rawKeyText(rawkeys::kPeakMemoryBytes), QVariant::fromValue<qulonglong>(diagnostics.peakMemory.value()));
    return map;
}

StrategyBacktestEntryService& requireRuntime(StrategyBacktestEntryService* entryService)
{
    if (!entryService) {
        fail(StrategyBacktestRequestAdapterErrorCode::MissingRuntimeService);
    }

    return *entryService;
}

AsyncBacktestHandle buildHandle(const qulonglong handleRunId)
{
    if (handleRunId == 0ULL) {
        fail(StrategyBacktestRequestAdapterErrorCode::InvalidBacktestHandle);
    }

    return AsyncBacktestHandle{RunId(static_cast<std::uint64_t>(handleRunId))};
}

StrategyBacktestRequestAdapterErrorCode mapEntryServiceError(const StrategyBacktestEntryServiceError& error)
{
    switch (error.code) {
    case StrategyBacktestEntryServiceErrorCode::MissingRuntimeService:
        return StrategyBacktestRequestAdapterErrorCode::MissingRuntimeService;
    case StrategyBacktestEntryServiceErrorCode::InvalidEntrySpec:
    case StrategyBacktestEntryServiceErrorCode::UnsupportedMarketEnvironment:
    case StrategyBacktestEntryServiceErrorCode::None:
        return StrategyBacktestRequestAdapterErrorCode::InvalidResolutionContext;
    }

    return StrategyBacktestRequestAdapterErrorCode::InvalidResolutionContext;
}

StrategyBacktestRequestAdapterErrorCode mapRequestFactoryError(
    const application::backtest::BacktestRequestFactoryError& error)
{
    using application::backtest::BacktestRequestFactoryErrorCode;

    switch (error.code) {
    case BacktestRequestFactoryErrorCode::None:
        return StrategyBacktestRequestAdapterErrorCode::None;
    case BacktestRequestFactoryErrorCode::InvalidUniverseResolution:
        return StrategyBacktestRequestAdapterErrorCode::InvalidUniverseResolution;
    case BacktestRequestFactoryErrorCode::InvalidFactorOverlayResolution:
        return StrategyBacktestRequestAdapterErrorCode::InvalidFactorOverlayResolution;
    case BacktestRequestFactoryErrorCode::InvalidAggregate:
    case BacktestRequestFactoryErrorCode::InvalidOverrides:
    case BacktestRequestFactoryErrorCode::UnsupportedUniverseMode:
    case BacktestRequestFactoryErrorCode::UnsupportedPositionSizingMethod:
    case BacktestRequestFactoryErrorCode::UnsupportedBehaviorKind:
    case BacktestRequestFactoryErrorCode::InvalidRequest:
        return StrategyBacktestRequestAdapterErrorCode::InvalidBacktestRequest;
    }

    return StrategyBacktestRequestAdapterErrorCode::InvalidBacktestRequest;
}

std::uint64_t buildStableRuntimeFactorId(const domain::strategy::FactorId& factorId)
{
    const QByteArray utf8 = factorId.text().toUtf8();
    std::uint64_t hashValue = 1469598103934665603ULL;
    for (const char byte : utf8) {
        hashValue ^= static_cast<unsigned char>(byte);
        hashValue *= 1099511628211ULL;
    }

    if (hashValue == 0ULL) {
        hashValue = 1ULL;
    }

    return hashValue;
}

bool requiresOverlayResolution(const domain::strategy::StrategyAggregate& aggregate)
{
    return aggregate.identity.executionKind == domain::strategy::StrategyExecutionKind::FactorWeightedPortfolio
        || aggregate.spec.factorOverlay.enabled;
}

application::backtest::FactorIdList synthesizeResolvedOverlayFactorIds(
    const domain::strategy::StrategyAggregate& aggregate)
{
    application::backtest::FactorIdList resolvedIds;
    if (!requiresOverlayResolution(aggregate)) {
        return resolvedIds;
    }

    const auto& selectedFactors = aggregate.spec.factorOverlay.selectedFactors;
    for (int index = 0; index < selectedFactors.size(); ++index) {
        resolvedIds.add(FactorId(buildStableRuntimeFactorId(selectedFactors.at(index))));
    }

    return resolvedIds;
}

QList<QVariantMap> runtimeSources(const QVariantMap& strategy)
{
    const QVariantMap parameters = variantMapValue(rawMapValue(strategy, rawkeys::kParameters));
    QList<QVariantMap> sources;

    const auto appendSource = [&sources](const QVariantMap& source) {
        if (!source.isEmpty()) {
            sources.push_back(source);
        }
    };

    appendSource(variantMapValue(rawMapValue(parameters, rawkeys::kBacktestRuntime)));
    appendSource(variantMapValue(rawMapValue(strategy, rawkeys::kBacktestRuntime)));
    appendSource(variantMapValue(rawMapValue(parameters, rawkeys::kBacktestSettings)));
    appendSource(variantMapValue(rawMapValue(strategy, rawkeys::kBacktestSettings)));
    return sources;
}

QVariant firstConfiguredValue(const QList<QVariantMap>& sources,
                             std::initializer_list<const char*> keys)
{
    for (const QVariantMap& source : sources) {
        for (const char* key : keys) {
            const QVariant value = source.value(QString::fromUtf8(key));
            if (!value.isValid() || value.isNull()) {
                continue;
            }
            if (value.typeId() == QMetaType::QString && value.toString().trimmed().isEmpty()) {
                continue;
            }
            return value;
        }
    }
    return {};
}

QVariant requiredContextValue(const QVariantMap& runContext, const QString& key)
{
    const QVariant value = runContext.value(key);
    if (!value.isValid() || value.isNull()) {
        fail(StrategyBacktestRequestAdapterErrorCode::MissingRunContextField);
    }
    if (value.typeId() == QMetaType::QString && value.toString().trimmed().isEmpty()) {
        fail(StrategyBacktestRequestAdapterErrorCode::MissingRunContextField);
    }
    return value;
}

QVariant requiredContextValue(const QVariantMap& runContext, const char* key)
{
    return requiredContextValue(runContext, rawKeyText(key));
}

std::uint64_t parseRequiredUnsigned(const QVariantMap& runContext, const char* key)
{
    bool ok = false;
    const qulonglong value = requiredContextValue(runContext, key).toULongLong(&ok);
    if (!ok || value == 0ULL) {
        fail(StrategyBacktestRequestAdapterErrorCode::InvalidRunContextField);
    }
    return static_cast<std::uint64_t>(value);
}

std::int32_t parseRequiredTradingDayIndex(const QVariantMap& runContext, const char* key)
{
    bool ok = false;
    const int value = requiredContextValue(runContext, key).toInt(&ok);
    if (!ok || value < 0) {
        fail(StrategyBacktestRequestAdapterErrorCode::InvalidRunContextField);
    }
    return static_cast<std::int32_t>(value);
}

template <typename ItemType, typename ListType>
ListType parseIdList(const QVariantMap& runContext, const char* key)
{
    ListType values;
    const QVariant rawValue = rawMapValue(runContext, key);
    if (!rawValue.isValid() || rawValue.isNull()) {
        return values;
    }

    const QVariantList items = rawValue.toList();
    for (const QVariant& item : items) {
        bool ok = false;
        const qulonglong parsed = item.toULongLong(&ok);
        if (!ok || parsed == 0ULL) {
            fail(StrategyBacktestRequestAdapterErrorCode::InvalidRunContextField);
        }
        values.add(ItemType(static_cast<std::uint64_t>(parsed)));
    }
    return values;
}

application::backtest::LayerType parseLayerType(const QVariantMap& runContext)
{
    bool ok = false;
    const int value = requiredContextValue(runContext, rawkeys::kLayerType).toInt(&ok);
    if (!ok) {
        fail(StrategyBacktestRequestAdapterErrorCode::InvalidRunContextField);
    }

    switch (static_cast<application::backtest::LayerType>(value)) {
    case application::backtest::LayerType::Strategic:
    case application::backtest::LayerType::Tactical:
    case application::backtest::LayerType::Execution:
        return static_cast<application::backtest::LayerType>(value);
    }

    fail(StrategyBacktestRequestAdapterErrorCode::UnsupportedLayerType);
}

application::backtest::ExecutionMode parseExecutionMode(const QVariantMap& runContext)
{
    bool ok = false;
    const int value = requiredContextValue(runContext, rawkeys::kExecutionMode).toInt(&ok);
    if (!ok) {
        fail(StrategyBacktestRequestAdapterErrorCode::InvalidRunContextField);
    }

    switch (static_cast<application::backtest::ExecutionMode>(value)) {
    case application::backtest::ExecutionMode::EndOfDay:
    case application::backtest::ExecutionMode::Intraday:
        return static_cast<application::backtest::ExecutionMode>(value);
    }

    fail(StrategyBacktestRequestAdapterErrorCode::UnsupportedExecutionMode);
}

DataSourceMode mapDataSourceMode(const QVariant& value)
{
    bool ok = false;
    const int modeIndex = value.toInt(&ok);
    if (!ok) {
        fail(StrategyBacktestRequestAdapterErrorCode::UnsupportedDataSourceMode);
    }

    switch (static_cast<DataSourceMode>(modeIndex)) {
    case DataSourceMode::Raw:
    case DataSourceMode::Cleaned:
    case DataSourceMode::CacheDataset:
        return static_cast<DataSourceMode>(modeIndex);
    }

    fail(StrategyBacktestRequestAdapterErrorCode::UnsupportedDataSourceMode);
}

RuntimeOptions buildRuntimeOptions(const QList<QVariantMap>& sources)
{
    const QVariant maxThreadsValue = firstConfiguredValue(sources, {rawkeys::kMaxThreads});
    bool ok = false;
    const int maxThreads = maxThreadsValue.toInt(&ok);
    if (!ok || maxThreads <= 0) {
        fail(StrategyBacktestRequestAdapterErrorCode::MissingRuntimeThreads);
    }

    const QVariant enableCacheValue = firstConfiguredValue(sources, {rawkeys::kEnableCache});
    const bool enableCache = enableCacheValue.isValid() ? enableCacheValue.toBool() : false;

    qint64 cacheTtlSeconds = 0;
    const QVariant cacheTtlValue = firstConfiguredValue(sources, {rawkeys::kCacheTtl});
    if (cacheTtlValue.isValid()) {
        cacheTtlSeconds = cacheTtlValue.toLongLong(&ok);
        if (!ok || cacheTtlSeconds < 0) {
            fail(StrategyBacktestRequestAdapterErrorCode::InvalidCacheTtl);
        }
    } else if (enableCache) {
        fail(StrategyBacktestRequestAdapterErrorCode::InvalidCacheTtl);
    }

    return RuntimeOptions{CandidateCount(static_cast<std::uint32_t>(maxThreads)),
                          enableCache,
                          DurationNs(cacheTtlSeconds * 1000000000LL)};
}

struct PreparedBacktestEntry final {
    domain::strategy::StrategyAggregate aggregate;
    StrategyBacktestEntrySpec entrySpec;
};

StrategyBacktestEntrySpec buildEntrySpec(const QVariantMap& strategy,
                                         const StrategyBacktestRunContext& runContext,
                                         const StrategyStructureResolution& resolution)
{
    const QList<QVariantMap> sources = runtimeSources(strategy);

    const QVariant dataSourceModeValue = firstConfiguredValue(
        {resolution.backtestAssumptions},
        {rawkeys::kDataSourceMode});
    if (!dataSourceModeValue.isValid()) {
        fail(StrategyBacktestRequestAdapterErrorCode::MissingDataSourceMode);
    }

    const DataSourceMode dataSourceMode = mapDataSourceMode(dataSourceModeValue);
    if (dataSourceMode == DataSourceMode::CacheDataset && !runContext.dataSourceDatasetId.isValid()) {
        fail(StrategyBacktestRequestAdapterErrorCode::MissingCacheDatasetId);
    }

    const RuntimeOptions runtimeOptions = buildRuntimeOptions(sources);

    StrategyBacktestEntrySpec entrySpec;
    entrySpec.strategyId = runContext.strategyId;
    entrySpec.overlayBindingScopeId = runContext.overlayBindingScopeId;
    entrySpec.universeId = runContext.universeId;
    entrySpec.layerId = runContext.layerId;
    entrySpec.layerType = runContext.layerType;
    entrySpec.resolvedExplicitSymbols = runContext.resolvedExplicitSymbols;
    entrySpec.resolvedOverlayFactorIds = runContext.resolvedOverlayFactorIds;
    entrySpec.targetPositionCount = runContext.targetPositionCount;
    entrySpec.window = runContext.window;
    entrySpec.dataSourceMode = dataSourceMode;
    entrySpec.universeDatasetId = runContext.universeDatasetId;
    entrySpec.dataSourceDatasetId = runContext.dataSourceDatasetId;
    entrySpec.maxThreads = runtimeOptions.maxThreads;
    entrySpec.enableCache = runtimeOptions.enableCache;
    entrySpec.cacheTtl = runtimeOptions.cacheTtl;
    entrySpec.executionMode = runContext.executionMode;
    entrySpec.benchmarkSymbol = runContext.benchmarkSymbol;
    return entrySpec;
}

PreparedBacktestEntry prepareBacktestEntry(const QVariantMap& strategy,
                                           const StrategyBacktestRunContext& runContext,
                                           const QVariantMap& appliedRiskConfig)
{
    if (!runContext.isValid()) {
        fail(StrategyBacktestRequestAdapterErrorCode::InvalidResolutionContext);
    }

    PreparedBacktestEntry prepared;
    prepared.aggregate = buildStrategyAggregate(strategy, appliedRiskConfig);
    const StrategyStructureResolution resolution = StrategyStructureResolverSet().resolve(strategy, appliedRiskConfig);
    prepared.entrySpec = buildEntrySpec(strategy, runContext, resolution);
    prepared.entrySpec.resolvedOverlayFactorIds = synthesizeResolvedOverlayFactorIds(prepared.aggregate);

    const QString configuredBenchmark = risk::config::benchmarkSymbol(resolution.backtestAssumptions).trimmed();
    if (!configuredBenchmark.isEmpty() && !runContext.benchmarkSymbol.has_value()) {
        fail(StrategyBacktestRequestAdapterErrorCode::MissingBenchmarkResolution);
    }

    return prepared;
}

} // namespace

StrategyBacktestRunContext buildStrategyBacktestRunContext(const QVariantMap& runContext)
{
    StrategyBacktestRunContext typedContext;
    typedContext.strategyId = application::backtest::StrategyId(parseRequiredUnsigned(runContext, rawkeys::kStrategyId));
    typedContext.overlayBindingScopeId = createOverlayBindingScopeId();
    typedContext.universeId = application::backtest::UniverseId(parseRequiredUnsigned(runContext, rawkeys::kUniverseId));
    typedContext.layerId = application::backtest::LayerId(parseRequiredUnsigned(runContext, rawkeys::kLayerId));
    typedContext.layerType = parseLayerType(runContext);
    typedContext.targetPositionCount = application::backtest::CandidateCount(
        static_cast<std::uint32_t>(parseRequiredUnsigned(runContext, rawkeys::kTargetPositionCount)));
    typedContext.window = application::backtest::DateRange{
        domain::backtest::strategy_engine::TradingDayIndex(parseRequiredTradingDayIndex(runContext, rawkeys::kWindowStartDay)),
        domain::backtest::strategy_engine::TradingDayIndex(parseRequiredTradingDayIndex(runContext, rawkeys::kWindowEndDay))};
    typedContext.executionMode = parseExecutionMode(runContext);
    typedContext.resolvedExplicitSymbols = parseIdList<SymbolId, application::backtest::SymbolIdList>(
        runContext,
        rawkeys::kResolvedExplicitSymbolIds);
    typedContext.resolvedOverlayFactorIds = parseIdList<FactorId, application::backtest::FactorIdList>(
        runContext,
        rawkeys::kResolvedOverlayFactorIds);

    if (hasRawMapValue(runContext, rawkeys::kUniverseDatasetId)) {
        typedContext.universeDatasetId = application::backtest::DatasetId(
            static_cast<std::uint64_t>(parseRequiredUnsigned(runContext, rawkeys::kUniverseDatasetId)));
    }

    if (hasRawMapValue(runContext, rawkeys::kDataSourceDatasetId)) {
        typedContext.dataSourceDatasetId = application::backtest::DatasetId(
            static_cast<std::uint64_t>(parseRequiredUnsigned(runContext, rawkeys::kDataSourceDatasetId)));
    }

    if (hasRawMapValue(runContext, rawkeys::kBenchmarkSymbolId)) {
        typedContext.benchmarkSymbol = application::backtest::SymbolId(
            static_cast<std::uint64_t>(parseRequiredUnsigned(runContext, rawkeys::kBenchmarkSymbolId)));
    }

    if (!typedContext.isValid()) {
        fail(StrategyBacktestRequestAdapterErrorCode::InvalidResolutionContext);
    }

    return typedContext;
}

domain::backtest::strategy_engine::BacktestRequest buildStrategyBacktestRequest(
    const QVariantMap& strategy,
    const StrategyBacktestRunContext& runContext,
    const QVariantMap& appliedRiskConfig)
{
    const PreparedBacktestEntry prepared = prepareBacktestEntry(strategy, runContext, appliedRiskConfig);
    const CanonicalBacktestRequestFactory factory;
    const StrategyBacktestEntryService entryService(factory);
    try {
        return entryService.buildRequest(prepared.aggregate, prepared.entrySpec);
    } catch (const application::backtest::BacktestRequestFactoryError& error) {
        fail(mapRequestFactoryError(error));
    }
}

AsyncBacktestHandle launchStrategyBacktest(
    const QVariantMap& strategy,
    const StrategyBacktestRunContext& runContext,
    StrategyBacktestEntryService* entryService,
    const QVariantMap& appliedRiskConfig)
{
    const PreparedBacktestEntry prepared = prepareBacktestEntry(strategy, runContext, appliedRiskConfig);
    try {
        return requireRuntime(entryService).run(prepared.aggregate, prepared.entrySpec);
    } catch (const application::backtest::BacktestRequestFactoryError& error) {
        fail(mapRequestFactoryError(error));
    } catch (const StrategyBacktestEntryServiceError& error) {
        fail(mapEntryServiceError(error));
    }
}

QVariantMap buildStrategyBacktestHandleMap(const AsyncBacktestHandle& handle)
{
    if (!handle.isValid()) {
        fail(StrategyBacktestRequestAdapterErrorCode::InvalidBacktestHandle);
    }

    QVariantMap map;
    map.insert(rawKeyText(rawkeys::kHandleRunId), QVariant::fromValue<qulonglong>(handle.runId.value()));
    return map;
}

QVariantMap buildStrategyBacktestResultMap(const domain::backtest::strategy_engine::BacktestResultDto& result)
{
    if (!result.isValid()) {
        fail(StrategyBacktestRequestAdapterErrorCode::InvalidBacktestResult);
    }

    QVariantMap map;
    map.insert(rawKeyText(rawkeys::kRunMetadata), toVariantMap(result.runMetadata));
    map.insert(rawKeyText(rawkeys::kConfigSnapshot), toVariantMap(result.configSnapshot));
    map.insert(rawKeyText(rawkeys::kPerformance), toVariantMap(result.performance));
    map.insert(rawKeyText(rawkeys::kTradeStatistics), toVariantMap(result.tradeStatistics));
    map.insert(rawKeyText(rawkeys::kRiskMetrics), toVariantMap(result.riskMetrics));

    QVariantList timeSeries;
    for (const auto& point : result.timeSeries.values()) {
        timeSeries.append(toVariantMap(point));
    }
    map.insert(rawKeyText(rawkeys::kTimeSeries), timeSeries);

    QVariantList tradeRecords;
    for (const auto& tradeRecord : result.tradeRecords.values()) {
        tradeRecords.append(toVariantMap(tradeRecord));
    }
    map.insert(rawKeyText(rawkeys::kTradeRecords), tradeRecords);
    map.insert(rawKeyText(rawkeys::kUniverseResolution), toVariantMap(result.universeResolution));
    map.insert(rawKeyText(rawkeys::kRuleSummary), toVariantMap(result.ruleSummary));

    QVariantList contributions;
    for (const auto& contribution : result.layerAttribution.contributions.values()) {
        contributions.append(toVariantMap(contribution));
    }
    map.insert(rawKeyText(rawkeys::kLayerAttribution), QVariantMap{{rawKeyText(rawkeys::kContributions), contributions}});
    map.insert(rawKeyText(rawkeys::kBenchmark), toVariantMap(result.benchmark));
    map.insert(rawKeyText(rawkeys::kDiagnostics), toVariantMap(result.diagnostics));
    return map;
}

QVariantMap buildStrategyBacktestProgressMap(
    const domain::backtest::strategy_engine::BacktestProgressSnapshot& snapshot)
{
    if (!snapshot.isValid()) {
        fail(StrategyBacktestRequestAdapterErrorCode::InvalidProgressSnapshot);
    }

    QVariantMap map;
    map.insert(rawKeyText(rawkeys::kHandleRunId), QVariant::fromValue<qulonglong>(snapshot.handle.runId.value()));
    map.insert(rawKeyText(rawkeys::kState), static_cast<int>(snapshot.state));
    map.insert(rawKeyText(rawkeys::kCurrentTradingDay), snapshot.currentTradingDay.value());
    map.insert(rawKeyText(rawkeys::kCompletedTradingDays),
               QVariant::fromValue<qulonglong>(snapshot.completedTradingDays.value()));
    map.insert(rawKeyText(rawkeys::kTotalTradingDays),
               QVariant::fromValue<qulonglong>(snapshot.totalTradingDays.value()));
    map.insert(rawKeyText(rawkeys::kCompletionRatio), snapshot.completionRatio.value());

    if (snapshot.failureCode.has_value()) {
        map.insert(rawKeyText(rawkeys::kFailureCode), static_cast<int>(snapshot.failureCode.value()));
    }
    if (snapshot.failureDiagnostics.has_value()) {
        map.insert(rawKeyText(rawkeys::kFailureDiagnostics), toVariantMap(snapshot.failureDiagnostics.value()));
    }

    return map;
}

QVariantMap tryCollectStrategyBacktestResult(
    const qulonglong handleRunId,
    StrategyBacktestEntryService* entryService)
{
    const AsyncBacktestHandle handle = buildHandle(handleRunId);
    const std::optional<domain::backtest::strategy_engine::BacktestResultDto> result =
        requireRuntime(entryService).tryCollect(handle);

    QVariantMap map;
    map.insert(rawKeyText(rawkeys::kHasResult), result.has_value());
    if (result.has_value()) {
        map.insert(rawKeyText(rawkeys::kResult), buildStrategyBacktestResultMap(*result));
    }
    return map;
}

QVariantMap pollStrategyBacktestProgress(
    const qulonglong handleRunId,
    StrategyBacktestEntryService* entryService)
{
    const AsyncBacktestHandle handle = buildHandle(handleRunId);
    return buildStrategyBacktestProgressMap(requireRuntime(entryService).progress(handle));
}

void cancelStrategyBacktest(
    const qulonglong handleRunId,
    StrategyBacktestEntryService* entryService)
{
    requireRuntime(entryService).cancel(CancellationRequest{buildHandle(handleRunId)});
}

} // namespace bridge::config