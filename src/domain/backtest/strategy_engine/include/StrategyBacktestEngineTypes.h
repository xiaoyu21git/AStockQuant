#pragma once

#include "StrategyEngineModelTypes.h"
#include "StrategyEngineRuntimeTypes.h"

namespace domain::backtest::strategy_engine {

struct TradeRecord final {
    OrderId orderId;
    SymbolId symbolId;
    OrderSide side{OrderSide::Buy};
    ShareQuantity quantity;
    PriceValue executionPrice;
    TradingDayIndex tradingDay;

    [[nodiscard]] bool isValid() const
    {
        return orderId.isValid()
            && symbolId.isValid()
            && quantity.isPositive()
            && executionPrice.isPositive()
            && tradingDay.isValid();
    }
};

using TradeRecordList = ObjectList<TradeRecord>;

struct TradeStatistics final {
    CandidateCount tradeCount;
    CandidateCount winCount;
    Ratio winRate;

    [[nodiscard]] bool isValid() const
    {
        return winRate.isValid() && winCount.value() <= tradeCount.value();
    }
};

struct PerformanceSummary final {
    CashAmount startingEquity;
    CashAmount endingEquity;
    ReturnValue totalReturn;
    ReturnValue annualizedReturn;
    Ratio maxDrawdown;

    [[nodiscard]] bool isValid() const
    {
        return startingEquity.isPositive()
            && endingEquity.isNonNegative()
            && totalReturn.isValid()
            && annualizedReturn.isValid()
            && maxDrawdown.isValid();
    }
};

struct RiskMetrics final {
    Ratio maxDrawdown;
    Ratio averageExposure;
    ReturnValue volatility;

    [[nodiscard]] bool isValid() const
    {
        return maxDrawdown.isValid() && averageExposure.isValid() && volatility.isValid();
    }
};

struct TimeSeriesPoint final {
    TradingDayIndex tradingDay;
    CashAmount equity;
    ReturnValue periodReturn;

    [[nodiscard]] bool isValid() const
    {
        return tradingDay.isValid() && equity.isNonNegative() && periodReturn.isValid();
    }
};

using TimeSeries = ObjectList<TimeSeriesPoint>;

struct UniverseResolutionSummary final {
    UniverseId universeId;
    CandidateCount requestedSymbolCount;
    CandidateCount resolvedSymbolCount;

    [[nodiscard]] bool isValid() const
    {
        return universeId.isValid()
            && requestedSymbolCount.isPositive()
            && resolvedSymbolCount.isPositive();
    }
};

struct RuleTemplateSummary final {
    CandidateCount boundTemplateCount;
    CandidateCount matchedTemplateCount;
    CandidateCount blockedTemplateCount;
    CandidateCount forcedExitTemplateCount;
    RuleDecisionList recentDecisions;

    [[nodiscard]] bool isValid() const
    {
        for (const RuleDecision& decision : recentDecisions) {
            if (!decision.isValid()) {
                return false;
            }
        }

        return true;
    }
};

struct LayerContribution final {
    LayerId layerId;
    ReturnValue contributionReturn;
    Ratio hitRate;

    [[nodiscard]] bool isValid() const
    {
        return layerId.isValid() && contributionReturn.isValid() && hitRate.isValid();
    }
};

using LayerContributionList = ObjectList<LayerContribution>;

struct LayerAttribution final {
    LayerContributionList contributions;

    [[nodiscard]] bool isValid() const
    {
        for (const LayerContribution& contribution : contributions) {
            if (!contribution.isValid()) {
                return false;
            }
        }

        return true;
    }
};

struct BenchmarkComparison final {
    bool enabled{false};
    SymbolId benchmarkSymbol;
    ReturnValue benchmarkReturn;
    ReturnValue excessReturn;

    [[nodiscard]] bool isValid() const
    {
        if (!enabled) {
            return true;
        }

        return benchmarkSymbol.isValid() && benchmarkReturn.isValid() && excessReturn.isValid();
    }
};

struct DiagnosticRecord final {
    DiagnosticSeverity severity{DiagnosticSeverity::Info};
    EngineAssumptionCode assumptionCode{EngineAssumptionCode::None};
    ValidationIssueCode validationCode{ValidationIssueCode::None};
    TradingDayIndex tradingDay;
    LayerId layerId;
    SymbolId symbolId;

    [[nodiscard]] bool isValid() const
    {
        return assumptionCode != EngineAssumptionCode::None || validationCode != ValidationIssueCode::None;
    }
};

using DiagnosticRecordList = ObjectList<DiagnosticRecord>;

struct Diagnostics final {
    EngineAssumptionList assumptions;
    ValidationIssueList validationIssues;
    DiagnosticRecordList records;
    DurationNs elapsed;
    MemoryBytes peakMemory;

    [[nodiscard]] bool isValid() const
    {
        if (!elapsed.isValid() || !peakMemory.isValid()) {
            return false;
        }

        for (const EngineAssumption& assumption : assumptions) {
            if (!assumption.isValid()) {
                return false;
            }
        }

        for (const ValidationIssue& validationIssue : validationIssues) {
            if (!validationIssue.isValid()) {
                return false;
            }
        }

        for (const DiagnosticRecord& record : records) {
            if (!record.isValid()) {
                return false;
            }
        }

        return true;
    }
};

struct RunMetadata final {
    RunId runId;
    TimestampNs startedAt;
    TimestampNs finishedAt;
    DurationNs elapsed;
    BacktestRunState state{BacktestRunState::Created};

    [[nodiscard]] bool isValid() const
    {
        return runId.isValid()
            && startedAt.isValid()
            && finishedAt.isValid()
            && elapsed.isValid();
    }
};

struct BacktestResultDto final {
    RunMetadata runMetadata;
    BacktestRequest configSnapshot;
    PerformanceSummary performance;
    TradeStatistics tradeStatistics;
    RiskMetrics riskMetrics;
    TimeSeries timeSeries;
    TradeRecordList tradeRecords;
    UniverseResolutionSummary universeResolution;
    RuleTemplateSummary ruleSummary;
    LayerAttribution layerAttribution;
    BenchmarkComparison benchmark;
    Diagnostics diagnostics;

    [[nodiscard]] bool isValid() const
    {
        return runMetadata.isValid()
            && configSnapshot.isValid()
            && performance.isValid()
            && tradeStatistics.isValid()
            && riskMetrics.isValid()
            && universeResolution.isValid()
            && ruleSummary.isValid()
            && layerAttribution.isValid()
            && benchmark.isValid()
            && diagnostics.isValid();
    }
};

} // namespace domain::backtest::strategy_engine
