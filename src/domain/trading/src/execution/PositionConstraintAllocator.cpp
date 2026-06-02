#include "PositionConstraintAllocator.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace astock::domain::trading::position_constraints {

PositionConstraintResult PositionConstraintAllocator::allocate(
    PositionConstraintSpec spec,
    std::vector<RankedCandidate> candidates) const
{
    if (!spec.isValid()) {
        return PositionConstraintResult{PositionConstraintError::InvalidInput, std::nullopt};
    }
    if (candidates.empty()) {
        return PositionConstraintResult{PositionConstraintError::EmptyCandidates, std::nullopt};
    }

    std::unordered_set<uint32_t> seenInstruments;
    seenInstruments.reserve(candidates.size());
    for (const RankedCandidate& candidate : candidates) {
        if (!candidate.isValid()) {
            return PositionConstraintResult{PositionConstraintError::InvalidCandidate, std::nullopt};
        }
        if (!seenInstruments.insert(candidate.instrument.value).second) {
            return PositionConstraintResult{PositionConstraintError::InvalidCandidate, std::nullopt};
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const RankedCandidate& left, const RankedCandidate& right) {
                  if (left.score.value != right.score.value) {
                      return left.score.value > right.score.value;
                  }
                  return left.instrument.value < right.instrument.value;
              });

    const int32_t selectableCount =
        std::min(spec.maxPositions.value, static_cast<int32_t>(candidates.size()));

    ConstrainedPortfolio output;
    int32_t remainingBudget = spec.totalBudget.value;

    for (int32_t index = 0; index < selectableCount; ++index) {
        const int32_t slotsLeft = selectableCount - index;
        if (slotsLeft <= 0) {
            break;
        }

        const int32_t equalShare = remainingBudget / slotsLeft;
        const int32_t allocated = std::min(equalShare, spec.maxSinglePosition.value);

        output.byInstrument[candidates[static_cast<std::size_t>(index)].instrument.value] =
            WeightBps{allocated};
        remainingBudget -= allocated;
    }

    output.unallocatedBudget = WeightBps{remainingBudget};
    return PositionConstraintResult{PositionConstraintError::None, std::move(output)};
}

} // namespace astock::domain::trading::position_constraints



