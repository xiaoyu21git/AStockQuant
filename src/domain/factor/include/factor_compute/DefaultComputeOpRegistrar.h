#pragma once

#include "IFactorComputeOperatorExtension.h"

namespace factor::compute {

class DefaultOpRegistrar final : public IComputeOpRegistrar {
public:
    bool registerOperators(IFactorComputeOperatorRegistry& registry) const override;
};

} // namespace factor::compute
