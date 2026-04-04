# 量化交易系统交易功能设计

## 系统架构概述

基于现有代码分析，系统采用以下架构：

1. **前端**：QML + Qt Quick
2. **桥接层**：C++ QObject 桥接服务
3. **业务层**：C++ 事件总线 + 策略引擎
4. **数据层**：MySQL + 掘金SDK

## 现有交易相关组件

### 1. 交易执行服务 (TradeExecutionService)
- 位置：`src/ui/bridge/include/TradeExecutionService.h`
- 功能：处理订单提交、状态管理、事件发布
- 支持：手动测试订单、风险审批集成

### 2. 交易连接配置服务 (TradingConnectionConfigService)
- 位置：`src/ui/bridge/include/TradingConnectionConfigService.h`
- 功能：管理掘金连接配置、账户设置

### 3. 持仓账户服务 (PositionAccountService)
- 位置：`src/ui/bridge/include/PositionAccountService.h`
- 功能：管理账户资金、持仓信息

### 4. 市场数据服务 (MarketDataService)
- 位置：`src/ui/bridge/include/MarketDataService.h`
- 功能：提供实时行情数据

### 5. 掘金Broker (Python层)
- 位置：`astock_engine/broker/myquant_broker.py`
- 功能：掘金SDK的实际封装，提供下单、查询接口

## 缺失的组件分析

### 1. C++ 掘金API包装器 (JujinApi)
- 问题：TradeExecutionService中引用了`thirdparty::JujinApi`，但未找到实现
- 需要：创建C++层掘金SDK包装器，桥接到Python broker

### 2. 完整的交易管理界面
- 问题：现有TradingPanel.qml只提供基本的下单功能
- 需要：完整的交易管理页面，包括订单簿、持仓管理、资金管理

### 3. 策略交易集成
- 问题：策略引擎与交易执行之间的集成不完整
- 需要：完善策略信号到交易执行的完整流程

## 设计目标

### 1. 完整的交易功能栈
```
┌─────────────────────────────────────────┐
│            QML交易界面                   │
│  - 交易面板 (TradingPanel)              │
│  - 订单簿 (OrderBook)                   │
│  - 持仓管理 (PositionsPanel)            │
│  - 资金管理 (FundManagement)            │
│  - 交易记录 (TradeRecords)              │
└─────────────────┬───────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│         C++桥接服务层                    │
│  - TradeExecutionService                │
│  - PositionAccountService               │
│  - TradingConnectionConfigService       │
│  - MarketDataService                    │
└─────────────────┬───────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│         事件总线层                       │
│  - EventBus (engine::EventBus)          │
│  - 事件类型：                            │
│    • trading.order.submit.request       │
│    • trading.order.updated              │
│    • order.fill                         │
│    • risk.approval                      │
└─────────────────┬───────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│         Python Broker层                  │
│  - MyQuantBroker (掘金SDK封装)           │
│  - 提供：                                │
│    • place_order()                      │
│    • cancel_order()                     │
│    • query_positions()                  │
│    • query_account()                    │
└─────────────────┬───────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│          掘金SDK                         │
│  - gm.api                               │
│  - 实际交易接口                          │
└─────────────────────────────────────────┘
```

### 2. 交易流程
```
策略信号 → 风险审批 → 订单生成 → 订单提交 → 
订单状态跟踪 → 成交回报 → 持仓更新 → 资金更新
```

## 实现计划

### 阶段1：完善C++掘金API包装器
1. 创建 `thirdparty/JujinApi.h/cpp`
2. 实现与Python broker的通信
3. 集成到TradeExecutionService

### 阶段2：完善交易界面
1. 创建完整的交易管理QML页面
2. 实现订单簿组件
3. 实现持仓管理组件
4. 实现资金管理组件

### 阶段3：完善策略交易集成
1. 完善策略信号到交易的转换
2. 实现风险控制集成
3. 实现交易监控和日志

### 阶段4：测试和优化
1. 仿真环境测试
2. 实盘环境测试
3. 性能优化

## 技术细节

### 1. C++掘金API包装器设计
```cpp
class JujinApi {
public:
    // 初始化
    bool initialize(const ConfigParams& config);
    bool connect();
    bool is_connected() const;
    
    // 交易接口
    std::string place_order(const std::string& symbol, 
                           OrderSide side,
                           OrderType type,
                           double price,
                           double quantity);
    
    bool cancel_order(const std::string& order_id);
    
    // 查询接口
    std::vector<Position> query_positions();
    AccountInfo query_account();
    
    // 事件集成
    void set_event_bus(std::shared_ptr<engine::EventBus> bus);
    
private:
    // 与Python broker的通信机制
    // 可以通过进程间通信、网络接口或共享内存
};
```

### 2. 交易事件设计
```cpp
// 订单提交请求
struct OrderSubmitRequest {
    std::string order_id;
    std::string strategy_id;
    std::string symbol;
    OrderSide side;
    double price;
    int64_t quantity;
    OrderType type;
};

// 订单状态更新
struct OrderStatusUpdate {
    std::string order_id;
    std::string status;  // NEW, SUBMITTED, PARTIALLY_FILLED, FILLED, CANCELLED, REJECTED
    int64_t filled_quantity;
    double avg_price;
    std::string message;
    std::string timestamp;
};

// 成交回报
struct TradeFill {
    std::string fill_id;
    std::string order_id;
    std::string symbol;
    OrderSide side;
    double fill_price;
    int64_t fill_quantity;
    double filled_notional;
    std::string timestamp;
};
```

### 3. 风险控制集成
```cpp
class RiskControlService {
public:
    // 订单前检查
    RiskCheckResult pre_trade_check(const OrderSubmitRequest& order);
    
    // 持仓限制检查
    bool check_position_limit(const std::string& symbol, int64_t quantity);
    
    // 资金检查
    bool check_fund_sufficiency(double required_amount);
    
    // 风控规则配置
    void configure(const RiskConfig& config);
};
```

## 下一步行动

1. 首先实现缺失的C++掘金API包装器
2. 完善TradeExecutionService中的broker集成
3. 创建完整的交易管理QML页面
4. 测试整个交易流程

这个设计将提供完整的量化交易功能，支持策略自动交易和手动交易。