#include "domain/factor/include/ConfigurableFactorDetail.h"

#include <algorithm>
#include <cctype>
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

CalculationResult ConfigurableFactorBase::calculateMacro(const CalculationContext& context) const
{
    const CommonParams& common = commonParams_;
    const MacroParams& macro = macroParams();
    const std::string benchmarkSymbol = [&]() {
        const std::string trimmed = trimAsciiWhitespace(macro.benchmarkSymbol);
        return trimmed.empty() ? std::string("000300.SH") : trimmed;
    }();
    const TechnicalPriceIndicatorSpec priceIndicator = technicalPriceIndicatorSpec(macro.priceType);
    const factor::bridge::FieldKey* priceFieldKey = priceIndicator.common.fieldKey;
    const std::string priceField = priceFieldKey ? std::string(priceFieldKey->c_str()) : std::string();
    const DataFrequency frequency = macro.macroFrequency;
    if (!priceIndicator.common.hasResolvedSource() || priceIndicator.common.sourceTable != SourceTable::DAILY_BAR) {
        return createHistoricalViewRuntimeError(context, "宏观因子包含不支持的 priceType 配置");
    }
    const int baseWindow = macro.macroWindow > 0 ? macro.macroWindow : (common.lookbackWindow > 0 ? common.lookbackWindow : common.window);
    const int resolvedWindow = (std::max)(3, baseWindow) * macroWindowScale(frequency);

    std::vector<MacroDimension> selectedDimensions;
    const ConfiguredMode macroResolvedConfigMode = ConfiguredMode::Configured;
    for (MacroDimension dimension : macro.macroDimensions) {
        if (dimension != MacroDimension::UNKNOWN
                && std::find(selectedDimensions.begin(), selectedDimensions.end(), dimension) == selectedDimensions.end()) {
            selectedDimensions.push_back(dimension);
        }
    }
    std::vector<MacroIndicator> selectedIndicators;
    for (MacroIndicator indicator : macro.macroIndicators) {
        if (indicator != MacroIndicator::UNKNOWN
                && std::find(selectedIndicators.begin(), selectedIndicators.end(), indicator) == selectedIndicators.end()) {
            selectedIndicators.push_back(indicator);
        }
    }
    std::vector<MacroIndicator> dimensionScopedIndicators;
    for (MacroIndicator indicator : selectedIndicators) {
        const MacroIndicatorSpec spec = macroIndicatorSpec(indicator);
        if (std::find(selectedDimensions.begin(), selectedDimensions.end(), spec.dimension) != selectedDimensions.end()
                && std::find(dimensionScopedIndicators.begin(), dimensionScopedIndicators.end(), indicator) == dimensionScopedIndicators.end()) {
            dimensionScopedIndicators.push_back(indicator);
        }
    }
    selectedIndicators = dimensionScopedIndicators;
    if (selectedDimensions.empty() || selectedIndicators.empty()) {
        return createHistoricalViewRuntimeError(context, "宏观因子必须显式提供 macroDimensions 和 macroIndicators");
    }

    std::vector<std::string> benchmarkFields;
    std::unordered_set<std::string> seenBenchmarkFields;
    benchmarkFields.reserve(static_cast<size_t>(selectedIndicators.size()));
    for (MacroIndicator indicator : selectedIndicators) {
        const MacroIndicatorSpec spec = macroIndicatorSpec(indicator);
        if (!spec.common.hasField()) {
            continue;
        }
        const std::string fieldName = spec.common.fieldKey->c_str();
        if (!fieldName.empty() && seenBenchmarkFields.insert(fieldName).second) {
            benchmarkFields.push_back(fieldName);
        }
    }

    const auto symbols = effectiveSymbols(context);
    if (symbols.empty() || selectedIndicators.empty()) {
        return createHistoricalViewRuntimeError(context, "宏观因子缺少可用标的或驱动指标");
    }

    CalculationContext effectiveContext = context;
    effectiveContext.symbols = symbols;

    const bool useLocalBatchCache = context.historicalView
        && (!activeBatchComputationCache || activeBatchComputationCache->historicalView != context.historicalView);

    CommonMetricParams macroRuntimeParams = common;
    macroRuntimeParams.frequency = frequency;

    auto calculateMacroBody = [&]() -> CalculationResult {
        return executeWithCommonParams(
            context,
            macroRuntimeParams,
            [&]() {
                std::string resolvedDate = context.date;
                std::tm anchorDate = {};
                const bool anchorDateValid = parseIsoDate(resolvedDate, anchorDate);
                if (anchorDateValid) {
                    if (frequency == DataFrequency::Weekly) {
                        const int dayOfWeek = isoDayOfWeek(anchorDate);
                        const int shiftToPreviousFriday = dayOfWeek >= kFridayIndex ? dayOfWeek - kFridayIndex : dayOfWeek + 2;
                        anchorDate = addDays(anchorDate, -shiftToPreviousFriday);
                    } else if (frequency == DataFrequency::Monthly) {
                        std::tm monthStart = anchorDate;
                        monthStart.tm_mday = 1;
                        monthStart.tm_isdst = -1;
                        std::mktime(&monthStart);
                        anchorDate = addDays(monthStart, -1);
                    } else if (frequency == DataFrequency::Quarterly) {
                        const int month = anchorDate.tm_mon + 1;
                        const int quarterStartMonth = ((month - 1) / 3) * 3 + 1;
                        std::tm quarterStart = anchorDate;
                        quarterStart.tm_mon = quarterStartMonth - 1;
                        quarterStart.tm_mday = 1;
                        quarterStart.tm_isdst = -1;
                        std::mktime(&quarterStart);
                        anchorDate = addDays(quarterStart, -1);
                    }
                    resolvedDate = formatIsoDate(anchorDate);
                }

                const int maxOffset = (std::max)(0, static_cast<int>(common.lookbackWindow));
                const int startOffset = common.lagEnabled ? (std::max)(1, static_cast<int>(common.lagPeriods)) : 0;
                const std::vector<std::string> benchmarkSymbols{benchmarkSymbol};
                for (int offset = startOffset; offset <= maxOffset; ++offset) {
                    const std::string candidate = anchorDateValid ? formatIsoDate(addDays(anchorDate, -offset)) : resolvedDate;
                    if (context.historicalView->getCrossSection(candidate, priceField, symbols).empty()) {
                        continue;
                    }

                    bool hasBenchmarkSeries = false;
                    for (const auto& fieldName : benchmarkFields) {
                        if (!context.historicalView->getCrossSection(candidate, fieldName, benchmarkSymbols).empty()) {
                            hasBenchmarkSeries = true;
                            break;
                        }
                    }
                    if (hasBenchmarkSeries) {
                        return candidate;
                    }
                }

                return resolvedDate;
            },
            [this, &context, &effectiveContext, &priceField, resolvedWindow, &benchmarkFields, &benchmarkSymbol, &selectedIndicators](const CommonRuntimeState& runtime, CalculationResult& result) {
                effectiveContext.date = runtime.effectiveDate;

                if (!context.historicalView->hasField(priceField)) {
                    const std::string error = "宏观因子 HistoricalView 回测缺少字段 " + priceField;
                    result.dataStatus = CalculationResult::createError(error).dataStatus;
                    result.metadata.set("error", json_helper::toJsonValue(error));
                    return;
                }
                for (const auto& fieldName : benchmarkFields) {
                    if (!context.historicalView->hasField(fieldName)) {
                        const std::string error = "宏观因子 HistoricalView 回测缺少字段 " + fieldName;
                        result.dataStatus = CalculationResult::createError(error).dataStatus;
                        result.metadata.set("error", json_helper::toJsonValue(error));
                        return;
                    }
                }

                std::unordered_map<std::string, double> weightedScores;
                std::unordered_map<std::string, int> scoreCounts;
                const auto priceSeriesBySymbol = fetchBatchSeriesMap(effectiveContext, priceField, resolvedWindow + 1);
                const SeriesMatrixBatch priceSeriesBatch = collectSeriesMatrix(priceSeriesBySymbol, 2);
                const Eigen::MatrixXd allSymbolReturns = buildReturnMatrix(priceSeriesBatch.values);
                std::unordered_map<std::string, std::vector<double>> benchmarkSeriesByField;
                if (context.historicalView && !benchmarkFields.empty()) {
                    const auto benchmarkBatchValues = context.historicalView->getBatchTimeSeries(
                        {benchmarkSymbol},
                        runtime.effectiveDate,
                        resolvedWindow + 1,
                        benchmarkFields);

                    for (const std::string& fieldName : benchmarkFields) {
                        const auto fieldIt = benchmarkBatchValues.find(fieldName);
                        if (fieldIt == benchmarkBatchValues.end()) {
                            continue;
                        }

                        const auto symbolIt = fieldIt->second.find(benchmarkSymbol);
                        if (symbolIt != fieldIt->second.end()) {
                            benchmarkSeriesByField.emplace(fieldName, symbolIt->second);
                        }
                    }
                }

                for (MacroIndicator indicator : selectedIndicators) {
                    const MacroIndicatorSpec spec = macroIndicatorSpec(indicator);
                    const double dimensionWeight = indicatorWeightForDimension(spec.dimension);

                    std::vector<double> benchmarkSeries;
                    if (spec.common.hasField()) {
                        const auto benchmarkIt = benchmarkSeriesByField.find(spec.common.fieldKey->c_str());
                        if (benchmarkIt != benchmarkSeriesByField.end() && benchmarkIt->second.size() >= 2) {
                            benchmarkSeries = benchmarkIt->second;
                        }
                    }

                    if (benchmarkSeries.size() < 2) {
                        continue;
                    }

                    const Eigen::VectorXd benchmarkReturns = buildReturnVector(benchmarkSeries);
                    if (benchmarkReturns.size() < 2 || allSymbolReturns.cols() < 2 || priceSeriesBatch.symbols.empty()) {
                        continue;
                    }

                    const Eigen::VectorXd correlations = batchCorrelate(allSymbolReturns, benchmarkReturns);
                    for (int row = 0; row < correlations.size(); ++row) {
                        const double correlation = correlations(row);
                        if (!std::isfinite(correlation)) {
                            continue;
                        }

                        const std::string& symbol = priceSeriesBatch.symbols[static_cast<size_t>(row)];
                        weightedScores[symbol] += correlation * spec.direction * dimensionWeight;
                        scoreCounts[symbol] += 1;
                    }
                }

                if (weightedScores.empty()) {
                    result.metadata.set("emptyReason", json_helper::toJsonValue("宏观因子字段存在但没有可用代理数值"));
                    return;
                }

                for (const auto& [symbol, weightedScore] : weightedScores) {
                    const int count = scoreCounts[symbol];
                    if (count <= 0) {
                        continue;
                    }
                    result.values[symbol] = std::clamp(std::tanh(weightedScore / static_cast<double>(count)), -1.0, 1.0);
                }
            },
            [](const CommonRuntimeState&, CalculationResult&) {},
            [&](const CommonRuntimeState&, CalculationResult& result) {
                const double coverage = static_cast<double>(result.values.size()) / static_cast<double>((std::max)(size_t(1), symbols.size()));
                result.dataStatus.availability = result.values.size() == symbols.size() ? DataAvailability::AVAILABLE : DataAvailability::PARTIAL;
                result.dataStatus.coverage = coverage;
                result.dataStatus.message = "使用宏观代理敏感度运行时";
                result.metadata.set("macroConfigMode", json_helper::toJsonValue(static_cast<int>(macroResolvedConfigMode)));
                result.metadata.set("macroDimensions", macroDimensionArrayJson(selectedDimensions));
                result.metadata.set("macroIndicators", macroIndicatorArrayJson(selectedIndicators));
                result.metadata.set("macroFrequency", json_helper::toJsonValue(static_cast<int>(frequency)));
                result.metadata.set("macroWindow", json_helper::toJsonValue(baseWindow));
                result.metadata.set("macroMode", json_helper::toJsonValue(static_cast<int>(MacroComputationMode::ProxySensitivity)));
                result.metadata.set("benchmarkSymbol", json_helper::toJsonValue(benchmarkSymbol));
            },
            "使用宏观代理敏感度运行时");
    };

    if (useLocalBatchCache) {
        BatchComputationCache cache;
        cache.historicalView = context.historicalView;
        BatchComputationCacheScope scope(cache);
        return calculateMacroBody();
    }

    return calculateMacroBody();
}

} // namespace factor