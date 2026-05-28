#include "StrategyService.h"
#include "RuleTemplateRuntimeEvaluator.h"
#include "StrategyRuntimeRuleEvaluator.h"
#include "StrategyViewModel.h"
#include "StrategyBacktestRequestAdapter.h"
#include "StrategyBacktestRuntimeAccess.h"
#include "StrategyStructureResolvers.h"
#include "RiskConfigService.h"
#include "TradingConnectionConfigService.h"
#include "TradingMarketCalendarService.h"
#include "TradingRuntimeStatusService.h"
#include "PortfolioExecutionPlanUtils.h"
#include "RiskMonitorService.h"
#include "PositionAccountService.h"
#include "TradeExecutionService.h"
#include "StrategyLifecycleStatus.h"
#include "../../domain/strategy/include/RuleTemplateStringConstants.h"
#include "../../domain/strategy/include/StrategySnapshotTypes.h"
#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "GlobalEventBusRegistry.h"
#include "../../domain/backtest/include/ResolvedStrategyBehaviorVariant.h"
#include "../../ui/bridge/include/DatabaseConnectionManager.h"
#include "database/StrategyRepository.h"
#include <QDebug>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutex>
#include <QReadWriteLock>
#include <QCoreApplication>
#include <QRandomGenerator>
#include <QSet>
#include <QDir>
#include <QFileInfo>
#include <QMetaType>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <thread>

using namespace astock::database;

namespace {

namespace rule_template_strings = domain::strategy::rule_template_strings;

bool matchesRuleTemplateString(const QString& value, const char* literal)
{
    return value == QString::fromLatin1(literal);
}

domain::strategy::CandidateAction parseRuntimeRuleCandidateAction(const QString& candidateAction)
{
    const QString normalized = candidateAction.trimmed().toLower();
    if (normalized.isEmpty()) {
        return domain::strategy::CandidateAction::None;
    }

    if (matchesRuleTemplateString(normalized, rule_template_strings::kActionBuy)
            || matchesRuleTemplateString(normalized, rule_template_strings::kActionEntry)
            || matchesRuleTemplateString(normalized, rule_template_strings::kActionCandidateEntry)
            || matchesRuleTemplateString(normalized, rule_template_strings::kActionOpen)) {
        return domain::strategy::CandidateAction::Buy;
    }

    if (matchesRuleTemplateString(normalized, rule_template_strings::kActionSell)
            || matchesRuleTemplateString(normalized, rule_template_strings::kActionReduce)
            || matchesRuleTemplateString(normalized, rule_template_strings::kActionExit)
            || matchesRuleTemplateString(normalized, rule_template_strings::kActionClose)) {
        return domain::strategy::CandidateAction::Sell;
    }

    return domain::strategy::CandidateAction::None;
}

struct StrategyEditorDisplayState final {
    domain::strategy::StrategyId strategyId;
    domain::strategy::StrategyCode strategyCode;
    domain::strategy::StrategyName strategyName;
    domain::strategy::DescriptionText description;
    int strategyTypeIndex{-1};
    int strategyBehaviorKind{-1};
    int executionKind{-1};
    int statusIndex{-1};
    int assetTypeIndex{0};
    int timeFrameIndex{0};
    int riskLevelIndex{0};
    QVector<domain::strategy::StrategyTag> tags;
};

QVariantMap strategyMapFromRepositoryResult(const std::optional<StrategyData>& strategyData)
{
    return strategyData.has_value() ? strategyData->toVariantMap() : QVariantMap();
}

StrategyData strategyDataFromMap(const QVariantMap& strategyMap)
{
    QVariantMap normalizedStrategy = strategyMap;
    QVariantMap parameters = normalizedStrategy.value(QStringLiteral("parameters")).toMap();

    auto syncRuntimeIndex = [&](const QString& key) {
        const int index = normalizedStrategy.value(key).toInt();
        if (index > 0) {
            parameters.insert(key, index);
        }
    };

    syncRuntimeIndex(QStringLiteral("assetTypeIndex"));
    syncRuntimeIndex(QStringLiteral("timeFrameIndex"));
    syncRuntimeIndex(QStringLiteral("riskLevelIndex"));
    normalizedStrategy.insert(QStringLiteral("parameters"), parameters);
    return StrategyData::fromVariantMap(normalizedStrategy);
}

namespace strategy_backtest_preview_keys {

constexpr const char* kOk = "ok";
constexpr const char* kErrorCode = "errorCode";
constexpr const char* kRequest = "request";
constexpr const char* kStrategyId = "strategyId";
constexpr const char* kBehaviorKind = "behaviorKind";
constexpr const char* kExecutionMode = "executionMode";
constexpr const char* kLayers = "layers";
constexpr const char* kLayerId = "layerId";
constexpr const char* kLayerType = "layerType";
constexpr const char* kInputUniverseId = "inputUniverseId";
constexpr const char* kRuleTemplateId = "ruleTemplateId";
constexpr const char* kTargetPositionCount = "targetPositionCount";
constexpr const char* kEvaluationIntervalDays = "evaluationIntervalDays";
constexpr const char* kOverlayEnabled = "overlayEnabled";
constexpr const char* kOverlayFactorIds = "overlayFactorIds";
constexpr const char* kOverlayMinimumCompositeScore = "overlayMinimumCompositeScore";
constexpr const char* kOverlayTargetPositionCount = "overlayTargetPositionCount";
constexpr const char* kUniverseSelectionMode = "universeSelectionMode";
constexpr const char* kUniverseId = "universeId";
constexpr const char* kExplicitSymbolIds = "explicitSymbolIds";
constexpr const char* kUniverseDatasetId = "universeDatasetId";
constexpr const char* kMarketProfile = "marketProfile";
constexpr const char* kInitialCapital = "initialCapital";
constexpr const char* kCommissionRate = "commissionRate";
constexpr const char* kSlippageRate = "slippageRate";
constexpr const char* kTaxRate = "taxRate";
constexpr const char* kMaxPositionRatio = "maxPositionRatio";
constexpr const char* kMaxSinglePositionRatio = "maxSinglePositionRatio";
constexpr const char* kMaxDrawdownLimit = "maxDrawdownLimit";
constexpr const char* kStopLossRate = "stopLossRate";
constexpr const char* kPositionSizingMethod = "positionSizingMethod";
constexpr const char* kEnableShortSelling = "enableShortSelling";
constexpr const char* kRebalanceFrequencyDays = "rebalanceFrequencyDays";
constexpr const char* kDefaultOrderType = "defaultOrderType";
constexpr const char* kDataSourceMode = "dataSourceMode";
constexpr const char* kDataSourceDatasetId = "dataSourceDatasetId";
constexpr const char* kMaxThreads = "maxThreads";
constexpr const char* kEnableCache = "enableCache";
constexpr const char* kCacheTtlNs = "cacheTtlNs";
constexpr const char* kWindowStartDay = "windowStartDay";
constexpr const char* kWindowEndDay = "windowEndDay";
constexpr const char* kBenchmarkSymbolId = "benchmarkSymbolId";

} // namespace strategy_backtest_preview_keys

namespace strategy_backtest_runtime_keys {

constexpr const char* kOk = "ok";
constexpr const char* kErrorCode = "errorCode";
constexpr const char* kHandle = "handle";
constexpr const char* kProgress = "progress";
constexpr const char* kCollection = "collection";

} // namespace strategy_backtest_runtime_keys

namespace strategy_backtest_performance_keys {

constexpr const char* kPerformance = "performance";
constexpr const char* kTrades = "trades";
constexpr const char* kTimeSeries = "timeSeries";
constexpr const char* kTradeRecords = "tradeRecords";
constexpr const char* kRuleTemplateSummary = "ruleTemplateSummary";
constexpr const char* kDates = "dates";
constexpr const char* kPortfolioValues = "portfolioValues";
constexpr const char* kTotalReturn = "totalReturn";
constexpr const char* kMaxDrawdown = "maxDrawdown";
constexpr const char* kWinRate = "winRate";
constexpr const char* kSharpeRatio = "sharpeRatio";
constexpr const char* kAnnualizedReturn = "annualizedReturn";
constexpr const char* kVolatility = "volatility";
constexpr const char* kSortinoRatio = "sortinoRatio";
constexpr const char* kCalmarRatio = "calmarRatio";
constexpr const char* kProfitFactor = "profitFactor";
constexpr const char* kAverageWin = "averageWin";
constexpr const char* kAverageLoss = "averageLoss";
constexpr const char* kAlpha = "alpha";
constexpr const char* kBeta = "beta";
constexpr const char* kInformationRatio = "informationRatio";
constexpr const char* kTrackingError = "trackingError";
constexpr const char* kTotalTrades = "totalTrades";
constexpr const char* kWinningTrades = "winningTrades";
constexpr const char* kLosingTrades = "losingTrades";
constexpr const char* kTotalProfit = "totalProfit";
constexpr const char* kTotalLoss = "totalLoss";
constexpr const char* kLargestWin = "largestWin";
constexpr const char* kLargestLoss = "largestLoss";
constexpr const char* kAverageHoldingPeriod = "averageHoldingPeriod";
constexpr const char* kSelectedStrategyId = "selectedStrategyId";
constexpr const char* kSelectedStrategyName = "selectedStrategyName";
constexpr const char* kStrategyExecutionKind = "strategyExecutionKind";
constexpr const char* kRuntimeParameters = "runtimeParameters";
constexpr const char* kSelectedUniverseType = "selectedUniverseType";
constexpr const char* kUniverseLabel = "universeLabel";
constexpr const char* kSelectedIndexSymbol = "selectedIndexSymbol";
constexpr const char* kIndexLabel = "indexLabel";
constexpr const char* kDataSourceMode = "dataSourceMode";
constexpr const char* kStartDate = "startDate";
constexpr const char* kEndDate = "endDate";
constexpr const char* kFactorImportContext = "factorImportContext";
constexpr const char* kUniverseSourceKey = "universeSourceKey";
constexpr const char* kUniverseSourceLabel = "universeSourceLabel";
constexpr const char* kReplaceLatestBacktest = "replaceLatestBacktest";
constexpr const char* kRecordedAt = "recordedAt";
constexpr const char* kStrategyId = "strategyId";
constexpr const char* kStrategyName = "strategyName";
constexpr const char* kUniverseType = "universeType";
constexpr const char* kIndexSymbol = "indexSymbol";
constexpr const char* kTradingDays = "tradingDays";
constexpr const char* kEquityPointCount = "equityPointCount";
constexpr const char* kSummary = "summary";
constexpr const char* kReturns = "returns";
constexpr const char* kRunningDays = "runningDays";
constexpr const char* kTradesCount = "tradesCount";
constexpr const char* kAnnualReturn = "annualReturn";
constexpr const char* kPosition = "position";
constexpr const char* kDailyPnL = "dailyPnL";
constexpr const char* kLastBacktestAt = "lastBacktestAt";
constexpr const char* kBacktestHistoryEntry = "backtestHistoryEntry";
constexpr const char* kLatestBacktest = "latestBacktest";

} // namespace strategy_backtest_performance_keys

QString previewKeyText(const char* key)
{
    return QString::fromLatin1(key);
}

QVariantMap buildStrategyBacktestRuntimeResult(const bool ok,
                                              const int errorCode,
                                              const char* payloadKey = nullptr,
                                              const QVariant& payload = QVariant())
{
    QVariantMap result;
    result.insert(previewKeyText(strategy_backtest_runtime_keys::kOk), ok);
    result.insert(previewKeyText(strategy_backtest_runtime_keys::kErrorCode), errorCode);
    if (payloadKey) {
        result.insert(previewKeyText(payloadKey), payload);
    }
    return result;
}

template <typename Operation>
QVariantMap executeStrategyBacktestRuntimeAction(const Operation& operation)
{
    try {
        operation();
        return buildStrategyBacktestRuntimeResult(
            true,
            static_cast<int>(bridge::config::StrategyBacktestRequestAdapterErrorCode::None));
    } catch (const bridge::config::StrategyBacktestRequestAdapterError& error) {
        return buildStrategyBacktestRuntimeResult(false, static_cast<int>(error.code));
    }
}

template <typename Operation>
QVariantMap executeStrategyBacktestRuntimePayloadAction(const char* payloadKey,
                                                       const Operation& operation)
{
    try {
        return buildStrategyBacktestRuntimeResult(
            true,
            static_cast<int>(bridge::config::StrategyBacktestRequestAdapterErrorCode::None),
            payloadKey,
            operation());
    } catch (const bridge::config::StrategyBacktestRequestAdapterError& error) {
        return buildStrategyBacktestRuntimeResult(false, static_cast<int>(error.code));
    }
}

bool hasBacktestDisplayValue(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return false;
    }

    if (value.typeId() == QMetaType::QString) {
        return !value.toString().trimmed().isEmpty();
    }

    return true;
}

double backtestNumericValue(const QVariant& value, double fallback = 0.0)
{
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    return ok ? parsed : fallback;
}

QString backtestTextValue(const QVariantMap& map, const char* key, const QString& fallback = QString())
{
    const QString text = map.value(previewKeyText(key)).toString().trimmed();
    return text.isEmpty() ? fallback : text;
}

QVariantList backtestVariantList(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return {};
    }

    if (value.canConvert<QVariantList>()) {
        return value.toList();
    }

    if (value.canConvert<QStringList>()) {
        QVariantList result;
        const QStringList texts = value.toStringList();
        for (const QString& text : texts) {
            result.append(text);
        }
        return result;
    }

    return {};
}

QString formatBacktestRecordedAt(const QDateTime& dateTime = QDateTime::currentDateTime())
{
    return dateTime.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
}

int normalizedBacktestStrategyExecutionKind(const QVariantMap& context)
{
    const QVariantMap runtimeParameters = context.value(previewKeyText(strategy_backtest_performance_keys::kRuntimeParameters)).toMap();
    const QVariant rawValue = hasBacktestDisplayValue(context.value(previewKeyText(strategy_backtest_performance_keys::kStrategyExecutionKind)))
        ? context.value(previewKeyText(strategy_backtest_performance_keys::kStrategyExecutionKind))
        : runtimeParameters.value(previewKeyText(strategy_backtest_performance_keys::kStrategyExecutionKind));
    bool ok = false;
    const int parsed = rawValue.toInt(&ok);
    if (!ok || parsed < 0 || parsed > 1) {
        return 0;
    }
    return parsed;
}

QVariantMap normalizeBacktestTradeRecord(const QVariantMap& record)
{
    if (record.isEmpty()) {
        return {};
    }

    const QString symbol = backtestTextValue(record, "symbol");
    if (symbol.isEmpty()) {
        return {};
    }

    QVariantMap normalized;
    normalized.insert(QStringLiteral("tradeId"), backtestTextValue(record, "tradeId"));
    normalized.insert(QStringLiteral("entryTime"), backtestTextValue(record, "entryTime"));
    normalized.insert(QStringLiteral("exitTime"), backtestTextValue(record, "exitTime"));
    normalized.insert(QStringLiteral("symbol"), symbol);
    normalized.insert(QStringLiteral("direction"), backtestTextValue(record, "direction"));
    normalized.insert(QStringLiteral("entryPrice"), backtestNumericValue(record.value(QStringLiteral("entryPrice"))));
    normalized.insert(QStringLiteral("exitPrice"), backtestNumericValue(record.value(QStringLiteral("exitPrice"))));
    normalized.insert(QStringLiteral("quantity"), backtestNumericValue(record.value(QStringLiteral("quantity"))));
    normalized.insert(QStringLiteral("commission"), backtestNumericValue(record.value(QStringLiteral("commission"))));
    normalized.insert(QStringLiteral("profit"), backtestNumericValue(record.value(QStringLiteral("profit"))));
    normalized.insert(QStringLiteral("profitPct"), backtestNumericValue(record.value(QStringLiteral("profitPct"))));
    normalized.insert(QStringLiteral("notes"), backtestTextValue(record, "notes"));
    return normalized;
}

QVariantMap normalizeBacktestRuleTemplateSummary(const QVariantMap& backtestResult)
{
    const QVariantMap summary = backtestResult.value(
        previewKeyText(strategy_backtest_performance_keys::kRuleTemplateSummary)).toMap();
    if (summary.isEmpty()) {
        return {};
    }

    QVariantMap normalized;
    normalized.insert(QStringLiteral("hasTemplate"), summary.value(QStringLiteral("hasTemplate")).toBool());
    normalized.insert(QStringLiteral("templateFileName"), backtestTextValue(summary, "templateFileName"));
    normalized.insert(QStringLiteral("templateNamespace"), backtestTextValue(summary, "templateNamespace"));
    normalized.insert(QStringLiteral("groupId"), backtestTextValue(summary, "groupId"));
    normalized.insert(QStringLiteral("groupTitle"), backtestTextValue(summary, "groupTitle"));
    normalized.insert(QStringLiteral("groupOperatorLabel"), backtestTextValue(summary, "groupOperatorLabel"));
    normalized.insert(QStringLiteral("statusText"), backtestTextValue(summary, "statusText"));
    normalized.insert(QStringLiteral("triggeredCount"),
                      static_cast<int>(backtestNumericValue(summary.value(QStringLiteral("triggeredCount")))));
    normalized.insert(QStringLiteral("entryBlockCount"),
                      static_cast<int>(backtestNumericValue(summary.value(QStringLiteral("entryBlockCount")))));
    normalized.insert(QStringLiteral("forcedExitCount"),
                      static_cast<int>(backtestNumericValue(summary.value(QStringLiteral("forcedExitCount")))));

    if (!normalized.value(QStringLiteral("hasTemplate")).toBool()
            && normalized.value(QStringLiteral("triggeredCount")).toInt() <= 0
            && normalized.value(QStringLiteral("entryBlockCount")).toInt() <= 0
            && normalized.value(QStringLiteral("forcedExitCount")).toInt() <= 0) {
        return {};
    }

    return normalized;
}

QVariantMap buildStrategyBacktestHistoryEntryPayload(const QVariantMap& backtestResult,
                                                    const QVariantMap& performancePayload,
                                                    const QVariantMap& backtestContext,
                                                    const QString& recordedAt)
{
    const QVariantMap timeSeries = backtestResult.value(
        previewKeyText(strategy_backtest_performance_keys::kTimeSeries)).toMap();
    const QVariantList dates = backtestVariantList(timeSeries.value(
        previewKeyText(strategy_backtest_performance_keys::kDates)));
    const QVariantList portfolioValues = backtestVariantList(timeSeries.value(
        previewKeyText(strategy_backtest_performance_keys::kPortfolioValues)));
    const QString universeType = backtestTextValue(backtestContext,
                                                   strategy_backtest_performance_keys::kSelectedUniverseType,
                                                   QStringLiteral("market"));

    QVariantMap historyEntry;
    historyEntry.insert(previewKeyText(strategy_backtest_performance_keys::kRecordedAt), recordedAt);
    historyEntry.insert(previewKeyText(strategy_backtest_performance_keys::kStrategyId),
                        backtestTextValue(backtestContext, strategy_backtest_performance_keys::kSelectedStrategyId));
    historyEntry.insert(previewKeyText(strategy_backtest_performance_keys::kStrategyName),
                        backtestTextValue(backtestContext, strategy_backtest_performance_keys::kSelectedStrategyName));
    historyEntry.insert(previewKeyText(strategy_backtest_performance_keys::kStrategyExecutionKind),
                        normalizedBacktestStrategyExecutionKind(backtestContext));
    historyEntry.insert(previewKeyText(strategy_backtest_performance_keys::kUniverseType), universeType);
    historyEntry.insert(previewKeyText(strategy_backtest_performance_keys::kUniverseLabel),
                        backtestTextValue(backtestContext,
                                          strategy_backtest_performance_keys::kUniverseLabel,
                                          universeType == QStringLiteral("market")
                                              ? QStringLiteral("全市场")
                                              : universeType));
    historyEntry.insert(previewKeyText(strategy_backtest_performance_keys::kIndexSymbol),
                        universeType == QStringLiteral("index")
                            ? backtestTextValue(backtestContext,
                                                strategy_backtest_performance_keys::kSelectedIndexSymbol)
                            : QString());
    historyEntry.insert(previewKeyText(strategy_backtest_performance_keys::kIndexLabel),
                        universeType == QStringLiteral("index")
                            ? backtestTextValue(backtestContext,
                                                strategy_backtest_performance_keys::kIndexLabel)
                            : QString());
    historyEntry.insert(previewKeyText(strategy_backtest_performance_keys::kDataSourceMode),
                        backtestTextValue(backtestContext,
                                          strategy_backtest_performance_keys::kDataSourceMode,
                                          QStringLiteral("raw")));
    historyEntry.insert(previewKeyText(strategy_backtest_performance_keys::kStartDate),
                        backtestTextValue(backtestContext, strategy_backtest_performance_keys::kStartDate));
    historyEntry.insert(previewKeyText(strategy_backtest_performance_keys::kEndDate),
                        backtestTextValue(backtestContext, strategy_backtest_performance_keys::kEndDate));
    historyEntry.insert(previewKeyText(strategy_backtest_performance_keys::kFactorImportContext),
                        backtestContext.value(previewKeyText(strategy_backtest_performance_keys::kFactorImportContext)).toMap());
    historyEntry.insert(previewKeyText(strategy_backtest_performance_keys::kUniverseSourceKey),
                        backtestTextValue(backtestContext, strategy_backtest_performance_keys::kUniverseSourceKey));
    historyEntry.insert(previewKeyText(strategy_backtest_performance_keys::kUniverseSourceLabel),
                        backtestTextValue(backtestContext, strategy_backtest_performance_keys::kUniverseSourceLabel));
    historyEntry.insert(previewKeyText(strategy_backtest_performance_keys::kTradingDays), dates.size());
    historyEntry.insert(previewKeyText(strategy_backtest_performance_keys::kEquityPointCount), portfolioValues.size());
    historyEntry.insert(previewKeyText(strategy_backtest_performance_keys::kRuntimeParameters),
                        backtestContext.value(previewKeyText(strategy_backtest_performance_keys::kRuntimeParameters)).toMap());
    historyEntry.insert(previewKeyText(strategy_backtest_performance_keys::kSummary), QVariantMap{
        {previewKeyText(strategy_backtest_performance_keys::kReturns), performancePayload.value(previewKeyText(strategy_backtest_performance_keys::kReturns))},
        {previewKeyText(strategy_backtest_performance_keys::kMaxDrawdown), performancePayload.value(previewKeyText(strategy_backtest_performance_keys::kMaxDrawdown))},
        {previewKeyText(strategy_backtest_performance_keys::kSharpeRatio), performancePayload.value(previewKeyText(strategy_backtest_performance_keys::kSharpeRatio))},
        {previewKeyText(strategy_backtest_performance_keys::kWinRate), performancePayload.value(previewKeyText(strategy_backtest_performance_keys::kWinRate))},
        {previewKeyText(strategy_backtest_performance_keys::kRunningDays), performancePayload.value(previewKeyText(strategy_backtest_performance_keys::kRunningDays))},
        {previewKeyText(strategy_backtest_performance_keys::kTradesCount), performancePayload.value(previewKeyText(strategy_backtest_performance_keys::kTradesCount))},
        {previewKeyText(strategy_backtest_performance_keys::kTotalReturn), performancePayload.value(previewKeyText(strategy_backtest_performance_keys::kTotalReturn))},
        {previewKeyText(strategy_backtest_performance_keys::kAnnualReturn), performancePayload.value(previewKeyText(strategy_backtest_performance_keys::kAnnualReturn))},
        {previewKeyText(strategy_backtest_performance_keys::kAnnualizedReturn), performancePayload.value(previewKeyText(strategy_backtest_performance_keys::kAnnualizedReturn))},
        {previewKeyText(strategy_backtest_performance_keys::kVolatility), performancePayload.value(previewKeyText(strategy_backtest_performance_keys::kVolatility))},
        {previewKeyText(strategy_backtest_performance_keys::kSortinoRatio), performancePayload.value(previewKeyText(strategy_backtest_performance_keys::kSortinoRatio))},
        {previewKeyText(strategy_backtest_performance_keys::kCalmarRatio), performancePayload.value(previewKeyText(strategy_backtest_performance_keys::kCalmarRatio))},
        {previewKeyText(strategy_backtest_performance_keys::kProfitFactor), performancePayload.value(previewKeyText(strategy_backtest_performance_keys::kProfitFactor))}
    });
    const QVariantMap ruleTemplateSummary = normalizeBacktestRuleTemplateSummary(backtestResult);
    if (!ruleTemplateSummary.isEmpty()) {
        historyEntry.insert(previewKeyText(strategy_backtest_performance_keys::kRuleTemplateSummary),
                            ruleTemplateSummary);
    }
    return historyEntry;
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

QVariantMap serializeDecisionLayerPreview(const domain::backtest::strategy_engine::DecisionLayer& layer)
{
    QVariantMap result;
    result.insert(previewKeyText(strategy_backtest_preview_keys::kLayerId), unsignedIdVariant(layer.id));
    result.insert(previewKeyText(strategy_backtest_preview_keys::kLayerType), static_cast<int>(layer.type));
    result.insert(previewKeyText(strategy_backtest_preview_keys::kInputUniverseId), unsignedIdVariant(layer.inputUniverseId));
    result.insert(previewKeyText(strategy_backtest_preview_keys::kTargetPositionCount),
                  QVariant::fromValue<uint>(layer.targetPositionCount.value()));
    result.insert(previewKeyText(strategy_backtest_preview_keys::kEvaluationIntervalDays),
                  QVariant::fromValue<uint>(layer.evaluationIntervalDays.value()));
    result.insert(previewKeyText(strategy_backtest_preview_keys::kOverlayEnabled), layer.overlay.enabled);
    result.insert(previewKeyText(strategy_backtest_preview_keys::kOverlayFactorIds),
                  unsignedIdList(layer.overlay.factorIds));
    result.insert(previewKeyText(strategy_backtest_preview_keys::kOverlayMinimumCompositeScore),
                  layer.overlay.minimumCompositeScore.value());
    result.insert(previewKeyText(strategy_backtest_preview_keys::kOverlayTargetPositionCount),
                  QVariant::fromValue<uint>(layer.overlay.targetPositionCount.value()));
    if (layer.ruleTemplateId.isValid()) {
        result.insert(previewKeyText(strategy_backtest_preview_keys::kRuleTemplateId),
                      unsignedIdVariant(layer.ruleTemplateId));
    }
    return result;
}

QVariantMap serializeStrategyBacktestRequestPreview(
    const domain::backtest::strategy_engine::BacktestRequest& request)
{
    QVariantMap result;
    result.insert(previewKeyText(strategy_backtest_preview_keys::kStrategyId), unsignedIdVariant(request.identity.strategyId));
    result.insert(previewKeyText(strategy_backtest_preview_keys::kBehaviorKind),
                  static_cast<int>(request.identity.behaviorKind));
    result.insert(previewKeyText(strategy_backtest_preview_keys::kExecutionMode),
                  static_cast<int>(request.identity.executionMode));

    QVariantList layers;
    for (const auto& layer : request.spec.layers.values()) {
        layers.append(serializeDecisionLayerPreview(layer));
    }
    result.insert(previewKeyText(strategy_backtest_preview_keys::kLayers), layers);

    result.insert(previewKeyText(strategy_backtest_preview_keys::kUniverseSelectionMode),
                  static_cast<int>(request.universeSpec.mode));
    result.insert(previewKeyText(strategy_backtest_preview_keys::kUniverseId), unsignedIdVariant(request.universeSpec.universeId));
    result.insert(previewKeyText(strategy_backtest_preview_keys::kExplicitSymbolIds),
                  unsignedIdList(request.universeSpec.explicitSymbols));
    if (request.universeSpec.datasetId.isValid()) {
        result.insert(previewKeyText(strategy_backtest_preview_keys::kUniverseDatasetId),
                      unsignedIdVariant(request.universeSpec.datasetId));
    }

    result.insert(previewKeyText(strategy_backtest_preview_keys::kMarketProfile),
                  static_cast<int>(request.marketEnvironmentSpec.profile));
    result.insert(previewKeyText(strategy_backtest_preview_keys::kInitialCapital), request.costSpec.initialCapital.value());
    result.insert(previewKeyText(strategy_backtest_preview_keys::kCommissionRate), request.costSpec.commissionRate.value());
    result.insert(previewKeyText(strategy_backtest_preview_keys::kSlippageRate), request.costSpec.slippageRate.value());
    result.insert(previewKeyText(strategy_backtest_preview_keys::kTaxRate), request.costSpec.taxRate.value());
    result.insert(previewKeyText(strategy_backtest_preview_keys::kMaxPositionRatio), request.riskSpec.maxPositionRatio.value());
    result.insert(previewKeyText(strategy_backtest_preview_keys::kMaxSinglePositionRatio),
                  request.riskSpec.maxSinglePositionRatio.value());
    result.insert(previewKeyText(strategy_backtest_preview_keys::kMaxDrawdownLimit),
                  request.riskSpec.maxDrawdownLimit.value());
    result.insert(previewKeyText(strategy_backtest_preview_keys::kStopLossRate), request.riskSpec.stopLossRate.value());
    result.insert(previewKeyText(strategy_backtest_preview_keys::kPositionSizingMethod),
                  static_cast<int>(request.executionSpec.positionSizingMethod));
    result.insert(previewKeyText(strategy_backtest_preview_keys::kEnableShortSelling),
                  request.executionSpec.enableShortSelling);
    result.insert(previewKeyText(strategy_backtest_preview_keys::kRebalanceFrequencyDays),
                  QVariant::fromValue<uint>(request.executionSpec.rebalanceFrequencyDays.value()));
    result.insert(previewKeyText(strategy_backtest_preview_keys::kDefaultOrderType),
                  static_cast<int>(request.executionSpec.defaultOrderType));
    result.insert(previewKeyText(strategy_backtest_preview_keys::kDataSourceMode),
                  static_cast<int>(request.dataSourceSpec.mode));
    if (request.dataSourceSpec.datasetId.isValid()) {
        result.insert(previewKeyText(strategy_backtest_preview_keys::kDataSourceDatasetId),
                      unsignedIdVariant(request.dataSourceSpec.datasetId));
    }

    result.insert(previewKeyText(strategy_backtest_preview_keys::kMaxThreads),
                  QVariant::fromValue<uint>(request.runtimeOptions.maxThreads.value()));
    result.insert(previewKeyText(strategy_backtest_preview_keys::kEnableCache), request.runtimeOptions.enableCache);
    result.insert(previewKeyText(strategy_backtest_preview_keys::kCacheTtlNs), request.runtimeOptions.cacheTtl.value());
    result.insert(previewKeyText(strategy_backtest_preview_keys::kWindowStartDay), request.window.startDay.value());
    result.insert(previewKeyText(strategy_backtest_preview_keys::kWindowEndDay), request.window.endDay.value());
    if (request.benchmarkSymbol.has_value()) {
        result.insert(previewKeyText(strategy_backtest_preview_keys::kBenchmarkSymbolId),
                      unsignedIdVariant(*request.benchmarkSymbol));
    }
    return result;
}

constexpr int MAX_BACKTEST_HISTORY_ITEMS = 20;
constexpr qint64 kStrategySignalCooldownMs = 1000;
constexpr qint64 kTradingConfigurationCacheTtlMs = 250;
constexpr qint64 kRiskConfigurationCacheTtlMs = 500;
constexpr qint64 kMarketSessionCacheTtlMs = 250;
constexpr double kDefaultTemplateInitialCapital = 1000000.0;
constexpr double kDefaultTemplateCommissionRate = 0.0015;
constexpr double kDefaultTemplateSlippageRate = 0.001;

factor::MarketEnvironmentProfile resolveStrategyMarketEnvironmentProfile(const QVariantMap& strategy)
{
    const QVariantMap strategyScopeContext = strategy.value(QStringLiteral("strategyScopeContextSnapshot")).toMap();
    bool ok = false;
    const int profileIndex = strategyScopeContext.value(
        QStringLiteral("marketEnvironmentProfile"),
        strategy.value(
            QStringLiteral("marketEnvironmentProfile"),
            factor::marketEnvironmentProfileIndex(factor::MarketEnvironmentProfile::GENERIC_EQUITY))).toInt(&ok);
    return ok
        ? factor::marketEnvironmentProfileFromIndex(profileIndex)
        : factor::MarketEnvironmentProfile::GENERIC_EQUITY;
}

QString normalizePersistedStatus(const QString& rawStatus)
{
    const strategy_view::StrategyLifecycleStatus status =
        strategy_view::resolveStrategyLifecycleStatus(rawStatus);
    switch (status) {
    case strategy_view::StrategyLifecycleStatus::Active:
        return QStringLiteral("ACTIVE");
    case strategy_view::StrategyLifecycleStatus::Inactive:
        return QStringLiteral("INACTIVE");
    case strategy_view::StrategyLifecycleStatus::Testing:
        return QStringLiteral("TESTING");
    case strategy_view::StrategyLifecycleStatus::Archived:
        return QStringLiteral("ARCHIVED");
    case strategy_view::StrategyLifecycleStatus::Draft:
    case strategy_view::StrategyLifecycleStatus::Running:
    case strategy_view::StrategyLifecycleStatus::Paused:
    case strategy_view::StrategyLifecycleStatus::Stopped:
    case strategy_view::StrategyLifecycleStatus::Unknown:
    default:
        return {};
    }
}

strategy_view::StrategyLifecycleStatus strategyStatusFromMap(const QVariantMap& strategy)
{
    return strategy_view::resolveStrategyLifecycleStatus(strategy.value(QStringLiteral("statusIndex")));
}

void assignStrategyStatus(QVariantMap& strategy, strategy_view::StrategyLifecycleStatus status)
{
    strategy.remove(QStringLiteral("status"));
    if (strategy_view::isKnownStrategyLifecycleStatus(status)) {
        strategy.insert(QStringLiteral("statusIndex"), strategy_view::strategyLifecycleStatusIndex(status));
        return;
    }
    strategy.remove(QStringLiteral("statusIndex"));
}

bool isPersistableStrategyStatus(strategy_view::StrategyLifecycleStatus status)
{
    switch (status) {
    case strategy_view::StrategyLifecycleStatus::Active:
    case strategy_view::StrategyLifecycleStatus::Inactive:
    case strategy_view::StrategyLifecycleStatus::Testing:
    case strategy_view::StrategyLifecycleStatus::Archived:
        return true;
    case strategy_view::StrategyLifecycleStatus::Draft:
    case strategy_view::StrategyLifecycleStatus::Running:
    case strategy_view::StrategyLifecycleStatus::Paused:
    case strategy_view::StrategyLifecycleStatus::Stopped:
    case strategy_view::StrategyLifecycleStatus::Unknown:
    default:
        return false;
    }
}

domain::strategy::StrategyAggregate assembleStrategyAggregate(const QVariantMap& strategy)
{
    return bridge::config::buildStrategyAggregate(strategy);
}

QString storedTypeLabel(domain::backtest::StrategyStoredType storedType)
{
    switch (storedType) {
    case domain::backtest::StrategyStoredType::TrendFollowing:
        return QStringLiteral("趋势");
    case domain::backtest::StrategyStoredType::MeanReversion:
        return QStringLiteral("均值回归");
    case domain::backtest::StrategyStoredType::Alpha:
        return QStringLiteral("阿尔法");
    case domain::backtest::StrategyStoredType::Arbitrage:
        return QStringLiteral("套利");
    case domain::backtest::StrategyStoredType::HighFrequency:
        return QStringLiteral("高频");
    case domain::backtest::StrategyStoredType::Portfolio:
        return QStringLiteral("组合");
    case domain::backtest::StrategyStoredType::Custom:
        return QStringLiteral("自定义");
    case domain::backtest::StrategyStoredType::Unknown:
    default:
        return QStringLiteral("未知");
    }
}

QString behaviorLabel(domain::backtest::StrategyBehaviorKind behaviorKind)
{
    switch (behaviorKind) {
    case domain::backtest::StrategyBehaviorKind::TrendFollowing:
        return QStringLiteral("趋势跟踪");
    case domain::backtest::StrategyBehaviorKind::MeanReversion:
        return QStringLiteral("均值回归");
    case domain::backtest::StrategyBehaviorKind::Momentum:
        return QStringLiteral("动量");
    case domain::backtest::StrategyBehaviorKind::Arbitrage:
        return QStringLiteral("套利");
    case domain::backtest::StrategyBehaviorKind::MultiFactor:
        return QStringLiteral("多因子");
    case domain::backtest::StrategyBehaviorKind::MachineLearning:
        return QStringLiteral("机器学习");
    case domain::backtest::StrategyBehaviorKind::EventDriven:
        return QStringLiteral("事件驱动");
    case domain::backtest::StrategyBehaviorKind::HighFrequency:
        return QStringLiteral("高频");
    case domain::backtest::StrategyBehaviorKind::Custom:
    default:
        return QStringLiteral("自定义");
    }
}

QString lifecycleStatusLabel(strategy_view::StrategyLifecycleStatus status)
{
    switch (status) {
    case strategy_view::StrategyLifecycleStatus::Draft:
        return QStringLiteral("草稿");
    case strategy_view::StrategyLifecycleStatus::Active:
        return QStringLiteral("启用");
    case strategy_view::StrategyLifecycleStatus::Inactive:
        return QStringLiteral("停用");
    case strategy_view::StrategyLifecycleStatus::Testing:
        return QStringLiteral("测试中");
    case strategy_view::StrategyLifecycleStatus::Archived:
        return QStringLiteral("已归档");
    case strategy_view::StrategyLifecycleStatus::Running:
        return QStringLiteral("运行中");
    case strategy_view::StrategyLifecycleStatus::Paused:
        return QStringLiteral("已暂停");
    case strategy_view::StrategyLifecycleStatus::Stopped:
        return QStringLiteral("已停止");
    case strategy_view::StrategyLifecycleStatus::Unknown:
    default:
        return QStringLiteral("未知");
    }
}

QStringList symbolCodeListToStrings(const QVector<domain::strategy::SymbolCode>& symbols)
{
    QStringList values;
    values.reserve(symbols.size());
    for (const auto& symbol : symbols) {
        if (!symbol.text().isEmpty()) {
            values.append(symbol.text());
        }
    }
    return values;
}

QStringList strategyTagListToStrings(const QVector<domain::strategy::StrategyTag>& tags)
{
    QStringList values;
    values.reserve(tags.size());
    for (const auto& tag : tags) {
        if (!tag.text().isEmpty()) {
            values.append(tag.text());
        }
    }
    return values;
}

QStringList strategyTags(const QVariantMap& strategy);
QString firstNonEmptyStrategyText(const QVariantMap& strategy, std::initializer_list<const char*> keys);

StrategyEditorDisplayState editorStateFromStrategyMap(const QVariantMap& strategy,
                                                      strategy_view::StrategyLifecycleStatus fallbackStatus = strategy_view::StrategyLifecycleStatus::Inactive)
{
    StrategyEditorDisplayState editorState;
    editorState.strategyId = domain::strategy::StrategyId(
        firstNonEmptyStrategyText(strategy, {"strategy_id", "strategyId"}));
    editorState.strategyCode = domain::strategy::StrategyCode(
        firstNonEmptyStrategyText(strategy, {"strategy_code", "strategyCode"}));
    editorState.strategyName = domain::strategy::StrategyName(
        firstNonEmptyStrategyText(strategy, {"strategy_name", "strategyName"}));
    editorState.description = domain::strategy::DescriptionText(
        firstNonEmptyStrategyText(strategy, {"description"}));

    const domain::backtest::ResolvedStrategyIdentity strategyIdentity =
        domain::backtest::resolveStrategyIdentity(strategy);
    editorState.strategyTypeIndex = strategyIdentity.validStoredType
        ? strategyIdentity.storedTypeIndex()
        : strategy.value(QStringLiteral("strategyTypeIndex"), -1).toInt();
    editorState.strategyBehaviorKind = strategyIdentity.behavior.valid
        ? strategyIdentity.behavior.index()
        : strategy.value(QStringLiteral("strategyBehaviorKind"), -1).toInt();
    editorState.executionKind = strategy.value(QStringLiteral("executionKind"),
        strategy.value(QStringLiteral("strategyExecutionKind"), editorState.strategyTypeIndex == static_cast<int>(domain::backtest::StrategyStoredType::Portfolio) ? 1 : 0)).toInt();

    const strategy_view::StrategyLifecycleStatus status = strategy.contains(QStringLiteral("statusIndex"))
        ? strategy_view::resolveStrategyLifecycleStatus(strategy.value(QStringLiteral("statusIndex")))
        : fallbackStatus;
    editorState.statusIndex = strategy_view::isKnownStrategyLifecycleStatus(status)
        ? strategy_view::strategyLifecycleStatusIndex(status)
        : strategy_view::strategyLifecycleStatusIndex(fallbackStatus);
    editorState.assetTypeIndex = strategy.value(QStringLiteral("assetTypeIndex")).toInt();
    editorState.timeFrameIndex = strategy.value(QStringLiteral("timeFrameIndex")).toInt();
    editorState.riskLevelIndex = strategy.value(QStringLiteral("riskLevelIndex")).toInt();

    const QStringList tags = strategyTags(strategy);
    for (const QString& tag : tags) {
        editorState.tags.push_back(domain::strategy::StrategyTag(tag));
    }
    return editorState;
}

void applyProjectedEditorState(QVariantMap& strategy,
                               const StrategyEditorDisplayState& editorState)
{
    if (editorState.strategyId.isValid()) {
        strategy.insert(QStringLiteral("strategyId"), editorState.strategyId.text());
    }
    if (editorState.strategyCode.isValid()) {
        strategy.insert(QStringLiteral("strategyCode"), editorState.strategyCode.text());
    }
    if (editorState.strategyName.isValid()) {
        strategy.insert(QStringLiteral("strategyName"), editorState.strategyName.text());
    }
    if (editorState.description.isValid()) {
        strategy.insert(QStringLiteral("description"), editorState.description.text());
    }
    strategy.insert(QStringLiteral("strategyTypeIndex"), editorState.strategyTypeIndex);
    strategy.insert(QStringLiteral("strategyBehaviorKind"), editorState.strategyBehaviorKind);
    strategy.insert(QStringLiteral("executionKind"), editorState.executionKind);
    strategy.insert(QStringLiteral("statusIndex"), editorState.statusIndex);
    strategy.insert(QStringLiteral("assetTypeIndex"), editorState.assetTypeIndex);
    strategy.insert(QStringLiteral("timeFrameIndex"), editorState.timeFrameIndex);
    strategy.insert(QStringLiteral("riskLevelIndex"), editorState.riskLevelIndex);
    strategy.insert(QStringLiteral("tags"), strategyTagListToStrings(editorState.tags));
}

QVariantMap applyEditorDisplayProjection(const QVariantMap& strategy)
{
    if (strategy.isEmpty()) {
        return strategy;
    }

    QVariantMap projected = strategy;
    const StrategyEditorDisplayState editorState =
        editorStateFromStrategyMap(projected);
    applyProjectedEditorState(projected, editorState);

    return projected;
}

int positiveIntegerFromVariant(const QVariant& value, int fallback = 0)
{
    if (!value.isValid() || value.isNull()) {
        return fallback;
    }

    bool ok = false;
    int parsedValue = value.toInt(&ok);
    if (!ok) {
        const double parsedDouble = value.toDouble(&ok);
        if (ok) {
            parsedValue = qRound(parsedDouble);
        }
    }
    return ok && parsedValue > 0 ? parsedValue : fallback;
}

bool parseSupportedRuleBindingPhaseIndex(const QVariant& value, int* phaseIndex)
{
    if (!phaseIndex || !value.isValid() || value.isNull() || value.typeId() == QMetaType::QString) {
        return false;
    }

    bool ok = false;
    const int parsedValue = value.toInt(&ok);
    if (!ok) {
        return false;
    }

    switch (static_cast<domain::strategy::RuleBindingPhase>(parsedValue)) {
    case domain::strategy::RuleBindingPhase::Market:
    case domain::strategy::RuleBindingPhase::Signal:
    case domain::strategy::RuleBindingPhase::Entry:
    case domain::strategy::RuleBindingPhase::Rebalance:
    case domain::strategy::RuleBindingPhase::Exit:
    case domain::strategy::RuleBindingPhase::Risk:
    case domain::strategy::RuleBindingPhase::Watch:
        *phaseIndex = parsedValue;
        return true;
    }

    return false;
}

int configuredRuleBindingPhaseIndex(const QVariantMap& map)
{
    int phaseIndex = -1;
    if (parseSupportedRuleBindingPhaseIndex(map.value(QStringLiteral("bindingPhase")), &phaseIndex)) {
        return phaseIndex;
    }
    if (parseSupportedRuleBindingPhaseIndex(map.value(QStringLiteral("phase")), &phaseIndex)) {
        return phaseIndex;
    }
    return -1;
}

bool validateComposerStateBindingPhases(const QVariantMap& payload, QString* errorMessage = nullptr)
{
    QVariantMap ruleProfile = payload.value(QStringLiteral("rule_profile")).toMap();
    if (ruleProfile.isEmpty()) {
        ruleProfile = payload;
    }

    QVariantMap composerState = ruleProfile.value(QStringLiteral("ruleComposerState")).toMap();
    if (composerState.isEmpty()) {
        composerState = ruleProfile.value(QStringLiteral("rule_composer_state")).toMap();
    }
    if (composerState.isEmpty()) {
        if (errorMessage) {
            errorMessage->clear();
        }
        return true;
    }

    const QVariantList stages = composerState.value(QStringLiteral("stages")).toList();
    for (const QVariant& stageValue : stages) {
        const QVariantMap stage = stageValue.toMap();
        const int stagePhaseIndex = configuredRuleBindingPhaseIndex(stage);
        const bool stageHasPhaseField = stage.contains(QStringLiteral("bindingPhase"))
            || stage.contains(QStringLiteral("phase"));
        const QString stageId = stage.value(QStringLiteral("stageId")).toString().trimmed();
        if (stageHasPhaseField && stagePhaseIndex < 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("规则编排阶段必须使用数值 phase/bindingPhase，禁止传入字符串或无效阶段索引");
            }
            return false;
        }
        if (stagePhaseIndex < 0 && !stageId.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("规则编排阶段必须使用数值 phase/bindingPhase，禁止仅用 stageId 字符串表达阶段");
            }
            return false;
        }

        const QVariantList groups = stage.value(QStringLiteral("groups")).toList();
        for (const QVariant& groupValue : groups) {
            const QVariantMap group = groupValue.toMap();
            const QVariantList rules = group.value(QStringLiteral("rules")).toList();
            for (const QVariant& ruleValue : rules) {
                const QVariantMap rule = ruleValue.toMap();
                const QString filePath = rule.value(QStringLiteral("filePath")).toString().trimmed();
                const QString fileName = rule.value(QStringLiteral("fileName")).toString().trimmed();
                const QString templateId = rule.value(QStringLiteral("templateId")).toString().trimmed();
                if (filePath.isEmpty() && fileName.isEmpty() && templateId.isEmpty()) {
                    continue;
                }

                const int rulePhaseIndex = configuredRuleBindingPhaseIndex(rule);
                const bool ruleHasPhaseField = rule.contains(QStringLiteral("bindingPhase"))
                    || rule.contains(QStringLiteral("phase"));
                if (ruleHasPhaseField && rulePhaseIndex < 0) {
                    if (errorMessage) {
                        *errorMessage = QStringLiteral("规则编排规则必须使用数值 bindingPhase/phase，禁止传入字符串或无效阶段索引");
                    }
                    return false;
                }
                if (rulePhaseIndex < 0 && stagePhaseIndex < 0) {
                    if (errorMessage) {
                        *errorMessage = QStringLiteral("规则编排规则必须提供数值 bindingPhase/phase，或继承阶段的数值 phase/bindingPhase");
                    }
                    return false;
                }
            }
        }
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

QVariantMap mergePerformanceMetrics(const QVariantMap& existingStrategy,
                                   const QVariantMap& incomingPerformance,
                                   const QString& updatedAt)
{
    QVariantMap mergedPerformance = existingStrategy.value("performance_metrics").toMap();
    for (auto it = incomingPerformance.constBegin(); it != incomingPerformance.constEnd(); ++it) {
        mergedPerformance.insert(it.key(), it.value());
    }

    QVariantMap historyEntry = incomingPerformance.value("backtestHistoryEntry").toMap();
    QVariantList backtestHistory = mergedPerformance.value("backtestHistory").toList();
    if (!historyEntry.isEmpty()) {
        const bool replaceLatestBacktest = incomingPerformance.value("replaceLatestBacktest", true).toBool();
        historyEntry["recordedAt"] = historyEntry.value("recordedAt", updatedAt).toString();
        backtestHistory.prepend(historyEntry);
        while (backtestHistory.size() > MAX_BACKTEST_HISTORY_ITEMS) {
            backtestHistory.removeLast();
        }
        if (replaceLatestBacktest || !mergedPerformance.value("latestBacktest").canConvert<QVariantMap>()) {
            mergedPerformance["latestBacktest"] = historyEntry;
        }
        mergedPerformance["backtestHistory"] = backtestHistory;
    }

    mergedPerformance["lastBacktestAt"] = incomingPerformance.value("lastBacktestAt", updatedAt).toString();
    return mergedPerformance;
}

QVariantMap mergeVariantMapsRecursive(const QVariantMap& base, const QVariantMap& overlay)
{
    QVariantMap merged = base;
    for (auto it = overlay.constBegin(); it != overlay.constEnd(); ++it) {
        const QVariant existingValue = merged.value(it.key());
        if (existingValue.canConvert<QVariantMap>() && it.value().canConvert<QVariantMap>()) {
            merged.insert(it.key(), mergeVariantMapsRecursive(existingValue.toMap(), it.value().toMap()));
            continue;
        }

        merged.insert(it.key(), it.value());
    }
    return merged;
}

void mergeStrategyParameterPayload(QVariantMap& targetStrategy, const QVariantMap& incomingStrategy)
{
    const QVariantMap existingParameters = targetStrategy.value(QStringLiteral("parameters")).toMap();
    const QVariantMap incomingParameters = incomingStrategy.value(QStringLiteral("parameters")).toMap();
    if (!incomingParameters.isEmpty() || incomingStrategy.contains(QStringLiteral("parameters"))) {
        targetStrategy.insert(QStringLiteral("parameters"), mergeVariantMapsRecursive(existingParameters, incomingParameters));
    }

    const QStringList structuredSnapshotKeys = {
        QStringLiteral("ruleProfileSnapshot"),
        QStringLiteral("executionPolicySnapshot"),
        QStringLiteral("backtestAssumptionsSnapshot"),
        QStringLiteral("strategyScopeContextSnapshot")
    };

    for (const QString& key : structuredSnapshotKeys) {
        const QVariantMap incomingSnapshot = incomingStrategy.value(key).toMap();
        if (incomingSnapshot.isEmpty() && !incomingStrategy.contains(key)) {
            continue;
        }

        const QVariantMap existingSnapshot = targetStrategy.value(key).toMap();
        targetStrategy.insert(key, mergeVariantMapsRecursive(existingSnapshot, incomingSnapshot));
    }
}

void applyCanonicalStrategyIdentity(QVariantMap& strategy)
{
    strategy.remove(QStringLiteral("sub_type"));
    strategy.remove(QStringLiteral("subType"));

    const domain::backtest::ResolvedStrategyIdentity strategyIdentity =
        domain::backtest::resolveStrategyIdentity(strategy);
    if (!strategyIdentity.validStoredType) {
        return;
    }

    strategy.insert(QStringLiteral("strategyTypeIndex"), strategyIdentity.storedTypeIndex());
    switch (strategyIdentity.storedType) {
    case domain::backtest::StrategyStoredType::TrendFollowing:
        strategy.insert(QStringLiteral("strategy_type"), QStringLiteral("TREND"));
        break;
    case domain::backtest::StrategyStoredType::MeanReversion:
        strategy.insert(QStringLiteral("strategy_type"), QStringLiteral("MEAN_REVERSION"));
        break;
    case domain::backtest::StrategyStoredType::Alpha:
        strategy.insert(QStringLiteral("strategy_type"), QStringLiteral("ALPHA"));
        break;
    case domain::backtest::StrategyStoredType::Arbitrage:
        strategy.insert(QStringLiteral("strategy_type"), QStringLiteral("ARBITRAGE"));
        break;
    case domain::backtest::StrategyStoredType::HighFrequency:
        strategy.insert(QStringLiteral("strategy_type"), QStringLiteral("HFT"));
        break;
    case domain::backtest::StrategyStoredType::Portfolio:
        strategy.insert(QStringLiteral("strategy_type"), QStringLiteral("PORTFOLIO"));
        break;
    case domain::backtest::StrategyStoredType::Custom:
        strategy.insert(QStringLiteral("strategy_type"), QStringLiteral("CUSTOM"));
        break;
    case domain::backtest::StrategyStoredType::Unknown:
    default:
        strategy.remove(QStringLiteral("strategy_type"));
        break;
    }
    if (strategyIdentity.behavior.valid) {
        strategy.insert(QStringLiteral("strategyBehaviorKind"), strategyIdentity.behavior.index());
    }
}

QVariantMap applyListDisplayProjection(const QVariantMap& strategy);

QVariantList buildStrategyListFromCache(const QHash<QString, QVariantMap>& memoryCache)
{
    QVariantList strategies;
    for (const QVariantMap& strategy : memoryCache) {
        strategies.append(applyListDisplayProjection(strategy));
    }
    return strategies;
}

QVariantMap applyListDisplayProjection(const QVariantMap& strategy)
{
    if (strategy.isEmpty()) {
        return strategy;
    }

    QVariantMap projected = strategy;
    const domain::strategy::StrategyAggregate aggregate = assembleStrategyAggregate(strategy);

    if (aggregate.identity.strategyId.isValid()) {
        projected.insert(QStringLiteral("strategyId"), aggregate.identity.strategyId.text());
    }
    if (aggregate.identity.strategyCode.isValid()) {
        projected.insert(QStringLiteral("strategyCode"), aggregate.identity.strategyCode.text());
    }
    if (aggregate.identity.strategyName.isValid()) {
        projected.insert(QStringLiteral("strategyName"), aggregate.identity.strategyName.text());
    }
    if (aggregate.metadata.description.isValid()) {
        projected.insert(QStringLiteral("description"), aggregate.metadata.description.text());
    }

    projected.insert(QStringLiteral("storedTypeLabel"), storedTypeLabel(aggregate.identity.storedType));
    projected.insert(QStringLiteral("behaviorLabel"), behaviorLabel(aggregate.identity.behaviorKind));
    projected.insert(QStringLiteral("statusLabel"), lifecycleStatusLabel(aggregate.lifecycle.status));
    projected.insert(QStringLiteral("signalsEnabled"), aggregate.lifecycle.allowsSignalEmission());
    projected.insert(QStringLiteral("tags"), strategyTagListToStrings(aggregate.metadata.tags));
    return projected;
}

QString firstNonEmptyStrategyText(const QVariantMap& strategy, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const QString text = strategy.value(QString::fromUtf8(key)).toString().trimmed();
        if (!text.isEmpty()) {
            return text;
        }
    }
    return {};
}

QStringList strategyTags(const QVariantMap& strategy)
{
    const QVariant tagsValue = strategy.value(QStringLiteral("tags"));
    QStringList tags;
    if (tagsValue.canConvert<QStringList>()) {
        tags = tagsValue.toStringList();
    } else {
        const QVariantList tagList = tagsValue.toList();
        for (const QVariant& tagValue : tagList) {
            const QString tagText = tagValue.toString().trimmed();
            if (!tagText.isEmpty()) {
                tags.append(tagText);
            }
        }
    }
    tags.removeDuplicates();
    return tags;
}

QString normalizeStrategySymbol(const QString& rawSymbol)
{
    const QString symbol = rawSymbol.trimmed().toUpper();
    if (symbol.isEmpty()) {
        return {};
    }

    const QStringList parts = symbol.split('.');
    if (parts.size() == 2) {
        const QString left = parts.at(0);
        const QString right = parts.at(1);
        if (left == QStringLiteral("SHSE") || left == QStringLiteral("SZSE") || left == QStringLiteral("BSE")
            || left == QStringLiteral("CFFEX") || left == QStringLiteral("SHFE") || left == QStringLiteral("DCE")
            || left == QStringLiteral("CZCE") || left == QStringLiteral("INE") || left == QStringLiteral("GFEX")) {
            if (left == QStringLiteral("SHSE")) {
                return right + QStringLiteral(".SH");
            }
            if (left == QStringLiteral("SZSE")) {
                return right + QStringLiteral(".SZ");
            }
            if (left == QStringLiteral("BSE")) {
                return right + QStringLiteral(".BJ");
            }
            return right + QStringLiteral(".") + left;
        }
    }

    return symbol;
}

