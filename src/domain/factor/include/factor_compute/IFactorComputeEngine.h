#pragma once

#include "FactorSignalTypes.h"

namespace factor::compute {

class IFactorComputeEngine {
public:
    virtual ~IFactorComputeEngine() = default;

    [[nodiscard]] virtual FactorResult<SignalSet>
    generate(const GenerateSpec& spec) = 0;

    [[nodiscard]] virtual FactorResult<SignalValue>
    query(const QuerySpec& spec) const = 0;
};

} // namespace factor::compute

