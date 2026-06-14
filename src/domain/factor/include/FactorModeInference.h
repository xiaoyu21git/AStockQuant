#pragma once

#include <cstdint>

namespace domain::factor {

enum class FactorMode : uint8_t {
    Single = 0,
    Dual = 1,
    Composite = 2
};

class FactorModeInference {
public:
    static FactorMode infer(int factorCount) {
        if (factorCount <= 1) return FactorMode::Single;
        if (factorCount == 2) return FactorMode::Dual;
        return FactorMode::Composite;
    }
};

} // namespace domain::factor