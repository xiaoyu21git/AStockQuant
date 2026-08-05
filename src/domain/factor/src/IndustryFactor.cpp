#include "domain/factor/include/IndustryFactor.h"
#include "domain/factor/include/BaseFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace factor {

namespace {

/// @brief 行业分类字段枚举精确映射，零 fallback —— 未知枚举返回 nullptr
constexpr const char* sectorTypeToFieldName(ConfigurableSectorType type) noexcept
{
    switch (type) {
    case ConfigurableSectorType::SW_L1:   return "sw_industry_1";
    case ConfigurableSectorType::SW_L2:   return "sw_industry_2";
    case ConfigurableSectorType::CITIC_L1: return "citics_industry_1";
    case ConfigurableSectorType::CITIC_L2: return "citics_industry_2";
    default: return nullptr;
    }
}

} // anonymous namespace

IndustryFactor::IndustryFactor()
{
    factorType_ = FactorType::INDUSTRY;
}

CalculationResult IndustryFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(context, "行业因子需要 HistoricalView");
    }

    const CommonParams& common = params_;
    const auto symbols = effectiveSymbols(context);
    const IndustryMetric metricKind = params_.industryMetricKind;
    const int window = (std::max)(1, static_cast<int>(common.window));

    // 提前解析行业字段（计算前校验），未知枚举 → nullptr → 后续直接全零
    const char* const sectorField = sectorTypeToFieldName(params_.sectorType);

    return executeWithCommonParams(
        context,
        common,
        [&]() { return context.date; },

        // ── Lambda 2: 原始值计算 ──
        [&](const CommonRuntimeState& runtime, CalculationResult& result) {
            // 1. 校验行业字段存在性 —— 零 fallback，字段缺失直接全零
            if (!sectorField || !context.historicalView->hasField(sectorField)) {
                for (const auto& symbol : symbols) {
                    result.values[symbol] = 0.0;
                }
                result.metadata.set("sectorFieldMissing",
                    json_helper::toJsonValue(sectorField ? sectorField : "null"));
                return;
            }

            // 2. 获取行业码与市值横切面（仅一次，全 symbol 复用）
            const auto industryCs = context.historicalView->getCrossSection(
                runtime.effectiveDate, sectorField, symbols);
            const auto marketCapCs = context.historicalView->getCrossSection(
                runtime.effectiveDate, "market_cap", symbols);

            // 3. 集中度走快速路径（只需横切面，无需逐 symbol 取序列）
            if (metricKind == IndustryMetric::INDUSTRY_CONCENTRATION) {
                // 按行业分组：行业码 → [市值]
                std::unordered_map<int, std::vector<double>> capGroups;
                for (const auto& symbol : symbols) {
                    auto indIt = industryCs.find(symbol);
                    if (indIt == industryCs.end()) continue;
                    const double indCodeDbl = indIt->second;
                    if (!std::isfinite(indCodeDbl)) continue;
                    const int indCode = static_cast<int>(indCodeDbl);
                    if (indCode <= 0) continue;

                    auto mktIt = marketCapCs.find(symbol);
                    if (mktIt == marketCapCs.end()) continue;
                    const double mktCap = mktIt->second;
                    if (!std::isfinite(mktCap) || mktCap <= 0.0) continue;

                    capGroups[indCode].push_back(mktCap);
                }

                // HHI: Σ(市值占比²)
                std::unordered_map<int, double> industryHhi;
                for (auto& [indCode, caps] : capGroups) {
                    if (caps.size() < 2) {
                        industryHhi[indCode] = 0.0;
                        continue;
                    }
                    double totalCap = 0.0;
                    for (double c : caps) totalCap += c;
                    if (totalCap <= 0.0) {
                        industryHhi[indCode] = 0.0;
                        continue;
                    }
                    double hhi = 0.0;
                    for (double c : caps) {
                        const double share = c / totalCap;
                        hhi += share * share;
                    }
                    industryHhi[indCode] = hhi;
                }

                // 广播到所有 symbol
                for (const auto& symbol : symbols) {
                    auto indIt = industryCs.find(symbol);
                    if (indIt == industryCs.end()) {
                        result.values[symbol] = 0.0;
                        continue;
                    }
                    const double indCodeDbl = indIt->second;
                    if (!std::isfinite(indCodeDbl)) {
                        result.values[symbol] = 0.0;
                        continue;
                    }
                    const int indCode = static_cast<int>(indCodeDbl);
                    auto valIt = industryHhi.find(indCode);
                    result.values[symbol] = (valIt != industryHhi.end()) ? valIt->second : 0.0;
                }

                if (result.values.empty()) {
                    result.metadata.set("emptyReason",
                        json_helper::toJsonValue("行业集中度没有可用的市值数据"));
                }
                return;
            }

            // 4. 景气度 / 动量：按行业分组 → [(收益率, 市值)]
            std::unordered_map<int, std::vector<std::pair<double, double>>> groups;

            for (const auto& symbol : symbols) {
                auto indIt = industryCs.find(symbol);
                if (indIt == industryCs.end()) continue;
                const double indCodeDbl = indIt->second;
                if (!std::isfinite(indCodeDbl)) continue;
                const int indCode = static_cast<int>(indCodeDbl);
                if (indCode <= 0) continue;

                // 市值
                double mktCap = 0.0;
                auto mktIt = marketCapCs.find(symbol);
                if (mktIt != marketCapCs.end() && std::isfinite(mktIt->second) && mktIt->second > 0.0) {
                    mktCap = mktIt->second;
                }

                // 收盘价序列（窗口回溯）
                auto series = context.historicalView->getSeries(
                    symbol, runtime.effectiveDate, window, "close");
                if (series.size() < 2) continue;
                const double firstClose = series.front().value;
                const double lastClose  = series.back().value;
                if (!std::isfinite(firstClose) || !std::isfinite(lastClose)
                    || firstClose <= 0.0 || lastClose <= 0.0) continue;

                const double ret = lastClose / firstClose - 1.0;
                groups[indCode].emplace_back(ret, mktCap);
            }

            // 5. 按行业计算指标值
            std::unordered_map<int, double> industryValues;
            for (auto& [indCode, items] : groups) {
                if (items.size() < 2) {
                    industryValues[indCode] = 0.0;
                    continue;
                }

                double indValue = 0.0;
                switch (metricKind) {
                case IndustryMetric::INDUSTRY_PROSPERITY: {
                    // 市值加权平均收益率
                    double totalCap = 0.0;
                    for (const auto& item : items) totalCap += item.second;
                    if (totalCap <= 0.0) {
                        // 全零市值 → 退化为等权
                        double sum = 0.0;
                        for (const auto& item : items) sum += item.first;
                        indValue = sum / static_cast<double>(items.size());
                    } else {
                        for (const auto& item : items) {
                            indValue += (item.second / totalCap) * item.first;
                        }
                    }
                    break;
                }
                case IndustryMetric::INDUSTRY_MOMENTUM: {
                    // 等权平均收益率
                    double sum = 0.0;
                    for (const auto& item : items) sum += item.first;
                    indValue = sum / static_cast<double>(items.size());
                    break;
                }
                default: {
                    indValue = 0.0;
                    break;
                }
                }
                industryValues[indCode] = indValue;
            }

            // 6. 广播行业值到同行业所有 symbol
            for (const auto& symbol : symbols) {
                auto indIt = industryCs.find(symbol);
                if (indIt == industryCs.end()) {
                    result.values[symbol] = 0.0;
                    continue;
                }
                const double indCodeDbl = indIt->second;
                if (!std::isfinite(indCodeDbl)) {
                    result.values[symbol] = 0.0;
                    continue;
                }
                const int indCode = static_cast<int>(indCodeDbl);
                auto valIt = industryValues.find(indCode);
                result.values[symbol] = (valIt != industryValues.end()) ? valIt->second : 0.0;
            }

            if (result.values.empty()) {
                result.metadata.set("emptyReason",
                    json_helper::toJsonValue("行业因子没有可用数据"));
            }
        },

        // ── Lambda 3: 标准化前处理（空实现）──
        [](const CommonRuntimeState&, CalculationResult&) {},

        // ── Lambda 4: 元数据 ──
        [&](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("industryMetricKind",
                json_helper::toJsonValue(static_cast<int>(metricKind)));
            result.metadata.set("sectorType",
                json_helper::toJsonValue(static_cast<int>(params_.sectorType)));
            result.metadata.set("window",
                json_helper::toJsonValue(window));
            result.metadata.set("actualSectorField",
                json_helper::toJsonValue(sectorField ? sectorField : "MISSING"));
        });
}

std::shared_ptr<IndustryFactor> IndustryFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<IndustryFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

DataRequirements IndustryFactor::getDataRequirements() const
{
    DataRequirements req;

    // 行业分类字段 —— 仅当枚举精确匹配到已知字段时才声明依赖
    const char* field = sectorTypeToFieldName(params_.sectorType);
    if (field) {
        appendRequiredField(req, field);
    }
    appendRequiredField(req, "close");
    appendRequiredField(req, "market_cap");
    appendHistoricalNeutralizationRequirements(req, params_.neutralizationEnabled);
    return req;
}

BoundaryRules IndustryFactor::getBoundaryRules() const
{
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints, 1);
    return rules;
}

void IndustryFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    if (config::hasCalculationConfig(config))
        params_.fromJson(config::calculationConfig(config));
    dataRequirements_ = getDataRequirements();
}

} // namespace factor
