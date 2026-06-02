#pragma once

#include "CommonExecutionTypes.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace astock::domain::trading::position_constraints {

using InstrumentId = execution::ExecutionInstrumentId;

struct RankScore final {
    static constexpr int32_t kInvalidValue = 0;

    int32_t value{kInvalidValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value > kInvalidValue;
    }
};

struct WeightBps final {
    static constexpr int32_t kZero = 0;
    static constexpr int32_t kFull = 10000;

    int32_t value{kZero};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value >= kZero && value <= kFull;
    }
};

struct PositionCount final {
    static constexpr int32_t kInvalidValue = 0;

    int32_t value{kInvalidValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value > kInvalidValue;
    }
};

struct RankedCandidate final {
    InstrumentId instrument{};
    RankScore score{};

    [[nodiscard]] bool isValid() const noexcept
    {
        return instrument.isValid() && score.isValid();
    }
};

struct PositionConstraintSpec final {
    PositionCount maxPositions{};
    WeightBps totalBudget{WeightBps::kFull};
    WeightBps maxSinglePosition{WeightBps::kFull};

    [[nodiscard]] bool isValid() const noexcept
    {
        return maxPositions.isValid() && totalBudget.isValid() && maxSinglePosition.isValid();
    }
};

struct ConstrainedPortfolio final {
    std::unordered_map<uint32_t, WeightBps> byInstrument;
    WeightBps unallocatedBudget{};
};

enum class PositionConstraintError {
    None,
    InvalidInput,
    EmptyCandidates,
    InvalidCandidate
};

struct PositionConstraintResult final {
    PositionConstraintError error{PositionConstraintError::None};
    std::optional<ConstrainedPortfolio> value;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == PositionConstraintError::None && value.has_value();
    }
};

class IPositionConstraintAllocator {
public:
    virtual ~IPositionConstraintAllocator() = default;

    virtual PositionConstraintResult allocate(
        PositionConstraintSpec spec,
        std::vector<RankedCandidate> candidates) const = 0;
};

class PositionConstraintAllocator final : public IPositionConstraintAllocator {
public:
    PositionConstraintResult allocate(PositionConstraintSpec spec,
                                      std::vector<RankedCandidate> candidates) const override;
};

} // namespace astock::domain::trading::position_constraints

