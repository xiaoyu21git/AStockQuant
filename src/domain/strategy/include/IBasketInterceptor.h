#pragma once
// IBasketInterceptor — 半自动篮子拦截器接口 (纯 C++, 零 Qt)
//
// Bridge 层实现此接口，在订单到达 TradeExecutionEngine 之前插入用户确认环节。
// 引擎层只持有指针，不关心实现细节（QML 弹窗 / 命令行 / 远程确认等）。
//
// 线程契约:
//   onBasketReady() 在引擎线程调用，实现方负责跨线程通知 Qt 主线程。
//   返回 true = 成功受理; false = 繁忙 (已有篮子待确认), 引擎自行决定丢弃或重试。

#include "StrategyServiceTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace domain::strategy {

class IBasketInterceptor {
public:
    virtual ~IBasketInterceptor() = default;

    /// @brief 引擎产出一篮子订单，通知实现方展示确认界面
    /// @param basketId        篮子唯一ID (用于后续 confirmBasket / rejectBasket)
    /// @param orders          待确认的订单列表 (含 symbol/side/qty/weight/score/traceId)
    /// @param strategyId      策略UUID (用于 confirmBasket/rejectBasket 时查找引擎)
    /// @param strategyName    策略名称 (显示用)
    /// @param contextDescription 上下文描述 (如 "14:50 EOD评估" / "盘中信号")
    /// @return true = 成功受理, false = 已有篮子待确认，本次篮子被拒绝
    virtual bool onBasketReady(
        std::uint64_t basketId,
        const std::vector<OrderRequest>& orders,
        const std::string& strategyId,
        const std::string& strategyName,
        const std::string& contextDescription) = 0;
};

} // namespace domain::strategy
