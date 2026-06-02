#pragma once

#include "IFactorOperatorLibrary.h"

#include <memory>

namespace factor::compute {

class IComputeOp {
public:
    virtual ~IComputeOp() = default;

    virtual void execute(
        const IFactorOperatorLibrary& factorOperatorLibrary,
        NumericConstMatrixView closeView,
        NumericMatrixView outputView) const = 0;
};

class IFactorComputeOperatorRegistry {
public:
    virtual ~IFactorComputeOperatorRegistry() = default;

    virtual bool registerOperator(
        uint32_t computeToken,
        std::unique_ptr<IComputeOp> computeOperator) = 0;
};

class IComputeOpRegistrar {
public:
    virtual ~IComputeOpRegistrar() = default;

    virtual bool registerOperators(IFactorComputeOperatorRegistry& registry) const = 0;
};

} // namespace factor::compute

