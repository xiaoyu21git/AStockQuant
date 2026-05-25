#include "../include/UnifiedStrategyBacktestPreviewService.h"

#include "../include/DefaultStrategyBacktestTradingAdapter.h"
#include "../../../domain/backtest/include/StockDataProvider.h"
#include "../../../domain/trading/include/DefaultTradingCore.h"

#include <cmath>

namespace application::trading {
namespace {

StrategyBacktestTradingPreviewSummary::Status previewStatusFromRiskDecision(
    domain::trading::RiskDecisionType type)
{
    switch (type) {
    case domain::trading::RiskDecisionType::Warn:
        return StrategyBacktestTradingPreviewSummary::Status::Warn;
    case domain::trading::RiskDecisionType::Block:
        return StrategyBacktestTradingPreviewSummary::Status::Blocked;
    case domain::trading::RiskDecisionType::ForceReduce:
        return StrategyBacktestTradingPreviewSummary::Status::ForceReduce;
    case domain::trading::RiskDecisionType::TradingHalt:
        return StrategyBacktestTradingPreviewSummary::Status::TradingHalt;
    case domain::trading::RiskDecisionType::Pass:
    default:
        return StrategyBacktestTradingPreviewSummary::Status::Pass;
    }
}

std::string dataSourceModeText(domain::strategy::DataSourceMode mode)
{
    switch (mode) {
    case domain::strategy::DataSourceMode::Cleaned:
        return "cleaned";
    case domain::strategy::DataSourceMode::CacheDataset:
        return "cache";
    case domain::strategy::DataSourceMode::Raw:
    default:
        return "raw";
    }
}

bool enrichTargetPositionsFromSnapshot(
    domain::trading::TradeIntentBatch& batch,
    const domain::backtest::BacktestRequest& request,
    domain::backtest::StockDataProvider* stockDataProvider,
    QVariantMap* diagnostics)
{
    if (stockDataProvider == nullptr || batch.targetPositions.isEmpty()) {
        return false;
    }

    const double initialCapital = request.costSpec.initialCapital.value;
    if (!std::isfinite(initialCapital) || initialCapital <= 0.0) {
        return false;
    }

    const QString snapshotDate = request.window.startDate.toString(Qt::ISODate);
    if (snapshotDate.isEmpty()) {
        return false;
    }

    stockDataProvider->setDataSourceContext(
        dataSourceModeText(request.dataSourceSpec.mode),
        request.dataSourceSpec.datasetId.value);

    int pricedTargetCount = 0;
    int missingPriceCount = 0;
    for (domain::trading::TargetPosition& target : batch.targetPositions) {
        if (!target.symbol.isValid() || !target.targetWeight.isValid() || target.targetWeight.value <= 0.0) {
            continue;
        }

        try {
            const std::vector<domain::model::Bar> bars = stockDataProvider->getStockBars(
                target.symbol.text().toStdString(),
                snapshotDate.toStdString(),
                snapshotDate.toStdString());
            if (bars.empty()) {
                ++missingPriceCount;
                continue;
            }

            const double closePrice = bars.front().close;
            if (!std::isfinite(closePrice) || closePrice <= 0.0) {
                ++missingPriceCount;
                continue;
            }

            const qint64 estimatedQuantity = static_cast<qint64>(
                std::floor((initialCapital * target.targetWeight.value) / closePrice));
            if (estimatedQuantity <= 0) {
                ++missingPriceCount;
                continue;
            }

            target.referencePrice.value = closePrice;
            target.targetQuantity.value = estimatedQuantity;
            ++pricedTargetCount;
        } catch (...) {
            ++missingPriceCount;
        }
    }

    if (diagnostics != nullptr) {
        diagnostics->insert(QStringLiteral("snapshotDate"), snapshotDate);
        diagnostics->insert(QStringLiteral("pricedTargetCount"), pricedTargetCount);
        diagnostics->insert(QStringLiteral("missingPriceCount"), missingPriceCount);
    }
    return pricedTargetCount > 0;
}

} // namespace

StrategyBacktestTradingPreviewSummary UnifiedStrategyBacktestPreviewService::preview(
    const domain::backtest::BacktestRequest& request,
    domain::backtest::StockDataProvider* stockDataProvider) const
{
    StrategyBacktestTradingPreviewSummary summary;
    if (!request.isValid()) {
        summary.status = StrategyBacktestTradingPreviewSummary::Status::InvalidRequest;
        summary.message = QStringLiteral("统一交易预执行请求无效");
        summary.diagnosticCode = domain::strategy::DiagnosticCode::ValidationFailed;
        return summary;
    }

    const DefaultStrategyBacktestTradingAdapter adapter;
    domain::trading::TradeIntentBatch batch = adapter.buildIntentBatch(request);
    const domain::trading::TradingExecutionContext context = adapter.buildExecutionContext(request);
    QVariantMap enrichmentDiagnostics;
    enrichTargetPositionsFromSnapshot(batch, request, stockDataProvider, &enrichmentDiagnostics);
    summary.intentCount = batch.intents.size();
    summary.targetPositionCount = batch.targetPositions.size();

    if (!batch.isValid()) {
        summary.status = StrategyBacktestTradingPreviewSummary::Status::InvalidBatch;
        summary.message = QStringLiteral("统一交易预执行批次为空");
        summary.diagnosticCode = domain::strategy::DiagnosticCode::ValidationFailed;
        return summary;
    }

    if (!context.isValid()) {
        summary.status = StrategyBacktestTradingPreviewSummary::Status::InvalidContext;
        summary.message = QStringLiteral("统一交易预执行上下文无效");
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
            ? QStringLiteral("统一交易预执行被风险规则阻断")
            : executionResult.riskDecision.message;
        summary.diagnosticCode = domain::strategy::DiagnosticCode::RuntimeRuleBlocked;
        return summary;
    }

    const QString executionStatus = executionResult.diagnostics.value(QStringLiteral("status")).toString().trimmed();
    if (summary.orderPlanCount == 0) {
        summary.status = StrategyBacktestTradingPreviewSummary::Status::NoOrderPlan;
        const int pricedTargetCount = enrichmentDiagnostics.value(QStringLiteral("pricedTargetCount")).toInt();
        const int missingPriceCount = enrichmentDiagnostics.value(QStringLiteral("missingPriceCount")).toInt();
        summary.message = executionStatus.isEmpty()
            ? QStringLiteral("统一交易预执行已接入，但当前策略链尚未生成订单计划")
            : QStringLiteral("统一交易预执行状态=%1").arg(executionStatus);
        if (stockDataProvider != nullptr && pricedTargetCount <= 0) {
            summary.message = QStringLiteral("统一交易预执行已接入，但回测起始日未取得有效参考价: missing=%1")
                .arg(missingPriceCount);
        }
        summary.diagnosticCode = domain::strategy::DiagnosticCode::None;
        return summary;
    }

    summary.message = QStringLiteral("统一交易预执行完成");
    return summary;
}

} // namespace application::trading