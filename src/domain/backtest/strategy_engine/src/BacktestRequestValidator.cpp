#include "BacktestRequestValidator.h"

namespace domain::backtest::strategy_engine {

ValidationIssueList BacktestRequestValidator::validate(const BacktestRequest& request) const
{
    ValidationIssueList issues;

    if (!request.identity.isValid()) {
        issues.add(ValidationIssue{ValidationIssueCode::InvalidIdentity, {}, {}});
    }

    if (!request.window.isValid()) {
        issues.add(ValidationIssue{ValidationIssueCode::InvalidWindow, {}, {}});
    }

    if (!request.universeSpec.isValid()) {
        issues.add(ValidationIssue{ValidationIssueCode::InvalidUniverse, {}, {}});
    }

    if (!request.costSpec.isValid()) {
        issues.add(ValidationIssue{ValidationIssueCode::InvalidCost, {}, {}});
    }

    if (!request.riskSpec.isValid()) {
        issues.add(ValidationIssue{ValidationIssueCode::InvalidRisk, {}, {}});
    }

    if (!request.executionSpec.isValid()) {
        issues.add(ValidationIssue{ValidationIssueCode::InvalidExecution, {}, {}});
    }

    if (!request.dataSourceSpec.isValid()) {
        issues.add(ValidationIssue{ValidationIssueCode::InvalidDataSource, {}, {}});
    }

    if (!request.runtimeOptions.isValid()) {
        issues.add(ValidationIssue{ValidationIssueCode::InvalidRuntimeOptions, {}, {}});
    }

    for (const DecisionLayer& layer : request.spec.layers) {
        if (!layer.isValid()) {
            issues.add(ValidationIssue{ValidationIssueCode::InvalidLayer, layer.id, {}});
        }
    }

    return issues;
}

bool BacktestRequestValidator::accepts(const BacktestRequest& request) const
{
    return validate(request).empty();
}

} // namespace domain::backtest::strategy_engine