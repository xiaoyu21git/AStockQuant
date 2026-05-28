#pragma once

#include "StrategyEnginePrimitiveTypes.h"

namespace domain::backtest::strategy_engine {

using SymbolIdList = ObjectList<SymbolId>;
using FactorIdList = ObjectList<FactorId>;

struct FactorWeight final {
    FactorId factorId;
    Weight weight;

    [[nodiscard]] bool isValid() const
    {
        return factorId.isValid() && weight.isValid();
    }
};

using FactorWeightList = ObjectList<FactorWeight>;

struct CandidateScore final {
    SymbolId symbolId;
    ScoreValue score;

    [[nodiscard]] bool isValid() const
    {
        return symbolId.isValid() && score.isValid();
    }
};

using CandidateScoreList = ObjectList<CandidateScore>;

struct TargetWeight final {
    SymbolId symbolId;
    Weight weight;

    [[nodiscard]] bool isValid() const
    {
        return symbolId.isValid() && weight.isValid();
    }
};

using TargetWeightList = ObjectList<TargetWeight>;

struct DateRange final {
    TradingDayIndex startDay;
    TradingDayIndex endDay;

    [[nodiscard]] bool isValid() const
    {
        return startDay.isValid() && endDay.isValid() && startDay <= endDay;
    }
};

struct StrategyIdentity final {
    StrategyId strategyId;
    StrategyBehaviorKind behaviorKind{StrategyBehaviorKind::Custom};
    ExecutionMode executionMode{ExecutionMode::EndOfDay};

    [[nodiscard]] bool isValid() const
    {
        return strategyId.isValid();
    }
};

struct FactorOverlayConfig final {
    bool enabled{false};
    FactorIdList factorIds;
    FactorWeightList weights;
    ScoreThreshold minimumCompositeScore;
    CandidateCount targetPositionCount;

    [[nodiscard]] bool isValid() const
    {
        if (!enabled) {
            return true;
        }

        if (!minimumCompositeScore.isValid() || !targetPositionCount.isPositive()) {
            return false;
        }

        if (factorIds.empty() || factorIds.size() != weights.size()) {
            return false;
        }

        for (const FactorId& factorId : factorIds) {
            if (!factorId.isValid()) {
                return false;
            }
        }

        for (const FactorWeight& weight : weights) {
            if (!weight.isValid()) {
                return false;
            }
        }

        return true;
    }
};

struct DecisionLayer final {
    LayerId id;
    LayerType type{LayerType::Strategic};
    UniverseId inputUniverseId;
    FactorOverlayConfig overlay;
    RuleTemplateId ruleTemplateId;
    CandidateCount targetPositionCount;
    CandidateCount evaluationIntervalDays;

    [[nodiscard]] bool isValid() const
    {
        return id.isValid()
            && inputUniverseId.isValid()
            && overlay.isValid()
            && targetPositionCount.isPositive()
            && evaluationIntervalDays.isPositive();
    }
};

using DecisionLayerList = ObjectList<DecisionLayer>;

struct StrategySpec final {
    DecisionLayerList layers;

    [[nodiscard]] bool isValid() const
    {
        if (layers.empty()) {
            return false;
        }

        for (const DecisionLayer& layer : layers) {
            if (!layer.isValid()) {
                return false;
            }
        }

        return true;
    }
};

struct UniverseSpec final {
    UniverseSelectionMode mode{UniverseSelectionMode::ExplicitSymbols};
    UniverseId universeId;
    SymbolIdList explicitSymbols;
    DatasetId datasetId;

    [[nodiscard]] bool isValid() const
    {
        if (!universeId.isValid()) {
            return false;
        }

        if (mode == UniverseSelectionMode::ExplicitSymbols) {
            if (explicitSymbols.empty()) {
                return false;
            }

            for (const SymbolId& symbolId : explicitSymbols) {
                if (!symbolId.isValid()) {
                    return false;
                }
            }
        }

        if (mode != UniverseSelectionMode::ExplicitSymbols && !datasetId.isValid()) {
            return false;
        }

        return true;
    }
};

struct MarketEnvironmentSpec final {
    MarketProfile profile{MarketProfile::GenericEquity};

