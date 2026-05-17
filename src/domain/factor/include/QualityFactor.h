#pragma once

#include "ConfigurableFactor.h"

namespace factor {

class QualityFactorTestAccess;

class QualityFactor : public BaseFactor {
public:
    struct Params : ConfigurableFactorBase::CommonParams {
        QualityMetric metric = QualityMetric::ROE;
        double qualityThreshold = 0.1;
    };

    QualityFactor();
    ~QualityFactor() override = default;

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;

    static std::shared_ptr<QualityFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

private:
    friend class QualityFactorTestAccess;

    Params params_;

    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor