// ISignalFormatter.h — 信号格式化器接口
// 纯 C++, 零 Qt 依赖
#pragma once

#include "SignalTypes.h"

#include <memory>
#include <string>

namespace domain::sigout {

/// @brief 信号格式化器接口 — 将 SignalOutput 转为平台特定格式
/// 实现: ThsSignalFormatter (同花顺制表符) / TdxSignalFormatter (通达信 CSV)
class ISignalFormatter {
public:
    virtual ~ISignalFormatter() = default;

    /// @brief 将单条信号格式化为文本行 (含换行符)
    [[nodiscard]] virtual std::string format(const SignalOutput& signal) const = 0;

    /// @brief 格式标识名, 用于: (1) QML 下拉菜单展示 (2) StoredSignalRecord.formatterName 持久化
    [[nodiscard]] virtual std::string formatName() const = 0;
};

// ── 工厂函数 (Bridge 层调用) ──

/// @brief 创建同花顺格式化器
std::unique_ptr<ISignalFormatter> createThsFormatter();

/// @brief 创建通达信格式化器
std::unique_ptr<ISignalFormatter> createTdxFormatter();

} // namespace domain::sigout
