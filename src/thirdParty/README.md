# 第三方API统一接口库

## 概述

第三方API统一接口库是一个用于封装不同量化平台API的C++库，提供统一的接口来调用掘金、聚宽等平台的接口。该库设计为可扩展的架构，支持多种第三方量化平台的无缝切换。

## 功能特性

- **统一接口设计**：为不同平台提供一致的API接口
- **多平台支持**：支持掘金、聚宽、米筐、TuShare等平台
- **异步回调机制**：支持市场数据、订单状态、持仓变化等异步回调
- **线程安全**：内置线程安全机制，支持多线程环境
- **代码格式转换**：自动处理不同平台的代码格式转换
- **可扩展架构**：易于添加新的第三方平台支持

## 目录结构

```
src/thirdParty/
├── include/                    # 头文件目录
│   ├── ThirdPartyApi.h        # 统一接口定义
│   └── JujinApi.h            # 掘金API实现
├── src/                       # 源文件目录
│   ├── ThirdPartyApi.cpp     # 统一接口实现
│   └── JujinApi.cpp          # 掘金API实现
├── examples/                  # 示例代码
│   └── ThirdPartyApiDemo.cpp # 使用示例
└── README.md                 # 本文档
```

## 快速开始

### 1. 包含头文件

```cpp
#include "thirdParty/include/ThirdPartyApi.h"
using namespace thirdparty;
```

### 2. 创建API实例

```cpp
// 使用工厂方法创建掘金API
auto api = ThirdPartyApiFactory::create_jujin_api();

// 或者使用通用工厂方法
auto api = ThirdPartyApiFactory::create_api(PlatformType::JUJIN);
```

### 3. 配置和初始化

```cpp
ConfigParams config;
config.platform = PlatformType::JUJIN;
config.token = "your_jujin_token";
config.account_id = "your_account_id";
config.server_url = "https://www.myquant.cn";

// 额外参数
config.extra_params["strategy_id"] = "my_strategy";
config.extra_params["mode"] = "2";  // 回测模式

// 初始化
if (!api->initialize(config)) {
    std::cerr << "初始化失败" << std::endl;
    return;
}
```

### 4. 设置回调函数

```cpp
// 市场数据回调
api->subscribe_market_data({"000001.SZ"}, MarketDataType::TICK, 
    [](const MarketData& data) {
        std::cout << "收到行情: " << data.symbol << " " << data.close << std::endl;
    });

// 订单回调
api->set_order_callback([](const OrderInfo& order) {
    std::cout << "订单状态更新: " << order.order_id << std::endl;
});

// 错误回调
api->set_error_callback([](int code, const std::string& msg) {
    std::cerr << "错误: " << code << " - " << msg << std::endl;
});
```

### 5. 连接平台

```cpp
if (!api->connect()) {
    std::cerr << "连接失败" << std::endl;
    return;
}
```

### 6. 使用API功能

```cpp
// 获取账户信息
AccountInfo account = api->get_account_info();

// 获取持仓
auto positions = api->get_positions();

// 获取实时行情
auto quotes = api->get_realtime_quotes({"000001.SZ", "600000.SH"});

// 下单
std::string order_id = api->place_order(
    "000001.SZ",
    OrderSide::BUY,
    OrderType::LIMIT,
    10.5,  // 价格
    100    // 数量
);

// 撤单
api->cancel_order(order_id);

// 获取历史数据
auto history = api->get_market_data(
    "000001.SZ",
    MarketDataType::BAR_1D,
    start_time,
    end_time
);
```

### 7. 断开连接

```cpp
api->disconnect();
```

## 平台支持

### 已实现平台

1. **掘金量化 (Jujin)**
   - 支持实时行情订阅
   - 支持历史数据获取
   - 支持交易下单和撤单
   - 支持账户和持仓查询

### 计划支持平台

1. **聚宽 (Juquan)**
2. **米筐 (RiceQuant)**
3. **TuShare**
4. **自定义API**

## 代码格式转换

库内置了代码格式转换工具，支持不同平台间的代码格式自动转换：

```cpp
// 内部格式 -> 平台格式
std::string platform_symbol = convert_symbol_format("000001.SZ", PlatformType::JUJIN);
// 结果: "SZSE.000001"

// 平台格式 -> 内部格式
std::string internal_symbol = convert_to_internal_format("SZSE.000001", PlatformType::JUJIN);
// 结果: "000001.SZ"
```

## 数据类型

### 市场数据类型

```cpp
enum class MarketDataType {
    TICK,           // 实时Tick数据
    BAR_1M,         // 1分钟K线
    BAR_5M,         // 5分钟K线
    BAR_15M,        // 15分钟K线
    BAR_30M,        // 30分钟K线
    BAR_60M,        // 60分钟K线
    BAR_1D,         // 日K线
    BAR_1W,         // 周K线
    BAR_1MTH        // 月K线
};
```

### 订单方向

```cpp
enum class OrderSide {
    BUY,            // 买入
    SELL            // 卖出
};
```

### 订单类型

```cpp
enum class OrderType {
    LIMIT,          // 限价单
    MARKET,         // 市价单
    STOP,           // 止损单
    STOP_LIMIT      // 止损限价单
};
```

## 编译配置

### CMake配置

在项目的CMakeLists.txt中添加：

```cmake
# 添加第三方API库
add_subdirectory(src/thirdParty)

# 包含头文件路径
target_include_directories(your_target
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/thirdParty/include
)

# 链接库
target_link_libraries(your_target
    PRIVATE
        thirdParty
)
```

### 依赖项

- C++11或更高版本
- 标准库：`<thread>`, `<mutex>`, `<chrono>`, `<functional>`
- 第三方平台SDK（如掘金SDK）

## 示例程序

完整的示例程序位于 `examples/ThirdPartyApiDemo.cpp`，演示了：

1. 工具函数使用
2. 工厂方法使用
3. 掘金API完整使用流程
4. 回调函数设置
5. 交易操作演示

运行示例：

```bash
cd build
./thirdParty_example
```

## 扩展新平台

要添加新的第三方平台支持：

1. 创建新的API实现类，继承 `IThirdPartyApi`
2. 实现所有纯虚函数
3. 在 `ThirdPartyApiFactory` 中添加创建方法
4. 在 `convert_symbol_format` 和 `convert_to_internal_format` 中添加代码格式转换逻辑

示例：

```cpp
class NewPlatformApi : public IThirdPartyApi {
    // 实现所有接口...
};

// 在工厂类中添加
std::unique_ptr<IThirdPartyApi> ThirdPartyApiFactory::create_newplatform_api() {
    return std::make_unique<NewPlatformApi>();
}
```

## 注意事项

1. **线程安全**：API设计为线程安全，但回调函数需要用户自行保证线程安全
2. **资源管理**：确保正确调用 `disconnect()` 释放资源
3. **错误处理**：设置错误回调函数处理异常情况
4. **平台差异**：不同平台的功能和限制可能不同，请参考具体平台文档

## 许可证

本项目采用MIT许可证。

## 贡献

欢迎提交Issue和Pull Request来改进这个库。

## 联系方式

如有问题或建议，请通过项目Issue页面联系。