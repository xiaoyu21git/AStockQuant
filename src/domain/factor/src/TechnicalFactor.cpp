#include "domain/factor/include/TechnicalFactor.h"
#include "domain/factor/include/BaseFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include "batch_technical_indicators.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <unordered_set>

namespace factor {

namespace {

/// @brief 从 HistoricalView 收集所有 symbol 的字段序列
std::unordered_map<std::string, std::vector<double>> collectSeries(
    const CalculationContext& context,
    const std::string& field,
    const std::vector<std::string>& symbols,
    const std::string& effectiveDate,
    int window)
{
    std::unordered_map<std::string, std::vector<double>> result;
    const int lookback = (std::max)(2, window + 10);  // 加余量确保序列够长
    for (const auto& symbol : symbols) {
        auto series = context.historicalView->getSeries(
            symbol, effectiveDate, lookback, field);
        if (series.size() < 2) continue;
        std::vector<double> values;
        values.reserve(series.size());
        for (const auto& dp : series) {
            if (std::isfinite(dp.value)) values.push_back(dp.value);
        }
        if (values.size() >= 2) {
            result.emplace(symbol, std::move(values));
        }
    }
    return result;
}

/// @brief 对指标结果做截面 ZScore 标准化
void crossSectionalZScore(std::unordered_map<std::string, double>& values)
{
    if (values.empty()) return;
    double sum = 0.0;
    size_t count = 0;
    for (const auto& [sym, val] : values) {
        if (std::isfinite(val)) { sum += val; ++count; }
    }
    if (count < 2) return;
    const double mean = sum / static_cast<double>(count);
    double variance = 0.0;
    for (const auto& [sym, val] : values) {
        if (std::isfinite(val)) {
            const double delta = val - mean;
            variance += delta * delta;
        }
    }
    const double stdev = std::sqrt(variance / static_cast<double>(count));
    if (stdev < 1e-12) {
        for (auto& [sym, val] : values) val = 0.0;
        return;
    }
    for (auto& [sym, val] : values) {
        val = (val - mean) / stdev;
    }
}

/// @brief 判断指示器是否需要 high/low 数据
bool indicatorNeedsHighLow(TechnicalIndicator indicator)
{
    switch (indicator) {
    case TechnicalIndicator::KDJ:
    case TechnicalIndicator::ATR:
        return true;
    default:
        return false;
    }
}

/// @brief 判断指示器是否需要 volume 数据
bool indicatorNeedsVolume(TechnicalIndicator indicator)
{
    switch (indicator) {
    case TechnicalIndicator::OBV:
    case TechnicalIndicator::VWAP:
        return true;
    default:
        return false;
    }
}

} // anonymous namespace

TechnicalFactor::TechnicalFactor()
{
    factorType_ = FactorType::TECHNICAL;
}

CalculationResult TechnicalFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(context, "技术因子需要 HistoricalView");
    }

    const CommonParams& common = params_;
    const auto symbols = effectiveSymbols(context);
    const int window = (std::max)(2, static_cast<int>(common.window));

    // 若无配置指示器，默认用全部
    std::vector<TechnicalIndicator> indicators = params_.technicalIndicators;
    if (indicators.empty()) {
        // 默认启用全部（UNKNOWN 排除）
        for (int i = static_cast<int>(TechnicalIndicator::RSI);
             i <= static_cast<int>(TechnicalIndicator::TURNOVER_STABILITY); ++i) {
            indicators.push_back(static_cast<TechnicalIndicator>(i));
        }
    }

    // 预判所需字段
    bool needHighLow = false;
    bool needVolume = false;
    bool needTurnover = false;
    for (auto ind : indicators) {
        if (indicatorNeedsHighLow(ind)) needHighLow = true;
        if (indicatorNeedsVolume(ind)) needVolume = true;
        if (ind == TechnicalIndicator::TURNOVER_STABILITY) needTurnover = true;
        if (ind == TechnicalIndicator::VOLUME_RATIO) needVolume = true;
    }

    return executeWithCommonParams(
        context,
        common,
        [&]() { return context.date; },

        // ── Lambda 2: 技术指标计算与组合 ──
        [&](const CommonRuntimeState& runtime, CalculationResult& result) {
            // 收集 close 序列（几乎所有指标都用到）
            auto allCloses = collectSeries(context, "close", symbols,
                runtime.effectiveDate, window);

            // 条件收集其他序列
            std::unordered_map<std::string, std::vector<double>> allHighs, allLows, allVolumes, allTurnovers;
            if (needHighLow) {
                allHighs = collectSeries(context, "high", symbols,
                    runtime.effectiveDate, window);
                allLows  = collectSeries(context, "low", symbols,
                    runtime.effectiveDate, window);
            }
            if (needVolume) {
                allVolumes = collectSeries(context, "volume", symbols,
                    runtime.effectiveDate, window);
            }
            if (needTurnover) {
                allTurnovers = collectSeries(context, "turnover_rate", symbols,
                    runtime.effectiveDate, window);
            }

            if (allCloses.empty()) {
                for (const auto& symbol : symbols) result.values[symbol] = 0.0;
                result.metadata.set("emptyReason",
                    json_helper::toJsonValue("技术因子缺少 close 序列数据"));
                return;
            }

            // 逐指标计算
            std::vector<std::unordered_map<std::string, double>> indicatorResults;
            indicatorResults.reserve(indicators.size());

            for (auto ind : indicators) {
                std::unordered_map<std::string, double> indResult;
                switch (ind) {
                case TechnicalIndicator::RSI:
                    indResult = batchCalculateRsi(allCloses, params_.rsiWindow);
                    break;
                case TechnicalIndicator::MACD:
                    indResult = batchCalculateMacd(allCloses,
                        params_.macdFastPeriod, params_.macdSlowPeriod,
                        params_.macdSignalPeriod);
                    break;
                case TechnicalIndicator::MA:
                    indResult = batchCalculateMa(allCloses, params_.maWindow);
                    break;
                case TechnicalIndicator::EMA:
                    indResult = batchCalculateEma(allCloses, params_.emaWindow);
                    break;
                case TechnicalIndicator::BOLL:
                    indResult = batchCalculateBoll(allCloses,
                        params_.bollWindow, params_.bollStdDev);
                    break;
                case TechnicalIndicator::KDJ:
                    indResult = batchCalculateKdj(allHighs, allLows, allCloses,
                        params_.kdjWindow, params_.kdjKPeriod, params_.kdjDPeriod);
                    break;
                case TechnicalIndicator::ATR:
                    indResult = batchCalculateAtr(allHighs, allLows, allCloses,
                        params_.atrWindow);
                    break;
                case TechnicalIndicator::VWAP:
                    indResult = batchCalculateVwap(allCloses, allVolumes);
                    break;
                case TechnicalIndicator::VOLUME_RATIO:
                    indResult = batchCalculateVolumeRatio(allVolumes,
                        params_.volumeRatioWindow);
                    break;
                case TechnicalIndicator::OBV:
                    indResult = batchCalculateObv(allCloses, allVolumes,
                        params_.obvWindow);
                    break;
                case TechnicalIndicator::TURNOVER_STABILITY:
                    indResult = batchCalculateTurnoverStability(allTurnovers,
                        params_.turnoverStabilityWindow);
                    break;
                default:
                    continue;
                }
                if (!indResult.empty()) {
                    indicatorResults.push_back(std::move(indResult));
                }
            }

            if (indicatorResults.empty()) {
                for (const auto& symbol : symbols) result.values[symbol] = 0.0;
                result.metadata.set("emptyReason",
                    json_helper::toJsonValue("所有技术指标均无有效输出"));
                return;
            }

            // 组合：EqualWeight 或 NormalizedAverage
            if (params_.technicalCombinationMode == TechnicalCombinationMode::NormalizedAverage) {
                for (auto& ir : indicatorResults) {
                    crossSectionalZScore(ir);
                }
            }

            // 等权聚合
            for (const auto& symbol : symbols) {
                double sum = 0.0;
                int count = 0;
                for (const auto& ir : indicatorResults) {
                    auto it = ir.find(symbol);
                    if (it != ir.end() && std::isfinite(it->second)) {
                        sum += it->second;
                        ++count;
                    }
                }
                if (count > 0) {
                    result.values[symbol] = sum / static_cast<double>(count);
                } else {
                    result.values[symbol] = 0.0;
                }
            }

            if (result.values.empty()) {
                result.metadata.set("emptyReason",
                    json_helper::toJsonValue("技术因子聚合后无有效值"));
            }
        },

        // ── Lambda 3: 标准化前处理（空）──
        [](const CommonRuntimeState&, CalculationResult&) {},

        // ── Lambda 4: 元数据 ──
        [&](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("indicatorCount",
                json_helper::toJsonValue(static_cast<int>(indicators.size())));
            result.metadata.set("combinationMode",
                json_helper::toJsonValue(static_cast<int>(params_.technicalCombinationMode)));
            result.metadata.set("window",
                json_helper::toJsonValue(window));
        });
}

std::shared_ptr<TechnicalFactor> TechnicalFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<TechnicalFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

DataRequirements TechnicalFactor::getDataRequirements() const
{
    DataRequirements req;
    appendRequiredField(req, "close");

    // 按配置的指示器追加所需字段
    std::vector<TechnicalIndicator> indicators = params_.technicalIndicators;
    if (indicators.empty()) {
        // 默认全部 → 需要所有字段
        appendRequiredField(req, "high");
        appendRequiredField(req, "low");
        appendRequiredField(req, "volume");
        appendRequiredField(req, "turnover_rate");
    } else {
        bool needHighLow = false, needVolume = false;
        for (auto ind : indicators) {
            if (indicatorNeedsHighLow(ind)) needHighLow = true;
            if (indicatorNeedsVolume(ind)) needVolume = true;
            if (ind == TechnicalIndicator::VOLUME_RATIO) needVolume = true;
            if (ind == TechnicalIndicator::TURNOVER_STABILITY) {
                appendRequiredField(req, "turnover_rate");
            }
        }
        if (needHighLow) {
            appendRequiredField(req, "high");
            appendRequiredField(req, "low");
        }
        if (needVolume) {
            appendRequiredField(req, "volume");
        }
    }

    appendHistoricalNeutralizationRequirements(req, params_.neutralizationEnabled);
    return req;
}

