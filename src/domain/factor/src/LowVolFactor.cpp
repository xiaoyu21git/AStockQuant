#include "domain/factor/include/LowVolFactor.h"
#include "domain/factor/include/ConfigurableFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include "domain/factor/include/HistoricalView.h"
#include "ui/bridge/include/DataFetchFieldContractUtils.h"

#include <ta_libc.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <numeric>
#include <sstream>
#include <unordered_map>

namespace factor {

namespace {

namespace lowvol_json {

constexpr const char* kWindowKey = "window";
constexpr const char* kBenchmarkSymbolKey = "benchmarkSymbol";
constexpr const char* kComponentsKey = "components";
constexpr const char* kVolatilityWeightKey = "volatilityWeight";
constexpr const char* kDrawdownWeightKey = "drawdownWeight";
constexpr const char* kBetaWeightKey = "betaWeight";

} // namespace lowvol_json

LowVolFactor::Params lowVolParamsFromJson(const foundation::json::JsonFacade& json)
{
    LowVolFactor::Params params;
    params.fromJson(json);

    if (json.has(lowvol_json::kWindowKey)) params.window = json.get(lowvol_json::kWindowKey).asInt();
    if (json.has(lowvol_json::kBenchmarkSymbolKey)) params.benchmarkSymbol = json.get(lowvol_json::kBenchmarkSymbolKey).asString();
    if (json.has(lowvol_json::kComponentsKey)) {
        params.components.clear();
        const auto componentList = json.get(lowvol_json::kComponentsKey);
        if (!componentList.isArray()) {
            throw std::runtime_error("components 不是枚举数组字段");
        }
        for (size_t index = 0; index < componentList.size(); ++index) {
            const LowVolComponent component = requireNumericEnumValue<LowVolComponent>(
                componentList.at(index),
                lowvol_json::kComponentsKey,
                static_cast<int>(LowVolComponent::VOLATILITY),
                static_cast<int>(LowVolComponent::BETA));
            if (std::find(params.components.begin(), params.components.end(), component) == params.components.end()) {
                params.components.push_back(component);
            }
        }
        if (params.components.empty()) {
            params.components = {LowVolComponent::VOLATILITY, LowVolComponent::DRAWDOWN, LowVolComponent::BETA};
        }
    }
    if (json.has(lowvol_json::kVolatilityWeightKey)) params.volatilityWeight = json.get(lowvol_json::kVolatilityWeightKey).asDouble();
    if (json.has(lowvol_json::kDrawdownWeightKey)) params.drawdownWeight = json.get(lowvol_json::kDrawdownWeightKey).asDouble();
    if (json.has(lowvol_json::kBetaWeightKey)) params.betaWeight = json.get(lowvol_json::kBetaWeightKey).asDouble();
    return params;
}

bool parseIsoDate(const std::string& text, std::tm& out)
{
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        return false;
    }
    try {
        std::tm candidate = {};
        candidate.tm_year = std::stoi(text.substr(0, 4)) - 1900;
        candidate.tm_mon = std::stoi(text.substr(5, 2)) - 1;
        candidate.tm_mday = std::stoi(text.substr(8, 2));
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

std::string earliestLowVolSeriesDate(const std::string& anchorDate, int window)
{
    std::tm parsedDate = {};
    if (!parseIsoDate(anchorDate, parsedDate)) {
        return "";
    }
    const int lookbackDays = std::max(45, (window + 10) * 2);
    parsedDate.tm_mday -= lookbackDays;
    parsedDate.tm_isdst = -1;
    std::mktime(&parsedDate);
    return formatIsoDate(parsedDate);
}

std::string trimAsciiWhitespace(std::string text)
{
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    const auto begin = std::find_if_not(text.begin(), text.end(), isSpace);
    const auto end = std::find_if_not(text.rbegin(), text.rend(), isSpace).base();
    if (begin >= end) {
        return "";
    }
    return std::string(begin, end);
}

std::string resolveBenchmarkSymbol(const LowVolFactor::Params& params)
{
    return trimAsciiWhitespace(params.benchmarkSymbol);
}

std::string joinByComma(const std::vector<std::string>& values)
{
    if (values.empty()) {
        return "";
    }
    std::ostringstream stream;
    for (size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            stream << ",";
        }
        stream << values[index];
    }
    return stream.str();
}

std::vector<LowVolComponent> selectedComponentsOrDefault(const std::vector<LowVolComponent>& components)
{
    if (!components.empty()) {
        return components;
    }
    return {LowVolComponent::VOLATILITY, LowVolComponent::DRAWDOWN, LowVolComponent::BETA};
}

double normalizedComponentWeight(double weight)
{
    return weight > 0.0 ? weight : 0.0;
}

std::vector<double> extractCloses(const std::vector<HistoricalDataPoint>& series)
{
    std::vector<double> closes;
    closes.reserve(series.size());
    for (const auto& point : series) {
        closes.push_back(point.value);
    }
    return closes;
}

std::unordered_map<std::string, double> buildCloseLookup(const std::vector<HistoricalDataPoint>& series)
{
    std::unordered_map<std::string, double> lookup;
    lookup.reserve(series.size());
    for (const auto& point : series) {
        if (std::isfinite(point.value) && point.value > 0.0) {
            lookup.emplace(point.date, point.value);
        }
    }
    return lookup;
}

double normalizedInverseScore(double rawValue, double minValue, double maxValue)
{
    if (!std::isfinite(rawValue)) {
        return 0.0;
    }
    if (!std::isfinite(minValue) || !std::isfinite(maxValue) || maxValue <= minValue) {
        return 1.0;
    }
    return (maxValue - rawValue) / (maxValue - minValue);
}

bool hasSelectedComponent(const std::vector<LowVolComponent>& components, LowVolComponent component)
{
    return std::find(components.begin(), components.end(), component) != components.end();
}

double lowVolComponentWeight(const LowVolFactor::Params& params, LowVolComponent component)
{
    switch (component) {
    case LowVolComponent::VOLATILITY:
        return normalizedComponentWeight(params.volatilityWeight);
    case LowVolComponent::DRAWDOWN:
        return normalizedComponentWeight(params.drawdownWeight);
    case LowVolComponent::BETA:
        return normalizedComponentWeight(params.betaWeight);
    default:
        return 0.0;
    }
}

void ensureTaLibInitialized()
{
    static std::once_flag initFlag;
    static TA_RetCode initResult = TA_BAD_PARAM;
    std::call_once(initFlag, []() {
        initResult = TA_Initialize();
    });
    if (initResult != TA_SUCCESS) {
        throw std::runtime_error("TA-Lib initialization failed for low volatility factor");
    }
}

std::mutex& taLibExecutionMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::optional<double> taOptionalLast(const std::vector<double>& output, int outBegIdx, int outNBElement)
{
    if (outBegIdx < 0 || outNBElement <= 0) {
        return std::nullopt;
    }
    const size_t lastIndex = static_cast<size_t>(outNBElement - 1);
    if (lastIndex >= output.size() || !std::isfinite(output[lastIndex])) {
        return std::nullopt;
    }
    return output[lastIndex];
}

}

LowVolFactor::LowVolFactor() {
    factorType_ = FactorType::LOW_VOLATILITY;
}

CalculationResult LowVolFactor::calculate(const CalculationContext& context) {
    const CommonMetricParams commonParams = buildCommonMetricParams(
        params_.lookbackWindow,
        params_.lagEnabled,
        params_.frequency,
        params_.standardization,
        params_.neutralizationEnabled);

    return executeWithCommonParams(
        context,
        commonParams,
        [this, &context, &commonParams]() {
            return resolveCommonEffectiveDateForFields(
                context,
                commonParams,
                std::vector<std::string>{std::string(factor::bridge::MarketBarFieldKeys::CLOSE.c_str())},
                CommonFieldRequirementMode::AllFields);
        },
        [this, &context](const CommonRuntimeState& runtime, CalculationResult& result) {
            auto failWithMessage = [&](const std::string& message) {
                result.dataStatus.availability = DataAvailability::UNAVAILABLE;
                result.dataStatus.coverage = 0.0;
                result.dataStatus.message = message;
                result.metadata.set("error", json_helper::toJsonValue(result.dataStatus.message));
            };

            if (!context.historicalView->hasField(std::string(factor::bridge::MarketBarFieldKeys::CLOSE.c_str()))) {
                failWithMessage("缓存数据集缺少字段 close，无法计算低波因子");
                return;
            }

            std::tm parsedEffectiveDate = {};
            if (!parseIsoDate(runtime.effectiveDate, parsedEffectiveDate)) {
                failWithMessage(std::string("低波因子无法解析有效日期 ") + runtime.effectiveDate);
                return;
            }

            const std::string startDate = earliestLowVolSeriesDate(runtime.effectiveDate, params_.window);
            if (startDate.empty()) {
                failWithMessage(std::string("低波因子无法计算回看起始日期 ") + runtime.effectiveDate);
                return;
            }
            const std::vector<LowVolComponent> components = selectedComponentsOrDefault(params_.components);
            const bool needsBeta = hasSelectedComponent(components, LowVolComponent::BETA);
            const bool needsVolatility = hasSelectedComponent(components, LowVolComponent::VOLATILITY);
            const bool needsDrawdown = hasSelectedComponent(components, LowVolComponent::DRAWDOWN);
            const std::string benchmarkSymbol = resolveBenchmarkSymbol(params_);

            std::vector<HistoricalDataPoint> benchmarkSeries;
            if (needsBeta) {
                benchmarkSeries = context.historicalView->getSeries(
                    benchmarkSymbol,
                    startDate,
                    runtime.effectiveDate,
                    "close");

                if (benchmarkSeries.size() < 2) {
                    failWithMessage("低波因子需要基准指数 " + benchmarkSymbol + " 的收盘价序列，HistoricalView 未提供该基准数据");
                    return;
                }
            }

            double activeWeightSum = 0.0;
            for (const LowVolComponent component : components) {
                activeWeightSum += lowVolComponentWeight(params_, component);
            }
            if (activeWeightSum <= 0.0) {
                failWithMessage("低波因子权重总和不能为 0");
                return;
            }

            std::vector<std::string> symbols = context.symbols;
            if (symbols.empty()) {
                symbols = context.historicalView->getAvailableSymbols(runtime.effectiveDate);
            }

            std::map<std::string, std::vector<HistoricalDataPoint>> seriesBySymbol;
            for (const auto& symbol : symbols) {
                seriesBySymbol[symbol] = context.historicalView->getSeries(
                    symbol,
                    startDate,
                    runtime.effectiveDate,
                    "close");
            }

            std::map<std::string, SymbolMetrics> metricsBySymbol;
            int shortSeriesCount = 0;
            int volatilityMissingCount = 0;
            int drawdownMissingCount = 0;
            int betaMissingCount = 0;
            for (const auto& [symbol, series] : seriesBySymbol) {
                if (static_cast<int>(series.size()) < params_.window) {
                    ++shortSeriesCount;
                    continue;
                }

                const auto trailingBegin = series.end() - params_.window;
                std::vector<HistoricalDataPoint> trailingSeries(trailingBegin, series.end());

                SymbolMetrics metrics;
                if (needsVolatility) {
                    metrics.volatility = computeVolatility(extractCloses(trailingSeries));
                }
                if (needsDrawdown) {
                    metrics.maxDrawdown = computeMaxDrawdown(extractCloses(trailingSeries));
                }
                if (needsBeta) {
                    metrics.beta = computeBeta(trailingSeries, benchmarkSeries);
                }

                if (needsVolatility && !metrics.volatility.has_value()) {
                    ++volatilityMissingCount;
                }
                if (needsDrawdown && !metrics.maxDrawdown.has_value()) {
                    ++drawdownMissingCount;
                }
                if (needsBeta && !metrics.beta.has_value()) {
                    ++betaMissingCount;
                }

                if ((needsVolatility && !metrics.volatility.has_value())
                    || (needsDrawdown && !metrics.maxDrawdown.has_value())
                    || (needsBeta && !metrics.beta.has_value())) {
                    continue;
                }

                metricsBySymbol[symbol] = std::move(metrics);
            }

            if (metricsBySymbol.empty()) {
                std::vector<std::string> componentCodes;
                componentCodes.reserve(components.size());
                for (const LowVolComponent component : components) {
                    componentCodes.push_back(std::to_string(static_cast<int>(component)));
                }

                std::ostringstream reason;
                reason << "低波因子没有可用样本: effectiveDate=" << runtime.effectiveDate
                       << " window=" << params_.window
                       << " lookbackWindow=" << params_.lookbackWindow
                       << " symbolCount=" << seriesBySymbol.size()
                       << " shortSeries=" << shortSeriesCount
                       << " volatilityMissing=" << volatilityMissingCount
                       << " drawdownMissing=" << drawdownMissingCount
                       << " betaMissing=" << betaMissingCount
                       << " benchmarkSeries=" << benchmarkSeries.size()
                       << " components=" << joinByComma(componentCodes)
                       << " benchmarkSymbol=" << benchmarkSymbol;
                result.metadata.set(
                    "emptyReason",
                    json_helper::toJsonValue(reason.str()));
                return;
            }

            std::map<std::string, double> weightedScores;
            std::map<std::string, double> usedWeights;
            auto accumulateScores = [&](LowVolComponent component,
                                        const char* componentName,
                                        std::optional<double> SymbolMetrics::*member) {
                const double componentWeight = lowVolComponentWeight(params_, component);
                if (componentWeight <= 0.0) {
                    return;
                }

                std::vector<std::pair<std::string, double>> rawValues;
                rawValues.reserve(metricsBySymbol.size());
                for (const auto& [symbol, metrics] : metricsBySymbol) {
                    const auto& metricValue = metrics.*member;
                    if (metricValue.has_value()) {
                        rawValues.emplace_back(symbol, *metricValue);
                    }
                }

                if (rawValues.empty()) {
                    return;
                }

                if (rawValues.size() < 2) {
                    result.metadata.set(componentName, json_helper::toJsonValue(1));
                    return;
                }

                double minValue = rawValues.front().second;
                double maxValue = rawValues.front().second;
                for (const auto& [symbol, value] : rawValues) {
                    (void)symbol;
                    minValue = std::min(minValue, value);
                    maxValue = std::max(maxValue, value);
                }

                for (const auto& [symbol, value] : rawValues) {
                    weightedScores[symbol] += normalizedInverseScore(value, minValue, maxValue) * componentWeight;
                    usedWeights[symbol] += componentWeight;
                }

                result.metadata.set(componentName, json_helper::toJsonValue(static_cast<int>(rawValues.size())));
            };

            if (needsVolatility) {
                accumulateScores(LowVolComponent::VOLATILITY, "volatility_count", &SymbolMetrics::volatility);
            }
            if (needsDrawdown) {
                accumulateScores(LowVolComponent::DRAWDOWN, "drawdown_count", &SymbolMetrics::maxDrawdown);
            }
            if (needsBeta) {
                accumulateScores(LowVolComponent::BETA, "beta_count", &SymbolMetrics::beta);
            }

            for (const auto& [symbol, weightedScore] : weightedScores) {
                const double denominator = usedWeights[symbol];
                if (denominator <= 0.0) {
                    continue;
                }
                result.values[symbol] = weightedScore / denominator;
            }

            if (result.values.empty()) {
                std::ostringstream reason;
                reason << "低波因子缺少至少两个可比样本，无法进行横截面低波排序: effectiveDate="
                       << runtime.effectiveDate
                       << " window=" << params_.window;
                result.metadata.set(
                    "emptyReason",
                    json_helper::toJsonValue(reason.str()));
            }
        },
        [](const CommonRuntimeState&, CalculationResult&) {},
        [this](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set(lowvol_json::kWindowKey, json_helper::toJsonValue(params_.window));
            auto componentsJson = foundation::json::JsonFacade::createArray();
            for (const LowVolComponent component : selectedComponentsOrDefault(params_.components)) {
                componentsJson.push_back(json_helper::toJsonValue(static_cast<int>(component)));
            }
            result.metadata.set(lowvol_json::kComponentsKey, componentsJson);
            if (hasSelectedComponent(params_.components, LowVolComponent::BETA) || params_.components.empty()) {
                result.metadata.set(lowvol_json::kBenchmarkSymbolKey, json_helper::toJsonValue(resolveBenchmarkSymbol(params_)));
            }
            result.metadata.set(lowvol_json::kVolatilityWeightKey, json_helper::toJsonValue(params_.volatilityWeight));
            result.metadata.set(lowvol_json::kDrawdownWeightKey, json_helper::toJsonValue(params_.drawdownWeight));
            result.metadata.set(lowvol_json::kBetaWeightKey, json_helper::toJsonValue(params_.betaWeight));
        });
}

DataRequirements LowVolFactor::getDataRequirements() const {
    DataRequirements req;
    appendRequiredField(req, "close");
    appendHistoricalNeutralizationRequirements(req,
                                               params_.neutralizationEnabled,
                                               SourceTable::DAILY_BAR);
    return req;
}

BoundaryRules LowVolFactor::getBoundaryRules() const {
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints, params_.window);
    return rules;
}

