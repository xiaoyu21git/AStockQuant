#include "../include/UnifiedFactorBacktestPreviewService.h"

#include "../include/DefaultFactorBacktestTradingAdapter.h"
#include "../../../domain/backtest/include/StockDataProvider.h"
#include "../../../domain/factor/include/FactorBacktestExecutor.h"
#include "../../../domain/trading/include/DefaultTradingCore.h"

#include <cmath>
#include <unordered_set>

namespace application::trading {
namespace {

FactorBacktestTradingPreviewSummary::Status previewStatusFromRiskDecision(
    domain::trading::RiskDecisionType type)
{
    switch (type) {
    case domain::trading::RiskDecisionType::Warn:
        return FactorBacktestTradingPreviewSummary::Status::Warn;
    case domain::trading::RiskDecisionType::Block:
        return FactorBacktestTradingPreviewSummary::Status::Blocked;
    case domain::trading::RiskDecisionType::ForceReduce:
        return FactorBacktestTradingPreviewSummary::Status::ForceReduce;
    case domain::trading::RiskDecisionType::TradingHalt:
        return FactorBacktestTradingPreviewSummary::Status::TradingHalt;
    case domain::trading::RiskDecisionType::Pass:
    default:
        return FactorBacktestTradingPreviewSummary::Status::Pass;
    }
}

bool enrichTargetPositionsFromSnapshot(
    domain::trading::TradeIntentBatch& batch,
    const factor::CalculationResult& factorResult,
    const factor::BacktestConfig& config,
    domain::backtest::StockDataProvider* stockDataProvider,
    QVariantMap* diagnostics)
{
    if (stockDataProvider == nullptr || batch.targetPositions.isEmpty()) {
        return false;
    }
    if (!std::isfinite(config.initialCapital) || config.initialCapital <= 0.0) {
        return false;
    }

    const QString snapshotDate = QString::fromStdString(factorResult.date).trimmed();
    if (snapshotDate.isEmpty()) {
        return false;
    }

    stockDataProvider->setDataSourceContext(config.dataSourceMode, config.datasetId);

    std::vector<std::string> requestedSymbols;
    requestedSymbols.reserve(static_cast<size_t>(batch.targetPositions.size()));
    std::unordered_set<std::string> seenSymbols;
    seenSymbols.reserve(static_cast<size_t>(batch.targetPositions.size()));
    for (const domain::trading::TargetPosition& target : batch.targetPositions) {
        if (!target.symbol.isValid() || !target.targetWeight.isValid() || target.targetWeight.value <= 0.0) {
            continue;
        }

        const std::string symbol = target.symbol.text().toStdString();
        if (!symbol.empty() && seenSymbols.insert(symbol).second) {
            requestedSymbols.push_back(symbol);
        }
    }

    std::map<std::string, std::vector<domain::model::Bar>> barsBySymbol;
    try {
        if (!requestedSymbols.empty()) {
            barsBySymbol = stockDataProvider->getMultipleStockBars(
                requestedSymbols,
                snapshotDate.toStdString(),
                snapshotDate.toStdString());
        }
    } catch (...) {
        barsBySymbol.clear();
    }

    int pricedTargetCount = 0;
    int missingPriceCount = 0;
    for (domain::trading::TargetPosition& target : batch.targetPositions) {
        if (!target.symbol.isValid() || !target.targetWeight.isValid() || target.targetWeight.value <= 0.0) {
            continue;
        }

        const auto barsIt = barsBySymbol.find(target.symbol.text().toStdString());
        if (barsIt == barsBySymbol.end() || barsIt->second.empty()) {
            ++missingPriceCount;
            continue;
        }

        const double closePrice = barsIt->second.front().close;
        if (!std::isfinite(closePrice) || closePrice <= 0.0) {
            ++missingPriceCount;
            continue;
        }

        const qint64 estimatedQuantity = static_cast<qint64>(
            std::floor((config.initialCapital * target.targetWeight.value) / closePrice));
        if (estimatedQuantity <= 0) {
            ++missingPriceCount;
            continue;
        }

        target.referencePrice.value = closePrice;
        target.targetQuantity.value = estimatedQuantity;
        ++pricedTargetCount;
    }

    if (diagnostics != nullptr) {
        diagnostics->insert(QStringLiteral("snapshotDate"), snapshotDate);
        diagnostics->insert(QStringLiteral("pricedTargetCount"), pricedTargetCount);
        diagnostics->insert(QStringLiteral("missingPriceCount"), missingPriceCount);
    }
    return pricedTargetCount > 0;
}

} // namespace

FactorBacktestTradingPreviewSummary UnifiedFactorBacktestPreviewService::preview(
    const factor::BacktestResult& result,
    domain::backtest::StockDataProvider* stockDataProvider) const
{
    FactorBacktestTradingPreviewSummary summary;
    if (result.instanceId.empty()) {
        summary.status = FactorBacktestTradingPreviewSummary::Status::InvalidBacktestResult;
        summary.message = QStringLiteral("因子回测结果无效，无法执行统一交易预执行");
        summary.diagnosticCode = domain::strategy::DiagnosticCode::ValidationFailed;
        return summary;
    }

    if (result.latestFactorResult.isEmpty()) {
        summary.status = FactorBacktestTradingPreviewSummary::Status::MissingFactorSnapshot;
        summary.message = QStringLiteral("因子回测未保留最后一次有效截面，禁止伪造统一交易预执行输入");
        summary.diagnosticCode = domain::strategy::DiagnosticCode::ValidationFailed;
        return summary;
    }

    const DefaultFactorBacktestTradingAdapter adapter;
    const domain::trading::TargetPortfolio portfolio = adapter.buildTargetPortfolio(result.latestFactorResult, result.config);
    domain::trading::TradeIntentBatch batch = adapter.buildIntentBatch(portfolio, result.config);
    const domain::trading::TradingExecutionContext context = adapter.buildExecutionContext(result.config);
    QVariantMap enrichmentDiagnostics;
    enrichTargetPositionsFromSnapshot(batch, result.latestFactorResult, result.config, stockDataProvider, &enrichmentDiagnostics);
    summary.targetPositionCount = batch.targetPositions.size();

    if (!batch.isValid()) {
        summary.status = FactorBacktestTradingPreviewSummary::Status::InvalidBatch;
        summary.message = QStringLiteral("因子统一交易预执行批次为空");
        summary.diagnosticCode = domain::strategy::DiagnosticCode::ValidationFailed;
        return summary;
    }

    if (!context.isValid()) {
        summary.status = FactorBacktestTradingPreviewSummary::Status::InvalidContext;
        summary.message = result.config.initialCapital > 0.0
            ? QStringLiteral("因子统一交易预执行上下文无效")
            : QStringLiteral("因子统一交易预执行缺少初始资金，禁止生成订单计划");
        summary.diagnosticCode = domain::strategy::DiagnosticCode::ValidationFailed;
        return summary;
    }

    domain::trading::DefaultTradingCore tradingCore;
    const domain::trading::ExecutionResult executionResult = tradingCore.execute(batch, context);
    summary.status = previewStatusFromRiskDecision(executionResult.riskDecision.type);
    summary.riskReasonCode = executionResult.riskDecision.reasonCode;
    summary.orderPlanCount = executionResult.orderPlan.items.size();
    summary.acceptedOrderCount = executionResult.acceptedOrders.size();
    summary.fillCount = executionResult.fills.size();

    if (executionResult.isBlocked()) {
        summary.message = executionResult.riskDecision.message.trimmed().isEmpty()
            ? QStringLiteral("因子统一交易预执行被风险规则阻断")
            : executionResult.riskDecision.message;
        summary.diagnosticCode = domain::strategy::DiagnosticCode::RuntimeRuleBlocked;
        return summary;
    }

    if (summary.orderPlanCount == 0) {
        summary.status = FactorBacktestTradingPreviewSummary::Status::NoOrderPlan;
        const int pricedTargetCount = enrichmentDiagnostics.value(QStringLiteral("pricedTargetCount")).toInt();
        const int missingPriceCount = enrichmentDiagnostics.value(QStringLiteral("missingPriceCount")).toInt();
        summary.message = pricedTargetCount > 0
            ? QStringLiteral("因子统一交易预执行已接入，但当前截面未生成订单计划")
            : QStringLiteral("因子统一交易预执行已接入，但截面日期未取得有效参考价: missing=%1")
                  .arg(missingPriceCount);
        return summary;
    }

    summary.message = QStringLiteral("因子统一交易预执行完成");
    return summary;
}

} // namespace application::trading