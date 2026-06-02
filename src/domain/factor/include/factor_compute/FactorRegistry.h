#pragma once

#include "IFactorRegistry.h"

#include <unordered_map>

namespace factor::compute {

class FactorRegistry final : public IFactorRegistry {
public:
    [[nodiscard]] FactorResult<FactorId>
    registerFormula(
        FactorName factorName,
        FormulaExpr formulaExpression,
        const std::vector<FieldKey>& requiredFields) override;

    [[nodiscard]] FactorResult<FactorId>
    registerCustom(
        FactorName factorName,
        uint32_t customComputeToken,
        const std::vector<FieldKey>& requiredFields) override;

    [[nodiscard]] FactorResult<ComputePlan>
    buildPlan(
        const std::vector<FactorId>& requestedFactors,
        DateRange dateRange,
        const std::vector<InstrumentId>& universe) const override;

private:
    struct RegisteredFactor final {
        FactorId factorId{};
        uint32_t computeFunctionToken{0U};
        std::vector<FieldKey> requiredFields;
    };

    [[nodiscard]] const RegisteredFactor*
    findRegisteredFactorById(const FactorId& factorId) const noexcept;

    [[nodiscard]] ComputePlanNode
    buildComputePlanNode(const FactorId& factorId, const RegisteredFactor& registeredFactor) const;

    std::unordered_map<uint32_t, RegisteredFactor> factorMapByName_;
    std::unordered_map<uint32_t, RegisteredFactor> factorMapById_;
    uint32_t nextFactorId_{1U};
};

} // namespace factor::compute