QString buildSignalPublicationKey(const QString& strategyId, const QString& symbol)
{
    return strategyId.trimmed() + QChar('|') + normalizeStrategySymbol(symbol);
}

QStringList strategyBacktestCandidateRepoRoots()
{
    QStringList candidates;
    candidates << QDir::currentPath();

    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        candidates << appDir;
        candidates << QDir(appDir).absoluteFilePath(QStringLiteral(".."));
        candidates << QDir(appDir).absoluteFilePath(QStringLiteral("../.."));
        candidates << QDir(appDir).absoluteFilePath(QStringLiteral("../../.."));
    }

    candidates.removeDuplicates();
    return candidates;
}

QString resolveStrategyBacktestRepoRoot()
{
    for (const QString& candidate : strategyBacktestCandidateRepoRoots()) {
        const QFileInfo scriptInfo(QDir(candidate).absoluteFilePath(QStringLiteral("tools/trading_day_utils.py")));
        if (scriptInfo.exists() && scriptInfo.isFile()) {
            return QDir(candidate).absolutePath();
        }
    }

    return {};
}

QString resolveStrategyBacktestPythonExecutable(const QString& repoRoot)
{
    if (!repoRoot.isEmpty()) {
        const QString windowsVenv = QDir(repoRoot).absoluteFilePath(QStringLiteral(".venv/Scripts/python.exe"));
        if (QFileInfo::exists(windowsVenv)) {
            return windowsVenv;
        }

        const QString unixVenv = QDir(repoRoot).absoluteFilePath(QStringLiteral(".venv/bin/python"));
        if (QFileInfo::exists(unixVenv)) {
            return unixVenv;
        }
    }

    return QStringLiteral("python");
}

QString normalizeStrategyBacktestTradeDate(const QString& value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QDate parsed = QDate::fromString(trimmed, Qt::ISODate);
    return parsed.isValid() ? parsed.toString(QStringLiteral("yyyy-MM-dd")) : QString();
}

int resolveTradingDayIndexForBacktest(const QString& repoRoot, const QString& tradeDate)
{
    const QString normalizedDate = normalizeStrategyBacktestTradeDate(tradeDate);
    if (repoRoot.isEmpty() || normalizedDate.isEmpty()) {
        return -1;
    }

    static const QString script = QStringLiteral(
        "import sys\n"
        "from tools.trading_day_utils import get_trade_calendar\n"
        "target = sys.argv[1]\n"
        "dates = [trade_date.isoformat() for trade_date in get_trade_calendar()]\n"
        "print(dates.index(target) if target in dates else -1)\n");

    QProcess process;
    process.setWorkingDirectory(repoRoot);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString existingPythonPath = environment.value(QStringLiteral("PYTHONPATH"));
    environment.insert(
        QStringLiteral("PYTHONPATH"),
        existingPythonPath.isEmpty()
            ? repoRoot
            : (repoRoot + QDir::listSeparator() + existingPythonPath));
    process.setProcessEnvironment(environment);

    process.start(resolveStrategyBacktestPythonExecutable(repoRoot), {QStringLiteral("-c"), script, normalizedDate});
    if (!process.waitForStarted(2000)) {
        return -1;
    }
    if (!process.waitForFinished(12000)) {
        process.kill();
        process.waitForFinished(1000);
        return -1;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return -1;
    }

    bool ok = false;
    const int index = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed().toInt(&ok);
    return ok ? index : -1;
}

qulonglong positiveUnsignedId(const QVariant& value)
{
    bool ok = false;
    const qulonglong parsed = value.toULongLong(&ok);
    return ok && parsed > 0ULL ? parsed : 0ULL;
}

QVariantList toVariantIdList(const QList<qulonglong>& ids)
{
    QVariantList result;
    result.reserve(ids.size());
    for (const qulonglong id : ids) {
        result.append(QVariant::fromValue<qulonglong>(id));
    }
    return result;
}

QVariantMap resolveSymbolIdMap(const QStringList& normalizedSymbols)
{
    QVariantMap resolved;
    if (normalizedSymbols.isEmpty()) {
        return resolved;
    }

    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        return resolved;
    }

    QStringList placeholders;
    std::map<QString, QVariant> params;
    placeholders.reserve(normalizedSymbols.size());
    for (int index = 0; index < normalizedSymbols.size(); ++index) {
        const QString placeholder = QStringLiteral(":symbol_%1").arg(index);
        placeholders.push_back(placeholder);
        params[placeholder] = normalizedSymbols.at(index);
    }

    const QString sql = QStringLiteral(
        "SELECT symbol_id, symbol FROM symbol_info WHERE symbol IN (%1)")
        .arg(placeholders.join(QStringLiteral(", ")));
    const auto queryResult = database->executeQuery(sql, params);
    for (size_t rowIndex = 0; rowIndex < queryResult.rowCount(); ++rowIndex) {
        const auto& row = queryResult.getRow(rowIndex);
        const qulonglong symbolId = row.getValueAs<qulonglong>(QStringLiteral("symbol_id"), 0ULL);
        const QString symbolCode = normalizeStrategySymbol(row.getString(QStringLiteral("symbol")));
        if (symbolId == 0ULL || symbolCode.isEmpty()) {
            continue;
        }
        resolved.insert(symbolCode, QVariant::fromValue<qulonglong>(symbolId));
    }

    return resolved;
}

void applyCanonicalStrategyStructures(QVariantMap& strategy);

QString serializeRuleTemplateStage(domain::strategy::RuleTemplateStage stage)
{
    switch (stage) {
    case domain::strategy::RuleTemplateStage::Market:
        return QStringLiteral("market");
    case domain::strategy::RuleTemplateStage::Signal:
        return QStringLiteral("signal");
    case domain::strategy::RuleTemplateStage::Entry:
        return QStringLiteral("entry");
    case domain::strategy::RuleTemplateStage::Rebalance:
        return QStringLiteral("rebalance");
    case domain::strategy::RuleTemplateStage::Exit:
        return QStringLiteral("exit");
    case domain::strategy::RuleTemplateStage::Risk:
        return QStringLiteral("risk");
    case domain::strategy::RuleTemplateStage::Watch:
        return QStringLiteral("watch");
    case domain::strategy::RuleTemplateStage::Eligibility:
        return QStringLiteral("eligibility");
    case domain::strategy::RuleTemplateStage::Portfolio:
        return QStringLiteral("portfolio");
    case domain::strategy::RuleTemplateStage::Execution:
        return QStringLiteral("execution");
    case domain::strategy::RuleTemplateStage::AccountRisk:
        return QStringLiteral("account_risk");
    case domain::strategy::RuleTemplateStage::Unspecified:
        return {};
    }

    return {};
}

QString serializeRuleTemplateResultType(domain::strategy::RuleTemplateResultType resultType)
{
    switch (resultType) {
    case domain::strategy::RuleTemplateResultType::Pass:
        return QStringLiteral("pass");
    case domain::strategy::RuleTemplateResultType::StateSwitch:
        return QStringLiteral("state_switch");
    case domain::strategy::RuleTemplateResultType::Halt:
        return QStringLiteral("halt");
    case domain::strategy::RuleTemplateResultType::Block:
        return QStringLiteral("block");
    case domain::strategy::RuleTemplateResultType::CandidateEntry:
        return QStringLiteral("candidate_entry");
    case domain::strategy::RuleTemplateResultType::Open:
        return QStringLiteral("open");
    case domain::strategy::RuleTemplateResultType::Reduce:
        return QStringLiteral("reduce");
    case domain::strategy::RuleTemplateResultType::Exit:
        return QStringLiteral("exit");
    case domain::strategy::RuleTemplateResultType::Unspecified:
        return {};
    }

    return {};
}

domain::strategy::RuleGroupRole parseComposerGroupRole(const QVariant& value)
{
    const QString groupRole = value.toString().trimmed().toLower();
    if (groupRole == QStringLiteral("must_pass")) {
        return domain::strategy::RuleGroupRole::MustPass;
    }
    if (groupRole == QStringLiteral("any_pass")) {
        return domain::strategy::RuleGroupRole::AnyPass;
    }
    if (groupRole == QStringLiteral("trigger")) {
        return domain::strategy::RuleGroupRole::Trigger;
    }
    if (groupRole == QStringLiteral("score_boost")) {
        return domain::strategy::RuleGroupRole::ScoreBoost;
    }
    if (groupRole == QStringLiteral("entry_guard")) {
        return domain::strategy::RuleGroupRole::EntryGuard;
    }
    if (groupRole == QStringLiteral("exit_guard")) {
        return domain::strategy::RuleGroupRole::ExitGuard;
    }
    if (groupRole == QStringLiteral("position_management")) {
        return domain::strategy::RuleGroupRole::PositionManagement;
    }
    return domain::strategy::RuleGroupRole::Unspecified;
}

domain::strategy::RuleGroupOperator parseComposerGroupOperator(const QVariant& value)
{
    const QString groupOperator = value.toString().trimmed().toLower();
    if (groupOperator == QStringLiteral("all")) {
        return domain::strategy::RuleGroupOperator::All;
    }
    if (groupOperator == QStringLiteral("at_least")) {
        return domain::strategy::RuleGroupOperator::MinimumMatch;
    }
    if (groupOperator == QStringLiteral("first_match")) {
        return domain::strategy::RuleGroupOperator::FirstMatch;
    }
    if (groupOperator == QStringLiteral("score_sum")) {
        return domain::strategy::RuleGroupOperator::ScoreSum;
    }
    return domain::strategy::RuleGroupOperator::Any;
}

QString serializeRuleGroupRole(const QVariant& value)
{
    bool ok = false;
    const int index = value.toInt(&ok);
    if (!ok) {
        return value.toString().trimmed();
    }

    switch (static_cast<domain::strategy::RuleGroupRole>(index)) {
    case domain::strategy::RuleGroupRole::MustPass:
        return QStringLiteral("must_pass");
    case domain::strategy::RuleGroupRole::AnyPass:
        return QStringLiteral("any_pass");
    case domain::strategy::RuleGroupRole::Trigger:
        return QStringLiteral("trigger");
    case domain::strategy::RuleGroupRole::ScoreBoost:
        return QStringLiteral("score_boost");
    case domain::strategy::RuleGroupRole::EntryGuard:
        return QStringLiteral("entry_guard");
    case domain::strategy::RuleGroupRole::ExitGuard:
        return QStringLiteral("exit_guard");
    case domain::strategy::RuleGroupRole::PositionManagement:
        return QStringLiteral("position_management");
    case domain::strategy::RuleGroupRole::Unspecified:
        return {};
    }

    return {};
}

QString serializeRuleGroupOperator(const QVariant& value)
{
    bool ok = false;
    const int index = value.toInt(&ok);
    if (!ok) {
        return value.toString().trimmed();
    }

    switch (static_cast<domain::strategy::RuleGroupOperator>(index)) {
    case domain::strategy::RuleGroupOperator::All:
        return QStringLiteral("all");
    case domain::strategy::RuleGroupOperator::Any:
        return QStringLiteral("any");
    case domain::strategy::RuleGroupOperator::MinimumMatch:
        return QStringLiteral("at_least");
    case domain::strategy::RuleGroupOperator::FirstMatch:
        return QStringLiteral("first_match");
    case domain::strategy::RuleGroupOperator::ScoreSum:
        return QStringLiteral("score_sum");
    }

    return {};
}

QVariantList extractRuleTemplateBindingsFromComposerState(const QVariantMap& ruleProfile)
{
    QVariantMap composerState = ruleProfile.value(QStringLiteral("ruleComposerState")).toMap();
    if (composerState.isEmpty()) {
        composerState = ruleProfile.value(QStringLiteral("rule_composer_state")).toMap();
    }
    if (composerState.isEmpty()) {
        return {};
    }

    QVariantList bindings;
    const QVariantList stages = composerState.value(QStringLiteral("stages")).toList();
    for (const QVariant& stageValue : stages) {
        const QVariantMap stage = stageValue.toMap();
        const int stagePhaseIndex = configuredRuleBindingPhaseIndex(stage);
        const QVariantList groups = stage.value(QStringLiteral("groups")).toList();
        for (const QVariant& groupValue : groups) {
            const QVariantMap group = groupValue.toMap();
            const QString groupId = group.value(QStringLiteral("groupId")).toString().trimmed();
            const QString groupTitle = group.value(QStringLiteral("title")).toString().trimmed();
            const domain::strategy::RuleGroupRole groupRole = parseComposerGroupRole(group.value(QStringLiteral("role")));
            const domain::strategy::RuleGroupOperator groupOperator = parseComposerGroupOperator(group.value(QStringLiteral("operator")));
            const int groupMinMatchCount = positiveIntegerFromVariant(
                group.value(QStringLiteral("groupMinMatchCount")),
                positiveIntegerFromVariant(
                    group.value(QStringLiteral("minMatchCount")),
                    positiveIntegerFromVariant(
                        group.value(QStringLiteral("minimumMatches")),
                        positiveIntegerFromVariant(group.value(QStringLiteral("atLeastCount"))))));
            const QVariantList rules = group.value(QStringLiteral("rules")).toList();
            for (const QVariant& ruleValue : rules) {
                const QVariantMap rule = ruleValue.toMap();
                const QString filePath = rule.value(QStringLiteral("filePath")).toString().trimmed();
                const QString fileName = rule.value(QStringLiteral("fileName")).toString().trimmed();
                const QString templateId = rule.value(QStringLiteral("templateId")).toString().trimmed();
                if (filePath.isEmpty() && fileName.isEmpty() && templateId.isEmpty()) {
                    continue;
                }

                const int rulePhaseIndex = configuredRuleBindingPhaseIndex(rule);
                const int bindingPhaseIndex = rulePhaseIndex >= 0 ? rulePhaseIndex : stagePhaseIndex;
                if (bindingPhaseIndex < 0) {
                    continue;
                }

                QVariantMap binding;
                binding.insert(QStringLiteral("bindingPhase"), bindingPhaseIndex);
                if (!fileName.isEmpty()) {
                    binding.insert(QStringLiteral("file_name"), fileName);
                }
                if (!filePath.isEmpty()) {
                    binding.insert(QStringLiteral("file_path"), filePath);
                }
                if (!templateId.isEmpty()) {
                    binding.insert(QStringLiteral("template_id"), templateId);
                }

                const QString templateName = rule.value(QStringLiteral("templateName")).toString().trimmed();
                if (!templateName.isEmpty()) {
                    binding.insert(QStringLiteral("template_display_name"), templateName);
                }
                const QString summary = rule.value(QStringLiteral("summary")).toString().trimmed();
                if (!summary.isEmpty()) {
                    binding.insert(QStringLiteral("summary"), summary);
                }
                const QString category = rule.value(QStringLiteral("category")).toString().trimmed();
                if (!category.isEmpty()) {
                    binding.insert(QStringLiteral("category"), category);
                }
                const QString termId = rule.value(QStringLiteral("termId")).toString().trimmed();
                if (!termId.isEmpty()) {
                    binding.insert(QStringLiteral("term_id"), termId);
                }
                const QString termName = rule.value(QStringLiteral("termName")).toString().trimmed();
                if (!termName.isEmpty()) {
                    binding.insert(QStringLiteral("term_display_name"), termName);
                }
                if (!groupId.isEmpty()) {
                    binding.insert(QStringLiteral("group_id"), groupId);
                }
                if (!groupTitle.isEmpty()) {
                    binding.insert(QStringLiteral("group_title"), groupTitle);
                }
                binding.insert(QStringLiteral("group_role"), static_cast<int>(groupRole));
                binding.insert(QStringLiteral("group_operator"), static_cast<int>(groupOperator));
                if (groupMinMatchCount > 0) {
                    binding.insert(QStringLiteral("group_min_match_count"), groupMinMatchCount);
                }

                bool exists = false;
                for (const QVariant& existing : bindings) {
                    if (existing.toMap() == binding) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    bindings.append(binding);
                }
            }
        }
    }

    return bindings;
}

QVariantList strategyRuleTemplateBindings(const QVariantMap& strategy)
{
    const QVariantMap parameters = strategy.value(QStringLiteral("parameters")).toMap();
    QVariantList bindings = extractRuleTemplateBindingsFromComposerState(parameters.value(QStringLiteral("rule_profile")).toMap());
    if (!bindings.isEmpty()) {
        return bindings;
    }

    bindings = extractRuleTemplateBindingsFromComposerState(strategy.value(QStringLiteral("ruleProfileSnapshot")).toMap());
    if (!bindings.isEmpty()) {
        return bindings;
    }

    return {};
}

QVariantMap primaryRuleTemplateBinding(const QVariantList& bindings)
{
    QVariantMap fallback;
    for (const QVariant& bindingValue : bindings) {
        const QVariantMap binding = bindingValue.toMap();
        if (binding.isEmpty()) {
            continue;
        }
        if (configuredRuleBindingPhaseIndex(binding)
                == static_cast<int>(domain::strategy::RuleBindingPhase::Signal)) {
            return binding;
        }
        if (fallback.isEmpty()) {
            fallback = binding;
        }
    }
    return fallback;
}

void applyCanonicalStrategyStructures(QVariantMap& strategy);

bool hasCompleteEditableRulePayload(const QVariantMap& strategy)
{
    if (strategy.isEmpty()) {
        return false;
    }

    const QVariantMap parameters = strategy.value(QStringLiteral("parameters")).toMap();
    if (!parameters.isEmpty()) {
        const QVariantMap ruleProfile = parameters.value(QStringLiteral("rule_profile")).toMap();
        if (!ruleProfile.isEmpty()) {
            return true;
        }

        const QVariantMap composerState = parameters.value(QStringLiteral("rule_composer_state")).toMap();
        if (!composerState.isEmpty()) {
            return true;
        }

        const QVariantMap factorOverlay = parameters.value(QStringLiteral("factor_overlay")).toMap();
        if (!factorOverlay.isEmpty()) {
            return true;
        }
    }

    if (!strategy.value(QStringLiteral("ruleProfileSnapshot")).toMap().isEmpty()) {
        return true;
    }

    if (!strategy.value(QStringLiteral("factorOverlaySnapshot")).toMap().isEmpty()) {
        return true;
    }

    return false;
}

QVariantMap recoverEditableRulePayloadFromBacktest(const QVariantMap& strategy)
{
    if (strategy.isEmpty() || hasCompleteEditableRulePayload(strategy)) {
        return strategy;
    }

    QVariantMap repairedStrategy = strategy;
    QVariantMap parameters = repairedStrategy.value(QStringLiteral("parameters")).toMap();

    auto mergeRuntimeParameters = [&](const QVariantMap& runtimeParameters) {
        if (runtimeParameters.isEmpty()) {
            return false;
        }

        bool changed = false;

        const QVariantMap runtimeRuleProfile = runtimeParameters.value(QStringLiteral("rule_profile")).toMap();
        if (parameters.value(QStringLiteral("rule_profile")).toMap().isEmpty() && !runtimeRuleProfile.isEmpty()) {
            parameters.insert(QStringLiteral("rule_profile"), runtimeRuleProfile);
            changed = true;
        }

        const QVariantMap runtimeComposerState = runtimeParameters.value(QStringLiteral("rule_composer_state")).toMap();
        if (parameters.value(QStringLiteral("rule_composer_state")).toMap().isEmpty() && !runtimeComposerState.isEmpty()) {
            parameters.insert(QStringLiteral("rule_composer_state"), runtimeComposerState);
            changed = true;
        }

        const QVariantMap runtimeExecutionPolicy = runtimeParameters.value(QStringLiteral("execution_policy")).toMap();
        if (parameters.value(QStringLiteral("execution_policy")).toMap().isEmpty() && !runtimeExecutionPolicy.isEmpty()) {
            parameters.insert(QStringLiteral("execution_policy"), runtimeExecutionPolicy);
            changed = true;
        }

        const QVariantMap runtimeBacktestAssumptions = runtimeParameters.value(QStringLiteral("backtest_assumptions")).toMap();
        if (parameters.value(QStringLiteral("backtest_assumptions")).toMap().isEmpty() && !runtimeBacktestAssumptions.isEmpty()) {
            parameters.insert(QStringLiteral("backtest_assumptions"), runtimeBacktestAssumptions);
            changed = true;
        }

        const QVariantMap runtimeScopeContext = runtimeParameters.value(QStringLiteral("strategy_scope_context")).toMap();
        if (parameters.value(QStringLiteral("strategy_scope_context")).toMap().isEmpty() && !runtimeScopeContext.isEmpty()) {
            parameters.insert(QStringLiteral("strategy_scope_context"), runtimeScopeContext);
            changed = true;
        }

        const QVariantMap runtimeFactorOverlay = runtimeParameters.value(QStringLiteral("factor_overlay")).toMap();
        if (parameters.value(QStringLiteral("factor_overlay")).toMap().isEmpty() && !runtimeFactorOverlay.isEmpty()) {
            parameters.insert(QStringLiteral("factor_overlay"), runtimeFactorOverlay);
            changed = true;
        }

        if (changed) {
            repairedStrategy.insert(QStringLiteral("parameters"), parameters);
            applyCanonicalStrategyStructures(repairedStrategy);
        }

        return changed;
    };

    const QVariantMap performanceMetrics = repairedStrategy.value(QStringLiteral("performance_metrics")).toMap();
    const QVariantMap latestBacktest = performanceMetrics.value(QStringLiteral("latestBacktest")).toMap();
    if (mergeRuntimeParameters(latestBacktest.value(QStringLiteral("runtimeParameters")).toMap())) {
        qInfo() << "StrategyService: recovered editable rule payload from latestBacktest"
                << repairedStrategy.value(QStringLiteral("strategy_id")).toString();
        return repairedStrategy;
    }

    const QVariantList backtestHistory = performanceMetrics.value(QStringLiteral("backtestHistory")).toList();
    for (const QVariant& historyEntryValue : backtestHistory) {
        const QVariantMap historyEntry = historyEntryValue.toMap();
        if (mergeRuntimeParameters(historyEntry.value(QStringLiteral("runtimeParameters")).toMap())) {
            qInfo() << "StrategyService: recovered editable rule payload from backtestHistory"
                    << repairedStrategy.value(QStringLiteral("strategy_id")).toString();
            return repairedStrategy;
        }
    }

    return repairedStrategy;
}

bool validateRuleTemplateBindings(const QVariantList& bindings, QString* errorMessage = nullptr)
{
    if (bindings.isEmpty()) {
        if (errorMessage) {
            errorMessage->clear();
        }
        return true;
    }
    return !bridge::rules::loadCompiledRuleTemplates(bindings, errorMessage).isEmpty();
}

QVariantMap loadTradingConfigurationSnapshot()
{
    TradingConnectionConfigService* configService = TradingConnectionConfigService::instance();
    if (!configService) {
        return {};
    }

    QVariantMap configuration = configService->currentConfiguration();
    if (configuration.isEmpty()) {
        configuration = configService->loadConfiguration();
    }
    return configuration;
}

QVariantMap loadRiskConfigurationSnapshot()
{
    RiskConfigService* riskConfigService = RiskConfigService::instance();
    if (!riskConfigService) {
        return {};
    }

    QVariantMap riskConfiguration = riskConfigService->appliedConfiguration();
    if (riskConfiguration.isEmpty()) {
        riskConfiguration = riskConfigService->currentConfiguration();
    }
    return riskConfiguration;
}

QVariant firstConfiguredValue(const QVariantMap& map, const QStringList& keys)
{
    for (const QString& key : keys) {
        if (!map.contains(key)) {
            continue;
        }

        const QVariant value = map.value(key);
        if (!value.isValid() || value.isNull()) {
            continue;
        }

        if (value.typeId() == QMetaType::QString && value.toString().trimmed().isEmpty()) {
            continue;
        }

        return value;
    }

    return {};
}

double numericParam(const QVariantMap& map, const QStringList& keys, double fallback)
{
    const QVariant rawValue = firstConfiguredValue(map, keys);
    if (!rawValue.isValid()) {
        return fallback;
    }

    bool ok = false;
    const double numericValue = rawValue.toDouble(&ok);
    return ok ? numericValue : fallback;
}

double normalizedRatio(double value, double fallback)
{
    if (!std::isfinite(value) || value <= 0.0) {
        return fallback;
    }
    return value > 1.0 ? value / 100.0 : value;
}

double numericRatioParam(const QVariantMap& map, const QStringList& keys, double fallback)
{
    const QVariant rawValue = firstConfiguredValue(map, keys);
    if (!rawValue.isValid()) {
        return fallback;
    }

    bool ok = false;
    const double numericValue = rawValue.toDouble(&ok);
    if (!ok) {
        return fallback;
    }
    return normalizedRatio(numericValue, fallback);
}

double resolvedRatioLimit(const QVariantMap& primary,
                          const QVariantMap& secondary,
                          const QStringList& keys,
                          double fallback)
{
    const double primaryValue = numericRatioParam(primary, keys, -1.0);
    const double secondaryValue = numericRatioParam(secondary, keys, -1.0);
    if (primaryValue > 0.0 && secondaryValue > 0.0) {
        return std::min(primaryValue, secondaryValue);
    }
    if (primaryValue > 0.0) {
        return primaryValue;
    }
    if (secondaryValue > 0.0) {
        return secondaryValue;
    }
    return fallback;
}

double resolvedNumericLimit(const QVariantMap& primary,
                            const QVariantMap& secondary,
                            const QStringList& keys,
                            double fallback)
{
    const double primaryValue = numericParam(primary, keys, -1.0);
    const double secondaryValue = numericParam(secondary, keys, -1.0);
    if (primaryValue > 0.0 && secondaryValue > 0.0) {
        return std::min(primaryValue, secondaryValue);
    }
    if (primaryValue > 0.0) {
        return primaryValue;
    }
    if (secondaryValue > 0.0) {
        return secondaryValue;
    }
    return fallback;
}

QVariantMap positionSnapshotForSymbol(const QVariantList& positions, const QString& symbol)
{
    const QString normalizedSymbol = normalizeStrategySymbol(symbol);
    for (const QVariant& rawPosition : positions) {
        const QVariantMap position = rawPosition.toMap();
        if (normalizeStrategySymbol(position.value(QStringLiteral("symbol")).toString()) == normalizedSymbol) {
            return position;
        }
    }

    return {};
}

qint64 closeableQuantityForPosition(const QVariantMap& position)
{
    return static_cast<qint64>(numericParam(
        position,
        {QStringLiteral("closeableQuantity"), QStringLiteral("closeable_quantity"), QStringLiteral("availableQuantity"), QStringLiteral("available_quantity"), QStringLiteral("quantity")},
        0.0));
}

double symbolMarketValue(const QVariantList& positions, const QString& symbol)
{
    return numericParam(positionSnapshotForSymbol(positions, symbol),
                        {QStringLiteral("marketValue"), QStringLiteral("market_value")},
                        0.0);
}

double totalAssetValue(const QVariantMap& accountSnapshot)
{
    const double totalAsset = numericParam(
        accountSnapshot,
        {QStringLiteral("totalAsset"), QStringLiteral("total_asset"), QStringLiteral("nav")},
        0.0);
    if (totalAsset > 0.0) {
        return totalAsset;
    }

    const double availableCash = numericParam(
        accountSnapshot,
        {QStringLiteral("availableCash"), QStringLiteral("available_cash")},
        0.0);
    const double marketValue = numericParam(
        accountSnapshot,
        {QStringLiteral("marketValue"), QStringLiteral("market_value")},
        0.0);
    return availableCash + marketValue;
}

qint64 boardLotQuantity(double notionalBudget, double price)
{
    if (!std::isfinite(notionalBudget) || notionalBudget <= 0.0 || price <= 0.0) {
        return 0;
    }

    const double boardLots = std::floor(notionalBudget / price / 100.0);
    return boardLots > 0.0 ? static_cast<qint64>(boardLots) * 100 : 0;
}

qint64 shareQuantity(double notionalBudget, double price)
{
    if (!std::isfinite(notionalBudget) || notionalBudget <= 0.0 || price <= 0.0) {
        return 0;
    }

    const double rawQuantity = std::floor(notionalBudget / price);
    return rawQuantity > 0.0 ? static_cast<qint64>(rawQuantity) : 0;
}

