#pragma once

// 示例：业务层/事件总线直接集成gmsdk适配层
// 只需包含适配头文件，无需自造类型
#include "thirdparty/gmsdk/GmApiWrapper.h"
#include "thirdparty/gmsdk/GmUnifiedAdapter.h"
#include "Event/EventBus.hpp"

// 示例：自定义策略，直接继承gmsdk_adapter::GmUnifiedStrategy
class MyGmStrategy : public gmsdk_adapter::GmUnifiedStrategy {
public:
    using GmUnifiedStrategy::GmUnifiedStrategy;
    void on_tick(const Tick& tick) override {
        // 业务逻辑...
    }
    void on_bar(const Bar& bar) override {
        // 业务逻辑...
    }
};

// 示例：事件总线桥接
inline void register_gm_eventbus_bridge(engine::EventBus& bus, gmsdk::Strategy* strategy) {
    // 这里可将gmsdk事件转发到EventBus
    // 例如：on_tick回调中发布EventFormat事件
    // bus.publish(...);
}

// 业务层可直接include本头文件，零自造类型，直接用gmsdk所有接口