std::shared_ptr<LowVolFactor> LowVolFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker) {

    auto factor = std::make_shared<LowVolFactor>();
    factor->dataChecker_ = dataChecker;
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

std::optional<double> LowVolFactor::computeVolatility(const std::vector<double>& closes) const {
    if (closes.size() < 2) {
        return std::nullopt;
    }

    std::vector<double> returns;
    returns.reserve(closes.size() - 1);
    for (size_t i = 1; i < closes.size(); ++i) {
        if (closes[i - 1] <= 0.0 || closes[i] <= 0.0) {
            continue;
        }
        returns.push_back((closes[i] - closes[i - 1]) / closes[i - 1]);
    }

    if (returns.empty()) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(taLibExecutionMutex());
    ensureTaLibInitialized();
    std::vector<double> output(returns.size(), std::numeric_limits<double>::quiet_NaN());
    int outBegIdx = 0;
    int outNBElement = 0;
    const TA_RetCode ret = TA_STDDEV(0,
                                     static_cast<int>(returns.size() - 1),
                                     returns.data(),
                                     static_cast<int>(returns.size()),
                                     1.0,
                                     &outBegIdx,
                                     &outNBElement,
                                     output.data());
    if (ret != TA_SUCCESS) {
        return std::nullopt;
    }

    return taOptionalLast(output, outBegIdx, outNBElement);
}

std::optional<double> LowVolFactor::computeMaxDrawdown(const std::vector<double>& closes) const {
    if (closes.size() < 2) {
        return std::nullopt;
    }

    double peak = 0.0;
    double maxDrawdown = 0.0;
    bool hasValidClose = false;
    for (double close : closes) {
        if (!std::isfinite(close) || close <= 0.0) {
            continue;
        }
        hasValidClose = true;
        peak = peak <= 0.0 ? close : std::max(peak, close);
        if (peak > 0.0) {
            maxDrawdown = std::max(maxDrawdown, (peak - close) / peak);
        }
    }

    if (!hasValidClose) {
        return std::nullopt;
    }

    return maxDrawdown;
}

std::optional<double> LowVolFactor::computeBeta(
    const std::vector<HistoricalDataPoint>& symbolSeries,
    const std::vector<HistoricalDataPoint>& benchmarkSeries) const {

    if (symbolSeries.size() < 2 || benchmarkSeries.size() < 2) {
        return std::nullopt;
    }

    const auto benchmarkLookup = buildCloseLookup(benchmarkSeries);

    std::vector<double> symbolReturns;
    std::vector<double> benchmarkReturns;
    symbolReturns.reserve(symbolSeries.size() - 1);
    benchmarkReturns.reserve(symbolSeries.size() - 1);

    for (size_t i = 1; i < symbolSeries.size(); ++i) {
        const auto& previousSymbol = symbolSeries[i - 1];
        const auto& currentSymbol = symbolSeries[i];
        if (!std::isfinite(previousSymbol.value) || !std::isfinite(currentSymbol.value)
            || previousSymbol.value <= 0.0 || currentSymbol.value <= 0.0) {
            continue;
        }

        const auto previousBenchmark = benchmarkLookup.find(previousSymbol.date);
        const auto currentBenchmark = benchmarkLookup.find(currentSymbol.date);
        if (previousBenchmark == benchmarkLookup.end() || currentBenchmark == benchmarkLookup.end()) {
            continue;
        }

        if (previousBenchmark->second <= 0.0 || currentBenchmark->second <= 0.0) {
            continue;
        }

        symbolReturns.push_back((currentSymbol.value - previousSymbol.value) / previousSymbol.value);
        benchmarkReturns.push_back((currentBenchmark->second - previousBenchmark->second) / previousBenchmark->second);
    }

    if (symbolReturns.size() < 2 || benchmarkReturns.size() < 2) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(taLibExecutionMutex());
    ensureTaLibInitialized();
    const int period = static_cast<int>(std::min(symbolReturns.size(), benchmarkReturns.size()));
    std::vector<double> output(static_cast<size_t>(period), std::numeric_limits<double>::quiet_NaN());
    int outBegIdx = 0;
    int outNBElement = 0;
    const TA_RetCode ret = TA_BETA(0,
                                   period - 1,
                                   symbolReturns.data(),
                                   benchmarkReturns.data(),
                                   period,
                                   &outBegIdx,
                                   &outNBElement,
                                   output.data());
    if (ret != TA_SUCCESS) {
        return std::nullopt;
    }

    return taOptionalLast(output, outBegIdx, outNBElement);
}

void LowVolFactor::loadConfig(const foundation::json::JsonFacade& config) {
    BaseFactor::loadConfig(config);
    if (config::hasCalculationConfig(config)) {
        const auto calculation = config::calculationConfig(config);
        params_ = lowVolParamsFromJson(calculation);
    }
    dataRequirements_ = getDataRequirements();
    boundaryRules_ = getBoundaryRules();
}

} // namespace factor