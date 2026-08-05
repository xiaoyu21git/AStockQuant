#include "domain/factor/include/DividendFactor.h"
#include "domain/factor/include/BaseFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"

#include <cmath>
#include <algorithm>

namespace factor {

namespace {

constexpr const char* dividendMetricFieldName(DividendMetric metric) noexcept
{
    switch (metric) {
    case DividendMetric::DIVIDEND_YIELD:     return "dividend_yield";
    case DividendMetric::PAYOUT_RATIO:       return "payout_ratio";
    case DividendMetric::DIVIDEND_STABILITY: return "dividend_stability";
    default: return nullptr;
    }
}
} // anonymous namespace

DividendFactor::DividendFactor()
{
    factorType_ = FactorType::DIVIDEND;
}

CalculationResult DividendFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(context, "红利因子需要 HistoricalView");
    }
    const CommonParams& common = params_;
    const auto symbols = effectiveSymbols(context);

    // 选择指标：dividendMetrics 向量优先，否则回退到 dividendMetric 单值
    std::vector<DividendMetric> metrics;
    if (!params_.dividendMetrics.empty()) {
        metrics = params_.dividendMetrics;
    } else if (params_.dividendMetric != DividendMetric::UNKNOWN) {
        metrics = {params_.dividendMetric};
    } else {
        metrics = {DividendMetric::DIVIDEND_YIELD};
    }

    return executeWithCommonParams(
        context,
        common,
        [&]() { return context.date; },

        // ── Lambda 2: 红利指标计算 ──
        [&](const CommonRuntimeState& runtime, CalculationResult& result) {
            const double minYield = params_.minDividendYield;

            // 收集各指标的裸值，不做归一化（尺度对齐交给框架标准化或 CompositeFactor）
            std::vector<std::unordered_map<std::string, double>> metricResults;
            metricResults.reserve(metrics.size());

            for (DividendMetric metric : metrics) {
                const char* field = dividendMetricFieldName(metric);
                if (!field || !context.historicalView->hasField(field)) continue;

                std::unordered_map<std::string, double> mr;
                const auto cs = context.historicalView->getCrossSection(
                    runtime.effectiveDate, field, symbols);
                for (const auto& [symbol, value] : cs) {
                    if (!std::isfinite(value) || value < 0) continue;
                    if (metric == DividendMetric::DIVIDEND_YIELD
                        && minYield > 0.0 && value < minYield) continue;
                    mr[symbol] = value;
                }
                if (!mr.empty()) metricResults.push_back(std::move(mr));
            }

            if (metricResults.empty()) {
                for (const auto& symbol : symbols) result.values[symbol] = 0.0;
                result.metadata.set("emptyReason",
                    json_helper::toJsonValue("红利因子没有可用数据"));
                return;
            }

            // 多指标时简单等权平均裸值，不做额外归一化
            for (const auto& symbol : symbols) {
                double sum = 0.0;
                int count = 0;
                for (const auto& mr : metricResults) {
                    auto it = mr.find(symbol);
                    if (it != mr.end() && std::isfinite(it->second)) {
                        sum += it->second;
                        ++count;
                    }
                }
                result.values[symbol] = (count > 0) ? (sum / static_cast<double>(count)) : 0.0;
            }
        },

        [](const CommonRuntimeState&, CalculationResult&) {},

        [&](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("metricSourceTable",
                json_helper::toJsonValue(static_cast<int>(SourceTable::FINANCIAL_INDICATOR)));
            result.metadata.set("dividendMetric",
                json_helper::toJsonValue(static_cast<int>(metrics.front())));
            result.metadata.set("metricCount",
                json_helper::toJsonValue(static_cast<int>(metrics.size())));
        });
}

std::shared_ptr<DividendFactor> DividendFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<DividendFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

DataRequirements DividendFactor::getDataRequirements() const
{
    DataRequirements req;

    std::vector<DividendMetric> metrics;
    if (!params_.dividendMetrics.empty()) {
        metrics = params_.dividendMetrics;
    } else if (params_.dividendMetric != DividendMetric::UNKNOWN) {
        metrics = {params_.dividendMetric};
    } else {
        metrics = {DividendMetric::DIVIDEND_YIELD};
    }

    for (DividendMetric metric : metrics) {
        const char* field = dividendMetricFieldName(metric);
        if (field) appendRequiredField(req, field);
    }
    appendHistoricalNeutralizationRequirements(req, params_.neutralizationEnabled);
    return req;
}

BoundaryRules DividendFactor::getBoundaryRules() const
{
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints, 1);
    return rules;
}

void DividendFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    if (config::hasCalculationConfig(config))
        params_.fromJson(config::calculationConfig(config));
    dataRequirements_ = getDataRequirements();
}

} // namespace factor
