// ISignalListener.h — 信号监听器接口 + 具体实现
// 纯 C++, 零 Qt 依赖
#pragma once

#include "ISignalFormatter.h"
#include "ISignalPusher.h"

#include <foundation/log/logging.hpp>

#include <memory>
#include <vector>

namespace domain::sigout {

// 前向声明 (避免头文件循环依赖)
class ISignalPersistencePort;

// ═══════════════════════════════════════════════════════════════════════════
// 信号监听器接口 (平行于 IOrderListener)
// ═══════════════════════════════════════════════════════════════════════════

/// @brief 信号监听器接口
/// 信号模式下引擎调用 onSignals() 替代 IOrderListener::onOrders()
class ISignalListener {
public:
    virtual ~ISignalListener() = default;

    /// @brief 接收策略产生的信号列表
    /// @param signalList 本次 step() 产生的所有信号
    virtual void onSignals(const std::vector<SignalOutput>& signalList) = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// SignalListener — ISignalListener 的具体实现, 组合 Formatter + Pusher + Persistence
// ═══════════════════════════════════════════════════════════════════════════

/// @brief 信号监听器实现类
/// Bridge 层负责构造并注入:
///   new SignalListener(unique_ptr<Formatter>, unique_ptr<Pusher>, shared_ptr<PersistencePort>)
/// 引擎层只持有 ISignalListener*, 不关心内部装配
class SignalListener final : public ISignalListener {
public:
    /// @brief 构造 (Bridge 层调用)
    /// @param formatter 格式化器 (如 ThsSignalFormatter)
    /// @param pusher 推送器 (如 FileSignalPusher)
    /// @param persistence 持久化端口 (可选, nullptr=不持久化)
    SignalListener(std::unique_ptr<ISignalFormatter> formatter,
                   std::unique_ptr<ISignalPusher> pusher,
                   std::shared_ptr<ISignalPersistencePort> persistence = nullptr);

    /// @brief 析构时等待所有异步持久化任务完成, 防止 use-after-free
    /// 定义在 .cpp 中 (避免头文件依赖 ISignalPersistencePort 完整定义)
    ~SignalListener() override;

    /// @brief 格式化 + 推送 + 异步持久化所有信号
    void onSignals(const std::vector<SignalOutput>& signalList) override;

private:
    std::unique_ptr<ISignalFormatter> m_formatter;
    std::unique_ptr<ISignalPusher> m_pusher;
    std::shared_ptr<ISignalPersistencePort> m_persistence;
};

} // namespace domain::sigout
