#pragma once

#include "BacktestRequest.h"

namespace domain::backtest {

struct BacktestRunMetadata final {
    strategy::BacktestTaskId taskId;
    QDateTime startedAt;
    QDateTime finishedAt;
    double elapsedSeconds{0.0};
    strategy::BacktestRunStatus status{strategy::BacktestRunStatus::Failed};

    [[nodiscard]] bool isValid() const
    {
        return taskId.isValid() && startedAt.isValid() && finishedAt.isValid() && elapsedSeconds >= 0.0;
    }
};

struct TradeRecord final {
    strategy::SymbolCode symbol;
    strategy::RuntimeOrderSide side{strategy::RuntimeOrderSide::Buy};
    strategy::Quantity quantity;
    strategy::Money price;
    QDateTime executedAt;

    [[nodiscard]] bool isValid() const
    {
        return symbol.isValid() && quantity.isPositive() && price.isPositive() && executedAt.isValid();
    }
};

struct BacktestResultDto final {
    BacktestRunMetadata runMetadata;
    BacktestRequest configSnapshot;
    strategy::PerformanceSummaryMetrics performanceSummary;
    strategy::TradeStatistics tradeStatistics;
    strategy::RiskMetrics riskMetrics;
    strategy::TimeSeriesSnapshot timeSeries;
    QVector<TradeRecord> tradeRecords;
    strategy::UniverseResolutionSummary universeResolutionSummary;
    strategy::RuleTemplateSummary ruleTemplateSummary;
    int ruleTemplateEntryBlockCount{0};
    int ruleTemplateForcedExitCount{0};
    QVector<strategy::DiagnosticMessage> diagnostics;

    [[nodiscard]] bool isValid() const
    {
        return runMetadata.isValid() && configSnapshot.isValid() && timeSeries.isValid();
    }

    [[nodiscard]] bool isSuccessful() const
    {
        return runMetadata.status == strategy::BacktestRunStatus::Succeeded;
    }
};

} // namespace domain::backtest