#include "domain/factor/include/GrowthFactor.h"
#include "domain/factor/include/BaseFactor.h"
#include "domain/factor/include/FactorInstanceManager.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_set>

namespace factor {

namespace {

constexpr int kBasicSeriesPoints = 2;
constexpr int kSueSeasonalLag = 4;
constexpr int kSueHistoryCount = 3;
constexpr int kSueSeriesPoints = kSueSeasonalLag + kSueHistoryCount + 1;

double safeRatio(double numerator, double denominator)
{
    if (!std::isfinite(numerator) || !std::isfinite(denominator) || std::abs(denominator) < 1e-12)
        return 0.0;
    return numerator / denominator;
}

using LatestFinancialSeriesFn = std::function<std::unordered_map<std::string, std::vector<double>>(
    const CalculationContext&, const std::string&, const std::string&, int)>;

std::unordered_map<std::string, double> computeYoYScoreMap(
    const LatestFinancialSeriesFn& resolver,
    const CalculationContext& context,
    const std::string& effectiveDate,
    const std::string& field)
{
    std::unordered_map<std::string, double> scores;
    auto seriesMap = resolver(context, field, effectiveDate, kBasicSeriesPoints);
    for (const auto& [symbol, values] : seriesMap) {
        if (values.size() < static_cast<size_t>(kBasicSeriesPoints)) continue;
        double prev = values[1];
        if (std::abs(prev) < 1e-12) continue;
        double g = safeRatio(values[0] - values[1], std::abs(prev));
        if (std::isfinite(g)) scores[symbol] = g;
    }
    return scores;
}

std::unordered_map<std::string, double> computeDiffScoreMap(
    const LatestFinancialSeriesFn& resolver,
    const CalculationContext& context,
    const std::string& effectiveDate,
    const std::string& field)
{
    std::unordered_map<std::string, double> scores;
    auto seriesMap = resolver(context, field, effectiveDate, kBasicSeriesPoints);
    for (const auto& [symbol, values] : seriesMap) {
        if (values.size() < static_cast<size_t>(kBasicSeriesPoints)) continue;
        double delta = values[0] - values[1];
        if (std::isfinite(delta)) scores[symbol] = delta;
    }
    return scores;
}

std::unordered_map<std::string, double> computeSueScoreMap(
    const LatestFinancialSeriesFn& resolver,
    const CalculationContext& context,
    const std::string& effectiveDate)
{
    std::unordered_map<std::string, double> scores;
    auto seriesMap = resolver(context, std::string("eps"), effectiveDate, kSueSeriesPoints);
    for (const auto& [symbol, values] : seriesMap) {
        if (values.size() < static_cast<size_t>(kSueSeriesPoints)) continue;
        std::vector<double> chronological(values.rbegin(), values.rend());
        std::vector<double> historical;
        historical.reserve(kSueHistoryCount);
        bool valid = true;
        for (size_t i = 0; i < static_cast<size_t>(kSueHistoryCount); ++i) {
            double surprise = chronological[i + kSueSeasonalLag] - chronological[i];
            if (!std::isfinite(surprise)) { valid = false; break; }
            historical.push_back(surprise);
        }
        if (!valid || historical.size() != static_cast<size_t>(kSueHistoryCount)) continue;
        double current = chronological[kSueSeriesPoints - 1] - chronological[kSueHistoryCount];
        if (!std::isfinite(current)) continue;
        double mean = std::accumulate(historical.begin(), historical.end(), 0.0) / static_cast<double>(historical.size());
        double var = 0.0;
        for (double s : historical) { double d = s - mean; var += d * d; }
        double stdDev = std::sqrt(var / static_cast<double>(historical.size()));
        if (stdDev <= 1e-12) continue;
        double sue = (current - mean) / stdDev;
        if (std::isfinite(sue)) scores[symbol] = sue;
    }
    return scores;
}

void normalizeScores(StandardizationMethod stdMethod, std::unordered_map<std::string, double>& scores)
{
    if (scores.empty()) return;
    if (stdMethod == StandardizationMethod::Percentile || stdMethod == StandardizationMethod::Rank) {
        std::vector<std::pair<std::string, double>> ranked(scores.begin(), scores.end());
        std::sort(ranked.begin(), ranked.end(), [](auto& a, auto& b) { return a.second < b.second; });
        double denom = static_cast<double>(ranked.size());
        for (size_t i = 0; i < ranked.size();) {
            size_t end = i + 1;
            while (end < ranked.size() && ranked[end].second == ranked[i].second) ++end;
            double frac = static_cast<double>(i) / denom;
            for (size_t j = i; j < end; ++j) scores[ranked[j].first] = frac;
            i = end;
        }
        return;
    }
    std::vector<double> vals;
    for (auto& [sym, v] : scores) if (std::isfinite(v)) vals.push_back(v);
    if (vals.empty()) return;
    if (stdMethod == StandardizationMethod::ZScore) {
        double mean = std::accumulate(vals.begin(), vals.end(), 0.0) / static_cast<double>(vals.size());
        double var = 0.0;
        for (double v : vals) { double d = v - mean; var += d * d; }
        double stdev = std::sqrt(var / static_cast<double>(vals.size()));
        if (stdev > 1e-12)
            for (auto& [sym, v] : scores) v = (v - mean) / stdev;
    } else if (stdMethod == StandardizationMethod::MinMax) {
        auto [mi, ma] = std::minmax_element(vals.begin(), vals.end());
        double range = *ma - *mi;
        if (range > 1e-12)
            for (auto& [sym, v] : scores) v = (v - *mi) / range;
    }
}

} // namespace

GrowthFactor::GrowthFactor()
{
    factorType_ = FactorType::GROWTH;
}

CalculationResult GrowthFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView)
        return createHistoricalViewRuntimeError(context, "成长因子需要 HistoricalView");

    const auto& p = params_;
    if (p.growthMetrics.empty() || p.growthWeights.empty() || p.growthMetrics.size() != p.growthWeights.size())
        return createHistoricalViewRuntimeError(context, "成长因子必须显式提供等长的 growthMetrics 和 growthWeights");

    const StandardizationMethod standardization = p.standardization;
    std::unordered_set<std::string> seen;

    struct Selection { GrowthMetric metric; double weight; std::string field; };
    std::vector<Selection> selections;
    selections.reserve(p.growthMetrics.size());

    for (size_t i = 0; i < p.growthMetrics.size(); ++i) {
        auto metric = p.growthMetrics[i];
        double weight = p.growthWeights[i];
        if (metric == GrowthMetric::UNKNOWN || !std::isfinite(weight) || weight < 0.0)
            return createHistoricalViewRuntimeError(context, "成长因子配置包含非法指标或权重");
        std::string key = std::to_string(static_cast<int>(metric));
        if (!seen.insert(key).second)
            return createHistoricalViewRuntimeError(context, "成长因子配置不允许重复指标");

        std::string field;
        switch (metric) {
        case GrowthMetric::REVENUE_GROWTH:  field = "total_revenue"; break;
        case GrowthMetric::NET_PROFIT_GROWTH: field = "net_profit"; break;
        case GrowthMetric::DELTA_ROE:       field = "roe"; break;
        case GrowthMetric::SUE:             field = "eps"; break;
        default: return createHistoricalViewRuntimeError(context, "成长因子配置包含不支持的指标");
        }
        selections.push_back({metric, weight, field});
    }

    std::vector<std::string> dateFields;
    for (auto& s : selections) dateFields.push_back(s.field);

    return executeWithCommonParams(
        context, p,
        [this, &context, &p, &dateFields]() {
            return resolveCommonEffectiveDateForFields(context, p, dateFields, CommonFieldRequirementMode::AllFields);
        },
        [this, &context, &selections, standardization](const CommonRuntimeState& rt, CalculationResult& result) {
            CalculationContext effCtx = context;
            effCtx.date = rt.effectiveDate;
            auto resolver = [this](const CalculationContext& q, const std::string& f, const std::string& d, int limit) {
                return latestFinancialSeries(q, f, d, limit);
            };

            std::unordered_map<std::string, double> combined;
            std::unordered_map<std::string, double> activeWeights;

            for (auto& sel : selections) {
                if (sel.weight <= 0.0) continue;
                std::unordered_map<std::string, double> metricScores;
                switch (sel.metric) {
                case GrowthMetric::REVENUE_GROWTH:
                case GrowthMetric::NET_PROFIT_GROWTH:
                    metricScores = computeYoYScoreMap(resolver, effCtx, rt.effectiveDate, sel.field);
                    break;
                case GrowthMetric::DELTA_ROE:
                    metricScores = computeDiffScoreMap(resolver, effCtx, rt.effectiveDate, sel.field);
                    break;
                case GrowthMetric::SUE:
                    metricScores = computeSueScoreMap(resolver, effCtx, rt.effectiveDate);
                    break;
                default: break;
                }
                normalizeScores(standardization, metricScores);
                for (auto& [sym, score] : metricScores) {
                    if (!std::isfinite(score)) continue;
                    combined[sym] += score * sel.weight;
                    activeWeights[sym] += sel.weight;
                }
            }
            if (combined.empty()) {
                result.metadata.set("emptyReason", json_helper::toJsonValue("成长因子没有可用财务数据"));
                return;
            }
            for (auto& [sym, ws] : combined) {
                double aw = activeWeights[sym];
                if (aw > 1e-12) result.values[sym] = ws / aw;
            }
        },
        [](const CommonRuntimeState&, CalculationResult&) {},
        [&selections](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("metric", json_helper::toJsonValue(static_cast<int>(
                selections.empty() ? GrowthMetric::UNKNOWN : selections.front().metric)));
        });
}

