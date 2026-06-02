#pragma once

#include "FactorSignalTypes.h"

namespace factor::compute {

struct ComputePlanNode final {
    FactorId factor{};
    std::vector<FactorId> dependencies;
    uint32_t computeFunctionToken{0U};

    [[nodiscard]] bool isValid() const noexcept
    {
        return factor.isValid() && computeFunctionToken != 0U;
    }
};

struct ComputePlan final {
    std::vector<ComputePlanNode> nodes;
    DateRange dateRange{};
    std::vector<InstrumentId> universe;

    [[nodiscard]] bool isValid() const noexcept
    {
        return !nodes.empty() && dateRange.isValid() && !universe.empty();
    }
};

class IFactorRegistry {
public:
    virtual ~IFactorRegistry() = default;

    [[nodiscard]] virtual FactorResult<FactorId>
    registerFormula(
        FactorName factorName,
        FormulaExpr formulaExpression,
        const std::vector<FieldKey>& requiredFields) = 0;

    [[nodiscard]] virtual FactorResult<FactorId>
    registerCustom(
        FactorName factorName,
        uint32_t customComputeToken,
        const std::vector<FieldKey>& requiredFields) = 0;

    [[nodiscard]] virtual FactorResult<ComputePlan>
    buildPlan(
        const std::vector<FactorId>& requestedFactors,
        DateRange dateRange,
        const std::vector<InstrumentId>& universe) const = 0;
};

} // namespace factor::compute

