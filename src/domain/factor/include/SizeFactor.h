#pragma once

#include "ConfigurableFactor.h"

#include <QString>

namespace factor {

class SizeFactor : public BaseFactor {
public:
    struct Params : ConfigurableFactorBase::CommonParams {
        SizeMetric sizeMetric = SizeMetric::MARKET_CAP;
        bool logTransform = true;
    };

    SizeFactor();
    ~SizeFactor() override = default;

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;

    static std::shared_ptr<SizeFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

    friend class SizeFactorTestAccess;

private:
    Params params_;

    QString selectedColumn() const;
    double scoreFromRawValue(double rawValue) const;
    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor