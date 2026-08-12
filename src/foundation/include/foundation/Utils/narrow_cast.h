// foundation/Utils/narrow_cast.h
/**
 * @file narrow_cast.h
 * @brief 安全数值窄化转换 — 溢出则 throw, 替代 C 风格转型
 */

#pragma once

#include <limits>
#include <stdexcept>
#include <type_traits>

namespace foundation {
namespace utils {

/// @brief 数值窄化转换 — 目标类型无法表示源值时 throw std::overflow_error
///
/// 使用场景:
///   - int64_t → int32_t (可能溢出)
///   - double   → int64_t (NaN/Inf/范围)
///   - size_t   → int     (无符号 → 有符号)
///
/// 不需要 narrow_cast 的场景 (无信息丢失):
///   - int32_t → int64_t  (拓宽, 用 static_cast)
///   - float   → double   (拓宽, 用 static_cast)
///
/// 用法: auto v = narrow_cast<int>(someInt64);
template<typename To, typename From>
To narrow_cast(From value) {
    static_assert(std::is_arithmetic_v<From> && std::is_arithmetic_v<To>,
                  "narrow_cast 仅支持算数类型");

    if constexpr (std::is_floating_point_v<From> && std::is_integral_v<To>) {
        // float/double → 整数: 检查 NaN/Inf + 范围
        if (value != value) {
            throw std::overflow_error("narrow_cast: NaN → 整数");
        }
        if (value > static_cast<From>(std::numeric_limits<To>::max()) ||
            value < static_cast<From>(std::numeric_limits<To>::min())) {
            throw std::overflow_error("narrow_cast: 浮点溢出");
        }
        return static_cast<To>(value);
    } else if constexpr (std::is_integral_v<From> && std::is_floating_point_v<To>) {
        // 整数 → float/double: 可能精度丢失 (大整数), 但不会溢出
        // 不检查 — 允许, 但调用方需注意精度
        return static_cast<To>(value);
    } else {
        // 整数 → 整数: 检查范围
        if constexpr (sizeof(From) > sizeof(To) ||
                      (std::is_signed_v<From> && std::is_unsigned_v<To>)) {
            if (value > static_cast<From>(std::numeric_limits<To>::max()) ||
                value < static_cast<From>(std::numeric_limits<To>::min())) {
                throw std::overflow_error("narrow_cast: 整数溢出");
            }
        }
        // 同大小、无符号→有符号: 范围检查
        if constexpr (std::is_unsigned_v<From> && std::is_signed_v<To>) {
            if (value > static_cast<From>(std::numeric_limits<To>::max())) {
                throw std::overflow_error("narrow_cast: 无符号→有符号溢出");
            }
        }
        return static_cast<To>(value);
    }
}

} // namespace utils
} // namespace foundation
