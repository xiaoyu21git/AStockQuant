#pragma once

#include "BaseFactor.h"
#include "FactorMetricConfig.h"
#include "factor_enums.h"

#include <string>
#include <vector>

namespace factor {

class CustomFactor final : public BaseFactor {
public:
    struct CustomVariableBinding {
        std::string name;
        std::string field;
        bool hasDefaultValue{false};
        double defaultValue{0.0};
    };

    struct Params : CommonParams {
        std::string expression;
        std::vector<CustomVariableBinding> variables;

        void fromJson(const foundation::json::JsonFacade& json);
    };

    CustomFactor();

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;

    static std::shared_ptr<CustomFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

private:
    Params params_;

    const CustomVariableBinding* findCustomVariableBinding(const std::string& variableName) const;

    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor