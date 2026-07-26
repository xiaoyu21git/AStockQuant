#include "domain/factor/include/DLFactor.h"
#include "domain/factor/include/BaseFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"

namespace factor {

DLFactor::DLFactor()
{
    factorType_ = FactorType::DL;
}

CalculationResult DLFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(context, "AI因子需要 HistoricalView");
    }

    const CommonParams& common = params_;
    const auto symbols = effectiveSymbols(context);

    return executeWithCommonParams(
        context,
        common,
        [&]() { return context.date; },
        [&](const CommonRuntimeState&, CalculationResult& result) {
            // NOTE: AI因子采用"离线训练 + 在线推理"模式。
            // 训练模块独立于回测流程，将模型权重序列化到 modelPath。
            // calculate() 仅加载权重执行前向推理，不触发训练。
            // 当前流程：
            // 1. 若 modelPath 为空或文件不存在 → 返回全零 + metadata 标记
            // 2. 若模型可用 → 加载权重 → 拼接输入特征 → 前向传播 → 输出因子值
            //
            // 输入特征来源：HistoricalView 中的 close, volume, amount, vwap,
            // market_cap 等字段，按 featureCount 和 lookbackWindow 构建张量。
            // 推理引擎建议：libtorch (C++原生) 或 ONNX Runtime (跨框架)。
            for (const auto& symbol : symbols) {
                result.values[symbol] = 0.0;
            }
            result.metadata.set("dlModelPending", json_helper::toJsonValue(true));
            if (params_.modelPath.empty()) {
                result.metadata.set("modelPathMissing", json_helper::toJsonValue(true));
            }
        },
        [](const CommonRuntimeState&, CalculationResult&) {},
        [&](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("modelType",
                json_helper::toJsonValue(static_cast<int>(params_.modelType)));
            result.metadata.set("featureCount",
                json_helper::toJsonValue(params_.featureCount));
            result.metadata.set("predictionHorizon",
                json_helper::toJsonValue(params_.predictionHorizon));
        });
}

std::shared_ptr<DLFactor> DLFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<DLFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

DataRequirements DLFactor::getDataRequirements() const
{
    DataRequirements req;
    appendRequiredField(req, "close");
    appendRequiredField(req, "volume");
    appendRequiredField(req, "vwap");
    appendRequiredField(req, "market_cap");
    appendHistoricalNeutralizationRequirements(req, params_.neutralizationEnabled);
    return req;
}

BoundaryRules DLFactor::getBoundaryRules() const
{
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints, static_cast<int>(params_.lookbackWindow));
    return rules;
}

void DLFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    if (config::hasCalculationConfig(config))
        params_.fromJson(config::calculationConfig(config));
    dataRequirements_ = getDataRequirements();
}

} // namespace factor