BoundaryRules TechnicalFactor::getBoundaryRules() const
{
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints, 2);
    return rules;
}

void TechnicalFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    if (config::hasCalculationConfig(config))
        params_.fromJson(config::calculationConfig(config));
    dataRequirements_ = getDataRequirements();
}

int TechnicalFactor::getLookbackDays() const
{
    // 按实际启用的指示器动态计算，未配置则默认全部
    const auto& indicators = params_.technicalIndicators;
    const bool allEnabled = indicators.empty();

    int maxNeed = params_.window + 10;  // collectSeries 的基值

    auto update = [&](TechnicalIndicator ind, int need) {
        if (allEnabled || std::find(indicators.begin(), indicators.end(), ind) != indicators.end()) {
            maxNeed = std::max(maxNeed, need);
        }
    };

    update(TechnicalIndicator::RSI,                params_.rsiWindow + 1);
    update(TechnicalIndicator::MACD,               params_.macdSlowPeriod + params_.macdSignalPeriod + 1);
    update(TechnicalIndicator::MA,                 params_.maWindow);
    update(TechnicalIndicator::EMA,                params_.emaWindow);
    update(TechnicalIndicator::BOLL,               params_.bollWindow);
    update(TechnicalIndicator::KDJ,                params_.kdjWindow);
    update(TechnicalIndicator::ATR,                params_.atrWindow + 1);
    update(TechnicalIndicator::VWAP,               2);
    update(TechnicalIndicator::VOLUME_RATIO,       params_.volumeRatioWindow);
    update(TechnicalIndicator::OBV,                params_.obvWindow + 1);
    update(TechnicalIndicator::TURNOVER_STABILITY, params_.turnoverStabilityWindow);

    return maxNeed;
}

} // namespace factor