    [[nodiscard]] bool isValid() const
    {
        return true;
    }
};

struct CostSpec final {
    CashAmount initialCapital;
    Ratio commissionRate;
    Ratio slippageRate;
    Ratio taxRate;

    [[nodiscard]] bool isValid() const
    {
        return initialCapital.isPositive()
            && commissionRate.isValid()
            && slippageRate.isValid()
            && taxRate.isValid();
    }
};

struct RiskSpec final {
    Ratio maxPositionRatio;
    Ratio maxSinglePositionRatio;
    Ratio maxDrawdownLimit;
    Ratio stopLossRate;

    [[nodiscard]] bool isValid() const
    {
        return maxPositionRatio.isValid()
            && maxSinglePositionRatio.isValid()
            && maxDrawdownLimit.isValid()
            && stopLossRate.isValid();
    }
};

struct ExecutionSpec final {
    ExecutionMode executionMode{ExecutionMode::EndOfDay};
    PositionSizingMethod positionSizingMethod{PositionSizingMethod::FixedFraction};
    bool enableShortSelling{false};
    CandidateCount rebalanceFrequencyDays;
    OrderType defaultOrderType{OrderType::MarketOnClose};

    [[nodiscard]] bool isValid() const
    {
        return rebalanceFrequencyDays.isPositive();
    }
};

struct DataSourceSpec final {
    DataSourceMode mode{DataSourceMode::Raw};
    DatasetId datasetId;

    [[nodiscard]] bool isValid() const
    {
        return mode != DataSourceMode::CacheDataset || datasetId.isValid();
    }
};

struct RuntimeOptions final {
    CandidateCount maxThreads;
    bool enableCache{false};
    DurationNs cacheTtl;

    [[nodiscard]] bool isValid() const
    {
        return maxThreads.isPositive() && cacheTtl.isValid();
    }
};

struct BacktestRequest final {
    OverlayBindingScopeId overlayBindingScopeId;
    StrategyIdentity identity;
    StrategySpec spec;
    UniverseSpec universeSpec;
    MarketEnvironmentSpec marketEnvironmentSpec;
    CostSpec costSpec;
    RiskSpec riskSpec;
    ExecutionSpec executionSpec;
    DataSourceSpec dataSourceSpec;
    RuntimeOptions runtimeOptions;
    DateRange window;
    std::optional<SymbolId> benchmarkSymbol;

    [[nodiscard]] bool isValid() const
    {
        return overlayBindingScopeId.isValid()
            && identity.isValid()
            && spec.isValid()
            && universeSpec.isValid()
            && marketEnvironmentSpec.isValid()
            && costSpec.isValid()
            && riskSpec.isValid()
            && executionSpec.isValid()
            && dataSourceSpec.isValid()
            && runtimeOptions.isValid()
            && window.isValid()
            && (!benchmarkSymbol.has_value() || benchmarkSymbol->isValid());
    }
};

struct RuleDecision final {
    RuleDecisionCode code{RuleDecisionCode::None};
    LayerId layerId;
    SymbolId symbolId;
    FactorId factorId;

    [[nodiscard]] bool isValid() const
    {
        return code != RuleDecisionCode::None;
    }
};

using RuleDecisionList = ObjectList<RuleDecision>;

struct ValidationIssue final {
    ValidationIssueCode code{ValidationIssueCode::None};
    LayerId layerId;
    SymbolId symbolId;

    [[nodiscard]] bool isValid() const
    {
        return code != ValidationIssueCode::None;
    }
};

using ValidationIssueList = ObjectList<ValidationIssue>;

struct EngineAssumption final {
    EngineAssumptionCode code{EngineAssumptionCode::None};
    LayerId layerId;

    [[nodiscard]] bool isValid() const
    {
        return code != EngineAssumptionCode::None;
    }
};

using EngineAssumptionList = ObjectList<EngineAssumption>;

} // namespace domain::backtest::strategy_engine