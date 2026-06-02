#include "factor_compute/FactorRegistry.h"

namespace factor::compute {

namespace {

bool hasInvalidFieldKey(const std::vector<FieldKey>& requiredFields)
{
    for (const FieldKey& fieldKey : requiredFields) {
        if (!fieldKey.isValid()) {
            return true;
        }
    }
    return false;
}

bool hasInvalidInstrument(const std::vector<InstrumentId>& universe)
{
    for (const InstrumentId& instrumentId : universe) {
        if (!instrumentId.isValid()) {
            return true;
        }
    }
    return false;
}

} // namespace

const FactorRegistry::RegisteredFactor*
FactorRegistry::findRegisteredFactorById(const FactorId& factorId) const noexcept
{
    const auto factorEntry = factorMapById_.find(factorId.value);
    if (factorEntry == factorMapById_.end()) {
        return nullptr;
    }
    return &factorEntry->second;
}

ComputePlanNode
FactorRegistry::buildComputePlanNode(const FactorId& factorId, const RegisteredFactor& registeredFactor) const
{
    ComputePlanNode node;
    node.factor = factorId;
    node.computeFunctionToken = registeredFactor.computeFunctionToken;
    return node;
}

FactorResult<FactorId>
FactorRegistry::registerFormula(
    FactorName factorName,
    FormulaExpr formulaExpression,
    const std::vector<FieldKey>& requiredFields)
{
    if (!factorName.isValid() || !formulaExpression.isValid()) {
        return FactorResult<FactorId>::failure(FactorError::InvalidFormula);
    }
    if (requiredFields.empty() || hasInvalidFieldKey(requiredFields)) {
        return FactorResult<FactorId>::failure(FactorError::InsufficientData);
    }

    if (factorMapByName_.find(factorName.value) != factorMapByName_.end()) {
        return FactorResult<FactorId>::failure(FactorError::InvalidFormula);
    }

    RegisteredFactor registeredFactor;
    registeredFactor.factorId = FactorId{nextFactorId_++};
    registeredFactor.computeFunctionToken = formulaExpression.token;
    registeredFactor.requiredFields = requiredFields;
    factorMapByName_.emplace(factorName.value, registeredFactor);
    factorMapById_.emplace(registeredFactor.factorId.value, registeredFactor);
    return FactorResult<FactorId>::success(registeredFactor.factorId);
}

FactorResult<FactorId>
FactorRegistry::registerCustom(
    FactorName factorName,
    uint32_t customComputeToken,
    const std::vector<FieldKey>& requiredFields)
{
    if (!factorName.isValid() || customComputeToken == 0U) {
        return FactorResult<FactorId>::failure(FactorError::InvalidFormula);
    }
    if (requiredFields.empty() || hasInvalidFieldKey(requiredFields)) {
        return FactorResult<FactorId>::failure(FactorError::InsufficientData);
    }

    if (factorMapByName_.find(factorName.value) != factorMapByName_.end()) {
        return FactorResult<FactorId>::failure(FactorError::InvalidFormula);
    }

    RegisteredFactor registeredFactor;
    registeredFactor.factorId = FactorId{nextFactorId_++};
    registeredFactor.computeFunctionToken = customComputeToken;
    registeredFactor.requiredFields = requiredFields;
    factorMapByName_.emplace(factorName.value, registeredFactor);
    factorMapById_.emplace(registeredFactor.factorId.value, registeredFactor);
    return FactorResult<FactorId>::success(registeredFactor.factorId);
}

FactorResult<ComputePlan>FactorRegistry::buildPlan(
    const std::vector<FactorId>& requestedFactors,
    DateRange dateRange,
    const std::vector<InstrumentId>& universe) const
{
    if (!dateRange.isValid()) {
        return FactorResult<ComputePlan>::failure(FactorError::InsufficientData);
    }
    if (requestedFactors.empty()) {
        return FactorResult<ComputePlan>::failure(FactorError::InvalidFormula);
    }
    if (universe.empty() || hasInvalidInstrument(universe)) {
        return FactorResult<ComputePlan>::failure(FactorError::InvalidUniverse);
    }

    ComputePlan computePlan;
    computePlan.dateRange = dateRange;
    computePlan.universe = universe;

    for (const FactorId& requestedFactor : requestedFactors) {
        if (!requestedFactor.isValid()) {
            return FactorResult<ComputePlan>::failure(FactorError::InvalidFormula);
        }

        const RegisteredFactor* const registeredFactor = findRegisteredFactorById(requestedFactor);
        if (registeredFactor == nullptr) {
            return FactorResult<ComputePlan>::failure(FactorError::InsufficientData);
        }

        computePlan.nodes.push_back(buildComputePlanNode(requestedFactor, *registeredFactor));
    }

    return FactorResult<ComputePlan>::success(computePlan);
}

} // namespace factor::compute


