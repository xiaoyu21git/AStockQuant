#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace factor::bridge::check {

enum class OutcomeCode {
    Supported = 0,
    InvalidBacktestWindow = 1,
    MissingCustomExpression = 2,
    MissingRequiredFields = 3,
    DatasetMissing = 4,
    DatasetInvalid = 5,
    DatasetEmpty = 6,
    MissingOrEmptyFields = 7,
    InsufficientHistory = 8,
};

struct Input final {
    bool useCacheMode{false};
    bool hasPartialBacktestWindow{false};
    bool customExpressionRequired{false};
    bool customExpressionAvailable{false};
    bool hasValidDataSet{false};
    bool hasUsableDataSetRows{false};

    int selectedDatasetId{0};
    int availableTradeDateCount{0};
    int requiredWarmupTradingDays{0};

    std::vector<std::string> requiredFields;
    std::unordered_set<std::string> availableFields;
    std::unordered_set<std::string> unusableFields;
};

struct EvaluationResult final {
    OutcomeCode code{OutcomeCode::Supported};
    bool supported{true};
    std::vector<std::string> missingFields;
    std::vector<std::string> emptyValueFields;
};

[[nodiscard]] EvaluationResult evaluateSupport(const Input& input);

} // namespace factor::bridge::check
