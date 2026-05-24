#pragma once

#include "factor_enums.h"

#include <string>
#include <vector>

namespace factor {

struct CompositeChildSpec {
    std::string instanceId;
    double weight{1.0};
    bool ascending{true};
    CompositeNormalizeMode normalizeMode{CompositeNormalizeMode::ZScore};
};

struct CompositeFactorParams {
    std::vector<CompositeChildSpec> children;
    CompositeCombineMode combineMode{CompositeCombineMode::WeightedAverage};
    CompositeMissingPolicy missingPolicy{CompositeMissingPolicy::RenormalizeWeights};
    double minimumCoverageRatio{0.5};
};

} // namespace factor