struct RuntimeOrderSizingResult {
    qint64 quantity = 0;
    double requestedNotional = 0.0;
    QString side;
    QString positionEffect;
    QString mode;
    QString action;
    QString failureReason;
    QString failureMessage;
};

bool normalizedRuntimeExecutionOption(const QVariant& value, bool fallback = false)
{
    if (!value.isValid() || value.isNull()) {
        return fallback;
    }

    if (value.typeId() == QMetaType::Bool) {
        return value.toBool();
    }

    const QString normalized = value.toString().trimmed().toLower();
    if (normalized.isEmpty()) {
        return fallback;
    }

    return normalized == QStringLiteral("true")
        || normalized == QStringLiteral("1")
        || normalized == QStringLiteral("yes")
        || normalized == QStringLiteral("y");
}

int normalizedRuntimeBatchCount(const QVariant& value)
{
    bool ok = false;
    const int numericValue = value.toInt(&ok);
    return ok && numericValue > 0 ? numericValue : 0;
}

double normalizedRuntimeBatchLimit(const QVariant& value)
{
    bool ok = false;
    const double numericValue = value.toDouble(&ok);
    return ok && numericValue > 0.0 ? numericValue : 0.0;
}

QVariantMap resolveRuntimeExecutionBatchOptions(const QVariantMap& executionPolicy)
{
    const QVariantMap batchPolicy = executionPolicy.value(QStringLiteral("batchExecution")).toMap();

    const int maxBatchOrders = normalizedRuntimeBatchCount(firstConfiguredValue(
        batchPolicy,
        {QStringLiteral("maxBatchOrders"), QStringLiteral("batchOrderLimit")}));
    double maxBatchNotionalWan = normalizedRuntimeBatchLimit(firstConfiguredValue(
        batchPolicy,
        {QStringLiteral("maxBatchNotionalWan"), QStringLiteral("batchNotionalLimitWan")}));
    const double maxBatchNotionalAbsolute = normalizedRuntimeBatchLimit(firstConfiguredValue(
        batchPolicy,
        {QStringLiteral("maxBatchNotional"), QStringLiteral("batchNotionalLimit")}));
    const double maxBatchNotional = maxBatchNotionalAbsolute > 0.0
        ? maxBatchNotionalAbsolute
        : maxBatchNotionalWan * 10000.0;
    if (maxBatchNotionalWan <= 0.0 && maxBatchNotional > 0.0) {
        maxBatchNotionalWan = maxBatchNotional / 10000.0;
    }

    const bool waitPreviousBatchFilled = normalizedRuntimeExecutionOption(
        firstConfiguredValue(batchPolicy, {QStringLiteral("waitPreviousBatchFilled")}),
        true);
    const bool pauseOnConflict = normalizedRuntimeExecutionOption(
        firstConfiguredValue(batchPolicy, {QStringLiteral("pauseOnConflict")}),
        false);
    const bool pauseOnAbnormalReject = normalizedRuntimeExecutionOption(
        firstConfiguredValue(batchPolicy, {QStringLiteral("pauseOnAbnormalReject")}),
        false);
    const int manualCheckpointBatchIndex = normalizedRuntimeBatchCount(firstConfiguredValue(
        batchPolicy,
        {QStringLiteral("manualCheckpointBatchIndex")}));
    const bool requireManualCheckpoint = manualCheckpointBatchIndex > 0 || normalizedRuntimeExecutionOption(
        firstConfiguredValue(batchPolicy, {QStringLiteral("requireManualCheckpoint")}),
        false);

    return QVariantMap{{QStringLiteral("maxBatchOrders"), maxBatchOrders},
                       {QStringLiteral("waitPreviousBatchFilled"), waitPreviousBatchFilled},
                       {QStringLiteral("pauseOnConflict"), pauseOnConflict},
                       {QStringLiteral("pauseOnAbnormalReject"), pauseOnAbnormalReject},
                       {QStringLiteral("requireManualCheckpoint"), requireManualCheckpoint},
                       {QStringLiteral("manualCheckpointBatchIndex"), manualCheckpointBatchIndex},
                       {QStringLiteral("maxBatchNotional"), maxBatchNotional},
                       {QStringLiteral("maxBatchNotionalWan"), maxBatchNotionalWan}};
}

QVariantMap resolvedRuntimeExecutionPolicy(const QVariantMap& strategy)
{
    const QVariantMap executionPolicySnapshot = strategy.value(QStringLiteral("executionPolicySnapshot")).toMap();
    if (!executionPolicySnapshot.isEmpty()) {
        return executionPolicySnapshot;
    }

    return strategy.value(QStringLiteral("parameters")).toMap().value(QStringLiteral("execution_policy")).toMap();
}

void applyRuntimeExecutionPlanMetadata(QVariantMap& request,
                                       const QVariantMap& executionPolicy)
{
    QVariantList plannedOrders{request};
    const QVariantMap batchOptions = resolveRuntimeExecutionBatchOptions(executionPolicy);
    const QVariantMap batchPlan = portfolio_execution_plan::buildSellFirstExecutionBatches(
        &plannedOrders,
        batchOptions);
    const QVariantList annotatedOrders = plannedOrders;
    if (!annotatedOrders.isEmpty()) {
        const QVariantMap annotatedOrder = annotatedOrders.first().toMap();
        for (auto it = annotatedOrder.constBegin(); it != annotatedOrder.constEnd(); ++it) {
            if (!request.contains(it.key()) || it.key().startsWith(QStringLiteral("batch"))
                || it.key().startsWith(QStringLiteral("execution"))
                || it.key().startsWith(QStringLiteral("previousBatch"))
                || it.key().startsWith(QStringLiteral("nextBatch"))
                || it.key().startsWith(QStringLiteral("requires"))
                || it.key().startsWith(QStringLiteral("pauseOn"))
                || it.key().startsWith(QStringLiteral("blocksFollowing"))
                || it.key() == QStringLiteral("manualCheckpointBatchIndex")) {
                request.insert(it.key(), it.value());
            }
        }
    }

    const QVariantMap batchSummary = batchPlan.value(QStringLiteral("summary")).toMap();
    if (!batchSummary.isEmpty()) {
        request.insert(QStringLiteral("executionBatchSummary"), batchSummary);
    }

    if (!request.value(QStringLiteral("batchId")).toString().trimmed().isEmpty()) {
        return;
    }

    const QString side = request.value(QStringLiteral("side")).toString().trimmed().toUpper();
    if (side.isEmpty()) {
        return;
    }

    const bool requireManualCheckpoint = batchOptions.value(QStringLiteral("requireManualCheckpoint")).toBool();
    const int manualCheckpointBatchIndex = batchOptions.value(QStringLiteral("manualCheckpointBatchIndex")).toInt();
    const int effectiveManualCheckpointBatchIndex = requireManualCheckpoint
        ? (manualCheckpointBatchIndex > 0 ? manualCheckpointBatchIndex : 1)
        : 0;
    const QString executionScopeId = portfolio_execution_plan::executionScopeIdForOrders(QVariantList{request});

    request.insert(QStringLiteral("batchId"), portfolio_execution_plan::batchIdForIndex(0));
    request.insert(QStringLiteral("batchIndex"), 0);
    request.insert(QStringLiteral("executionSequence"), 0);
    request.insert(QStringLiteral("batchRole"), portfolio_execution_plan::batchRoleForSide(side));
    request.insert(QStringLiteral("batchPhase"), portfolio_execution_plan::batchPhaseForSide(side));
    request.insert(QStringLiteral("batchOrderCount"), 1);
    request.insert(QStringLiteral("executionScopeId"), executionScopeId);
    request.insert(QStringLiteral("requiresPreviousBatchFilled"), false);
    request.insert(QStringLiteral("pauseOnConflict"), batchOptions.value(QStringLiteral("pauseOnConflict")).toBool());
    request.insert(QStringLiteral("pauseOnAbnormalReject"), batchOptions.value(QStringLiteral("pauseOnAbnormalReject")).toBool());
    request.insert(QStringLiteral("requiresManualCheckpoint"), false);
    request.insert(QStringLiteral("manualCheckpointBatchIndex"), effectiveManualCheckpointBatchIndex);
    request.insert(QStringLiteral("blocksFollowingBatches"), false);
}

RuntimeOrderSizingResult deriveRuntimeOrderSizing(const QVariantMap& strategy,
                                                  const QVariantMap& evaluation,
                                                  const QVariantMap& riskConfiguration)
{
    RuntimeOrderSizingResult result;

    const QString action = evaluation.value(QStringLiteral("candidateAction")).toString().trimmed().toUpper();
    const QString runtimeOrderSide = evaluation.value(QStringLiteral("runtimeOrderSide")).toString().trimmed().toUpper();
    const QString runtimePositionEffect = evaluation.value(QStringLiteral("runtimePositionEffect")).toString().trimmed().toUpper();
    const QString runtimeOrderMode = evaluation.value(QStringLiteral("runtimeOrderMode")).toString().trimmed().toLower();
    const QString runtimeOrderAction = evaluation.value(QStringLiteral("runtimeOrderAction")).toString().trimmed();
    const QString symbol = evaluation.value(QStringLiteral("symbol")).toString().trimmed().toUpper();
    const double price = evaluation.value(QStringLiteral("latestPrice")).toDouble();
    if (symbol.isEmpty() || price <= 0.0 || (action != QStringLiteral("BUY") && action != QStringLiteral("SELL"))) {
        result.failureReason = QStringLiteral("runtime_order_parameters_invalid");
        result.failureMessage = QStringLiteral("运行时候选信号缺少有效的价格、标的或方向");
        return result;
    }

    if ((runtimeOrderSide != QStringLiteral("BUY") && runtimeOrderSide != QStringLiteral("SELL"))
        || (runtimePositionEffect != QStringLiteral("OPEN") && runtimePositionEffect != QStringLiteral("CLOSE"))
        || runtimeOrderMode.isEmpty()
        || runtimeOrderAction.isEmpty()) {
        result.failureReason = QStringLiteral("runtime_order_semantics_missing");
        result.failureMessage = QStringLiteral("运行时候选信号缺少明确的开平方向合同");
        return result;
    }

    result.side = runtimeOrderSide;
    result.positionEffect = runtimePositionEffect;
    result.mode = runtimeOrderMode;
    result.action = runtimeOrderAction;

    PositionAccountService* positionAccountService = PositionAccountService::instance();
    if (!positionAccountService || !positionAccountService->isInitialized() || !positionAccountService->initialSnapshotLoaded()) {
        result.failureReason = QStringLiteral("position_account_snapshot_unavailable");
        result.failureMessage = QStringLiteral("账户与持仓快照尚未就绪，禁止自动提交运行时委托");
        return result;
    }

    const QVariantMap accountSnapshot = positionAccountService->accountSnapshot();
    const QVariantList positions = positionAccountService->positions();
    const QVariantMap ruleProfile = strategy.value(QStringLiteral("ruleProfileSnapshot")).toMap();
    const QVariantMap executionPolicy = resolvedRuntimeExecutionPolicy(strategy);

    const double orderSizeLimitWan = resolvedNumericLimit(
        executionPolicy,
        riskConfiguration,
        risk::config::orderSizeLimitKeys(),
        100.0);
    const double orderSizeLimitNotional = orderSizeLimitWan > 0.0
        ? orderSizeLimitWan * 10000.0
        : std::numeric_limits<double>::max();

    if (runtimePositionEffect == QStringLiteral("CLOSE")) {
        const qint64 closeableQuantity = closeableQuantityForPosition(positionSnapshotForSymbol(positions, symbol));
        if (closeableQuantity <= 0) {
            result.failureReason = QStringLiteral("no_closeable_position");
            result.failureMessage = runtimeOrderSide == QStringLiteral("SELL")
                ? QStringLiteral("当前无可卖持仓，运行时候选卖出信号不会自动下单")
                : QStringLiteral("当前无可平空持仓，运行时候选买入信号不会自动下单");
            return result;
        }

        qint64 quantity = closeableQuantity;
        const qint64 orderCapQuantity = shareQuantity(orderSizeLimitNotional, price);
        if (orderCapQuantity > 0) {
            quantity = std::min(quantity, orderCapQuantity);
        }

        if (quantity <= 0) {
            result.failureReason = QStringLiteral("order_budget_below_min_quantity");
            result.failureMessage = runtimeOrderSide == QStringLiteral("SELL")
                ? QStringLiteral("当前单笔委托上限不足以形成可执行卖出数量")
                : QStringLiteral("当前单笔委托上限不足以形成可执行平空数量");
            return result;
        }

        result.quantity = quantity;
        result.requestedNotional = price * static_cast<double>(quantity);
        return result;
    }

    const double availableCash = numericParam(
        accountSnapshot,
        {QStringLiteral("availableCash"), QStringLiteral("available_cash")},
        0.0);
    if (availableCash <= 0.0) {
        result.failureReason = QStringLiteral("insufficient_available_cash");
        result.failureMessage = runtimeOrderSide == QStringLiteral("SELL")
            ? QStringLiteral("当前可用资金不足，运行时候选融券卖出信号不会自动下单")
            : QStringLiteral("当前可用资金不足，运行时候选买入信号不会自动下单");
        return result;
    }

    const double totalAsset = totalAssetValue(accountSnapshot);
    const double marketValue = numericParam(
        accountSnapshot,
        {QStringLiteral("marketValue"), QStringLiteral("market_value")},
        0.0);
    const double currentSymbolExposure = symbolMarketValue(positions, symbol);
    const double maxPositionRatio = resolvedRatioLimit(
        ruleProfile,
        riskConfiguration,
        risk::config::maxPositionPercentKeys(),
        0.15);
    const double maxTotalExposureRatio = resolvedRatioLimit(
        ruleProfile,
        riskConfiguration,
        risk::config::maxTotalExposureKeys(),
        0.67);

    const double symbolBudget = totalAsset > 0.0
        ? std::max(0.0, (totalAsset * maxPositionRatio) - currentSymbolExposure)
        : 0.0;
    const double exposureBudget = totalAsset > 0.0
        ? std::max(0.0, (totalAsset * maxTotalExposureRatio) - marketValue)
        : 0.0;
    double budgetNotional = std::min(availableCash, orderSizeLimitNotional);
    if (symbolBudget > 0.0) {
        budgetNotional = std::min(budgetNotional, symbolBudget);
    }
    if (exposureBudget > 0.0) {
        budgetNotional = std::min(budgetNotional, exposureBudget);
    }

    if (budgetNotional <= 0.0) {
        result.failureReason = QStringLiteral("runtime_position_budget_exhausted");
        result.failureMessage = runtimeOrderSide == QStringLiteral("SELL")
            ? QStringLiteral("当前仓位或资金预算已耗尽，运行时候选融券卖出信号不会自动下单")
            : QStringLiteral("当前仓位或资金预算已耗尽，运行时候选买入信号不会自动下单");
        return result;
    }

    const qint64 quantity = boardLotQuantity(budgetNotional, price);
    if (quantity <= 0) {
        result.failureReason = QStringLiteral("order_budget_below_board_lot");
        result.failureMessage = runtimeOrderSide == QStringLiteral("SELL")
            ? QStringLiteral("当前预算不足以形成一手整股融券卖出委托，运行时候选信号不会自动下单")
            : QStringLiteral("当前预算不足以形成一手整股委托，运行时候选买入信号不会自动下单");
        return result;
    }

    result.quantity = quantity;
    result.requestedNotional = price * static_cast<double>(quantity);
    return result;
}

QVariantMap buildRuntimeAutoOrderRequest(const QVariantMap& strategy,
                                         const QVariantMap& evaluation,
                                         const QVariantMap& tradingConfiguration,
                                         const RuntimeOrderSizingResult& sizing)
{
    QVariantMap request;
    const QString trackingOrderId = QString::fromStdString(foundation::utils::Uuid::generate_v4().to_string());
    request.insert(QStringLiteral("orderId"), trackingOrderId);
    request.insert(QStringLiteral("clientOrderId"), trackingOrderId);
    request.insert(QStringLiteral("strategyId"), evaluation.value(QStringLiteral("strategyId")).toString());
    request.insert(QStringLiteral("strategyName"), evaluation.value(QStringLiteral("strategyName")).toString());
    request.insert(QStringLiteral("runtimeStrategyId"),
                   tradingConfiguration.value(QStringLiteral("runtimeStrategyId"),
                                              tradingConfiguration.value(QStringLiteral("gmStrategyId"))).toString().trimmed());
    request.insert(QStringLiteral("symbol"), evaluation.value(QStringLiteral("symbol")).toString().trimmed().toUpper());
    request.insert(QStringLiteral("side"), sizing.side);
    request.insert(QStringLiteral("action"), sizing.action);
    request.insert(QStringLiteral("positionEffect"), sizing.positionEffect);
    request.insert(QStringLiteral("price"), evaluation.value(QStringLiteral("latestPrice")).toDouble());
    request.insert(QStringLiteral("referencePrice"), evaluation.value(QStringLiteral("referencePrice")).toDouble());
    request.insert(QStringLiteral("quantity"), sizing.quantity);
    request.insert(QStringLiteral("requestedNotional"), sizing.requestedNotional);
    request.insert(QStringLiteral("strength"), evaluation.value(QStringLiteral("candidateStrength")).toDouble());
    request.insert(QStringLiteral("orderType"), QStringLiteral("LIMIT"));
    request.insert(QStringLiteral("mode"), sizing.mode);
    request.insert(QStringLiteral("riskActionSource"), QStringLiteral("runtime_rule_candidate"));
    request.insert(QStringLiteral("marketEventType"), evaluation.value(QStringLiteral("marketEventType")).toString());
    request.insert(QStringLiteral("runtimeRuleDecision"), evaluation.value(QStringLiteral("decision")).toString());
    request.insert(QStringLiteral("runtimeRuleGate"), evaluation.value(QStringLiteral("gate")).toString());
    request.insert(QStringLiteral("runtimeRuleReason"), evaluation.value(QStringLiteral("reason")).toString());
    const domain::backtest::ResolvedStrategyBehavior behavior =
        domain::backtest::resolveStrategyBehavior(strategy);
    if (behavior.valid) {
        request.insert(QStringLiteral("strategyBehaviorKind"), behavior.index());
        request.insert(QStringLiteral("strategy_behavior_kind"), behavior.index());
    }
    applyRuntimeExecutionPlanMetadata(request, resolvedRuntimeExecutionPolicy(strategy));
    return request;
}

bool runtimeAutoExecutionConfigured(const QVariantMap& tradingConfiguration)
{
    return tradingConfiguration.value(QStringLiteral("autoExecuteRuntimeCandidates"), false).toBool();
}

QVariantMap currentMarketSessionSnapshot()
{
    TradingMarketCalendarService* calendarService = TradingMarketCalendarService::instance();
    if (!calendarService) {
        return {};
    }

    return calendarService->currentSessionSnapshot();
}

bridge::config::StrategyStructureResolverSet& strategyStructureResolverSet()
{
    static bridge::config::StrategyStructureResolverSet resolverSet;
    return resolverSet;
}

void applyCanonicalStrategyStructures(QVariantMap& strategy);

void applyCanonicalStrategyStructures(QVariantMap& strategy)
{
    if (strategy.isEmpty()) {
        return;
    }

    QVariantMap parameters = strategy.value(QStringLiteral("parameters")).toMap();
    const bridge::config::StrategyStructureResolution resolution = strategyStructureResolverSet().resolve(strategy);

    auto applyStructuredMap = [&](const QString& embeddedKey,
                                 const QString& snapshotKey,
                                 const QVariantMap& values) {
        if (values.isEmpty()) {
            return;
        }

        parameters.insert(embeddedKey, values);
        strategy.insert(snapshotKey, values);
    };

    applyStructuredMap(QStringLiteral("rule_profile"),
                       QStringLiteral("ruleProfileSnapshot"),
                       resolution.ruleProfile);
    applyStructuredMap(QStringLiteral("execution_policy"),
                       QStringLiteral("executionPolicySnapshot"),
                       resolution.executionPolicy);
    applyStructuredMap(QStringLiteral("backtest_assumptions"),
                       QStringLiteral("backtestAssumptionsSnapshot"),
                       resolution.backtestAssumptions);
    applyStructuredMap(QStringLiteral("strategy_scope_context"),
                       QStringLiteral("strategyScopeContextSnapshot"),
                       resolution.strategyScopeContext);
    applyStructuredMap(QStringLiteral("factor_overlay"),
                       QStringLiteral("factorOverlaySnapshot"),
                       resolution.factorOverlay);

    auto syncRuntimeIndex = [&](const QString& key) {
        const int index = strategy.value(key).toInt();
        if (index > 0) {
            parameters.insert(key, index);
        } else {
            parameters.remove(key);
        }
    };

    syncRuntimeIndex(QStringLiteral("assetTypeIndex"));
    syncRuntimeIndex(QStringLiteral("timeFrameIndex"));
    syncRuntimeIndex(QStringLiteral("riskLevelIndex"));

    applyCanonicalStrategyIdentity(strategy);
    strategy.insert(QStringLiteral("parameters"), parameters);
}

bool isConfiguredRuleDefaultValue(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return false;
    }

    if (value.typeId() == QMetaType::QString) {
        return !value.toString().trimmed().isEmpty();
    }

    return true;
}

void mergeMissingRuleDefaults(QVariantMap& target, const QVariantMap& defaults)
{
    for (auto it = defaults.constBegin(); it != defaults.constEnd(); ++it) {
        if (!isConfiguredRuleDefaultValue(it.value())) {
            continue;
        }
        if (!isConfiguredRuleDefaultValue(target.value(it.key()))) {
            target.insert(it.key(), it.value());
        }
    }
}

void applyRuntimeRuleDefaults(QVariantMap& strategy, const QVariantMap& tradingConfiguration)
{
    const QVariantMap runtimeRuleDefaults = tradingConfiguration.value(QStringLiteral("runtimeRuleDefaults")).toMap();
    if (runtimeRuleDefaults.isEmpty()) {
        return;
    }

    QVariantMap parameters = strategy.value(QStringLiteral("parameters")).toMap();

    QVariantMap ruleProfile = strategy.value(QStringLiteral("ruleProfileSnapshot")).toMap();
    mergeMissingRuleDefaults(ruleProfile, runtimeRuleDefaults.value(QStringLiteral("ruleProfile")).toMap());
    if (!ruleProfile.isEmpty()) {
        strategy.insert(QStringLiteral("ruleProfileSnapshot"), ruleProfile);
        parameters.insert(QStringLiteral("rule_profile"), ruleProfile);
    }

    QVariantMap executionPolicy = strategy.value(QStringLiteral("executionPolicySnapshot")).toMap();
    mergeMissingRuleDefaults(executionPolicy, runtimeRuleDefaults.value(QStringLiteral("executionPolicy")).toMap());
    if (!executionPolicy.isEmpty()) {
        strategy.insert(QStringLiteral("executionPolicySnapshot"), executionPolicy);
        parameters.insert(QStringLiteral("execution_policy"), executionPolicy);
    }

    QVariantMap strategyScopeContext = strategy.value(QStringLiteral("strategyScopeContextSnapshot")).toMap();
    mergeMissingRuleDefaults(strategyScopeContext, runtimeRuleDefaults.value(QStringLiteral("strategyScopeContext")).toMap());
    if (!strategyScopeContext.isEmpty()) {
        strategy.insert(QStringLiteral("strategyScopeContextSnapshot"), strategyScopeContext);
        parameters.insert(QStringLiteral("strategy_scope_context"), strategyScopeContext);

        const QVariant executionTimeframe = strategyScopeContext.value(QStringLiteral("executionTimeframe"),
            strategyScopeContext.value(QStringLiteral("execution_timeframe")));
        if (isConfiguredRuleDefaultValue(executionTimeframe)
                && !isConfiguredRuleDefaultValue(parameters.value(QStringLiteral("execution_timeframe")))) {
            parameters.insert(QStringLiteral("execution_timeframe"), executionTimeframe);
        }
    }

    strategy.insert(QStringLiteral("parameters"), parameters);
}

QSet<QString> configuredBoundStrategyIds(const QVariantMap& configuration)
{
    QSet<QString> strategyIds;

    const QVariantList boundStrategies = configuration.value(QStringLiteral("boundStrategies")).toList();
    for (const QVariant& rawEntry : boundStrategies) {
        const QVariantMap entry = rawEntry.toMap();
        const QString strategyId = entry.value(QStringLiteral("strategyId"),
            entry.value(QStringLiteral("strategy_id"), entry.value(QStringLiteral("id")))).toString().trimmed();
        if (!strategyId.isEmpty()) {
            strategyIds.insert(strategyId);
        }
    }

    const QString primaryStrategyId = configuration.value(QStringLiteral("boundStrategyId")).toString().trimmed();
    if (!primaryStrategyId.isEmpty()) {
        strategyIds.insert(primaryStrategyId);
    }

    return strategyIds;
}

QString resolvedStrategyIdentifier(const QVariantMap& strategy)
{
    return firstNonEmptyStrategyText(strategy, {"strategy_id", "strategyId"});
}

bool runtimeSessionIsReady(const QVariantMap& runtimeSessionSnapshot)
{
    return !runtimeSessionSnapshot.isEmpty()
        && runtimeSessionSnapshot.value(QStringLiteral("initialized")).toBool()
        && runtimeSessionSnapshot.value(QStringLiteral("connected")).toBool()
        && runtimeSessionSnapshot.value(QStringLiteral("isRunning")).toBool()
        && !runtimeSessionSnapshot.value(QStringLiteral("hasError")).toBool();
}

QVariantMap effectiveRuntimeSessionSnapshot(const QVariantMap& runtimeSessionSnapshot)
{
    if (runtimeSessionSnapshot.value(QStringLiteral("source")).toString().trimmed()
            == QStringLiteral("default")) {
        return {};
    }
    return runtimeSessionSnapshot;
}

QVariantMap runtimeSessionSnapshotForStrategy(TradingRuntimeStatusService* runtimeStatusService,
                                             const QVariantMap& tradingConfiguration,
                                             const QVariantMap& strategy)
{
    if (!runtimeStatusService) {
        return {};
    }

    const QString strategyId = resolvedStrategyIdentifier(strategy);
    const QString primaryStrategyId = tradingConfiguration.value(QStringLiteral("boundStrategyId")).toString().trimmed();
    const QString runtimeStrategyId = tradingConfiguration.value(QStringLiteral("runtimeStrategyId")).toString().trimmed();
    const QString accountId = tradingConfiguration.value(QStringLiteral("accountId")).toString().trimmed();

    if (!runtimeStrategyId.isEmpty() && strategyId == primaryStrategyId) {
        const QVariantMap runtimeSnapshot = runtimeStatusService->sessionSnapshotForStrategy(runtimeStrategyId);
        if (!runtimeSnapshot.isEmpty()) {
            return effectiveRuntimeSessionSnapshot(runtimeSnapshot);
        }
    }

    if (!strategyId.isEmpty()) {
        const QVariantMap runtimeSnapshot = runtimeStatusService->sessionSnapshotForStrategy(strategyId);
        if (!runtimeSnapshot.isEmpty()) {
            return effectiveRuntimeSessionSnapshot(runtimeSnapshot);
        }
    }

    if (!runtimeStrategyId.isEmpty()) {
        const QVariantMap runtimeSnapshot = runtimeStatusService->sessionSnapshotForStrategy(runtimeStrategyId);
        if (!runtimeSnapshot.isEmpty()) {
            return effectiveRuntimeSessionSnapshot(runtimeSnapshot);
        }
    }

    if (!accountId.isEmpty()) {
        return effectiveRuntimeSessionSnapshot(runtimeStatusService->sessionSnapshotForAccount(accountId));
    }

    return {};
}

bool matchesStrategyStoredType(const QVariantMap& strategy,
                               domain::backtest::StrategyStoredType strategyType)
{
    const domain::backtest::ResolvedStrategyIdentity strategyIdentity =
        domain::backtest::resolveStrategyIdentity(strategy);
    return strategyIdentity.validStoredType && strategyIdentity.storedType == strategyType;
}

bool matchesStrategyStatus(const QVariantMap& strategy, strategy_view::StrategyLifecycleStatus status)
{
    if (!strategy_view::isKnownStrategyLifecycleStatus(status)) {
        return true;
    }

    return strategyStatusFromMap(strategy) == status;
}

