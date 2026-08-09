// ISignalPersistencePort.h — 信号持久化端口接口
// 纯 C++, 零 Qt 依赖
// 领域层定义接口, 桥接层实现 (PG 写入)
#pragma once

#include "SignalTypes.h"

#include <string>
#include <vector>

namespace domain::sigout {

/// @brief 信号持久化端口 — 异步批量写入信号记录
/// 桥接层实现 SignalPersistencePort (PG), 领域层不关心存储后端
class ISignalPersistencePort {
public:
    virtual ~ISignalPersistencePort() = default;

    /// @brief 异步批量保存信号记录
    /// 不阻塞调用线程, 内部使用 std::async 或线程池提交写入任务
    /// @param signalList 信号输出列表 (包含 traceId/symbol/intent 等)
    /// @param pushError 推送错误信息 (空字符串=推送成功)
    virtual void saveAsync(const std::vector<SignalOutput>& signalList,
                           const std::string& pushError) = 0;

    /// @brief 等待所有异步写入任务完成
    /// 调用时机: (1) SignalListener 析构 (2) 模式切换前 (3) 应用退出前
    virtual void flush() = 0;
};

} // namespace domain::sigout
