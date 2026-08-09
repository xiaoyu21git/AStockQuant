// ISignalPusher.h — 信号推送器接口
// 纯 C++, 零 Qt 依赖
#pragma once

#include <memory>
#include <string>

namespace domain::sigout {

/// @brief 信号推送器接口 — 将格式化后的信号文本推送到目标
/// 实现: FileSignalPusher (文件写入) / SocketSignalPusher (Phase 2)
class ISignalPusher {
public:
    virtual ~ISignalPusher() = default;

    /// @brief 推送格式化后的批量信号文本
    /// @param formattedBatch 已格式化的所有信号行 (每条一行, 末尾有 \n)
    /// @return true=全部成功, false=部分或全部失败 (失败详情记日志含 traceId)
    virtual bool push(const std::string& formattedBatch) = 0;

    /// @brief 推送目标标识名, 用于: (1) QML 下拉菜单展示 (2) StoredSignalRecord.pusherName 持久化
    [[nodiscard]] virtual std::string targetName() const = 0;
};

// ── 工厂函数 (Bridge 层调用) ──

/// @brief 创建文件推送器
/// @param outputDir 信号文件输出目录 (如 "./signals/")
/// @param formatTag 格式标签 (如 "ths", "tdx")
std::unique_ptr<ISignalPusher> createFilePusher(const std::string& outputDir,
                                                const std::string& formatTag);

} // namespace domain::sigout