bool matchesStrategyKeyword(const QVariantMap& strategy, const QString& keyword)
{
    const QString normalizedKeyword = keyword.trimmed();
    if (normalizedKeyword.isEmpty()) {
        return true;
    }

    const QStringList searchableTexts = {
        firstNonEmptyStrategyText(strategy, {"strategy_name", "strategyName"}),
        firstNonEmptyStrategyText(strategy, {"description"}),
        firstNonEmptyStrategyText(strategy, {"strategy_code", "strategyCode"}),
        firstNonEmptyStrategyText(strategy, {"strategy_type", "strategyType"}),
        firstNonEmptyStrategyText(strategy, {"sub_type", "subType"})
    };

    for (const QString& text : searchableTexts) {
        if (!text.isEmpty() && text.contains(normalizedKeyword, Qt::CaseInsensitive)) {
            return true;
        }
    }

    const QStringList tags = strategyTags(strategy);
    for (const QString& tag : tags) {
        if (tag.contains(normalizedKeyword, Qt::CaseInsensitive)) {
            return true;
        }
    }

    return false;
}

QString eventStringValue(const engine::EventFormat& event, const std::string& key)
{
    const auto metadataIt = event.metadata.find(key);
    if (metadataIt != event.metadata.end()) {
        return QString::fromStdString(metadataIt->second).trimmed();
    }

    auto dataValue = event.get<std::string>(key);
    if (dataValue.has_value()) {
        return QString::fromStdString(*dataValue).trimmed();
    }

    auto dataInt = event.get<int64_t>(key);
    if (dataInt.has_value()) {
        return QString::number(*dataInt);
    }

    auto dataDouble = event.get<double>(key);
    if (dataDouble.has_value()) {
        return QString::number(*dataDouble, 'f', 6);
    }

    return {};
}

double eventNumericValue(const engine::EventFormat& event, const std::string& key, double fallback = 0.0)
{
    auto dataDouble = event.get<double>(key);
    if (dataDouble.has_value()) {
        return *dataDouble;
    }

    auto dataInt = event.get<int64_t>(key);
    if (dataInt.has_value()) {
        return static_cast<double>(*dataInt);
    }

    const QString metadataValue = eventStringValue(event, key);
    if (metadataValue.isEmpty()) {
        return fallback;
    }

    bool ok = false;
    const double numericValue = metadataValue.toDouble(&ok);
    return ok ? numericValue : fallback;
}

QVariantMap buildEventFactSnapshot(const engine::EventFormat& event)
{
    QVariantMap facts;
    for (const auto& entry : event.metadata) {
        facts.insert(QString::fromStdString(entry.first), QString::fromStdString(entry.second));
    }
    return facts;
}

void applyRuntimeRuleTemplateResult(QVariantMap& evaluation,
                                    const bridge::rules::RuntimeRuleTemplateEvaluationResult& templateResult)
{
    if (!templateResult.hasTemplate) {
        return;
    }

    evaluation.insert(QStringLiteral("templateRuleTemplateNamespace"), templateResult.templateNamespace.text());
    evaluation.insert(QStringLiteral("templateRuleFilePath"), templateResult.templateFilePath.text());
    evaluation.insert(QStringLiteral("templateRuleMatched"), templateResult.matched);
    evaluation.insert(QStringLiteral("templateRuleActionPermitted"), templateResult.actionPermitted);
    if (!templateResult.groupDecisions.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRuleGroupDecisions"), templateResult.groupDecisions);
    }
    if (!templateResult.reasonCode.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRuleDecisionReasonCode"), templateResult.reasonCode.text());
    }
    if (!templateResult.message.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRuleDecisionMessage"), templateResult.message);
    }

    const QString groupId = firstConfiguredValue(
        templateResult.binding,
        {QStringLiteral("group_id"), QStringLiteral("groupId")}
    ).toString().trimmed();
    if (!groupId.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRuleGroupId"), groupId);
    }
    const QString groupTitle = firstConfiguredValue(
        templateResult.binding,
        {QStringLiteral("group_title"), QStringLiteral("groupTitle")}
    ).toString().trimmed();
    if (!groupTitle.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRuleGroupTitle"), groupTitle);
    }
    const QString groupRole = firstConfiguredValue(
        templateResult.binding,
        {QStringLiteral("group_role"), QStringLiteral("groupRole")}
    ).toString().trimmed().isEmpty()
        ? serializeRuleGroupRole(firstConfiguredValue(
            templateResult.binding,
            {QStringLiteral("group_role"), QStringLiteral("groupRole")}))
        : serializeRuleGroupRole(firstConfiguredValue(
            templateResult.binding,
            {QStringLiteral("group_role"), QStringLiteral("groupRole")}));
    if (!groupRole.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRuleGroupRole"), groupRole);
    }
    const QString groupOperator = firstConfiguredValue(
        templateResult.binding,
        {QStringLiteral("group_operator"), QStringLiteral("groupOperator")}
    ).toString().trimmed().isEmpty()
        ? serializeRuleGroupOperator(firstConfiguredValue(
            templateResult.binding,
            {QStringLiteral("group_operator"), QStringLiteral("groupOperator")}))
        : serializeRuleGroupOperator(firstConfiguredValue(
            templateResult.binding,
            {QStringLiteral("group_operator"), QStringLiteral("groupOperator")}));
    if (!groupOperator.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRuleGroupOperator"), groupOperator);
    }

    if (!templateResult.matched) {
        return;
    }

    const QString templateRuleStage = serializeRuleTemplateStage(templateResult.stage);
    if (!templateRuleStage.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRuleStage"), templateRuleStage);
    }
    evaluation.insert(QStringLiteral("templateRuleId"), templateResult.ruleId.text());
    evaluation.insert(QStringLiteral("templateRuleReasonCode"), templateResult.reasonCode.text());
    const QString templateRuleResult = serializeRuleTemplateResultType(templateResult.resultType);
    if (!templateRuleResult.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRuleResult"), templateRuleResult);
    }
    if (!templateResult.message.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRuleMessage"), templateResult.message);
    }
    if (!templateResult.payload.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRulePayload"), templateResult.payload);
    }
    if (!templateResult.state.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRuleState"), templateResult.state);
    }

}

QVariantList compiledTemplatesForStrategy(const QVariantMap& strategy, QString* errorMessage = nullptr)
{
    return bridge::rules::loadCompiledRuleTemplates(strategyRuleTemplateBindings(strategy), errorMessage);
}

QString runtimeAutoTrackingOrderId(const QVariantMap& orderRequest)
{
    const QString clientOrderId = orderRequest.value(QStringLiteral("clientOrderId")).toString().trimmed();
    if (!clientOrderId.isEmpty()) {
        return clientOrderId;
    }
    return orderRequest.value(QStringLiteral("orderId")).toString().trimmed();
}

QString runtimeAutoTrackingOrderId(const engine::EventFormat& event)
{
    const QString clientOrderId = eventStringValue(event, "client_order_id");
    if (!clientOrderId.isEmpty()) {
        return clientOrderId;
    }
    return eventStringValue(event, "order_id");
}

QString runtimeAutoExecutionStatus(const QString& orderStatus, const QString& statusOrigin)
{
    const QString normalizedStatus = orderStatus.trimmed().toUpper();
    const QString normalizedOrigin = statusOrigin.trimmed().toLower();
    if (normalizedStatus == QStringLiteral("PENDING_RISK") || normalizedOrigin == QStringLiteral("risk_pending")) {
        return QStringLiteral("risk_pending");
    }
    if (normalizedStatus == QStringLiteral("REJECTED")) {
        if (normalizedOrigin == QStringLiteral("execution_rule_reject")) {
            return QStringLiteral("execution_rule_reject");
        }
        if (normalizedOrigin == QStringLiteral("risk_reject")) {
            return QStringLiteral("risk_rejected");
        }
        if (normalizedOrigin == QStringLiteral("broker_reject")) {
            return QStringLiteral("broker_rejected");
        }
        return QStringLiteral("rejected");
    }
    if (normalizedStatus == QStringLiteral("FILLED")) {
        return QStringLiteral("filled");
    }
    if (normalizedStatus == QStringLiteral("PARTIAL_FILLED")) {
        return QStringLiteral("partial_filled");
    }
    if (normalizedStatus == QStringLiteral("CANCELLED")) {
        return QStringLiteral("cancelled");
    }
    if (normalizedStatus == QStringLiteral("SUBMITTED")) {
        return QStringLiteral("broker_submitted");
    }
    if (normalizedStatus == QStringLiteral("PENDING")) {
        if (normalizedOrigin == QStringLiteral("runtime")) {
            return QStringLiteral("runtime_pending");
        }
        if (normalizedOrigin == QStringLiteral("broker_submit")) {
            return QStringLiteral("broker_pending");
        }
        return QStringLiteral("pending");
    }
    return normalizedStatus.isEmpty() ? QStringLiteral("submitted") : normalizedStatus.toLower();
}

QString runtimeAutoExecutionStage(const QString& statusOrigin)
{
    const QString normalizedOrigin = statusOrigin.trimmed().toLower();
    if (normalizedOrigin == QStringLiteral("risk_pending") || normalizedOrigin == QStringLiteral("risk_reject")) {
        return QStringLiteral("risk");
    }
    if (normalizedOrigin == QStringLiteral("execution_rule_reject")) {
        return QStringLiteral("execution_rule");
    }
    if (normalizedOrigin == QStringLiteral("broker_submit") || normalizedOrigin == QStringLiteral("broker_reject")) {
        return QStringLiteral("broker");
    }
    if (normalizedOrigin == QStringLiteral("runtime")) {
        return QStringLiteral("runtime");
    }
    if (normalizedOrigin == QStringLiteral("fill")) {
        return QStringLiteral("fill");
    }
    if (normalizedOrigin == QStringLiteral("local_pending")) {
        return QStringLiteral("queue");
    }
    return QStringLiteral("submit");
}

bool isRuntimeAutoTerminalStatus(const QString& autoExecutionStatus)
{
    const QString normalizedStatus = autoExecutionStatus.trimmed().toLower();
    return normalizedStatus == QStringLiteral("filled")
        || normalizedStatus == QStringLiteral("cancelled")
        || normalizedStatus == QStringLiteral("rejected")
        || normalizedStatus == QStringLiteral("risk_rejected")
        || normalizedStatus == QStringLiteral("broker_rejected")
        || normalizedStatus == QStringLiteral("execution_rule_reject");
}

void applyRuntimeAutoOrderContext(QVariantMap& evaluation, const QVariantMap& orderRequest)
{
    for (const QString& key : {QStringLiteral("orderId"),
                               QStringLiteral("clientOrderId"),
                               QStringLiteral("executionScopeId"),
                               QStringLiteral("batchId"),
                               QStringLiteral("batchIndex"),
                               QStringLiteral("executionSequence"),
                               QStringLiteral("batchOrderCount"),
                               QStringLiteral("previousBatchId"),
                               QStringLiteral("nextBatchId"),
                               QStringLiteral("requiresPreviousBatchFilled"),
                               QStringLiteral("pauseOnConflict"),
                               QStringLiteral("pauseOnAbnormalReject"),
                               QStringLiteral("requiresManualCheckpoint"),
                               QStringLiteral("manualCheckpointBatchIndex")}) {
        if (!orderRequest.contains(key)) {
            continue;
        }
        evaluation.insert(key, orderRequest.value(key));
    }

    if (orderRequest.contains(QStringLiteral("orderId"))) {
        evaluation.insert(QStringLiteral("autoExecutionOrderId"), orderRequest.value(QStringLiteral("orderId")));
    }
    if (orderRequest.contains(QStringLiteral("clientOrderId"))) {
        evaluation.insert(QStringLiteral("autoExecutionClientOrderId"), orderRequest.value(QStringLiteral("clientOrderId")));
    }
}

QVariantMap buildRuntimeAutoExecutionFollowupEvaluation(const QVariantMap& baseEvaluation,
                                                        const engine::EventFormat& event,
                                                        const QString& eventType)
{
    QVariantMap evaluation = baseEvaluation;
    const QString orderStatus = eventStringValue(event, "status").trimmed().toUpper();
    const QString statusOrigin = eventStringValue(event, "status_origin").trimmed().toLower();
    const QString trackingOrderId = runtimeAutoTrackingOrderId(event);
    const QString brokerOrderId = eventStringValue(event, "broker_order_id");
    const QString message = eventStringValue(event, "message");
    const QString batchId = eventStringValue(event, "batch_id");
    const QString executionScopeId = eventStringValue(event, "execution_scope_id");
    const QString requiredBatchId = eventStringValue(event, "required_batch_id");
    const QString blockingBatchId = eventStringValue(event, "blocking_batch_id");
    const QString ruleId = eventStringValue(event, "rule_id");
    const QString reasonCode = eventStringValue(event, "reason_code");

    evaluation.insert(QStringLiteral("autoExecutionStatus"), runtimeAutoExecutionStatus(orderStatus, statusOrigin));
    evaluation.insert(QStringLiteral("autoExecutionStage"), runtimeAutoExecutionStage(statusOrigin));
    evaluation.insert(QStringLiteral("autoExecutionOrderStatus"), orderStatus);
    evaluation.insert(QStringLiteral("autoExecutionStatusOrigin"), statusOrigin);
    evaluation.insert(QStringLiteral("autoExecutionEventType"), eventType);
    if (!trackingOrderId.isEmpty()) {
        evaluation.insert(QStringLiteral("autoExecutionOrderId"), trackingOrderId);
        evaluation.insert(QStringLiteral("autoExecutionClientOrderId"), trackingOrderId);
    }
    if (!brokerOrderId.isEmpty()) {
        evaluation.insert(QStringLiteral("autoExecutionBrokerOrderId"), brokerOrderId);
    }
    if (!message.isEmpty()) {
        evaluation.insert(QStringLiteral("autoExecutionMessage"), message);
    }
    if (!batchId.isEmpty()) {
        evaluation.insert(QStringLiteral("batchId"), batchId);
    }
    if (!executionScopeId.isEmpty()) {
        evaluation.insert(QStringLiteral("executionScopeId"), executionScopeId);
    }
    if (!requiredBatchId.isEmpty()) {
        evaluation.insert(QStringLiteral("requiredBatchId"), requiredBatchId);
    }
    if (!blockingBatchId.isEmpty()) {
        evaluation.insert(QStringLiteral("blockingBatchId"), blockingBatchId);
    }
    if (!ruleId.isEmpty()) {
        evaluation.insert(QStringLiteral("ruleId"), ruleId);
    }
    if (!reasonCode.isEmpty()) {
        evaluation.insert(QStringLiteral("reasonCode"), reasonCode);
    }

    const double filledQuantity = eventNumericValue(event,
                                                    "filled_quantity",
                                                    eventNumericValue(event, "fill_quantity", 0.0));
    if (filledQuantity > 0.0) {
        evaluation.insert(QStringLiteral("autoExecutionFilledQuantity"), static_cast<qint64>(filledQuantity));
    }
    const double filledNotional = eventNumericValue(event, "filled_notional", 0.0);
    if (filledNotional > 0.0) {
        evaluation.insert(QStringLiteral("autoExecutionFilledNotional"), filledNotional);
    }

    return evaluation;
}

QString defaultTemplateDisplayName(domain::backtest::StrategyBehaviorKind behaviorKind)
{
    switch (behaviorKind) {
    case domain::backtest::StrategyBehaviorKind::TrendFollowing:
        return QStringLiteral("趋势跟踪");
    case domain::backtest::StrategyBehaviorKind::MeanReversion:
        return QStringLiteral("均值回归");
    case domain::backtest::StrategyBehaviorKind::Momentum:
        return QStringLiteral("动量");
    case domain::backtest::StrategyBehaviorKind::Arbitrage:
        return QStringLiteral("套利");
    case domain::backtest::StrategyBehaviorKind::MultiFactor:
        return QStringLiteral("多因子");
    case domain::backtest::StrategyBehaviorKind::MachineLearning:
        return QStringLiteral("机器学习");
    case domain::backtest::StrategyBehaviorKind::EventDriven:
        return QStringLiteral("事件驱动");
    case domain::backtest::StrategyBehaviorKind::HighFrequency:
        return QStringLiteral("高频");
    case domain::backtest::StrategyBehaviorKind::Custom:
    default:
        return QStringLiteral("自定义");
    }
}

QString templateDescriptionForBehaviorKind(domain::backtest::StrategyBehaviorKind behaviorKind)
{
    switch (behaviorKind) {
    case domain::backtest::StrategyBehaviorKind::TrendFollowing:
        return QStringLiteral("趋势跟踪策略 - 跟随市场趋势进行交易");
    case domain::backtest::StrategyBehaviorKind::MeanReversion:
        return QStringLiteral("均值回归策略 - 假设价格会回归均值水平");
    case domain::backtest::StrategyBehaviorKind::Momentum:
    case domain::backtest::StrategyBehaviorKind::MultiFactor:
    case domain::backtest::StrategyBehaviorKind::MachineLearning:
    case domain::backtest::StrategyBehaviorKind::EventDriven:
    case domain::backtest::StrategyBehaviorKind::HighFrequency:
        return QStringLiteral("阿尔法策略 - 寻找超越市场的超额收益");
    case domain::backtest::StrategyBehaviorKind::Arbitrage:
        return QStringLiteral("套利策略 - 利用市场定价差异获取无风险收益");
    case domain::backtest::StrategyBehaviorKind::Custom:
    default:
        return QStringLiteral("自定义策略 - 用户自定义的交易逻辑");
    }
}

bool strategyStatusAllowsSignals(const QVariantMap& strategy)
{
    return assembleStrategyAggregate(strategy).lifecycle.allowsSignalEmission();
}

void refreshViewModelFromCache(StrategyViewModel* viewModel,
                               const QHash<QString, QVariantMap>& memoryCache)
{
    if (!viewModel) {
        return;
    }

    viewModel->updateData(buildStrategyListFromCache(memoryCache));
}

QString defaultTemplatePositionSizingMethod(domain::backtest::StrategyBehaviorKind behaviorKind)
{
    switch (behaviorKind) {
    case domain::backtest::StrategyBehaviorKind::Momentum:
    case domain::backtest::StrategyBehaviorKind::MultiFactor:
    case domain::backtest::StrategyBehaviorKind::MachineLearning:
        return QStringLiteral("equal_weight");
    case domain::backtest::StrategyBehaviorKind::Arbitrage:
        return QStringLiteral("spread_neutral");
    case domain::backtest::StrategyBehaviorKind::Custom:
    case domain::backtest::StrategyBehaviorKind::EventDriven:
    case domain::backtest::StrategyBehaviorKind::HighFrequency:
        return QStringLiteral("discretionary");
    case domain::backtest::StrategyBehaviorKind::TrendFollowing:
    case domain::backtest::StrategyBehaviorKind::MeanReversion:
    default:
        return QStringLiteral("fixed_fraction");
    }
}

void applyRuleNativeTemplateDefaults(QVariantMap& strategy,
                                     domain::backtest::StrategyBehaviorKind behaviorKind,
                                     const QString& strategyName)
{
    QVariantMap parameters = strategy.value(QStringLiteral("parameters")).toMap();

    QVariantMap ruleProfile = parameters.value(QStringLiteral("rule_profile")).toMap();
    if (parameters.contains(QStringLiteral("stop_loss"))) {
        risk::config::setStopLossPercent(ruleProfile, parameters.value(QStringLiteral("stop_loss")).toDouble());
    }
    if (parameters.contains(QStringLiteral("take_profit"))) {
        risk::config::setTakeProfitPercent(ruleProfile, parameters.value(QStringLiteral("take_profit")).toDouble());
    }

    QVariantMap executionPolicy = parameters.value(QStringLiteral("execution_policy")).toMap();
    risk::config::setPositionSizingMethod(
        executionPolicy,
        risk::config::positionSizingMethod(executionPolicy, defaultTemplatePositionSizingMethod(behaviorKind)));
    if (parameters.contains(QStringLiteral("rebalance_days"))) {
        risk::config::setRebalanceDays(executionPolicy, parameters.value(QStringLiteral("rebalance_days")).toInt());
    }

    QVariantMap backtestAssumptions = parameters.value(QStringLiteral("backtest_assumptions")).toMap();
    backtestAssumptions.insert(QStringLiteral("initialCapital"),
                               backtestAssumptions.value(QStringLiteral("initialCapital"), kDefaultTemplateInitialCapital));
    risk::config::setCommissionRate(
        backtestAssumptions,
        risk::config::commissionRate(backtestAssumptions, kDefaultTemplateCommissionRate));
    risk::config::setSlippageRate(
        backtestAssumptions,
        risk::config::slippageRate(backtestAssumptions, kDefaultTemplateSlippageRate));

    QVariantMap strategyScopeContext = parameters.value(QStringLiteral("strategy_scope_context")).toMap();
    parameters.insert(QStringLiteral("strategyBehaviorKind"), static_cast<int>(behaviorKind));
    strategyScopeContext.insert(QStringLiteral("selectedStrategyName"), strategyName);
    strategyScopeContext.insert(QStringLiteral("strategyBehaviorKind"), static_cast<int>(behaviorKind));

    parameters.insert(QStringLiteral("rule_profile"), ruleProfile);
    parameters.insert(QStringLiteral("execution_policy"), executionPolicy);
    parameters.insert(QStringLiteral("backtest_assumptions"), backtestAssumptions);
    parameters.insert(QStringLiteral("strategy_scope_context"), strategyScopeContext);
    strategy.insert(QStringLiteral("parameters"), parameters);
}

} // namespace

StrategyService* StrategyService::m_instance = nullptr;
QMutex StrategyService::m_instanceMutex;

StrategyService* StrategyService::instance() {
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        m_instance = new StrategyService(qApp);
    }
    return m_instance;
}

StrategyService::StrategyService(QObject* parent) 
    : QObject(parent)
    , m_initialized(false)
    , m_isLoading(false)
    , m_cacheLoaded(false)
    , m_autoInitialize(true)
    , m_eventBusIntegrated(false)
    , m_viewModel(new StrategyViewModel(this)) {
    // 连接信号到视图模型
    connect(this, &StrategyService::strategiesLoaded, this, [this](const QVariantList& strategies) {
        if (m_viewModel) {
            m_viewModel->updateData(strategies);
        } else {
            qWarning() << "StrategyService: 视图模型为空，无法更新数据";
        }
    });
}

StrategyService::~StrategyService() {
    clearAllCache();
}

void StrategyService::initialize() {
    QMutexLocker locker(&m_initMutex);
    
    if (m_initialized.load()) {
        return;
    }
    
    if (m_isLoading.load()) {
        return;
    }
    
    m_isLoading = true;
    emit isLoadingChanged();
    
    try {
        // 初始化仓储
        initializeRepository();
        
        // 加载缓存
        loadStrategiesFromDatabase();

        initializeEventBusIntegration();
        
        m_initialized = true;
        m_cacheLoaded = true;
        m_isLoading = false;
        
        emit initializedChanged();
        emit isLoadingChanged();
        emit cacheLoadedChanged();
    } catch (const std::exception& e) {
        m_isLoading = false;
        emit isLoadingChanged();
        qWarning() << "StrategyService: 初始化失败 -" << e.what();
        emit errorOccurred(QString("初始化失败: %1").arg(e.what()));
    }
}

void StrategyService::initializeAsync() {
    {
        QMutexLocker locker(&m_initMutex);
        if (m_initialized.load() || m_isLoading.load()) {
            return;
        }
        m_isLoading = true;
    }

    emit isLoadingChanged();

    QPointer<StrategyService> safeService(this);
    std::thread([safeService]() {
        QVariantList strategies;
        QHash<QString, QVariantMap> loadedCache;
        std::shared_ptr<astock::database::IStrategyRepository> repository;
        QString errorMessage;

        try {
            auto& dbManager = astock::database::DatabaseConnectionManager::instance();
            if (!dbManager.initialize()) {
                throw std::runtime_error("数据库连接初始化失败");
            }

            repository = std::make_shared<StrategyRepository>();
            if (!repository->initialize()) {
                throw std::runtime_error("策略仓储初始化失败");
            }

            const auto strategyRecords = repository->findAll();
            for (const auto& strategy : strategyRecords) {
                QVariantMap normalizedStrategy = strategy.toVariantMap();
                applyCanonicalStrategyStructures(normalizedStrategy);
                const QString strategyId = normalizedStrategy.value("strategy_id").toString();
                if (!strategyId.isEmpty()) {
                    loadedCache[strategyId] = normalizedStrategy;
                }
                strategies.append(normalizedStrategy);
            }
        } catch (const std::exception& e) {
            errorMessage = QString::fromUtf8(e.what());
        }

        if (!safeService) {
            return;
        }

        QMetaObject::invokeMethod(safeService.data(),
            [safeService, repository, loadedCache, strategies, errorMessage]() {
                if (!safeService) {
                    return;
                }

                if (!errorMessage.isEmpty()) {
                    {
                        QMutexLocker locker(&safeService->m_initMutex);
                        safeService->m_isLoading = false;
                    }
                    emit safeService->isLoadingChanged();
                    qWarning() << "StrategyService: 异步初始化失败 -" << errorMessage;
                    emit safeService->errorOccurred(QString("初始化失败: %1").arg(errorMessage));
                    return;
                }

                {
                    QMutexLocker locker(&safeService->m_initMutex);
                    if (safeService->m_initialized.load()) {
                        safeService->m_isLoading = false;
                        emit safeService->isLoadingChanged();
                        return;
                    }

                    safeService->m_repository = repository;
                    safeService->m_memoryCache = loadedCache;
                    safeService->initializeEventBusIntegration();
                    safeService->m_initialized = true;
                    safeService->m_cacheLoaded = true;
                    safeService->m_isLoading = false;
                }

                emit safeService->strategiesLoaded(strategies);
                emit safeService->initializedChanged();
                emit safeService->isLoadingChanged();
                emit safeService->cacheLoadedChanged();
            },
            Qt::QueuedConnection);
    }).detach();
}

bool StrategyService::publishSyntheticMarketEvent(const QVariantMap& marketEvent)
{
    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        qWarning() << "StrategyService: EventBus unavailable, synthetic market event not published";
        return false;
    }

    const QString symbol = marketEvent.value("symbol").toString().trimmed();
    const double price = marketEvent.value("price").toDouble();
    if (symbol.isEmpty() || price <= 0.0) {
        qWarning() << "StrategyService: invalid synthetic market event" << marketEvent;
        return false;
    }

    engine::EventFormat event = engine::EventFormat::create_from_strings(
        engine::EventTypes::MARKET_TICK,
        "APP_SYNTHETIC_MARKET",
        0);
    event.set("symbol", symbol.toStdString());
    event.set("price", price);
    event.metadata["symbol"] = symbol.toStdString();
    event.metadata["price"] = QString::number(price, 'f', 6).toStdString();
    event.metadata["source"] = "StrategyService.publishSyntheticMarketEvent";

    const auto result = bus->publish(event, static_cast<int>(engine::EventPriority::NORMAL));
    if (!result) {
        qWarning() << "StrategyService: failed to publish synthetic market event" << QString::fromStdString(result.message);
        return false;
    }
    return true;
}

void StrategyService::initializeEventBusIntegration()
{
    QMutexLocker locker(&m_eventBusMutex);
    if (m_eventBusIntegrated.load()) {
        return;
    }

    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        qWarning() << "StrategyService: EventBus not ready, skip event integration";
        return;
    }

    m_marketTickSubscription = bus->subscribe(engine::EventTypes::MARKET_TICK,
        [this](const engine::EventFormat& event) {
            handleMarketEvent(event, QStringLiteral("market.tick"));
        });

    m_marketBarSubscription = bus->subscribe(engine::EventTypes::MARKET_BAR,
        [this](const engine::EventFormat& event) {
            handleMarketEvent(event, QStringLiteral("market.bar"));
        });

    m_tradingMarketTickSubscription = bus->subscribe(engine::EventTypes::TRADING_MARKET_TICK,
        [this](const engine::EventFormat& event) {
            handleMarketEvent(event, QStringLiteral("trading.market.tick"));
        });

    m_tradingMarketBarSubscription = bus->subscribe(engine::EventTypes::TRADING_MARKET_BAR,
        [this](const engine::EventFormat& event) {
            handleMarketEvent(event, QStringLiteral("trading.market.bar"));
        });

    m_tradingOrderUpdatedSubscription = bus->subscribe(engine::EventTypes::TRADING_ORDER_UPDATED,
        [this](const engine::EventFormat& event) {
            handleRuntimeAutoExecutionEvent(event, QStringLiteral("trading.order.updated"));
        });

    m_orderFillSubscription = bus->subscribe(engine::EventTypes::ORDER_FILL,
        [this](const engine::EventFormat& event) {
            handleRuntimeAutoExecutionEvent(event, QStringLiteral("order.fill"));
        });

    m_eventBusIntegrated = true;
    qDebug() << "StrategyService: EventBus integration initialized";
}

