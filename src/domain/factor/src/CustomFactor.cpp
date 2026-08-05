#include "domain/factor/include/CustomFactor.h"
#include "domain/factor/include/BaseFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace factor {

CustomFactor::CustomFactor()
{
    factorType_ = FactorType::CUSTOM;
}

CalculationResult CustomFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(context, "自定义因子需要 HistoricalView");
    }

    const CommonParams& common = params_;
    const auto symbols = effectiveSymbols(context);

    return executeWithCommonParams(
        context,
        common,
        [&]() { return context.date; },

        // ── Lambda 2: 自定义因子计算 ──
        [&](const CommonRuntimeState& runtime, CalculationResult& result) {
            // 模式 A：expression 匹配已知字段名 → 直接读横切面
            if (!params_.expression.empty()) {
                const std::string& field = params_.expression;
                if (context.historicalView->hasField(field)) {
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
                    for (const auto& symbol : symbols) {
                        result.values[symbol] = 0.0;
                    }
                    result.metadata.set("expressionNotSupported",
                        json_helper::toJsonValue(field));
                }
                return;
            }

            // 模式 B：variables 聚合（等权 ZScore 标准化后平均）
            if (!params_.variables.empty()) {
                // 逐变量读取横切面
                struct VarData {
                    std::string name;
                    std::unordered_map<std::string, double> values;
                    bool valid = false;
                };
                std::vector<VarData> varData(params_.variables.size());

                for (size_t vi = 0; vi < params_.variables.size(); ++vi) {
                    const auto& var = params_.variables[vi];
                    varData[vi].name = var.name;
                    if (!var.field.empty() && context.historicalView->hasField(var.field)) {
                        varData[vi].values = context.historicalView->getCrossSection(
                            runtime.effectiveDate, var.field, symbols);
                        varData[vi].valid = !varData[vi].values.empty();
                    }
                    // 字段缺失但有默认值 → 所有 symbol 赋默认值
                    if (!varData[vi].valid && var.hasDefaultValue) {
                        for (const auto& symbol : symbols) {
                            varData[vi].values[symbol] = var.defaultValue;
                        }
                        varData[vi].valid = true;
                    }
                }

                // 检查是否有任何有效变量
                const bool anyValid = std::any_of(varData.begin(), varData.end(),
                    [](const VarData& vd) { return vd.valid; });
                if (!anyValid) {
                    for (const auto& symbol : symbols) {
                        result.values[symbol] = 0.0;
                    }
                    result.metadata.set("noValidVariables",
                        json_helper::toJsonValue(true));
                    return;
                }

                // 裸值等权聚合，不做内部归一化
                for (const auto& symbol : symbols) {
                    double sum = 0.0;
                    int n = 0;
                    for (const auto& vd : varData) {
                        if (!vd.valid) continue;
                        auto it = vd.values.find(symbol);
                        if (it != vd.values.end() && std::isfinite(it->second)) {
                            sum += it->second;
                            ++n;
                        }
                    }
                    result.values[symbol] = (n > 0) ? (sum / static_cast<double>(n)) : 0.0;
                }

                if (result.values.empty()) {
                    result.metadata.set("emptyReason",
                        json_helper::toJsonValue("自定义变量聚合没有可用数据"));
                }
                return;
            }

            // 模式 C：无任何配置 → 全零
            for (const auto& symbol : symbols) {
                result.values[symbol] = 0.0;
            }
        },

        // ── Lambda 3: 标准化前处理（空）──
        [](const CommonRuntimeState&, CalculationResult&) {},

        // ── Lambda 4: 元数据 ──
        [&](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("expression",
                json_helper::toJsonValue(params_.expression));
            result.metadata.set("variableCount",
                json_helper::toJsonValue(static_cast<int>(params_.variables.size())));
            auto varNames = foundation::json::JsonFacade::createArray();
            for (const auto& v : params_.variables) {
                varNames.push_back(json_helper::toJsonValue(v.name.empty() ? v.field : v.name));
            }
            result.metadata.set("variableNames", varNames);
        });
}

std::shared_ptr<CustomFactor> CustomFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<CustomFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

DataRequirements CustomFactor::getDataRequirements() const
{
    DataRequirements req;
    // 模式 A：expression 作为字段名
    if (!params_.expression.empty()) {
        appendRequiredField(req, params_.expression);
    }
    // 模式 B：variables 中的字段
    for (const auto& var : params_.variables) {
        if (!var.field.empty()) {
            appendRequiredField(req, var.field);
        }
    }
    appendHistoricalNeutralizationRequirements(req, params_.neutralizationEnabled);
    return req;
}

BoundaryRules CustomFactor::getBoundaryRules() const
{
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints, 1);
    return rules;
}

void CustomFactor::Params::fromJson(const foundation::json::JsonFacade& json) {
    CommonParams::fromJson(json);
    if (json.has("expression")) expression = json.get("expression").asString();
    if (json.has("variables")) {
        variables.clear();
        const auto& arr = json.get("variables");
        if (arr.isArray()) {
            for (size_t i = 0; i < arr.size(); ++i) {
                const auto& elem = arr.at(i);
                if (!elem.isObject()) continue;
                CustomVariableBinding vb;
                if (elem.has("name")) vb.name = elem.get("name").asString();
                if (elem.has("field")) vb.field = elem.get("field").asString();
                if (elem.has("hasDefaultValue")) vb.hasDefaultValue = elem.get("hasDefaultValue").asBool();
                if (elem.has("defaultValue")) vb.defaultValue = elem.get("defaultValue").asDouble();
                variables.push_back(vb);
            }
        }
    }
}

void CustomFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    if (config::hasCalculationConfig(config))
        params_.fromJson(config::calculationConfig(config));
    dataRequirements_ = getDataRequirements();
}

} // namespace factor
