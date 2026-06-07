#pragma once

#include "FactorSignalTypes.h"
#include "ISignalEngine.h"

#include <functional>
#include <memory>
#include <vector>

namespace factor::compute {

/// @brief 数据流连接状态
enum class StreamStatus : uint8_t {
    Disconnected = 0,
    Connecting = 1,
    Connected = 2,
    Reconnecting = 3,
};

/// @brief 实时行情数据流订阅者
class IMarketDataSubscriber {
public:
    virtual ~IMarketDataSubscriber() = default;

    /// @brief 收到增量数据
    virtual void onData(const DeltaMarketData& delta) = 0;

    /// @brief 连接状态变更
    virtual void onStatusChange(StreamStatus status) = 0;
};

/// @brief 实时行情数据流
///
/// 支持订阅/取消订阅指定标的的实时行情。
/// 数据到达后通过 IMarketDataSubscriber 回调推送。
class IMarketDataStream {
public:
    virtual ~IMarketDataStream() = default;

    /// @brief 订阅实时行情
    virtual void subscribe(const std::vector<InstrumentId>& instruments) = 0;

    /// @brief 取消订阅
    virtual void unsubscribe(const std::vector<InstrumentId>& instruments) = 0;

    /// @brief 注册订阅者
    virtual void addSubscriber(std::shared_ptr<IMarketDataSubscriber> subscriber) = 0;

    /// @brief 启动数据流连接
    virtual void start() = 0;

    /// @brief 停止数据流
    virtual void stop() = 0;
};

} // namespace factor::compute