void StrategyService::trackRuntimeAutoExecution(const QVariantMap& orderRequest,
                                               const QVariantMap& evaluation)
{
    const QString trackingOrderId = runtimeAutoTrackingOrderId(orderRequest);
    if (trackingOrderId.isEmpty()) {
        return;
    }

    QVariantMap trackedEvaluation = evaluation;
    applyRuntimeAutoOrderContext(trackedEvaluation, orderRequest);

    QMutexLocker locker(&m_runtimeAutoExecutionMutex);
    m_runtimeAutoExecutionTracking.insert(trackingOrderId, trackedEvaluation);
    m_pendingRuntimeAutoExecutionUpdates.remove(trackingOrderId);
    m_runtimeAutoExecutionCommitted.remove(trackingOrderId);
}

void StrategyService::finalizeRuntimeAutoExecutionSubmission(const QString& trackingOrderId)
{
    QVariantMap pendingEvaluation;
    bool shouldEmitPendingEvaluation = false;
    bool shouldDiscardTracking = false;
    {
        QMutexLocker locker(&m_runtimeAutoExecutionMutex);
        if (!m_runtimeAutoExecutionTracking.contains(trackingOrderId)) {
            return;
        }

        m_runtimeAutoExecutionCommitted.insert(trackingOrderId);
        if (m_pendingRuntimeAutoExecutionUpdates.contains(trackingOrderId)) {
            pendingEvaluation = m_pendingRuntimeAutoExecutionUpdates.take(trackingOrderId);
            shouldEmitPendingEvaluation = true;
            if (isRuntimeAutoTerminalStatus(pendingEvaluation.value(QStringLiteral("autoExecutionStatus")).toString())) {
                m_runtimeAutoExecutionTracking.remove(trackingOrderId);
                m_runtimeAutoExecutionCommitted.remove(trackingOrderId);
                shouldDiscardTracking = true;
            } else {
                m_runtimeAutoExecutionTracking.insert(trackingOrderId, pendingEvaluation);
            }
        }
    }

    if (shouldEmitPendingEvaluation) {
        emit strategyRuntimeRuleEvaluated(pendingEvaluation);
    }

    if (shouldDiscardTracking) {
        discardRuntimeAutoExecutionTracking(trackingOrderId);
    }
}

void StrategyService::discardRuntimeAutoExecutionTracking(const QString& trackingOrderId)
{
    if (trackingOrderId.isEmpty()) {
        return;
    }

    QMutexLocker locker(&m_runtimeAutoExecutionMutex);
    m_runtimeAutoExecutionTracking.remove(trackingOrderId);
    m_pendingRuntimeAutoExecutionUpdates.remove(trackingOrderId);
    m_runtimeAutoExecutionCommitted.remove(trackingOrderId);
}

void StrategyService::handleRuntimeAutoExecutionEvent(const engine::EventFormat& event,
                                                      const QString& eventType)
{
    const QString trackingOrderId = runtimeAutoTrackingOrderId(event);
    if (trackingOrderId.isEmpty()) {
        return;
    }

    QVariantMap followupEvaluation;
    bool shouldEmit = false;
    bool shouldDiscard = false;
    {
        QMutexLocker locker(&m_runtimeAutoExecutionMutex);
        const auto trackedIt = m_runtimeAutoExecutionTracking.constFind(trackingOrderId);
        if (trackedIt == m_runtimeAutoExecutionTracking.constEnd()) {
            return;
        }

        followupEvaluation = buildRuntimeAutoExecutionFollowupEvaluation(trackedIt.value(), event, eventType);
        if (!m_runtimeAutoExecutionCommitted.contains(trackingOrderId)) {
            m_pendingRuntimeAutoExecutionUpdates.insert(trackingOrderId, followupEvaluation);
            return;
        }

        shouldEmit = true;
        if (isRuntimeAutoTerminalStatus(followupEvaluation.value(QStringLiteral("autoExecutionStatus")).toString())) {
            m_runtimeAutoExecutionTracking.remove(trackingOrderId);
            m_pendingRuntimeAutoExecutionUpdates.remove(trackingOrderId);
            m_runtimeAutoExecutionCommitted.remove(trackingOrderId);
            shouldDiscard = true;
        } else {
            m_runtimeAutoExecutionTracking.insert(trackingOrderId, followupEvaluation);
        }
    }

    if (shouldEmit) {
        emit strategyRuntimeRuleEvaluated(followupEvaluation);
    }

    if (shouldDiscard) {
        discardRuntimeAutoExecutionTracking(trackingOrderId);
    }
}

QVariantMap StrategyService::cachedTradingConfigurationSnapshot(qint64 nowMs)
{
    const QVariantMap snapshot = loadTradingConfigurationSnapshot();
    QMutexLocker locker(&m_marketStateMutex);
    m_cachedTradingConfiguration = snapshot;
    m_cachedTradingConfigurationAtMs = nowMs;
    return m_cachedTradingConfiguration;
}

QVariantMap StrategyService::cachedRiskConfigurationSnapshot(qint64 nowMs)
{
    {
        QMutexLocker locker(&m_marketStateMutex);
        if (!m_cachedRiskConfiguration.isEmpty()
            && nowMs - m_cachedRiskConfigurationAtMs <= kRiskConfigurationCacheTtlMs) {
            return m_cachedRiskConfiguration;
        }
    }

    const QVariantMap snapshot = loadRiskConfigurationSnapshot();
    QMutexLocker locker(&m_marketStateMutex);
    m_cachedRiskConfiguration = snapshot;
    m_cachedRiskConfigurationAtMs = nowMs;
    return m_cachedRiskConfiguration;
}

QVariantMap StrategyService::cachedMarketSessionSnapshot(qint64 nowMs)
{
    {
        QMutexLocker locker(&m_marketStateMutex);
        if (!m_cachedMarketSessionSnapshot.isEmpty()
            && nowMs - m_cachedMarketSessionSnapshotAtMs <= kMarketSessionCacheTtlMs) {
            return m_cachedMarketSessionSnapshot;
        }
    }

    const QVariantMap snapshot = currentMarketSessionSnapshot();
    QMutexLocker locker(&m_marketStateMutex);
    m_cachedMarketSessionSnapshot = snapshot;
    m_cachedMarketSessionSnapshotAtMs = nowMs;
    return m_cachedMarketSessionSnapshot;
}

void StrategyService::handleMarketEvent(const engine::EventFormat& event, const QString& marketEventType)
{
    const QString symbol = eventStringValue(event, "symbol");
    if (symbol.isEmpty()) {
        return;
    }

    double referencePrice = eventNumericValue(event, "open", 0.0);
    const double closePrice = eventNumericValue(event, "close", 0.0);
    const double tickPrice = eventNumericValue(event, "price", 0.0);
    const double latestPrice = closePrice > 0.0 ? closePrice : tickPrice;

    if (latestPrice <= 0.0) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (referencePrice <= 0.0) {
        QMutexLocker locker(&m_marketStateMutex);
        referencePrice = m_latestMarketPriceBySymbol.value(symbol, 0.0);
    }

    {
        QMutexLocker locker(&m_marketStateMutex);
        m_latestMarketPriceBySymbol.insert(symbol, latestPrice);
    }

    if (referencePrice <= 0.0) {
        return;
    }

    const QVariantMap tradingConfiguration = cachedTradingConfigurationSnapshot(nowMs);
    const bool tradingConfigured = tradingConfiguration.value(QStringLiteral("enabled")).toBool();
    const bool liveTradingEnabled = tradingConfigured
        && !tradingConfiguration.value(QStringLiteral("readOnly"), true).toBool();
    const bool autoExecutionEnabled = liveTradingEnabled && runtimeAutoExecutionConfigured(tradingConfiguration);
    if (!tradingConfigured) {
        return;
    }

    const QSet<QString> allowedStrategyIds = configuredBoundStrategyIds(tradingConfiguration);
    QVariantList candidateStrategies;
    {
        QReadLocker locker(&m_rwLock);
        for (auto it = m_memoryCache.constBegin(); it != m_memoryCache.constEnd(); ++it) {
            const QVariantMap strategy = it.value();
            if (!strategyStatusAllowsSignals(strategy)) {
                continue;
            }

            const QString strategyId = resolvedStrategyIdentifier(strategy);
            if (strategyId.isEmpty()) {
                continue;
            }

            if (!allowedStrategyIds.isEmpty() && !allowedStrategyIds.contains(strategyId)) {
                continue;
            }

            candidateStrategies.append(strategy);
        }
    }

    if (candidateStrategies.isEmpty()) {
        return;
    }

    const QVariantMap riskConfiguration = autoExecutionEnabled
        ? cachedRiskConfigurationSnapshot(nowMs)
        : QVariantMap{};

    TradingRuntimeStatusService* runtimeStatusService = TradingRuntimeStatusService::instance();

    const QVariantMap marketSessionSnapshot = cachedMarketSessionSnapshot(nowMs);
    const bool marketSessionKnown = !marketSessionSnapshot.isEmpty();
    const bool marketSessionOpen = !marketSessionKnown
        || marketSessionSnapshot.value(QStringLiteral("sessionOpen")).toBool();
    const QVariantMap eventFacts = buildEventFactSnapshot(event);

    const StrategyRuntimeRuleEvaluator evaluator;
    StrategyRuntimeRuleEvaluator::MarketContext marketContext;
    marketContext.symbol = symbol;
    marketContext.latestPrice = latestPrice;
    marketContext.referencePrice = referencePrice;
    marketContext.marketEventType = marketEventType;
    marketContext.liveTradingEnabled = liveTradingEnabled;
    marketContext.marketSessionKnown = marketSessionKnown;
    marketContext.marketSessionOpen = marketSessionOpen;
    marketContext.marketSessionSnapshot = marketSessionSnapshot;
    marketContext.tradingConfiguration = tradingConfiguration;
    if (PositionAccountService* positionAccountService = PositionAccountService::instance();
        positionAccountService && positionAccountService->isInitialized() && positionAccountService->initialSnapshotLoaded()) {
        marketContext.positionSnapshot = positionSnapshotForSymbol(positionAccountService->positions(), symbol);
    }

    for (const QVariant& rawStrategy : candidateStrategies) {
        QVariantMap strategy = rawStrategy.toMap();
        applyRuntimeRuleDefaults(strategy, tradingConfiguration);
        StrategyRuntimeRuleEvaluator::MarketContext strategyContext = marketContext;
        strategyContext.marketEnvironmentProfile = resolveStrategyMarketEnvironmentProfile(strategy);
        strategyContext.runtimeSessionSnapshot = runtimeSessionSnapshotForStrategy(runtimeStatusService,
                                                                                  tradingConfiguration,
                                                                                  strategy);
        strategyContext.runtimeSessionKnown = !strategyContext.runtimeSessionSnapshot.isEmpty();
        strategyContext.runtimeSessionReady = runtimeSessionIsReady(strategyContext.runtimeSessionSnapshot);

        QVariantMap evaluation = evaluator.evaluateMarketCandidate(strategy, strategyContext);
        evaluation.insert(QStringLiteral("autoExecutionEnabled"), autoExecutionEnabled);

        if (evaluation.value(QStringLiteral("decision")).toString() != QStringLiteral("candidate_ready")) {
            emit strategyRuntimeRuleEvaluated(evaluation);
            continue;
        }

        QString templateError;
        const QVariantList compiledTemplates = compiledTemplatesForStrategy(strategy, &templateError);
        if (!compiledTemplates.isEmpty()) {
            bridge::rules::RuntimeRuleTemplateEvaluationContext templateContext;
            templateContext.symbol = domain::strategy::SymbolCode(symbol);
            templateContext.latestPrice = latestPrice;
            templateContext.referencePrice = referencePrice;
            templateContext.marketEventType = domain::strategy::MarketEventTypeId(marketEventType);
            templateContext.candidateAction = parseRuntimeRuleCandidateAction(
                evaluation.value(QStringLiteral("candidateAction")).toString());
            templateContext.candidateStrength = evaluation.value(QStringLiteral("candidateStrength")).toDouble();
            templateContext.strategy = strategy;
            templateContext.flatEventFacts = eventFacts;
            templateContext.marketSessionSnapshot = marketSessionSnapshot;
            templateContext.runtimeSessionSnapshot = strategyContext.runtimeSessionSnapshot;

            const bridge::rules::RuntimeRuleTemplateEvaluationResult templateResult =
                bridge::rules::evaluateRuleTemplates(compiledTemplates, templateContext);
            applyRuntimeRuleTemplateResult(evaluation, templateResult);
            if (!templateResult.actionPermitted) {
                evaluation.insert(QStringLiteral("decision"), QStringLiteral("blocked"));
                evaluation.insert(QStringLiteral("gate"), QStringLiteral("rule_template"));
                evaluation.insert(QStringLiteral("reason"),
                    templateResult.reasonCode.isEmpty()
                        ? QStringLiteral("runtime_rule_template_blocked")
                        : templateResult.reasonCode.text());
                evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("blocked"));
                evaluation.insert(QStringLiteral("autoExecutionStatus"), QStringLiteral("blocked"));
                if (!templateResult.message.isEmpty()) {
                    evaluation.insert(QStringLiteral("autoExecutionMessage"), templateResult.message);
                }
                emit strategyRuntimeRuleEvaluated(evaluation);
                continue;
            }
        } else if (!templateError.isEmpty()) {
            evaluation.insert(QStringLiteral("decision"), QStringLiteral("blocked"));
            evaluation.insert(QStringLiteral("gate"), QStringLiteral("rule_template"));
            evaluation.insert(QStringLiteral("reason"), QStringLiteral("runtime_rule_template_invalid"));
            evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("blocked"));
            evaluation.insert(QStringLiteral("autoExecutionStatus"), QStringLiteral("blocked"));
            evaluation.insert(QStringLiteral("autoExecutionMessage"), templateError);
            emit strategyRuntimeRuleEvaluated(evaluation);
            continue;
        }

        const QString strategyId = evaluation.value(QStringLiteral("strategyId")).toString();
        const QString action = evaluation.value(QStringLiteral("candidateAction")).toString();
        if (!shouldPublishStrategySignal(strategyId, symbol, action)) {
            evaluation.insert(QStringLiteral("decision"), QStringLiteral("suppressed"));
            evaluation.insert(QStringLiteral("gate"), QStringLiteral("execution"));
            evaluation.insert(QStringLiteral("reason"), QStringLiteral("duplicate_action_cooldown"));
            evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("suppressed"));
            emit strategyRuntimeRuleEvaluated(evaluation);
            continue;
        }

        if (!autoExecutionEnabled) {
            evaluation.insert(QStringLiteral("reason"), QStringLiteral("auto_execution_disabled"));
            evaluation.insert(QStringLiteral("autoExecutionStatus"), QStringLiteral("disabled"));
            emit strategyRuntimeRuleEvaluated(evaluation);
            continue;
        }

        const RuntimeOrderSizingResult sizing = deriveRuntimeOrderSizing(strategy, evaluation, riskConfiguration);
        if (sizing.quantity <= 0) {
            evaluation.insert(QStringLiteral("decision"), QStringLiteral("blocked"));
            evaluation.insert(QStringLiteral("gate"), QStringLiteral("execution"));
            evaluation.insert(QStringLiteral("reason"), sizing.failureReason.isEmpty()
                ? QStringLiteral("runtime_order_quantity_unavailable")
                : sizing.failureReason);
            evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("blocked"));
            evaluation.insert(QStringLiteral("autoExecutionStatus"), QStringLiteral("blocked"));
            if (!sizing.failureMessage.isEmpty()) {
                evaluation.insert(QStringLiteral("autoExecutionMessage"), sizing.failureMessage);
            }
            emit strategyRuntimeRuleEvaluated(evaluation);
            continue;
        }

        TradeExecutionService* tradeExecutionService = TradeExecutionService::instance();
        if (!tradeExecutionService) {
            evaluation.insert(QStringLiteral("decision"), QStringLiteral("blocked"));
            evaluation.insert(QStringLiteral("gate"), QStringLiteral("execution"));
            evaluation.insert(QStringLiteral("reason"), QStringLiteral("trade_execution_service_unavailable"));
            evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("blocked"));
            evaluation.insert(QStringLiteral("autoExecutionStatus"), QStringLiteral("blocked"));
            evaluation.insert(QStringLiteral("autoExecutionMessage"), QStringLiteral("执行服务未就绪，运行时候选信号未提交"));
            emit strategyRuntimeRuleEvaluated(evaluation);
            continue;
        }

        const QVariantMap orderRequest = buildRuntimeAutoOrderRequest(strategy, evaluation, tradingConfiguration, sizing);
        const QString runtimeTrackingOrderId = runtimeAutoTrackingOrderId(orderRequest);
        trackRuntimeAutoExecution(orderRequest, evaluation);
        if (!tradeExecutionService->submitBridgeOrder(orderRequest)) {
            discardRuntimeAutoExecutionTracking(runtimeTrackingOrderId);
            evaluation.insert(QStringLiteral("decision"), QStringLiteral("blocked"));
            evaluation.insert(QStringLiteral("gate"), QStringLiteral("execution"));
            evaluation.insert(QStringLiteral("reason"), QStringLiteral("runtime_auto_submit_failed"));
            evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("blocked"));
            evaluation.insert(QStringLiteral("autoExecutionStatus"), QStringLiteral("rejected"));
            const QString errorMessage = tradeExecutionService->lastErrorMessage().trimmed();
            if (!errorMessage.isEmpty()) {
                evaluation.insert(QStringLiteral("autoExecutionMessage"), errorMessage);
            }
            emit strategyRuntimeRuleEvaluated(evaluation);
            continue;
        }

        evaluation.insert(QStringLiteral("reason"), QStringLiteral("submitted_for_risk_review"));
        evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("submitted"));
        evaluation.insert(QStringLiteral("autoExecutionStatus"), QStringLiteral("submitted"));
        evaluation.insert(QStringLiteral("autoExecutionQuantity"), sizing.quantity);
        evaluation.insert(QStringLiteral("autoExecutionRequestedNotional"), sizing.requestedNotional);
        applyRuntimeAutoOrderContext(evaluation, orderRequest);

        emit strategyRuntimeRuleEvaluated(evaluation);
        finalizeRuntimeAutoExecutionSubmission(runtimeTrackingOrderId);
    }

    return;
}

void StrategyService::publishStrategySignalForMarket(const QVariantMap& strategy,
                                                    const QString& symbol,
                                                    double latestPrice,
                                                    double referencePrice,
                                                    const QString& marketEventType,
                                                    const QString& eventId)
{
    const QString action = determineSignalAction(strategy, latestPrice, referencePrice);
    if (action.isEmpty()) {
        return;
    }

    const QString strategyId = strategy.value("strategy_id").toString();
    if (!shouldPublishStrategySignal(strategyId, symbol, action)) {
        return;
    }

    const double strength = determineSignalStrength(strategy, latestPrice, referencePrice);
    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        return;
    }

    const QString strategyName = strategy.value("strategy_name").toString();
    engine::EventFormat signalEvent = engine::EventFormat::create_from_strings(
        engine::EventTypes::STRATEGY_SIGNAL,
        "STRATEGY_SERVICE",
        0);
    signalEvent.correlation_id = eventId.toStdString();
    signalEvent.set("strategy_id", strategyId.toStdString());
    signalEvent.set("strategy_name", strategyName.toStdString());
    signalEvent.set("symbol", symbol.toStdString());
    signalEvent.set("action", action.toStdString());
    signalEvent.set("price", latestPrice);
    signalEvent.set("reference_price", referencePrice);
    signalEvent.set("strength", strength);
    signalEvent.metadata["strategy_id"] = strategyId.toStdString();
    signalEvent.metadata["strategy_name"] = strategyName.toStdString();
    signalEvent.metadata["symbol"] = symbol.toStdString();
    signalEvent.metadata["action"] = action.toStdString();
    signalEvent.metadata["price"] = QString::number(latestPrice, 'f', 6).toStdString();
    signalEvent.metadata["reference_price"] = QString::number(referencePrice, 'f', 6).toStdString();
    signalEvent.metadata["strength"] = QString::number(strength, 'f', 6).toStdString();
    signalEvent.metadata["market_event_type"] = marketEventType.toStdString();
    const domain::backtest::ResolvedStrategyBehavior behavior =
        domain::backtest::resolveStrategyBehavior(strategy);
    if (behavior.valid) {
        signalEvent.set("strategy_behavior_kind", static_cast<int64_t>(behavior.index()));
        signalEvent.metadata["strategy_behavior_kind"] = std::to_string(behavior.index());
    }

        qInfo() << "StrategyService: publish strategy signal"
            << "strategy=" << strategyId
            << "symbol=" << symbol
            << "action=" << action
            << "marketEventType=" << marketEventType
            << "latestPrice=" << latestPrice
            << "referencePrice=" << referencePrice
            << "strength=" << strength;

    const auto result = bus->publish(signalEvent, static_cast<int>(engine::EventPriority::NORMAL));
    if (!result) {
        qWarning() << "StrategyService: failed to publish strategy signal" << QString::fromStdString(result.message);
        return;
    }

    QVariantMap signalData;
    signalData.insert("strategyId", strategyId);
    signalData.insert("strategyName", strategyName);
    signalData.insert("symbol", symbol);
    signalData.insert("action", action);
    signalData.insert("price", latestPrice);
    signalData.insert("referencePrice", referencePrice);
    signalData.insert("strength", strength);
    signalData.insert("marketEventType", marketEventType);
    emit strategySignalPublished(signalData);
}

bool StrategyService::shouldPublishStrategySignal(const QString& strategyId,
                                                 const QString& symbol,
                                                 const QString& action)
{
    const QString publicationKey = buildSignalPublicationKey(strategyId, symbol);
    if (publicationKey.isEmpty() || action.trimmed().isEmpty()) {
        return true;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    QMutexLocker locker(&m_signalPublicationMutex);
    const auto existing = m_lastPublishedSignalByKey.constFind(publicationKey);
    if (existing != m_lastPublishedSignalByKey.constEnd()) {
        const bool sameAction = existing->action.compare(action, Qt::CaseInsensitive) == 0;
        const bool withinCooldown = (nowMs - existing->publishedAtMs) < kStrategySignalCooldownMs;
        if (sameAction || withinCooldown) {
            return false;
        }
    }

    m_lastPublishedSignalByKey.insert(publicationKey, StrategySignalPublicationState{action, nowMs});
    return true;
}

void StrategyService::clearSignalPublicationState(const QString& strategyId)
{
    const QString normalizedStrategyId = strategyId.trimmed();
    if (normalizedStrategyId.isEmpty()) {
        return;
    }

    QMutexLocker locker(&m_signalPublicationMutex);
    for (auto it = m_lastPublishedSignalByKey.begin(); it != m_lastPublishedSignalByKey.end();) {
        if (it.key().startsWith(normalizedStrategyId + QChar('|'))) {
            it = m_lastPublishedSignalByKey.erase(it);
            continue;
        }
        ++it;
    }
}

QString StrategyService::determineSignalAction(const QVariantMap& strategy,
                                              double latestPrice,
                                              double referencePrice) const
{
    return StrategyRuntimeRuleEvaluator::determineAction(strategy, latestPrice, referencePrice);
}

double StrategyService::determineSignalStrength(const QVariantMap& strategy,
                                                double latestPrice,
                                                double referencePrice) const
{
    return StrategyRuntimeRuleEvaluator::determineStrength(strategy, latestPrice, referencePrice);
}

QString StrategyService::createStrategy(const QVariantMap& strategyData) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QString();
    }

    QString strategyId;
    QVariantMap completeStrategy;
    {
        QWriteLocker locker(&m_rwLock);

        // 验证策略数据
        QString errorMessage;
        if (!validateStrategyData(strategyData, errorMessage)) {
            qWarning() << "StrategyService: 策略数据验证失败 -" << errorMessage;
            emit errorOccurred(errorMessage);
            return QString();
        }

        // 生成策略代码
        QString strategyCode = generateStrategyCode(strategyData);

        // 生成策略ID（使用生成的策略代码作为ID）
        strategyId = generateStrategyId(strategyData.value("strategy_name").toString());

        // 构建完整的策略数据
        completeStrategy = strategyData;
        applyProjectedEditorState(completeStrategy, editorStateFromStrategyMap(completeStrategy));
        completeStrategy["strategy_id"] = strategyId;
        completeStrategy["strategy_code"] = strategyCode;

        // 设置默认状态，数据库只支持：'ACTIVE', 'INACTIVE', 'TESTING', 'ARCHIVED'
        const strategy_view::StrategyLifecycleStatus status = strategyData.contains(QStringLiteral("statusIndex"))
            ? strategy_view::resolveStrategyLifecycleStatus(strategyData.value(QStringLiteral("statusIndex")))
            : strategy_view::StrategyLifecycleStatus::Inactive;
        assignStrategyStatus(completeStrategy, status);

        completeStrategy["created_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        completeStrategy["updated_at"] = completeStrategy["created_at"];
        applyCanonicalStrategyStructures(completeStrategy);

        QString savedStrategyId = saveStrategyToDatabase(completeStrategy);
        if (savedStrategyId.isEmpty()) {
            qWarning() << "StrategyService: 保存策略到数据库失败";
            emit errorOccurred("保存策略到数据库失败");
            return QString();
        }

        strategyId = savedStrategyId;
        completeStrategy["strategy_id"] = strategyId;
        saveStrategyToCache(strategyId, completeStrategy);
    }

    if (m_viewModel) {
        m_viewModel->appendData(completeStrategy);
    }
    
    // 发送信号
    emit strategyCreated(strategyId, completeStrategy);
    emit dataChanged();
    
    qDebug() << "StrategyService: 创建策略成功 - ID:" << strategyId << "名称:" 
             << strategyData.value("strategy_name").toString();
    
    return strategyId;
}

bool StrategyService::updateStrategy(const QString& strategyId, const QVariantMap& strategyData) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }

    QVariantMap updatedStrategy;
    {
        QWriteLocker locker(&m_rwLock);

        QString errorMessage;
        if (!validateStrategyData(strategyData, errorMessage)) {
            qWarning() << "StrategyService: 策略数据验证失败 -" << errorMessage;
            emit errorOccurred(errorMessage);
            return false;
        }

        QVariantMap existingStrategy = loadStrategyFromCache(strategyId);
        if (existingStrategy.isEmpty()) {
            qWarning() << "StrategyService: 策略不存在 - ID:" << strategyId;
            return false;
        }

        updatedStrategy = existingStrategy;
        mergeStrategyParameterPayload(updatedStrategy, strategyData);
        for (auto it = strategyData.begin(); it != strategyData.end(); ++it) {
            if (it.key() == QStringLiteral("parameters")
                    || it.key() == QStringLiteral("ruleProfileSnapshot")
                    || it.key() == QStringLiteral("executionPolicySnapshot")
                    || it.key() == QStringLiteral("backtestAssumptionsSnapshot")
                    || it.key() == QStringLiteral("strategyScopeContextSnapshot")) {
                continue;
            }
            updatedStrategy[it.key()] = it.value();
        }
        applyProjectedEditorState(updatedStrategy,
                                  editorStateFromStrategyMap(updatedStrategy, strategyStatusFromMap(existingStrategy)));

        if (updatedStrategy.contains(QStringLiteral("statusIndex"))) {
            const strategy_view::StrategyLifecycleStatus updatedStatus =
                strategy_view::resolveStrategyLifecycleStatus(updatedStrategy.value(QStringLiteral("statusIndex")));
            assignStrategyStatus(updatedStrategy, updatedStatus);
        }

        updatedStrategy["updated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        applyCanonicalStrategyStructures(updatedStrategy);

        if (!updateStrategyInDatabase(strategyId, updatedStrategy)) {
            qWarning() << "StrategyService: 更新策略到数据库失败";
            emit errorOccurred("更新策略到数据库失败");
            return false;
        }

        saveStrategyToCache(strategyId, updatedStrategy);
    }

    if (m_viewModel) {
        m_viewModel->updateStrategy(strategyId, updatedStrategy);
    }
    
    // 发送信号
    emit strategyUpdated(strategyId, updatedStrategy);
    emit dataChanged();
    
    qDebug() << "StrategyService: 更新策略成功 - ID:" << strategyId;
    
    return true;
}

