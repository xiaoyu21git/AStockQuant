// public/include/public.h （可选，为用户提供便利）
#pragma once

/**
 * @file public.h
 * @brief Foundation库便捷包含文件
 * 
 * 用户只需要包含此文件即可使用所有功能
 */

#include "foundation.h"

// 定义一些便捷宏
#define F_JSON foundation::Foundation::instance().json()
#define F_YAML foundation::Foundation::instance().yaml()
#define F_NET  foundation::Foundation::instance().net()