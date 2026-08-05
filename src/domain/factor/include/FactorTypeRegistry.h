#pragma once
// FactorTypeRegistry — 因子类型元数据查询, 使用 factor_enums.h 的权威枚举

#include "factor_enums.h"

#include <cstdint>
#include <string>

namespace domain::factor {

/// @brief 因子类型元数据查询 (委托到 factor::FactorType)
class FactorTypeRegistry {
public:
    static std::string displayName(factor::FactorType type);
    static std::string typeId(factor::FactorType type);
    static factor::FactorType fromIndex(int index);
};

} // namespace domain::factor
