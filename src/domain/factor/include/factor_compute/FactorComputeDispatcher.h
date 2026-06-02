#pragma once

#include "IFactorComputeDispatcher.h"
#include "IFactorComputeOperatorExtension.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace factor::compute {

class FactorComputeDispatcher final
    : public IFactorComputeDispatcher
    , public IFactorComputeOperatorRegistry {
public:
    explicit FactorComputeDispatcher(const IFactorOperatorLibrary& factorOperatorLibrary) noexcept;

    FactorComputeDispatcher(
        const IFactorOperatorLibrary& factorOperatorLibrary,
        const std::vector<const IComputeOpRegistrar*>& extensionRegistrars) noexcept;

    [[nodiscard]] FactorResult<std::vector<double>> evaluateOnClose(
        NumericConstMatrixView closeView,
        uint32_t computeToken) const override;

    bool registerOperator(
        uint32_t computeToken,
        std::unique_ptr<IComputeOp> computeOperator) override;

private:
    [[nodiscard]] static NumericMatrixView
    buildMutableMatrixView(double* data, int32_t rowCount, int32_t columnCount) noexcept;

    void registerDefaultOperators();
    bool registerExtensionOperators(
        const std::vector<const IComputeOpRegistrar*>& extensionRegistrars);

    [[nodiscard]] const IComputeOp* findOperator(uint32_t computeToken) const noexcept;

    const IFactorOperatorLibrary& factorOperatorLibrary_;
    std::unordered_map<uint32_t, std::unique_ptr<IComputeOp>> dispatchTable_;
    bool hasInvalidRegistration_{false};
};

} // namespace factor::compute

