#pragma once

#include "IFactorComputeOperatorExtension.h"

#include <cstdint>
#include <memory>

namespace factor::compute {

class ConstOpConfig final {
public:
    ConstOpConfig(uint32_t computeToken, double outputValue) noexcept;

    [[nodiscard]] uint32_t computeToken() const noexcept;
    [[nodiscard]] double outputValue() const noexcept;
    [[nodiscard]] bool isValid() const noexcept;

private:
    uint32_t computeToken_{0U};
    double outputValue_{0.0};
};

class ConstOpRegistrarFactory final {
public:
    [[nodiscard]] std::unique_ptr<IComputeOpRegistrar>
    create(const ConstOpConfig& config) const;
};

} // namespace factor::compute
