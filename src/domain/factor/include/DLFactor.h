#pragma once

#include "BaseFactor.h"
#include "FactorMetricConfig.h"
#include "factor_enums.h"

#include <string>

namespace factor {

class DLFactor final : public BaseFactor {
public:
    struct Params : CommonParams {
        DLModelType modelType{DLModelType::LSTM};
        int hiddenLayers = 3;
        int hiddenUnits = 128;
        int featureCount = 12;  // 与 train.py FEATURE_FIELDS 对齐
        int predictionHorizon = 5;         // 预测未来N日收益，必须与回测 forwardDays 一致
        double learningRate = 0.001;
        int batchSize = 512;
        int epochs = 100;
        DLOptimizer optimizer{DLOptimizer::ADAM};
        double dropoutRate = 0.2;
        bool orthogonalConstraint = false;
        bool ascending = true;             // 实际方向由训练目标决定，UI可调
        std::string modelPath;             // 预训练权重文件路径

        void fromJson(const foundation::json::JsonFacade& json);
    };

    DLFactor();

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;
    int getLookbackDays() const override { return params_.lookbackWindow; }

    static std::shared_ptr<DLFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

private:
    Params params_;

    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor
