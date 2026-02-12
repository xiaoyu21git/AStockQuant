#pragma once

// 事件总线与业务层集成的gmsdk统一适配层
// 只做事件桥接和接口转发，不自造类型
// 业务层直接用gmsdk::Strategy等类型

#include "GmApiWrapper.h"

// 示例：事件桥接适配（可根据实际EventBus接口扩展）
// 这里只做最基础的桥接，所有gmsdk事件、回调、数据结构均可直接转发

// 业务层如需自定义策略，直接继承gmsdk::Strategy
// 如需事件总线桥接，可在此扩展事件分发逻辑

// 示例：统一策略适配基类
namespace gmsdk_adapter {
    using namespace gmsdk;
    
    // 统一策略适配基类，业务层可直接继承
    class GmUnifiedStrategy : public Strategy {
    public:
        using Strategy::Strategy;
        // 可在此扩展事件总线桥接、日志、监控等
    };
}