bool StrategyService::deleteStrategy(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }

    {
        QWriteLocker locker(&m_rwLock);

        if (!deleteStrategyFromDatabase(strategyId)) {
            qWarning() << "StrategyService: 从数据库删除策略失败 - ID:" << strategyId;
            emit errorOccurred(QString("从数据库删除策略失败: %1").arg(strategyId));
            return false;
        }

        removeStrategyFromCache(strategyId);
    }

    if (m_viewModel) {
        m_viewModel->removeStrategy(strategyId);
    }
    
    // 发送信号
    emit strategyDeleted(strategyId);
    emit dataChanged();
    
    qDebug() << "StrategyService: 删除策略成功 - ID:" << strategyId;
    
    return true;
}

QVariantMap StrategyService::getStrategyById(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantMap();
    }

    {
        QReadLocker locker(&m_rwLock);
        QVariantMap cachedStrategy = applyEditorDisplayProjection(
            recoverEditableRulePayloadFromBacktest(loadStrategyFromCache(strategyId)));
        if (!cachedStrategy.isEmpty() && hasCompleteEditableRulePayload(cachedStrategy)) {
            return cachedStrategy;
        }
    }

    QVariantMap repositoryStrategy;
    if (m_repository) {
        repositoryStrategy = strategyMapFromRepositoryResult(m_repository->findById(strategyId));
    }

    repositoryStrategy = applyEditorDisplayProjection(recoverEditableRulePayloadFromBacktest(repositoryStrategy));

    if (!repositoryStrategy.isEmpty()) {
        QWriteLocker locker(&m_rwLock);
        saveStrategyToCache(strategyId, repositoryStrategy);
        return applyEditorDisplayProjection(loadStrategyFromCache(strategyId));
    }

    QReadLocker locker(&m_rwLock);
    return applyEditorDisplayProjection(loadStrategyFromCache(strategyId));
}

QVariantMap StrategyService::getStrategyByCode(const QString& strategyCode) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantMap();
    }
    
    QReadLocker locker(&m_rwLock);
    
    // 从数据库获取
    QVariantMap strategy = applyEditorDisplayProjection(
        strategyMapFromRepositoryResult(m_repository->findByCode(strategyCode)));
    if (!strategy.isEmpty()) {
        QString strategyId = strategy.value("strategy_id").toString();
        saveStrategyToCache(strategyId, strategy);
    }
    
    return strategy;
}

QVariantList StrategyService::getAllStrategies() {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantList();
    }
    
    QReadLocker locker(&m_rwLock);
    
    // 从缓存构建列表
    return buildStrategyListFromCache(m_memoryCache);
}

QVariantList StrategyService::getStrategiesByType(int strategyTypeIndex) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantList();
    }

    const domain::backtest::ResolvedStrategyIdentity requestedType =
        domain::backtest::resolveStrategyStoredType(strategyTypeIndex);
    if (!requestedType.validStoredType) {
        qWarning() << "StrategyService: 无效的 strategyTypeIndex" << strategyTypeIndex;
        return QVariantList();
    }
    
    QReadLocker locker(&m_rwLock);
    
    QVariantList result;
    for (const QVariantMap& strategy : m_memoryCache.values()) {
        if (matchesStrategyStoredType(strategy, requestedType.storedType)) {
            result.append(applyListDisplayProjection(strategy));
        }
    }
    
    return result;
}

QVariantList StrategyService::getStrategiesByStatus(int statusIndex) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantList();
    }

    const strategy_view::StrategyLifecycleStatus requestedStatus =
        strategy_view::strategyLifecycleStatusFromIndex(statusIndex);
    if (statusIndex >= 0 && !strategy_view::isKnownStrategyLifecycleStatus(requestedStatus)) {
        qWarning() << "StrategyService: 无效的 statusIndex" << statusIndex;
        return QVariantList();
    }
    
    QReadLocker locker(&m_rwLock);
    
    QVariantList result;
    for (const QVariantMap& strategy : m_memoryCache.values()) {
        if (matchesStrategyStatus(strategy, requestedStatus)) {
            result.append(applyListDisplayProjection(strategy));
        }
    }
    
    return result;
}

QVariantList StrategyService::searchStrategies(const QString& keyword) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantList();
    }
    
    QReadLocker locker(&m_rwLock);
    
    QVariantList result;
    for (const QVariantMap& strategy : m_memoryCache.values()) {
        if (matchesStrategyKeyword(strategy, keyword)) {
            result.append(applyListDisplayProjection(strategy));
        }
    }
    
    return result;
}

bool StrategyService::importStrategies(const QVariantList& strategies) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }
    
    QWriteLocker locker(&m_rwLock);
    
    QVariantList failedStrategies;
    std::vector<QVariantMap> successStrategies;
    
    for (const QVariant& strategyVar : strategies) {
        QVariantMap strategyData = strategyVar.toMap();
        
        QString errorMessage;
        if (!validateStrategyData(strategyData, errorMessage)) {
            strategyData["error"] = errorMessage;
            failedStrategies.append(strategyData);
            continue;
        }

        applyCanonicalStrategyStructures(strategyData);
        
        // 生成策略代码（策略ID由数据库自增生成）
        if (!strategyData.contains("strategy_code")) {
            QString strategyCode = generateStrategyCode(strategyData);
            strategyData["strategy_code"] = strategyCode;
        }
        
        // 保存到数据库
        QString savedStrategyId = saveStrategyToDatabase(strategyData);
        if (savedStrategyId.isEmpty()) {
            strategyData["error"] = "保存到数据库失败";
            failedStrategies.append(strategyData);
            continue;
        }
        
        // 使用数据库返回的ID
        strategyData["strategy_id"] = savedStrategyId;
        
        successStrategies.push_back(strategyData);
    }
    
    // 更新缓存
    if (!successStrategies.empty()) {
        updateCacheBatch(successStrategies);
        refreshViewModelFromCache(m_viewModel, m_memoryCache);
        emit dataChanged();
    }
    
    // 发送失败信号
    if (!failedStrategies.isEmpty()) {
        emit importFailed(failedStrategies);
    }
    
    return failedStrategies.isEmpty();
}

bool StrategyService::exportStrategies(const QString& format, const QString& filePath) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }
    
    QReadLocker locker(&m_rwLock);
    
    // 获取所有策略
    QVariantList strategies = getAllStrategies();
    
    // 构建导出数据
    QVariantMap exportData;
    exportData["version"] = "1.0";
    exportData["export_date"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    exportData["strategy_count"] = strategies.size();
    exportData["strategies"] = strategies;
    
    // 转换为JSON
    QJsonDocument doc = QJsonDocument::fromVariant(exportData);
    QByteArray jsonData = doc.toJson(QJsonDocument::Indented);
    
    // 保存到文件
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "StrategyService: 无法打开文件 -" << filePath << "错误:" << file.errorString();
        emit errorOccurred(QString("无法打开文件: %1").arg(file.errorString()));
        return false;
    }
    
    file.write(jsonData);
    file.close();
    
    qDebug() << "StrategyService: 导出策略成功 - 数量:" << strategies.size() << "路径:" << filePath;
    
    return true;
}

bool StrategyService::activateStrategy(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }

    QVariantMap updateData;
    updateData["statusIndex"] = strategy_view::strategyLifecycleStatusIndex(strategy_view::StrategyLifecycleStatus::Active);
    updateData["updated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    {
        QWriteLocker locker(&m_rwLock);

        if (!m_repository->updateStatus(strategyId, strategy_view::StrategyLifecycleStatus::Active)) {
            qWarning() << "StrategyService: 激活策略失败 - ID:" << strategyId;
            emit errorOccurred(QString("激活策略失败: %1").arg(strategyId));
            return false;
        }

        QVariantMap strategy = loadStrategyFromCache(strategyId);
        if (!strategy.isEmpty()) {
            assignStrategyStatus(strategy, strategy_view::StrategyLifecycleStatus::Active);
            strategy["updated_at"] = updateData["updated_at"];
            saveStrategyToCache(strategyId, strategy);
        }

        clearSignalPublicationState(strategyId);
    }

    if (m_viewModel) {
        m_viewModel->updateStrategyStatus(
            strategyId,
            strategy_view::strategyLifecycleStatusIndex(strategy_view::StrategyLifecycleStatus::Active));
    }

    emit strategyActivated(strategyId);
    emit dataChanged();

    return true;
}

bool StrategyService::deactivateStrategy(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }

    QVariantMap updateData;
    updateData["statusIndex"] = strategy_view::strategyLifecycleStatusIndex(strategy_view::StrategyLifecycleStatus::Inactive);
    updateData["updated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    {
        QWriteLocker locker(&m_rwLock);

        if (!m_repository->updateStatus(strategyId, strategy_view::StrategyLifecycleStatus::Inactive)) {
            qWarning() << "StrategyService: 停用策略失败 - ID:" << strategyId;
            emit errorOccurred(QString("停用策略失败: %1").arg(strategyId));
            return false;
        }

        QVariantMap strategy = loadStrategyFromCache(strategyId);
        if (!strategy.isEmpty()) {
            assignStrategyStatus(strategy, strategy_view::StrategyLifecycleStatus::Inactive);
            strategy["updated_at"] = updateData["updated_at"];
            saveStrategyToCache(strategyId, strategy);
        }

        clearSignalPublicationState(strategyId);
    }

    if (m_viewModel) {
        m_viewModel->updateStrategyStatus(
            strategyId,
            strategy_view::strategyLifecycleStatusIndex(strategy_view::StrategyLifecycleStatus::Inactive));
    }

    emit strategyDeactivated(strategyId);
    emit dataChanged();

    return true;
}

bool StrategyService::archiveStrategy(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }
    
    QWriteLocker locker(&m_rwLock);
    
    // 更新状态 - 使用通用update方法
    QVariantMap updateData;
    updateData["statusIndex"] = strategy_view::strategyLifecycleStatusIndex(strategy_view::StrategyLifecycleStatus::Archived);
    updateData["updated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    if (!m_repository->updateStatus(strategyId, strategy_view::StrategyLifecycleStatus::Archived)) {
        qWarning() << "StrategyService: 归档策略失败 - ID:" << strategyId;
        emit errorOccurred(QString("归档策略失败: %1").arg(strategyId));
        return false;
    }
    
    // 更新缓存
    QVariantMap strategy = loadStrategyFromCache(strategyId);
    if (!strategy.isEmpty()) {
        assignStrategyStatus(strategy, strategy_view::StrategyLifecycleStatus::Archived);
        strategy["updated_at"] = updateData["updated_at"];
        saveStrategyToCache(strategyId, strategy);
    }

    if (m_viewModel) {
        m_viewModel->updateStrategyStatus(
            strategyId,
            strategy_view::strategyLifecycleStatusIndex(strategy_view::StrategyLifecycleStatus::Archived));
    }
    
    emit dataChanged();
    
    return true;
}

bool StrategyService::duplicateStrategy(const QString& sourceStrategyId, const QString& newName) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }
    
    QReadLocker locker(&m_rwLock);
    
    // 获取源策略
    QVariantMap sourceStrategy = getStrategyById(sourceStrategyId);
    if (sourceStrategy.isEmpty()) {
        qWarning() << "StrategyService: 源策略不存在 - ID:" << sourceStrategyId;
        emit errorOccurred(QString("源策略不存在: %1").arg(sourceStrategyId));
        return false;
    }
    
    // 创建副本
    QVariantMap newStrategy = sourceStrategy;
    
    // 更新名称
    newStrategy["strategy_name"] = newName;
    
    // 生成新的代码（策略ID由数据库自动生成）
    QString newStrategyCode = generateStrategyCode(newStrategy);
    
    // 移除ID，让数据库自动生成
    newStrategy.remove("strategy_id");
    newStrategy["strategy_code"] = newStrategyCode;
    
    // 重置状态和时间
    newStrategy["statusIndex"] = strategy_view::strategyLifecycleStatusIndex(strategy_view::StrategyLifecycleStatus::Inactive);
    newStrategy["created_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    newStrategy["updated_at"] = newStrategy["created_at"];
    
    // 移除性能指标
    newStrategy.remove("performance_metrics");
    
    // 创建新策略
    return createStrategy(newStrategy) != QString();
}

bool StrategyService::updateStrategyParameters(const QString& strategyId, const QVariantMap& parameters) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }

    QString templateError;
    QVariantMap strategyForValidation;
    strategyForValidation.insert(QStringLiteral("parameters"), parameters);
    const QVariantList templateBindings = strategyRuleTemplateBindings(strategyForValidation);
    if (!validateRuleTemplateBindings(templateBindings, &templateError)) {
        qWarning() << "StrategyService: 规则模板校验失败 - ID:" << strategyId << templateError;
        emit errorOccurred(templateError.isEmpty() ? QStringLiteral("规则模板校验失败") : templateError);
        return false;
    }
    
    QWriteLocker locker(&m_rwLock);
    
    // 获取现有策略
    QVariantMap existingStrategy = loadStrategyFromCache(strategyId);
    if (existingStrategy.isEmpty()) {
        existingStrategy = strategyMapFromRepositoryResult(m_repository->findById(strategyId));
        if (existingStrategy.isEmpty()) {
            qWarning() << "StrategyService: 策略不存在 - ID:" << strategyId;
            return false;
        }
    }
    
    // 构建更新数据
    QVariantMap normalizedStrategy = existingStrategy;
        normalizedStrategy["parameters"] = mergeVariantMapsRecursive(existingStrategy.value("parameters").toMap(), parameters);
    applyCanonicalStrategyStructures(normalizedStrategy);

    QVariantMap updateData;
    updateData["parameters"] = normalizedStrategy.value("parameters").toMap();
    updateData["ruleProfileSnapshot"] = normalizedStrategy.value("ruleProfileSnapshot").toMap();
    updateData["executionPolicySnapshot"] = normalizedStrategy.value("executionPolicySnapshot").toMap();
    updateData["backtestAssumptionsSnapshot"] = normalizedStrategy.value("backtestAssumptionsSnapshot").toMap();
    updateData["strategyScopeContextSnapshot"] = normalizedStrategy.value("strategyScopeContextSnapshot").toMap();
    updateData["updated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    // 更新数据库 - 使用通用update方法
    if (!m_repository->updateParameters(strategyId, updateData.value(QStringLiteral("parameters")).toMap())) {
        qWarning() << "StrategyService: 更新策略参数失败 - ID:" << strategyId;
        emit errorOccurred(QString("更新策略参数失败: %1").arg(strategyId));
        return false;
    }
    
    // 更新缓存
    QVariantMap strategy = loadStrategyFromCache(strategyId);
    if (!strategy.isEmpty()) {
        strategy = normalizedStrategy;
        strategy["updated_at"] = updateData["updated_at"];
        saveStrategyToCache(strategyId, strategy);
    }

    if (m_viewModel) {
        m_viewModel->updateStrategy(strategyId, strategy);
    }
    
    emit dataChanged();
    
    return true;
}

QVariantMap StrategyService::getStrategyParameters(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantMap();
    }

    QVariantMap cachedStrategy;
    QVariantMap cachedParameters;
    {
        QReadLocker locker(&m_rwLock);
        cachedStrategy = recoverEditableRulePayloadFromBacktest(loadStrategyFromCache(strategyId));
        cachedParameters = cachedStrategy.value(QStringLiteral("parameters")).toMap();
    }

    QVariantMap repositoryStrategy;
    if (m_repository) {
        repositoryStrategy = strategyMapFromRepositoryResult(m_repository->findById(strategyId));
    }

    repositoryStrategy = recoverEditableRulePayloadFromBacktest(repositoryStrategy);

    if (!repositoryStrategy.isEmpty()) {
        QWriteLocker locker(&m_rwLock);
        saveStrategyToCache(strategyId, repositoryStrategy);
        return repositoryStrategy.value("parameters").toMap();
    }

    if (!cachedParameters.isEmpty() && hasCompleteEditableRulePayload(cachedStrategy)) {
        return cachedParameters;
    }

    QReadLocker locker(&m_rwLock);
    return loadStrategyFromCache(strategyId).value("parameters").toMap();
}

bool StrategyService::updateStrategyPerformance(const QString& strategyId, const QVariantMap& performance) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }

    QVariantMap strategy;
    QString updatedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    QVariantMap mergedPerformance;
    {
        QWriteLocker locker(&m_rwLock);

        QVariantMap existingStrategy = loadStrategyFromCache(strategyId);
        if (existingStrategy.isEmpty() || !hasCompleteEditableRulePayload(existingStrategy)) {
            existingStrategy = strategyMapFromRepositoryResult(m_repository->findById(strategyId));
            if (existingStrategy.isEmpty()) {
                qWarning() << "StrategyService: 策略不存在 - ID:" << strategyId;
                return false;
            }
        }

        const bool payloadWasIncomplete = !hasCompleteEditableRulePayload(existingStrategy);
        existingStrategy = recoverEditableRulePayloadFromBacktest(existingStrategy);
        const bool payloadRecovered = !payloadWasIncomplete ? false : hasCompleteEditableRulePayload(existingStrategy);

        mergedPerformance = mergePerformanceMetrics(existingStrategy, performance, updatedAt);

        QVariantMap updateData;
        updateData["performance_metrics"] = mergedPerformance;
        if (payloadRecovered && !updateData.contains(QStringLiteral("parameters"))) {
            updateData["parameters"] = existingStrategy.value(QStringLiteral("parameters")).toMap();
        }
        updateData["updated_at"] = updatedAt;

        strategy = existingStrategy;
        strategy["performance_metrics"] = mergedPerformance;
        if (updateData.contains(QStringLiteral("parameters"))) {
            strategy["parameters"] = updateData.value(QStringLiteral("parameters")).toMap();
        }
        strategy["updated_at"] = updatedAt;

        if (!updateStrategyInDatabase(strategyId, strategy)) {
            qWarning() << "StrategyService: 更新策略性能失败 - ID:" << strategyId;
            emit errorOccurred(QString("更新策略性能失败: %1").arg(strategyId));
            return false;
        }
        strategy["updated_at"] = updatedAt;
        saveStrategyToCache(strategyId, strategy);
    }

    if (m_viewModel) {
        m_viewModel->updateStrategyPerformance(strategyId, mergedPerformance);
    }

    emit dataChanged();

    return true;
}

QVariantMap StrategyService::getStrategyPerformance(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantMap();
    }
    
    QReadLocker locker(&m_rwLock);
    
    QVariantMap strategy = getStrategyById(strategyId);
    if (strategy.isEmpty()) {
        return QVariantMap();
    }
    
    return strategy.value("performance_metrics").toMap();
}

void StrategyService::syncWithDatabase() {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return;
    }

    {
        QWriteLocker locker(&m_rwLock);
        clearAllCache();
    }

    loadStrategiesFromDatabase();

    if (m_viewModel) {
        refreshViewModelFromCache(m_viewModel, m_memoryCache);
    }
    emit dataChanged();
    
    qDebug() << "StrategyService: 与数据库同步完成";
}

void StrategyService::clearCache() {
    QWriteLocker locker(&m_rwLock);
    clearAllCache();
    m_cacheLoaded = false;
    emit cacheLoadedChanged();
    qDebug() << "StrategyService: 缓存已清除";
}

QVariantMap StrategyService::createDefaultStrategy(int strategyBehaviorKind, const QString& strategyName) {
    if (!domain::backtest::isValidStrategyBehaviorKind(strategyBehaviorKind)) {
        qWarning() << "StrategyService: invalid strategyBehaviorKind for template creation" << strategyBehaviorKind;
        return {};
    }

    const domain::backtest::StrategyBehaviorKind behaviorKind =
        static_cast<domain::backtest::StrategyBehaviorKind>(strategyBehaviorKind);
    QString name = strategyName.isEmpty()
        ? QString("%1策略").arg(defaultTemplateDisplayName(behaviorKind))
        : strategyName;
    
    QVariantMap strategy;

    switch (behaviorKind) {
    case domain::backtest::StrategyBehaviorKind::TrendFollowing:
        strategy = createTrendFollowingStrategy(name);
        break;
    case domain::backtest::StrategyBehaviorKind::MeanReversion:
        strategy = createMeanReversionStrategy(name);
        break;
    case domain::backtest::StrategyBehaviorKind::Momentum:
    case domain::backtest::StrategyBehaviorKind::MultiFactor:
    case domain::backtest::StrategyBehaviorKind::MachineLearning:
        strategy = createAlphaStrategy(name);
        break;
    case domain::backtest::StrategyBehaviorKind::Arbitrage:
        strategy = createArbitrageStrategy(name);
        break;
    case domain::backtest::StrategyBehaviorKind::EventDriven:
    case domain::backtest::StrategyBehaviorKind::HighFrequency:
    case domain::backtest::StrategyBehaviorKind::Custom:
    default:
        strategy = createCustomStrategy(name);
        break;
    }

    QVariantMap parameters = strategy.value("parameters").toMap();
    parameters["strategyBehaviorKind"] = static_cast<int>(behaviorKind);
    switch (behaviorKind) {
    case domain::backtest::StrategyBehaviorKind::MachineLearning:
        parameters["feature_window"] = parameters.value("feature_window", 60);
        parameters["prediction_days"] = parameters.value("prediction_days", 1);
        parameters["training_days"] = parameters.value("training_days", 1000);
        parameters["confidence_threshold"] = parameters.value("confidence_threshold", 0.6);
        break;
    case domain::backtest::StrategyBehaviorKind::MultiFactor:
        parameters["factor_types"] = parameters.value("factor_types", QStringList{"value", "quality", "growth", "momentum"});
        break;
    case domain::backtest::StrategyBehaviorKind::HighFrequency:
        parameters["execution_timeframe"] = parameters.value("execution_timeframe", "5min");
        break;
    case domain::backtest::StrategyBehaviorKind::EventDriven:
        parameters["event_types"] = parameters.value("event_types", QStringList{"earnings_release", "merger_announcement"});
        break;
    case domain::backtest::StrategyBehaviorKind::Custom:
        parameters["custom_code"] = parameters.value("custom_code", "# custom strategy\n");
        break;
    case domain::backtest::StrategyBehaviorKind::TrendFollowing:
    case domain::backtest::StrategyBehaviorKind::MeanReversion:
    case domain::backtest::StrategyBehaviorKind::Momentum:
    case domain::backtest::StrategyBehaviorKind::Arbitrage:
    default:
        break;
    }
    strategy["parameters"] = parameters;
    
    // 添加描述
    strategy["description"] = QString("%1 - %2").arg(name).arg(templateDescriptionForBehaviorKind(behaviorKind));
    applyRuleNativeTemplateDefaults(strategy, behaviorKind, name);
    applyCanonicalStrategyStructures(strategy);
    applyProjectedEditorState(strategy, editorStateFromStrategyMap(strategy));
    
    return strategy;
}

QVariantMap StrategyService::getStrategyTemplate(int strategyBehaviorKind) {
    return createDefaultStrategy(strategyBehaviorKind, "");
}

QVariantMap StrategyService::buildStrategyBacktestRunContextCandidate(
    const QVariantMap& strategyData,
    const QVariantMap& runtimeContext) const {
    QVariantMap result;
    QVariantList missingFields;
    QVariantList unresolvedSymbols;
    QVariantMap runContext;

    const auto appendMissingField = [&](const QString& fieldName) {
        if (!fieldName.isEmpty() && !missingFields.contains(fieldName)) {
            missingFields.append(fieldName);
        }
    };

    const qulonglong strategyId = positiveUnsignedId(
        strategyData.value(QStringLiteral("engineStrategyId"),
            strategyData.value(QStringLiteral("engine_strategy_id"),
                strategyData.value(QStringLiteral("strategyId"), strategyData.value(QStringLiteral("id"))))));
    if (strategyId == 0ULL) {
        appendMissingField(QStringLiteral("strategyId"));
    } else {
        runContext.insert(QStringLiteral("strategyId"), QVariant::fromValue<qulonglong>(strategyId));
        runContext.insert(QStringLiteral("layerId"), QVariant::fromValue<qulonglong>(strategyId));
    }

    const bridge::config::StrategyStructureResolution resolution =
        bridge::config::StrategyStructureResolverSet().resolve(strategyData);
    const qulonglong universeId = positiveUnsignedId(resolution.strategyScopeContext.value(QStringLiteral("universeId")));
    if (universeId == 0ULL) {
        appendMissingField(QStringLiteral("universeId"));
    } else {
        runContext.insert(QStringLiteral("universeId"), QVariant::fromValue<qulonglong>(universeId));
    }

    const uint targetPositionCount = resolution.factorOverlay.value(QStringLiteral("targetPositionCount")).toUInt();
    if (targetPositionCount == 0U) {
        appendMissingField(QStringLiteral("targetPositionCount"));
    } else {
        runContext.insert(QStringLiteral("targetPositionCount"), targetPositionCount);
    }

    runContext.insert(QStringLiteral("layerType"), static_cast<int>(application::backtest::LayerType::Tactical));
    runContext.insert(QStringLiteral("executionMode"), static_cast<int>(application::backtest::ExecutionMode::EndOfDay));

    const QString selectedStartDate = normalizeStrategyBacktestTradeDate(
        runtimeContext.value(QStringLiteral("startDate"), resolution.backtestAssumptions.value(QStringLiteral("startDate"))).toString());
    const QString selectedEndDate = normalizeStrategyBacktestTradeDate(
        runtimeContext.value(QStringLiteral("endDate"), resolution.backtestAssumptions.value(QStringLiteral("endDate"))).toString());

    if (selectedStartDate.isEmpty()) {
        appendMissingField(QStringLiteral("startDate"));
    }
    if (selectedEndDate.isEmpty()) {
        appendMissingField(QStringLiteral("endDate"));
    }

    if (!selectedStartDate.isEmpty() && !selectedEndDate.isEmpty()) {
        const QString repoRoot = resolveStrategyBacktestRepoRoot();
        const int windowStartDay = resolveTradingDayIndexForBacktest(repoRoot, selectedStartDate);
        const int windowEndDay = resolveTradingDayIndexForBacktest(repoRoot, selectedEndDate);
        if (windowStartDay < 0) {
            appendMissingField(QStringLiteral("windowStartDay"));
        } else {
            runContext.insert(QStringLiteral("windowStartDay"), windowStartDay);
        }
        if (windowEndDay < 0) {
            appendMissingField(QStringLiteral("windowEndDay"));
        } else {
            runContext.insert(QStringLiteral("windowEndDay"), windowEndDay);
        }
    }

    const QVariant resolvedDataSourceModeValue = runtimeContext.contains(QStringLiteral("dataSourceMode"))
        ? runtimeContext.value(QStringLiteral("dataSourceMode"))
        : resolution.backtestAssumptions.value(QStringLiteral("dataSourceMode"));
    bool dataSourceModeOk = false;
    const int dataSourceMode = resolvedDataSourceModeValue.toInt(&dataSourceModeOk);
    if (dataSourceModeOk && dataSourceMode == static_cast<int>(domain::strategy::DataSourceMode::CacheDataset)) {
        const qulonglong dataSourceDatasetId = positiveUnsignedId(runtimeContext.value(QStringLiteral("dataSourceDatasetId")));
        if (dataSourceDatasetId == 0ULL) {
            appendMissingField(QStringLiteral("dataSourceDatasetId"));
        } else {
            runContext.insert(QStringLiteral("dataSourceDatasetId"), QVariant::fromValue<qulonglong>(dataSourceDatasetId));
        }
    }

    const QString benchmarkSymbol = normalizeStrategySymbol(resolution.backtestAssumptions.value(QStringLiteral("benchmark")).toString());
    if (!benchmarkSymbol.isEmpty()) {
        const QVariantMap resolvedBenchmark = resolveSymbolIdMap(QStringList{benchmarkSymbol});
        const qulonglong benchmarkSymbolId = positiveUnsignedId(resolvedBenchmark.value(benchmarkSymbol));
        if (benchmarkSymbolId == 0ULL) {
            appendMissingField(QStringLiteral("benchmarkSymbolId"));
        } else {
            runContext.insert(QStringLiteral("benchmarkSymbolId"), QVariant::fromValue<qulonglong>(benchmarkSymbolId));
        }
    }

    result.insert(QStringLiteral("ok"), missingFields.isEmpty() && unresolvedSymbols.isEmpty());
    result.insert(QStringLiteral("runContext"), runContext);
    result.insert(QStringLiteral("missingFields"), missingFields);
    result.insert(QStringLiteral("unresolvedSymbols"), unresolvedSymbols);

    QStringList errorParts;
    if (!missingFields.isEmpty()) {
        QStringList fields;
        for (const QVariant& field : missingFields) {
            fields.append(field.toString());
        }
        errorParts.append(QStringLiteral("缺少字段: %1").arg(fields.join(QStringLiteral(", "))));
    }
    if (!unresolvedSymbols.isEmpty()) {
        QStringList symbols;
        for (const QVariant& symbol : unresolvedSymbols) {
            symbols.append(symbol.toString());
        }
        errorParts.append(QStringLiteral("无法解析 symbol_id: %1").arg(symbols.join(QStringLiteral(", "))));
    }
    result.insert(QStringLiteral("errorText"), errorParts.join(QStringLiteral("；")));
    return result;
}

