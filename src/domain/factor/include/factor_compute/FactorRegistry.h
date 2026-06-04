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

    /// @brief 注册字段键到实际字段名的映射（由桥接层在初始化时调用）
    void registerFieldMapping(FieldKey key, const std::string& fieldName);

    /// @brief 查询字段键对应的实际字段名
    [[nodiscard]] std::string resolveFieldName(FieldKey key) const;

private:
    struct RegisteredFactor final {
        FactorId factorId{};
        uint32_t computeFunctionToken{0U};
        std::vector<FieldKey> requiredFields;
        // 从 requiredFields 推断的首选字段名（如 "close", "pb_ratio", "roe"）
        std::string fieldName;
    };

    [[nodiscard]] const RegisteredFactor*
    findRegisteredFactorById(const FactorId& factorId) const noexcept;

    [[nodiscard]] ComputePlanNode
    buildComputePlanNode(const FactorId& factorId, const RegisteredFactor& registeredFactor) const;

    std::unordered_map<uint32_t, RegisteredFactor> factorMapByName_;
    std::unordered_map<uint32_t, RegisteredFactor> factorMapById_;
    std::unordered_map<uint32_t, std::string> fieldKeyToName_;
    uint32_t nextFactorId_{1U};
};

} // namespace factor::compute

