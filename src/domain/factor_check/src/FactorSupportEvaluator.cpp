#include "factor_check/FactorSupportEvaluator.h"

#include <algorithm>

namespace factor::check {

FactorCheckResult FactorSupportEvaluator::evaluate(const FactorCheckInput& input) const
{
    FactorCheckResult result;

    if (!input.useCacheMode && input.hasPartialBacktestWindow) {
        result.code = OutcomeCode::InvalidBacktestWindow;
        result.supported = false;
        return result;
    }

    if (input.customExpressionRequired && !input.customExpressionAvailable) {
        result.code = OutcomeCode::MissingCustomExpression;
        result.supported = false;
        return result;
    }

    if (input.requiredFields.empty()) {
        result.code = OutcomeCode::MissingRequiredFields;
        result.supported = false;
        return result;
    }

    if (!input.useCacheMode) {
        result.code = OutcomeCode::Supported;
        result.supported = true;
        return result;
    }

    if (input.selectedDatasetId <= 0) {
        result.code = OutcomeCode::DatasetMissing;
        result.supported = false;
        return result;
    }

    if (!input.hasValidDataSet) {
        result.code = OutcomeCode::DatasetInvalid;
        result.supported = false;
        return result;
    }

    if (!input.hasUsableDataSetRows) {
        result.code = OutcomeCode::DatasetEmpty;
        result.supported = false;
        return result;
    }

    for (const std::string& field : input.requiredFields) {
        if (input.availableFields.find(field) == input.availableFields.end()) {
            result.missingFields.push_back(field);
            continue;
        }
        if (input.unusableFields.find(field) != input.unusableFields.end()) {
            result.emptyValueFields.push_back(field);
        }
    }

    if (!result.missingFields.empty() || !result.emptyValueFields.empty()) {
        std::sort(result.missingFields.begin(), result.missingFields.end());
        result.missingFields.erase(
            std::unique(result.missingFields.begin(), result.missingFields.end()),
            result.missingFields.end());

        std::sort(result.emptyValueFields.begin(), result.emptyValueFields.end());
        result.emptyValueFields.erase(
            std::unique(result.emptyValueFields.begin(), result.emptyValueFields.end()),
            result.emptyValueFields.end());

        result.code = OutcomeCode::MissingOrEmptyFields;
        result.supported = false;
        return result;
    }

    if (input.availableTradeDateCount > 0
        && input.requiredWarmupTradingDays > 0
        && input.availableTradeDateCount < input.requiredWarmupTradingDays) {
        result.code = OutcomeCode::InsufficientHistory;
        result.supported = false;
        return result;
    }

    result.code = OutcomeCode::Supported;
    result.supported = true;
    return result;
}

} // namespace factor::check