QVariantMap StrategyService::buildStrategyBacktestRequestPreview(
    const QVariantMap& strategyData,
    const QVariantMap& runContext,
    const QVariantMap& appliedRiskConfig) const {
    try {
        const auto typedRunContext = bridge::config::buildStrategyBacktestRunContext(runContext);
        const auto request = bridge::config::buildStrategyBacktestRequest(strategyData,
                                                                          typedRunContext,
                                                                          appliedRiskConfig);
        QVariantMap result;
        result.insert(previewKeyText(strategy_backtest_preview_keys::kOk), true);
        result.insert(previewKeyText(strategy_backtest_preview_keys::kErrorCode),
                      static_cast<int>(bridge::config::StrategyBacktestRequestAdapterErrorCode::None));
        result.insert(previewKeyText(strategy_backtest_preview_keys::kRequest),
                      serializeStrategyBacktestRequestPreview(request));
        return result;
    } catch (const bridge::config::StrategyBacktestRequestAdapterError& error) {
        QVariantMap result;
        result.insert(previewKeyText(strategy_backtest_preview_keys::kOk), false);
        result.insert(previewKeyText(strategy_backtest_preview_keys::kErrorCode), static_cast<int>(error.code));
        return result;
    }
}

QVariantMap StrategyService::launchStrategyBacktest(
    const QVariantMap& strategyData,
    const QVariantMap& runContext,
    const QVariantMap& appliedRiskConfig) {
    return executeStrategyBacktestRuntimePayloadAction(
        strategy_backtest_runtime_keys::kHandle,
        [&]() {
        const auto typedRunContext = bridge::config::buildStrategyBacktestRunContext(runContext);
            const auto handle = bridge::config::launchStrategyBacktest(strategyData,
                                                                       typedRunContext,
                                                                       strategyBacktestEntryService(),
                                                                       appliedRiskConfig);
            return QVariant::fromValue(bridge::config::buildStrategyBacktestHandleMap(handle));
        });
}

QVariantMap StrategyService::pollStrategyBacktestProgress(const qulonglong handleRunId) const {
    return executeStrategyBacktestRuntimePayloadAction(
        strategy_backtest_runtime_keys::kProgress,
        [&]() {
            return QVariant::fromValue(
                bridge::config::pollStrategyBacktestProgress(handleRunId,
                                                             strategyBacktestEntryService()));
        });
}

QVariantMap StrategyService::collectStrategyBacktestResult(const qulonglong handleRunId) const {
    return executeStrategyBacktestRuntimePayloadAction(
        strategy_backtest_runtime_keys::kCollection,
        [&]() {
            return QVariant::fromValue(
                bridge::config::tryCollectStrategyBacktestResult(handleRunId,
                                                                 strategyBacktestEntryService()));
        });
}

QVariantMap StrategyService::cancelStrategyBacktest(const qulonglong handleRunId) {
    return executeStrategyBacktestRuntimeAction([&]() {
        bridge::config::cancelStrategyBacktest(handleRunId, strategyBacktestEntryService());
    });
}

QVariantMap StrategyService::buildStrategyBacktestPerformancePayload(
    const QVariantMap& backtestResult,
    const QVariantMap& backtestContext) const {
    const QVariantMap performance = backtestResult.value(
        previewKeyText(strategy_backtest_performance_keys::kPerformance)).toMap();
    const QVariantMap trades = backtestResult.value(
        previewKeyText(strategy_backtest_performance_keys::kTrades)).toMap();
    const QVariantMap timeSeries = backtestResult.value(
        previewKeyText(strategy_backtest_performance_keys::kTimeSeries)).toMap();
    const QVariantList dates = backtestVariantList(timeSeries.value(
        previewKeyText(strategy_backtest_performance_keys::kDates)));

    const double totalReturn = hasBacktestDisplayValue(performance.value(previewKeyText(strategy_backtest_performance_keys::kTotalReturn)))
        ? backtestNumericValue(performance.value(previewKeyText(strategy_backtest_performance_keys::kTotalReturn)))
        : 0.0;
    const double maxDrawdown = hasBacktestDisplayValue(performance.value(previewKeyText(strategy_backtest_performance_keys::kMaxDrawdown)))
        ? backtestNumericValue(performance.value(previewKeyText(strategy_backtest_performance_keys::kMaxDrawdown)))
        : 0.0;
    const double winRate = hasBacktestDisplayValue(performance.value(previewKeyText(strategy_backtest_performance_keys::kWinRate)))
        ? backtestNumericValue(performance.value(previewKeyText(strategy_backtest_performance_keys::kWinRate)))
        : 0.0;

    const QString recordedAt = formatBacktestRecordedAt();

    QVariantMap payload;
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kReturns), QString::number(totalReturn * 100.0, 'f', 2));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kMaxDrawdown), QString::number(maxDrawdown * 100.0, 'f', 2));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kSharpeRatio),
                   QString::number(backtestNumericValue(performance.value(previewKeyText(strategy_backtest_performance_keys::kSharpeRatio))), 'f', 2));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kWinRate), QString::number(winRate * 100.0, 'f', 2));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kRunningDays), dates.size());
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kTradesCount),
                   static_cast<int>(backtestNumericValue(trades.value(previewKeyText(strategy_backtest_performance_keys::kTotalTrades)))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kPosition), 0);
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kDailyPnL), 0);
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kTotalReturn), totalReturn);
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kAnnualReturn),
                   backtestNumericValue(performance.value(previewKeyText(strategy_backtest_performance_keys::kAnnualizedReturn))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kAnnualizedReturn),
                   backtestNumericValue(performance.value(previewKeyText(strategy_backtest_performance_keys::kAnnualizedReturn))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kVolatility),
                   backtestNumericValue(performance.value(previewKeyText(strategy_backtest_performance_keys::kVolatility))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kSortinoRatio),
                   backtestNumericValue(performance.value(previewKeyText(strategy_backtest_performance_keys::kSortinoRatio))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kCalmarRatio),
                   backtestNumericValue(performance.value(previewKeyText(strategy_backtest_performance_keys::kCalmarRatio))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kProfitFactor),
                   backtestNumericValue(performance.value(previewKeyText(strategy_backtest_performance_keys::kProfitFactor))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kAverageWin),
                   backtestNumericValue(performance.value(previewKeyText(strategy_backtest_performance_keys::kAverageWin))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kAverageLoss),
                   backtestNumericValue(performance.value(previewKeyText(strategy_backtest_performance_keys::kAverageLoss))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kAlpha),
                   backtestNumericValue(performance.value(previewKeyText(strategy_backtest_performance_keys::kAlpha))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kBeta),
                   backtestNumericValue(performance.value(previewKeyText(strategy_backtest_performance_keys::kBeta))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kInformationRatio),
                   backtestNumericValue(performance.value(previewKeyText(strategy_backtest_performance_keys::kInformationRatio))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kTrackingError),
                   backtestNumericValue(performance.value(previewKeyText(strategy_backtest_performance_keys::kTrackingError))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kTotalTrades),
                   static_cast<int>(backtestNumericValue(trades.value(previewKeyText(strategy_backtest_performance_keys::kTotalTrades)))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kWinningTrades),
                   static_cast<int>(backtestNumericValue(trades.value(previewKeyText(strategy_backtest_performance_keys::kWinningTrades)))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kLosingTrades),
                   static_cast<int>(backtestNumericValue(trades.value(previewKeyText(strategy_backtest_performance_keys::kLosingTrades)))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kTotalProfit),
                   backtestNumericValue(trades.value(previewKeyText(strategy_backtest_performance_keys::kTotalProfit))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kTotalLoss),
                   backtestNumericValue(trades.value(previewKeyText(strategy_backtest_performance_keys::kTotalLoss))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kLargestWin),
                   backtestNumericValue(trades.value(previewKeyText(strategy_backtest_performance_keys::kLargestWin))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kLargestLoss),
                   backtestNumericValue(trades.value(previewKeyText(strategy_backtest_performance_keys::kLargestLoss))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kAverageHoldingPeriod),
                   backtestNumericValue(trades.value(previewKeyText(strategy_backtest_performance_keys::kAverageHoldingPeriod))));
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kTradingDays), dates.size());
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kLastBacktestAt), recordedAt);

    const QVariantMap historyEntry = buildStrategyBacktestHistoryEntryPayload(backtestResult,
                                                                              payload,
                                                                              backtestContext,
                                                                              recordedAt);
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kBacktestHistoryEntry), historyEntry);
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kFactorImportContext),
                   backtestContext.value(previewKeyText(strategy_backtest_performance_keys::kFactorImportContext)).toMap());
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kLatestBacktest), historyEntry);
    payload.insert(previewKeyText(strategy_backtest_performance_keys::kReplaceLatestBacktest),
                   backtestContext.contains(previewKeyText(strategy_backtest_performance_keys::kReplaceLatestBacktest))
                       ? backtestContext.value(previewKeyText(strategy_backtest_performance_keys::kReplaceLatestBacktest)).toBool()
                       : true);
    return payload;
}

bool StrategyService::recordStrategyBacktestResult(
    const QString& strategyId,
    const QVariantMap& backtestResult,
    const QVariantMap& backtestContext) {
    if (strategyId.trimmed().isEmpty()) {
        qWarning() << "StrategyService: strategyId is required for backtest recording";
        emit errorOccurred(QStringLiteral("记录策略回测结果失败: 缺少 strategyId"));
        return false;
    }

    QVariantMap effectiveContext = backtestContext;
    if (backtestTextValue(effectiveContext, strategy_backtest_performance_keys::kSelectedStrategyId).isEmpty()) {
        effectiveContext.insert(previewKeyText(strategy_backtest_performance_keys::kSelectedStrategyId), strategyId);
    }

    if (backtestTextValue(effectiveContext, strategy_backtest_performance_keys::kSelectedStrategyName).isEmpty()) {
        const QVariantMap strategy = getStrategyById(strategyId);
        if (!strategy.isEmpty()) {
            const QString strategyName = strategy.value(QStringLiteral("strategy_name"),
                                                        strategy.value(QStringLiteral("strategyName"))).toString().trimmed();
            if (!strategyName.isEmpty()) {
                effectiveContext.insert(previewKeyText(strategy_backtest_performance_keys::kSelectedStrategyName), strategyName);
            }
        }
    }

    return updateStrategyPerformance(strategyId,
                                     buildStrategyBacktestPerformancePayload(backtestResult, effectiveContext));
}

application::backtest::StrategyBacktestEntryService* StrategyService::strategyBacktestEntryService() const
{
    return bridge::StrategyBacktestRuntimeAccess::entryService();
}

// ============ 私有方法实现 ============

void StrategyService::initializeRepository() {
    // 首先确保数据库连接管理器已初始化
    // 这会配置ConnectionPool，确保StrategyRepository使用的连接池有正确的配置
    auto& dbManager = astock::database::DatabaseConnectionManager::instance();
    if (!dbManager.initialize()) {
        qWarning() << "StrategyService::initializeRepository: Database connection manager initialization failed";
        throw std::runtime_error("数据库连接初始化失败");
    }
    
    qDebug() << "✅ StrategyService::initializeRepository: Database connection manager initialized";
    
    // 创建策略仓储实例
    m_repository = std::make_shared<StrategyRepository>();
    if (!m_repository->initialize()) {
        throw std::runtime_error("策略仓储初始化失败");
    }
    
    qDebug() << "✅ StrategyService::initializeRepository: Repository initialized successfully";
}

QString StrategyService::saveStrategyToDatabase(const QVariantMap& strategyData) {
    if (!m_repository) {
        return QString();
    }
    return m_repository->save(strategyDataFromMap(strategyData));
}

bool StrategyService::updateStrategyInDatabase(const QString& strategyId, const QVariantMap& strategyData) {
    if (!m_repository) {
        return false;
    }
    return m_repository->update(strategyId, strategyDataFromMap(strategyData));
}

bool StrategyService::deleteStrategyFromDatabase(const QString& strategyId) {
    if (!m_repository) {
        return false;
    }
    return m_repository->remove(strategyId);
}

QVariantList StrategyService::loadStrategiesFromDatabase() {
    qDebug() << "=======================================";
    qDebug() << "StrategyService::loadStrategiesFromDatabase: 开始加载策略数据";
    qDebug() << "=======================================";
    QVariantList strategies;
    
    if (!m_repository) {
        qWarning() << "StrategyService::loadStrategiesFromDatabase: 仓储未初始化";
        qDebug() << "m_repository 为空指针";
        return strategies;
    }
    
    try {
        qDebug() << "调用 m_repository->findAll()...";
        auto strategyRecords = m_repository->findAll();
        qDebug() << "数据库查询返回" << strategyRecords.size() << "个策略";
        
        if (strategyRecords.empty()) {
            qWarning() << "数据库查询返回空结果集";
        } else {
            // 输出前几个策略的信息用于调试
            for (size_t i = 0; i < std::min(strategyRecords.size(), (size_t)3); ++i) {
                const QVariantMap strategy = strategyRecords[i].toVariantMap();
                qDebug() << "策略" << i << ":";
                qDebug() << "  ID:" << strategy.value("strategy_id").toString();
                qDebug() << "  名称:" << strategy.value("strategy_name").toString();
                qDebug() << "  类型:" << strategy.value("strategy_type").toString();
                qDebug() << "  状态索引:" << strategy.value("statusIndex").toInt();
            }
        }
        
        // 清空缓存
        qDebug() << "清空内存缓存...";
        m_memoryCache.clear();
        
        // 加载到缓存
        qDebug() << "加载策略到缓存...";
        for (const auto& strategy : strategyRecords) {
            QVariantMap normalizedStrategy = strategy.toVariantMap();
            applyCanonicalStrategyStructures(normalizedStrategy);
            QString strategyId = normalizedStrategy.value("strategy_id").toString();
            if (!strategyId.isEmpty()) {
                m_memoryCache[strategyId] = normalizedStrategy;
            }
            strategies.append(normalizedStrategy);
        }
        
        m_cacheLoaded = true;
        qDebug() << "缓存状态更新，发出cacheLoadedChanged信号";
        emit cacheLoadedChanged();

        // 发出加载完成信号
        qDebug() << "发出strategiesLoaded信号，策略数量:" << strategies.size();
        emit strategiesLoaded(strategies);
        
        qDebug() << "StrategyService::loadStrategiesFromDatabase: 加载完成";
        qDebug() << "缓存策略数量:" << m_memoryCache.size();
        qDebug() << "=======================================";
        return strategies;
        
    } catch (const std::exception& e) {
        qCritical() << "StrategyService::loadStrategiesFromDatabase: Error:" << e.what();
        qDebug() << "=======================================";
        return QVariantList();
    }
}

void StrategyService::saveStrategyToCache(const QString& strategyId, const QVariantMap& strategyData) {
    QVariantMap normalizedStrategy = strategyData;
    applyCanonicalStrategyStructures(normalizedStrategy);
    m_memoryCache[strategyId] = normalizedStrategy;
}

QVariantMap StrategyService::loadStrategyFromCache(const QString& strategyId) {
    return m_memoryCache.value(strategyId);
}

void StrategyService::removeStrategyFromCache(const QString& strategyId) {
    m_memoryCache.remove(strategyId);
}

void StrategyService::clearAllCache() {
    m_memoryCache.clear();
}

void StrategyService::updateCacheBatch(const std::vector<QVariantMap>& strategies) {
    for (const auto& strategy : strategies) {
        QString strategyId = strategy.value("strategy_id").toString();
        if (!strategyId.isEmpty()) {
            saveStrategyToCache(strategyId, strategy);
        }
    }
}

bool StrategyService::validateStrategyData(const QVariantMap& strategyData, QString& errorMessage) {
    // 检查必填字段
    if (!strategyData.contains("strategy_name") || strategyData.value("strategy_name").toString().isEmpty()) {
        errorMessage = "策略名称不能为空";
        return false;
    }

    if (strategyData.contains(QStringLiteral("sub_type")) || strategyData.contains(QStringLiteral("subType"))) {
        errorMessage = QStringLiteral("策略子类型不能再使用 sub_type/subType 字符串输入");
        return false;
    }

    const domain::backtest::ResolvedStrategyIdentity strategyIdentity =
        domain::backtest::resolveStrategyIdentity(strategyData);
    if (!strategyIdentity.validStoredType) {
        errorMessage = QStringLiteral("策略类型必须使用 strategyTypeIndex 或 strategyBehaviorKind");
        return false;
    }
    
    // 验证状态（如果提供）
    if (strategyData.contains(QStringLiteral("status")) && !strategyData.contains(QStringLiteral("statusIndex"))) {
        errorMessage = QStringLiteral("策略状态必须使用 statusIndex，禁止传入 status 字符串");
        return false;
    }

    if (strategyData.contains(QStringLiteral("statusIndex"))) {
        const strategy_view::StrategyLifecycleStatus status =
            strategy_view::resolveStrategyLifecycleStatus(strategyData.value(QStringLiteral("statusIndex")));
        if (!isPersistableStrategyStatus(status)) {
            errorMessage = QStringLiteral("无效的策略状态索引，策略保存仅允许 Active、Inactive、Testing、Archived");
            return false;
        }
    }

    if ((strategyData.contains(QStringLiteral("asset_type")) || strategyData.contains(QStringLiteral("assetType")))
        && !strategyData.contains(QStringLiteral("assetTypeIndex"))) {
        errorMessage = QStringLiteral("策略资产类型必须使用 assetTypeIndex，禁止传入资产类型字符串");
        return false;
    }

    if (strategyData.contains(QStringLiteral("assetTypeIndex"))) {
        const int assetTypeIndex = strategyData.value(QStringLiteral("assetTypeIndex")).toInt();
        if (assetTypeIndex <= 0 || assetTypeIndex > 6) {
            errorMessage = QStringLiteral("无效的策略资产类型索引");
            return false;
        }
    }

    if ((strategyData.contains(QStringLiteral("time_frame")) || strategyData.contains(QStringLiteral("timeFrame")))
        && !strategyData.contains(QStringLiteral("timeFrameIndex"))) {
        errorMessage = QStringLiteral("策略周期必须使用 timeFrameIndex，禁止传入周期字符串");
        return false;
    }

    if (strategyData.contains(QStringLiteral("timeFrameIndex"))) {
        const int timeFrameIndex = strategyData.value(QStringLiteral("timeFrameIndex")).toInt();
        if (timeFrameIndex <= 0 || timeFrameIndex > 10) {
            errorMessage = QStringLiteral("无效的策略周期索引");
            return false;
        }
    }

    if ((strategyData.contains(QStringLiteral("risk_level")) || strategyData.contains(QStringLiteral("riskLevel")))
        && !strategyData.contains(QStringLiteral("riskLevelIndex"))) {
        errorMessage = QStringLiteral("策略风险等级必须使用 riskLevelIndex，禁止传入风险等级字符串");
        return false;
    }

    if (strategyData.contains(QStringLiteral("riskLevelIndex"))) {
        const int riskLevelIndex = strategyData.value(QStringLiteral("riskLevelIndex")).toInt();
        if (riskLevelIndex <= 0 || riskLevelIndex > 4) {
            errorMessage = QStringLiteral("无效的策略风险等级索引");
            return false;
        }
    }

    const QVariantMap parameters = strategyData.value(QStringLiteral("parameters")).toMap();
    if (strategyData.contains(QStringLiteral("rule_template_binding"))
            || strategyData.contains(QStringLiteral("rule_template_bindings"))
            || parameters.contains(QStringLiteral("rule_template_binding"))
            || parameters.contains(QStringLiteral("rule_template_bindings"))) {
        errorMessage = QStringLiteral("规则模板绑定必须通过 rule_profile.ruleComposerState 提供，禁止传入 rule_template_binding/rule_template_bindings");
        return false;
    }
    if (!validateComposerStateBindingPhases(parameters, &errorMessage)) {
        return false;
    }
    if (!validateComposerStateBindingPhases(strategyData.value(QStringLiteral("ruleProfileSnapshot")).toMap(), &errorMessage)) {
        return false;
    }

    const QVariantList templateBindings = strategyRuleTemplateBindings(strategyData);
    if (!templateBindings.isEmpty()) {
        QString templateError;
        if (!validateRuleTemplateBindings(templateBindings, &templateError)) {
            errorMessage = templateError.isEmpty() ? QStringLiteral("规则模板校验失败") : templateError;
            return false;
        }
    }
    
    return true;
}

QString StrategyService::generateStrategyId(const QString& strategyName) {
    // 使用名称哈希加随机数生成ID
    QString nameHash = QString::number(qHash(strategyName));
    QString timestamp = QString::number(QDateTime::currentMSecsSinceEpoch());
    QString random = QString::number(QRandomGenerator::global()->generate());
    
    return QString("STR_%1_%2_%3").arg(nameHash.left(6)).arg(timestamp.right(6)).arg(random.right(4));
}

QString StrategyService::generateStrategyCode(const QVariantMap& strategyData) {
    const QString strategyName = strategyData.value(QStringLiteral("strategy_name")).toString();
    const domain::backtest::ResolvedStrategyIdentity strategyIdentity =
        domain::backtest::resolveStrategyIdentity(strategyData);

    // 生成简写代码
    QString codePrefix;
    switch (strategyIdentity.storedType) {
    case domain::backtest::StrategyStoredType::TrendFollowing:
        codePrefix = QStringLiteral("TRD");
        break;
    case domain::backtest::StrategyStoredType::MeanReversion:
        codePrefix = QStringLiteral("MR");
        break;
    case domain::backtest::StrategyStoredType::Alpha:
        codePrefix = QStringLiteral("ALPHA");
        break;
    case domain::backtest::StrategyStoredType::Arbitrage:
        codePrefix = QStringLiteral("ARB");
        break;
    case domain::backtest::StrategyStoredType::Portfolio:
        codePrefix = QStringLiteral("PTF");
        break;
    case domain::backtest::StrategyStoredType::HighFrequency:
        codePrefix = QStringLiteral("HFT");
        break;
    case domain::backtest::StrategyStoredType::Custom:
    case domain::backtest::StrategyStoredType::Unknown:
    default:
        codePrefix = QStringLiteral("CST");
        break;
    }
    
    // 使用名称的前几个字符，转换为大写，移除空格
    QString namePart = strategyName.left(6).toUpper().replace(" ", "_").replace("-", "_");
    
    // 添加毫秒级时间戳和随机尾缀，避免同名策略在同一分钟内重复创建时撞唯一键
    QString timestamp = QDateTime::currentDateTimeUtc().toString("yyMMddHHmmsszzz");
    QString entropy = QString::number(QRandomGenerator::global()->bounded(1000, 10000));
    
    return QString("%1_%2_%3%4").arg(codePrefix).arg(namePart).arg(timestamp).arg(entropy);
}

QVariantMap StrategyService::createTrendFollowingStrategy(const QString& name) {
    QVariantMap strategy;
    strategy["strategy_name"] = name;
    strategy["strategyTypeIndex"] = static_cast<int>(domain::backtest::StrategyStoredType::TrendFollowing);
    strategy["description"] = QString("趋势跟踪策略 - %1").arg(name);
    strategy["author"] = "系统";
    strategy["language"] = "Python";
    strategy["version"] = "1.0";
    
    // 默认参数
    QVariantMap parameters;
    parameters["fast_period"] = 10;
    parameters["slow_period"] = 30;
    parameters["positionSize"] = 0.1;
    parameters["stop_loss"] = 0.05;
    parameters["take_profit"] = 0.15;
    strategy["parameters"] = parameters;
    
    return strategy;
}

QVariantMap StrategyService::createMeanReversionStrategy(const QString& name) {
    QVariantMap strategy;
    strategy["strategy_name"] = name;
    strategy["strategyTypeIndex"] = static_cast<int>(domain::backtest::StrategyStoredType::MeanReversion);
    strategy["description"] = QString("均值回归策略 - %1").arg(name);
    strategy["author"] = "系统";
    strategy["language"] = "Python";
    strategy["version"] = "1.0";
    
    // 默认参数
    QVariantMap parameters;
    parameters["boll_period"] = 20;
    parameters["boll_std"] = 2.0;
    parameters["positionSize"] = 0.1;
    parameters["reversion_threshold"] = 0.5;
    strategy["parameters"] = parameters;
    
    return strategy;
}

QVariantMap StrategyService::createAlphaStrategy(const QString& name) {
    QVariantMap strategy;
    strategy["strategy_name"] = name;
    strategy["strategyTypeIndex"] = static_cast<int>(domain::backtest::StrategyStoredType::Alpha);
    strategy["description"] = QString("阿尔法策略 - %1").arg(name);
    strategy["author"] = "系统";
    strategy["language"] = "Python";
    strategy["version"] = "1.0";
    
    // 默认参数
    QVariantMap parameters;
    parameters["topN"] = 10;
    parameters["rebalance_days"] = 20;
    parameters["momentum_period"] = 60;
    parameters["positionSize"] = 0.1;
    strategy["parameters"] = parameters;
    
    return strategy;
}

QVariantMap StrategyService::createArbitrageStrategy(const QString& name) {
    QVariantMap strategy;
    strategy["strategy_name"] = name;
    strategy["strategyTypeIndex"] = static_cast<int>(domain::backtest::StrategyStoredType::Arbitrage);
    strategy["description"] = QString("套利策略 - %1").arg(name);
    strategy["author"] = "系统";
    strategy["language"] = "Python";
    strategy["version"] = "1.0";
    
    // 默认参数
    QVariantMap parameters;
    parameters["spread_threshold"] = 0.02;
    parameters["entry_z_score"] = 2.0;
    parameters["exit_z_score"] = 0.5;
    strategy["parameters"] = parameters;
    
    return strategy;
}

QVariantMap StrategyService::createCustomStrategy(const QString& name) {
    QVariantMap strategy;
    strategy["strategy_name"] = name;
    strategy["strategyTypeIndex"] = static_cast<int>(domain::backtest::StrategyStoredType::Custom);
    strategy["description"] = QString("自定义策略 - %1").arg(name);
    strategy["author"] = "系统";
    strategy["language"] = "Python";
    strategy["version"] = "1.0";
    
    // 默认参数
    QVariantMap parameters;
    parameters["custom_param1"] = "value1";
    parameters["custom_param2"] = 0.5;
    strategy["parameters"] = parameters;
    
    return strategy;
}

StrategyViewModel* StrategyService::getViewModel() { 
    // 确保ViewModel总是有效
    if (!m_viewModel) {
        // 创建ViewModel作为Service的子对象
        m_viewModel = new StrategyViewModel(this);
        qDebug() << "StrategyService: 创建默认视图模型，地址:" << m_viewModel;
    }
    return m_viewModel; 
}

void StrategyService::setViewModel(StrategyViewModel* viewModel) { 
    if (m_viewModel && m_viewModel != viewModel) {
        // 删除旧的ViewModel，如果它是Service的子对象
        if (m_viewModel->parent() == this) {
            m_viewModel->deleteLater();
        }
    }
    m_viewModel = viewModel; 
    qDebug() << "StrategyService: 设置视图模型，地址:" << m_viewModel;
}
