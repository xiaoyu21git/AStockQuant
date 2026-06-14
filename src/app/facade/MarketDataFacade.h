#pragma once

#include "factor_compute/IMarketDataView.h"
#include "factor_compute/IMarketDataStream.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace app::facade {

/// @brief 行情数据入口门面
///
/// 行情数据来源：网络推送（实时）或缓存（历史），非数据库直连。
/// Facade 仅提供接口适配，实际数据由具体 Adapter 注入。
class MarketDataFacade {
public:
    MarketDataFacade() = default;

    /// 注册数据流（网络推送或缓存流）
    void setDataStream(std::shared_ptr<factor::compute::IMarketDataStream> stream);

    /// 获取当前流
    auto stream() const { return stream_; }

    /// 启动/停止实时
    void startRealtime(const std::vector<std::string>& symbols);
    void stopRealtime();

    using DataCallback = std::function<void()>;
    void setDataCallback(DataCallback cb);

private:
    std::shared_ptr<factor::compute::IMarketDataStream> stream_;
    std::mutex mutex_;
};

} // namespace app::facade