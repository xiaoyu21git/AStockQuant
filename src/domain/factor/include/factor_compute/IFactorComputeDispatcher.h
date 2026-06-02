#pragma once

#include "IFactorOperatorLibrary.h"

#include <vector>

namespace factor::compute {

class IFactorComputeDispatcher {
public:
    virtual ~IFactorComputeDispatcher() = default;

    [[nodiscard]] virtual FactorResult<std::vector<double>> evaluateOnClose(
        NumericConstMatrixView closeView,
        uint32_t computeToken) const = 0;
};

} // namespace factor::compute

