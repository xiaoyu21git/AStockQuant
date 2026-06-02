#pragma once

#include <cstdint>

namespace factor::compute {

enum class ComputeToken : uint32_t {
    Close = 1U,
    Lag1 = 2U,
    RollingMean2 = 3U,
    RollingSum2 = 4U,
    Rank = 5U,
    GroupByMean = 6U
};

constexpr int32_t kLagWindow = 1;
constexpr int32_t kRollingWindow = 2;
constexpr uint32_t kDefaultGroupKey = 1U;

[[nodiscard]] constexpr uint32_t toToken(ComputeToken token) noexcept
{
    return static_cast<uint32_t>(token);
}

} // namespace factor::compute
