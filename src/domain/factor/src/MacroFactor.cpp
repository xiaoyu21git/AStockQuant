#include "domain/factor/include/MacroFactor.h"
#include "domain/factor/include/BaseFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace factor {

namespace {

constexpr const char* kDefaultBenchmark = "000300.SH";

constexpr const char* priceTypeFieldName(TechnicalPriceType type) noexcept
{
    switch (type) {
    case TechnicalPriceType::CLOSE: return "close";
    case TechnicalPriceType::OPEN:  return "open";
    case TechnicalPriceType::HIGH:  return "high";
    case TechnicalPriceType::LOW:   return "low";
    default: return "close";
    }
}

constexpr const char* indicatorFieldName(MacroIndicator ind) noexcept
{
    switch (ind) {
    case MacroIndicator::INDUSTRIAL_ADDED_VALUE_YOY: return MacroFactor::FIELD_INDUSTRIAL_ADDED_VALUE_YOY;
    case MacroIndicator::MANUFACTURING_PMI:          return MacroFactor::FIELD_MANUFACTURING_PMI;
    case MacroIndicator::GDP_YOY:                    return MacroFactor::FIELD_GDP_YOY;
    case MacroIndicator::CPI_YOY:                    return MacroFactor::FIELD_CPI_YOY;
    case MacroIndicator::PPI_YOY:                    return MacroFactor::FIELD_PPI_YOY;
    case MacroIndicator::M2_YOY:                     return MacroFactor::FIELD_M2_YOY;
    case MacroIndicator::SOCIAL_FINANCING_STOCK_YOY:  return MacroFactor::FIELD_SOCIAL_FINANCING_STOCK_YOY;
    case MacroIndicator::M1_M2_SPREAD:               return MacroFactor::FIELD_M1_M2_SPREAD;
    case MacroIndicator::TEN_YEAR_BOND_YIELD:         return MacroFactor::FIELD_TEN_YEAR_BOND_YIELD;
    case MacroIndicator::SHIBOR_3M:                  return MacroFactor::FIELD_SHIBOR_3M;
    case MacroIndicator::LPR_1Y:                     return MacroFactor::FIELD_LPR_1Y;
    case MacroIndicator::RESERVE_REQUIREMENT_RATIO:   return MacroFactor::FIELD_RESERVE_REQUIREMENT_RATIO;
    case MacroIndicator::AA_CREDIT_SPREAD:            return MacroFactor::FIELD_AA_CREDIT_SPREAD;
    case MacroIndicator::VIX_PROXY:                  return MacroFactor::FIELD_VIX_PROXY;
    default: return nullptr;
    }
}

} // anonymous namespace

MacroFactor::MacroFactor()
{
    factorType_ = FactorType::MACRO;
}

CalculationResult MacroFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(context, "宏观因子需要 HistoricalView");
    }

    const CommonParams& common = params_;
    const auto symbols = effectiveSymbols(context);
    const int macroWin = (std::max)(1, params_.macroWindow);
    const std::string benchmark = params_.benchmarkSymbol.empty()
        ? kDefaultBenchmark : params_.benchmarkSymbol;
    const char* const priceField = priceTypeFieldName(params_.priceType);
    const bool useIndicators = !params_.macroIndicators.empty();

    return executeWithCommonParams(
        context,
        common,
        [&]() { return context.date; },
        [&](const CommonRuntimeState& runtime, CalculationResult& result) {
            if (useIndicators) {
                // 按配置的宏观指标逐个读取横切面（市场级字段，取首值即可）
                std::vector<double> values;
                values.reserve(params_.macroIndicators.size());
                bool anyAvailable = false;
                for (MacroIndicator ind : params_.macroIndicators) {
                    const char* field = indicatorFieldName(ind);
                    if (!field || !context.historicalView->hasField(field)) continue;
                    const auto cs = context.historicalView->getCrossSection(
                        runtime.effectiveDate, field, symbols);
                    for (const auto& [sym, val] : cs) {
                        (void)sym;
                        if (std::isfinite(val)) { values.push_back(val); anyAvailable = true; break; }
                    }
                }
                if (anyAvailable) {
                    const double macroValue = std::accumulate(values.begin(), values.end(), 0.0)
                        / static_cast<double>(values.size());
                    for (const auto& symbol : symbols) result.values[symbol] = macroValue;
                } else {
                    for (const auto& symbol : symbols) result.values[symbol] = 0.0;
                    result.metadata.set("macroDataUnavailable", json_helper::toJsonValue(true));
                }
                return;
            }

            // macroIndicators 未配置 → benchmark 收益率代理
            auto series = context.historicalView->getSeries(
                benchmark, runtime.effectiveDate, macroWin, priceField);
            double macroRet = 0.0;
            bool available = false;
            if (series.size() >= 2) {
                const double first = series.front().value;
                const double last  = series.back().value;
                if (std::isfinite(first) && std::isfinite(last) && first > 0.0 && last > 0.0) {
                    macroRet = last / first - 1.0;
                    available = true;
                }
            }
            for (const auto& symbol : symbols) {
                result.values[symbol] = available ? macroRet : 0.0;
            }
            if (!available) {
                result.metadata.set("benchmarkUnavailable", json_helper::toJsonValue(benchmark));
            }
        },
        [](const CommonRuntimeState&, CalculationResult&) {},
        [&](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("benchmarkSymbol", json_helper::toJsonValue(benchmark));
            result.metadata.set("macroWindow", json_helper::toJsonValue(macroWin));
            result.metadata.set("macroFrequency",
                json_helper::toJsonValue(static_cast<int>(params_.macroFrequency)));
            if (useIndicators) {
                result.metadata.set("indicatorCount",
                    json_helper::toJsonValue(static_cast<int>(params_.macroIndicators.size())));
            }
        });
}

std::shared_ptr<MacroFactor> MacroFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<MacroFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

DataRequirements MacroFactor::getDataRequirements() const
{
    DataRequirements req;
    if (!params_.macroIndicators.empty()) {
        for (MacroIndicator ind : params_.macroIndicators) {
            const char* field = indicatorFieldName(ind);
            if (field) appendRequiredField(req, field);
        }
    } else {
        appendRequiredField(req, priceTypeFieldName(params_.priceType));
    }
    appendHistoricalNeutralizationRequirements(req, params_.neutralizationEnabled);
    return req;
}

BoundaryRules MacroFactor::getBoundaryRules() const
{
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints,
        (std::max)(1, static_cast<int>(params_.window)) * 3);
    return rules;
}

void MacroFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    if (config::hasCalculationConfig(config))
        params_.fromJson(config::calculationConfig(config));
    dataRequirements_ = getDataRequirements();
}

} // namespace factor
