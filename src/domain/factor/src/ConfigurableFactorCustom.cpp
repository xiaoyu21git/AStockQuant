#include "domain/factor/include/ConfigurableFactorDetail.h"
#include "domain/factor/include/CustomExpressionUtils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <unordered_set>

namespace factor {

using namespace configurable_factor_detail;

namespace {

constexpr int kMonthsPerYear = 12;
constexpr int kFridayIndex = 5;
constexpr int kIsoWeekLength = 7;

std::string trimAsciiWhitespace(std::string text)
{
    const auto isSpace = [](unsigned char ch) {
        return std::isspace(ch) != 0;
    };
    const auto begin = std::find_if_not(text.begin(), text.end(), isSpace);
    const auto end = std::find_if_not(text.rbegin(), text.rend(), isSpace).base();
    if (begin >= end) {
        return "";
    }
    return std::string(begin, end);
}

std::string toLowerAscii(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

bool parseIsoDate(const std::string& text, std::tm& out)
{
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        return false;
    }

    try {
        const int year = std::stoi(text.substr(0, 4));
        const int month = std::stoi(text.substr(5, 2));
        const int day = std::stoi(text.substr(8, 2));
        if (month < 1 || month > kMonthsPerYear || day < 1 || day > 31) {
            return false;
        }

        std::tm candidate = {};
        candidate.tm_year = year - 1900;
        candidate.tm_mon = month - 1;
        candidate.tm_mday = day;
        candidate.tm_isdst = -1;
        if (std::mktime(&candidate) == -1) {
            return false;
        }
        out = candidate;
        return true;
    } catch (...) {
        return false;
    }
}

std::string formatIsoDate(const std::tm& value)
{
    char buffer[11] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &value);
    return std::string(buffer);
}

std::tm addDays(const std::tm& base, int dayOffset)
{
    std::tm shifted = base;
    shifted.tm_mday += dayOffset;
    shifted.tm_isdst = -1;
    std::mktime(&shifted);
    return shifted;
}

int isoDayOfWeek(const std::tm& value)
{
    const int wday = value.tm_wday;
    return ((wday + 6) % kIsoWeekLength) + 1;
}

} // namespace

std::unordered_map<std::string, double> ConfigurableFactorBase::evaluateCustomExpression(
    const CalculationContext& context,
    const std::string& expression,
    const std::vector<std::string>& symbols,
    std::string* errorMessage) const
{
    std::unordered_map<std::string, double> results;
    const std::string resolvedExpression = trimAsciiWhitespace(expression);
    if (resolvedExpression.empty()) {
        if (errorMessage) {
            *errorMessage = "自定义因子必须显式提供 expression";
        }
        return results;
    }
    std::string parseError;
    const std::vector<std::string> rpn = factor::custom_expression::toRpn(toLowerAscii(resolvedExpression), &parseError);
    if (rpn.empty()) {
        if (errorMessage) {
            *errorMessage = parseError;
        }
        return results;
    }

    const std::vector<std::string> variables = factor::custom_expression::extractVariables(toLowerAscii(resolvedExpression));
    std::unordered_map<std::string, std::string> sourceFieldByVariable;
    std::vector<std::string> batchFields;
    std::unordered_set<std::string> seenBatchFields;
    for (const std::string& variable : variables) {
        const auto* binding = findCustomVariableBinding(variable);
        std::string sourceField = variable;
        if (binding) {
            sourceField = trimAsciiWhitespace(binding->field);
            if (sourceField.empty()) {
                if (errorMessage && errorMessage->empty()) {
                    *errorMessage = "自定义表达式变量 " + variable + " 缺少 sourceField 配置";
                }
                return results;
            }
        }
        sourceFieldByVariable[variable] = sourceField;
        if (!sourceField.empty() && seenBatchFields.insert(sourceField).second) {
            batchFields.push_back(sourceField);
        }
    }

    std::unordered_map<std::string, std::unordered_map<std::string, double>> batchCrossSections;
    if (context.historicalView && !batchFields.empty()) {
        for (const auto& fieldName : batchFields) {
            if (!context.historicalView->hasField(fieldName)) {
                if (errorMessage && errorMessage->empty()) {
                    *errorMessage = "自定义表达式缺少可用字段 " + fieldName;
                }
                return results;
            }
        }
        batchCrossSections = context.historicalView->getBatchCrossSections(context.date, symbols, batchFields);
        if (activeBatchComputationCache && activeBatchComputationCache->historicalView == context.historicalView) {
            for (const auto& [fieldName, symbolValues] : batchCrossSections) {
                std::string batchKey;
                buildBatchCrossSectionKey(batchKey, context.date, fieldName);
                activeBatchComputationCache->crossSectionsByKey[batchKey] = symbolValues;
            }
        }
    }

    for (const auto& symbol : symbols) {
        std::unordered_map<std::string, double> variableMap;
        bool missingVariable = false;
        for (const std::string& variable : variables) {
            const auto sourceFieldIt = sourceFieldByVariable.find(variable);
            if (sourceFieldIt == sourceFieldByVariable.end()) {
                if (errorMessage && errorMessage->empty()) {
                    *errorMessage = "自定义表达式变量 " + variable + " 缺少字段映射";
                }
                missingVariable = true;
                break;
            }

            const auto fieldIt = batchCrossSections.find(sourceFieldIt->second);
            if (fieldIt == batchCrossSections.end()) {
                missingVariable = true;
                break;
            }
            const auto valueIt = fieldIt->second.find(symbol);
            if (valueIt == fieldIt->second.end()) {
                missingVariable = true;
                break;
            }
            variableMap[variable] = valueIt->second;
        }
        if (missingVariable) {
            continue;
        }

        std::string evalError;
        const auto evaluated = factor::custom_expression::evaluateRpn(rpn, variableMap, &evalError);
        if (!evaluated.has_value() || !std::isfinite(*evaluated)) {
            continue;
        }
        results[symbol] = *evaluated;
    }
    return results;
}

CalculationResult ConfigurableFactorBase::calculateCustom(const CalculationContext& context) const
{
    const CommonParams& common = commonParams_;
    const CustomParams& custom = customParams();
    const auto symbols = effectiveSymbols(context);
    const ConfiguredMode customResolvedExpressionMode = ConfiguredMode::Configured;

    const bool useLocalBatchCache = context.historicalView
        && (!activeBatchComputationCache || activeBatchComputationCache->historicalView != context.historicalView);

    auto calculateCustomBody = [&]() -> CalculationResult {
        return executeWithCommonParams(
            context,
            common,
            [&]() {
                std::string effectiveDate = context.date;
                std::tm anchorDate = {};
                const bool anchorDateValid = parseIsoDate(effectiveDate, anchorDate);
                if (anchorDateValid) {
                    if (common.frequency == DataFrequency::Weekly) {
                        const int dayOfWeek = isoDayOfWeek(anchorDate);
                        const int shiftToPreviousFriday = dayOfWeek >= kFridayIndex ? dayOfWeek - kFridayIndex : dayOfWeek + 2;
                        anchorDate = addDays(anchorDate, -shiftToPreviousFriday);
                    } else if (common.frequency == DataFrequency::Monthly) {
                        std::tm monthStart = anchorDate;
                        monthStart.tm_mday = 1;
                        monthStart.tm_isdst = -1;
                        std::mktime(&monthStart);
                        anchorDate = addDays(monthStart, -1);
                    }
                    effectiveDate = formatIsoDate(anchorDate);
                }

                const int maxOffset = (std::max)(0, static_cast<int>(common.lookbackWindow));
                const int startOffset = common.lagEnabled ? (std::max)(1, static_cast<int>(common.lagPeriods)) : 0;
                for (int offset = startOffset; offset <= maxOffset; ++offset) {
                    const std::string candidate = anchorDateValid
                        ? formatIsoDate(addDays(anchorDate, -offset))
                        : effectiveDate;
                    CalculationContext candidateContext = context;
                    candidateContext.date = candidate;
                    candidateContext.symbols = symbols;
                    std::string candidateError;
                    const auto candidateValues = evaluateCustomExpression(
                        candidateContext,
                        custom.expression,
                        symbols,
                        &candidateError);
                    if (!candidateValues.empty()) {
                        return candidate;
                    }
                }

                return effectiveDate;
            },
            [this, &context, &custom, &symbols](const CommonRuntimeState& runtime, CalculationResult& result) {
                CalculationContext effectiveContext = context;
                effectiveContext.date = runtime.effectiveDate;
                effectiveContext.symbols = symbols;

                std::string errorMessage;
                result.values = evaluateCustomExpression(
                    effectiveContext,
                    custom.expression,
                    symbols,
                    &errorMessage);

                if (!errorMessage.empty()) {
                    result.dataStatus = CalculationResult::createError(errorMessage).dataStatus;
                    result.metadata.set("error", json_helper::toJsonValue(errorMessage));
                    return;
                }

                if (result.values.empty()) {
                    result.metadata.set("emptyReason", json_helper::toJsonValue("自定义表达式字段存在但没有可用数值"));
                }
            },
            [](const CommonRuntimeState&, CalculationResult&) {},
            [&](const CommonRuntimeState&, CalculationResult& result) {
                if (result.dataStatus.isValid()) {
                    const double coverage = static_cast<double>(result.values.size()) / static_cast<double>((std::max)(size_t(1), symbols.size()));
                    result.dataStatus.availability = result.values.size() == symbols.size() ? DataAvailability::AVAILABLE : DataAvailability::PARTIAL;
                    result.dataStatus.coverage = coverage;
                }
                result.metadata.set("expression", json_helper::toJsonValue(custom.expression));
                result.metadata.set("customExpressionMode", json_helper::toJsonValue(static_cast<int>(customResolvedExpressionMode)));
                result.metadata.set("variableCount", json_helper::toJsonValue(static_cast<int>(custom.variables.size())));
            },
            "使用自定义表达式");
    };

    if (useLocalBatchCache) {
        BatchComputationCache cache;
        cache.historicalView = context.historicalView;
        BatchComputationCacheScope scope(cache);
        return calculateCustomBody();
    }

    return calculateCustomBody();
}

} // namespace factor