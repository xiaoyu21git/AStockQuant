#include "domain/factor/include/SentimentFactor.h"
#include "domain/factor/include/BaseFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include <cmath>
#include <algorithm>

namespace factor {

namespace {

/// @brief SentimentMetric 枚举 → HistoricalView 字段名，零 fallback
constexpr const char* sentimentMetricToFieldName(SentimentMetric metric) noexcept
{
    switch (metric) {
    case SentimentMetric::SENTIMENT_SCORE:    return "sentiment_score";
    case SentimentMetric::SOCIAL_SENTIMENT:   return "social_sentiment";
    case SentimentMetric::INVESTOR_SENTIMENT: return "investor_sentiment";
    case SentimentMetric::MARKET_SENTIMENT:   return "market_sentiment";
    default: return nullptr;
    }
}

} // anonymous namespace

SentimentFactor::SentimentFactor()
{
    factorType_ = FactorType::SENTIMENT;
}

CalculationResult SentimentFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(context, "情绪因子需要 HistoricalView");
    }

    const CommonParams& common = params_;
    const auto symbols = effectiveSymbols(context);
    const SentimentMetric metricKind = params_.sentimentMetric;
    const int window = (std::max)(1, static_cast<int>(common.window));
    const char* const field = sentimentMetricToFieldName(metricKind);

    return executeWithCommonParams(
        context,
        common,
        [&]() { return context.date; },

        // ── Lambda 2: 情绪数据读取 ──
        [&](const CommonRuntimeState& runtime, CalculationResult& result) {
            // 字段缺失或未知枚举 → 全零
            if (!field || !context.historicalView->hasField(field)) {
                for (const auto& symbol : symbols) {
                    result.values[symbol] = 0.0;
                }
                result.metadata.set("sentimentFieldMissing",
                    json_helper::toJsonValue(field ? field : "null"));
                return;
            }

            if (window <= 1) {
                // 单期：直接读横切面
                const auto cs = context.historicalView->getCrossSection(
                    runtime.effectiveDate, field, symbols);
                for (const auto& symbol : symbols) {
                    auto it = cs.find(symbol);
                    if (it != cs.end() && std::isfinite(it->second)) {
                        result.values[symbol] = it->second;
                    } else {
                        result.values[symbol] = 0.0;
                    }
                }
            } else {
                // 多期平滑：逐 symbol 取窗口序列求均值
                for (const auto& symbol : symbols) {
                    auto series = context.historicalView->getSeries(
                        symbol, runtime.effectiveDate, window, field);
                    if (series.empty()) {
                        result.values[symbol] = 0.0;
                        continue;
                    }
                    double sum = 0.0;
                    int count = 0;
                    for (const auto& dp : series) {
                        if (std::isfinite(dp.value)) {
                            sum += dp.value;
                            ++count;
                        }
                    }
                    result.values[symbol] = (count > 0) ? (sum / static_cast<double>(count)) : 0.0;
                }
            }

            if (result.values.empty()) {
                result.metadata.set("emptyReason",
                    json_helper::toJsonValue("情绪因子没有可用数据"));
            }
        },

        // ── Lambda 3: 标准化前处理（空）──
        [](const CommonRuntimeState&, CalculationResult&) {},

        // ── Lambda 4: 元数据 ──
        [&](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("sentimentMetric",
                json_helper::toJsonValue(static_cast<int>(metricKind)));
            result.metadata.set("sentimentSource",
                json_helper::toJsonValue(static_cast<int>(params_.sentimentSource)));
            result.metadata.set("window",
                json_helper::toJsonValue(window));
        });
}

std::shared_ptr<SentimentFactor> SentimentFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<SentimentFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

DataRequirements SentimentFactor::getDataRequirements() const
{
    DataRequirements req;
    const char* field = sentimentMetricToFieldName(params_.sentimentMetric);
    if (field) {
        appendRequiredField(req, field);
    }
    appendHistoricalNeutralizationRequirements(req, params_.neutralizationEnabled);
    return req;
}

BoundaryRules SentimentFactor::getBoundaryRules() const
{
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints, 1);
    return rules;
}

void SentimentFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    if (config::hasCalculationConfig(config))
        params_.fromJson(config::calculationConfig(config));
    dataRequirements_ = getDataRequirements();
}

} // namespace factor
