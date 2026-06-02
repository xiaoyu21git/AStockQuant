#pragma once

#include "FactorSignalTypes.h"

#include <cstdint>

namespace factor::compute {

/// @brief 设计文档 Section 7 (P1-T4)：ErrorCode 到展示文案的单向映射目录
///
/// 约束：
/// - 仅供桥接层使用，核心层不依赖展示文本。
/// - 错误处理仅使用 FactorError 枚举，禁止在核心层拼接字符串语义。
/// - 所有文案以 const char* 编译期常量提供，避免动态分配。
class FactorErrorCatalog {
public:
    FactorErrorCatalog() = delete;

    /// @brief 获取错误码对应的展示文案
    [[nodiscard]] static const char* displayText(FactorError error) noexcept
    {
        switch (error) {
        case FactorError::None:
            return "No error";
        case FactorError::InvalidFormula:
            return "Invalid factor formula expression";
        case FactorError::CircularDependency:
            return "Circular dependency detected in factor DAG";
        case FactorError::InvalidUniverse:
            return "Invalid or empty instrument universe";
        case FactorError::InsufficientData:
            return "Insufficient market data for the requested window";
        case FactorError::Timeout:
            return "Computation timed out — partial results returned";
        case FactorError::MemoryExceeded:
            return "Estimated memory requirement exceeded budget limit";
        case FactorError::InternalError:
            return "Internal engine error — check diagnostics";
        }
        return "Unknown error";
    }

    /// @brief 获取错误码的数字标识（用于日志/监控系统）
    [[nodiscard]] static uint8_t numericCode(FactorError error) noexcept
    {
        return static_cast<uint8_t>(error);
    }
};

} // namespace factor::compute