std::shared_ptr<GrowthFactor> GrowthFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<GrowthFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

DataRequirements GrowthFactor::getDataRequirements() const
{
    DataRequirements req;
    for (auto m : params_.growthMetrics) {
        switch (m) {
        case GrowthMetric::REVENUE_GROWTH: appendRequiredField(req, "total_revenue"); break;
        case GrowthMetric::NET_PROFIT_GROWTH: appendRequiredField(req, "net_profit"); break;
        case GrowthMetric::DELTA_ROE: appendRequiredField(req, "roe"); break;
        case GrowthMetric::SUE: appendRequiredField(req, "eps"); break;
        default: break;
        }
    }
    appendHistoricalNeutralizationRequirements(req, params_.neutralizationEnabled);
    return req;
}

BoundaryRules GrowthFactor::getBoundaryRules() const
{
    BoundaryRules rules = boundaryRules_;
    int minDp = 1;
    for (auto m : params_.growthMetrics) {
        switch (m) {
        case GrowthMetric::REVENUE_GROWTH:
        case GrowthMetric::NET_PROFIT_GROWTH:
        case GrowthMetric::DELTA_ROE:
            minDp = (std::max)(minDp, kBasicSeriesPoints); break;
        case GrowthMetric::SUE:
            minDp = (std::max)(minDp, kSueSeriesPoints); break;
        default: break;
        }
    }
    int lag = params_.lagEnabled ? (std::max)(1, static_cast<int>(params_.lagPeriods)) : 0;
    rules.minDataPoints = minDp + lag;
    return rules;
}

void GrowthFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    if (config.has("growth")) params_.fromJson(config.get("growth"));
    if (config.has("common")) params_.fromJson(config.get("common"));
}

} // namespace factor