#pragma once

// 统一转发gmsdk官方全部头文件、类型、接口、事件，禁止自造类型
// 只做include和命名空间适配，便于业务层和事件总线直接调用

extern "C" {
#include <strategy.h>
#include <gmapi.h>
#include <gmdef.h>
#include <error.h>
}

// 可选：如需C++封装，可用namespace gmsdk_wrapper，但不做任何类型改造
// 业务层直接用gmsdk::Strategy等类型
