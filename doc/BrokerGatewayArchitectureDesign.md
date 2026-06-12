# 券商网关抽象层架构设计文档 v2.1 (评审通过版)

> 版本: v2.1 | 日期: 2026-06-12 | 状态: 评审通过, 待实施
> 修订历史: v1.0 初稿 -> v2.0 第一轮评审修订 -> v2.1 第二轮评审补充

---

## 1. 背景与目标

### 1.1 现状

TradeExecutionService 直接持有 JujinApi* 实现下单，存在以下问题:

| 问题 | 严重度 |
|------|--------|
| 无抽象接口 | 高 |
| 编译时宏切换 | 高 |
| 3条下单路径分裂 | 中 |
| God Class (2921行) | 中 |

### 1.2 目标

1. 券商可替换: 换券商只换适配器
2. 运行时选择: 启动时通过配置选择券商
3. 性能: 虚函数调用开销 < 50ns
4. 稳定性: 断线重连、订单保障、异常隔离

---
## 2. 架构设计

### 2.1 接口层次

IBrokerGatewayEx : public IBrokerGateway  (继承关系)

基础接口 IBrokerGateway (所有券商必须实现):
- submitOrder(OrderRequest) -> void (异步,结果通过回调返回)
- cancelOrder(orderId) -> void (异步)
- queryOrder(orderId) -> void (异步)
- queryPositions() -> void (异步)
- queryAccount() -> void (异步)

回调注册 (统一异步,所有结果通过回调返回):
- setOrderCallback(OrderCallback)
- setTradeCallback(TradeCallback)
- setErrorCallback(ErrorCallback)

扩展接口 IBrokerGatewayEx (券商特有功能):
- submitAlgoOrder(AlgoReq) -> void (异步)
- submitBasket(BasketReq) -> void (异步)
- capability() -> BrokerCapability
- hasCapability(CapabilityId) -> bool (避免结构体膨胀)

### 2.2 适配器层

- JujinBrokerGateway (IBrokerGatewayEx) -> 掘金 SDK
- XtpBrokerGateway (IBrokerGatewayEx, 未来)
- CtpBrokerGateway (IBrokerGateway, 未来)
- SimulatedBrokerGateway (IBrokerGateway) -> 内存撮合

### 2.3 数据流

策略信号 -> TradeExeSvc(风控+编排) -> gateway.submitOrder(req)
  -> [异步] 适配器 -> SDK -> 回调 -> 无锁SPSC队列 -> 主线程钩子
  -> OrderCallback -> EventBus -> 策略引擎

所有回调统一为异步模式,避免部分同步/部分异步的调用语义不一致。

---

## 3. 效率设计

| 指标 | 目标值 |
|------|--------|
| 虚函数调用开销 | < 50ns |
| 订单提交延迟(不含网络IO) | < 5us |
| 内存分配 | 0次堆分配 (OrderRequest 全栈,SSO+惰性metadata) |
| 适配器额外开销 | < 0.7% (vs SDK直调) |

设计要点:
- OrderRequest 全栈分配 (~128字节)
- metadata 仅用于"配置级"透传,不承载高频订单级动态参数
- metadata key 约定前缀 (gm_, xtp_),通用层不校验/不持久化
- 典型 metadata 大小应在 128 字节以内,避免 map 堆分配过多

---

## 4. 稳定性设计

### 4.1 连接生命周期

状态机: Connected -> Reconnecting -> ErrorState -> (5分钟后) Reconnecting
重连策略: 指数退避 1s/2s/4s/8s/16s/30s, 最多3次后进入 ErrorState
ErrorState 恢复: 5分钟后自动尝试重连, 避免频繁重连导致柜台封锁
人工介入: ErrorState 通知 ErrorCallback, UI 可提供手动重连按钮

### 4.2 订单状态保障

前提: SDK 提供 queryOrders() 全量查询接口
若 SDK 不支持全量查询 -> 标注为风险, 依赖本地日志+人工对账

正常流程:
1. 每笔订单维护本地状态副本 (内存 map<orderId, OrderStatus>)
2. 重连后调用 SDK queryOrders() 获取当日全部订单
3. 对比本地缓存, 找出新增/变更/缺失订单
4. 推送差异到上层 (通过 OrderCallback)
5. 提交积压的新订单

持久化策略: 本地状态副本仅在内存中, 进程崩溃后重启通过 SDK 全量查询恢复。
如需跨进程重启保障, 可扩展为写入 SQLite (阶段扩展)

### 4.3 异常隔离与熔断

SDK 调用统一 try/catch, 转为错误返回
熔断触发条件 (区分错误类型):
- 网络错误/SDK异常/超时: 计入熔断计数
- 参数错误/业务拒绝: 不计入熔断, 直接返回错误
连续系统级失败 >= 5 次 -> 断开连接, 进入 ErrorState, 通知 ErrorCallback

### 4.4 线程模型

主线程 (Qt EventLoop): 调用 IBrokerGateway 所有方法 (submit/cancel/query)
SDK 回调线程: 由券商库内部创建
转发机制: 有界无锁 SPSC 队列 (如 moodycamel::ReaderWriterQueue)
队列满策略: 背压模式 (阻塞 SDK 回调线程, 直到主线程消费),
  配合超时 (100ms), 超时丢弃并告警
非网关线程 (如风控/手动干预) 需通过向主线程投递任务调用, 统一调度

### 4.5 降级策略

策略需声明"刚性/柔性"需求:
- 刚性: 要求 TWAP, 不支持时直接报错 -> submitAlgoOrFail()
- 柔性: 建议 TWAP, 不支持时降级为限价单 -> submitAlgoOrder() 自动降级
降级决策在 TradeExecutionService 中根据策略配置决定

---

## 5. 券商扩展能力

### 5.1 metadata 透传规范

适用范围: 仅用于"配置级"透传 (如算法名、执行风格), 不承载高频订单级动态参数
命名规范: 券商前缀 (gm_, xtp_, ctp_) 防冲突
校验职责: 通用层不校验/不持久化, 适配器层校验必要字段并返回明确错误码

### 5.2 能力查询

BrokerCapability 提供常用能力位掩码, 同时提供 hasCapability(id) 避免结构体膨胀:
hasCapability("algo_twap"), hasCapability("short_selling") 等

---

## 6. 实施计划

### 6.1 工时估算 (已根据评审意见修订)

| 阶段 | 任务 | 工时 | 说明 |
|------|------|------|------|
| 1 | 值对象层 TradingTypes.h | 0.5h | 纯头文件 |
| 2 | 抽象接口 IBrokerGateway.h + Factory | 1h | 接口+能力查询 |
| 3 | 掘金适配器 JujinBrokerGateway | 1天 | 含SDK集成验证 |
| 4 | 模拟适配器 SimulatedBrokerGateway | 3h | 内存撮合+一致性 |
| 5 | TradeExeSvc 改造 | 1天 | 重构+单元测试 |
| 6 | AppBootstrap 组装 | 1h | 配置驱动初始化 |
| 7 | 集成测试 | 1天 | 端到端验证 |
| **合计** | | **3-5天** | |

### 6.2 实施顺序建议

1. 阶段 3 开始前先构建精简原型: 实现 submitOrder/cancelOrder 基础流程并在掘金模拟环境验证线程模型和 SDK 集成
2. 验证通过后再补齐完整的适配器和其他接口
3. 避免全量实现后才发现线程/SDK 问题导致返工

### 6.3 文件变更

新增:
- src/domain/trading/TradingTypes.h
- src/domain/trading/IBrokerGateway.h
- src/domain/trading/BrokerGatewayFactory.h
- src/app/adapters/JujinBrokerGateway.h/cpp
- src/app/adapters/SimulatedBrokerGateway.h/cpp

修改:
- src/domain/CMakeLists.txt
- TradeExecutionService.h/cpp
- AppBootstrap.cpp

移动:
- JujinApi.h/cpp: bridge -> adapters

---

## 7. 测试策略

### 7.1 单元测试
- SimulatedBrokerGateway 必须与真实网关行为一致 (异步回调、部分成交、延迟模拟)
- 值对象序列化: OrderRequest/OrderResult 往返转换
- 工厂创建: register -> create -> submitOrder
- 降级逻辑: 刚性/柔性策略路径覆盖

### 7.2 集成测试
- 掘金精简原型验证 (阶段3前期): 线程模型 + SDK 集成可行性
- 端到端: 掘金模拟环境下单 -> 状态回调 -> 订单恢复
- 断线重连: 主动断开网络, 验证重连+ErrorState+自动恢复
- 背压测试: 高频回调触发队列满, 验证阻塞+超时丢弃+告警

### 7.3 性能测试
- 虚函数开销: 1M次 submitOrder, <50ns/call
- 订单吞吐: 1000笔/秒, 持续10分钟, 零丢单
- 内存泄漏: valgrind/asan 24小时, 零泄漏

---

## 8. 实施注意事项 (第二轮评审补充)

### 8.1 队列背压超时与 SDK 兼容性
- 默认背压超时 100ms, 但需检查 SDK 文档: 若回调须快速返回, 保守设 10-50ms
- 不确定上限时先用 30ms, 线上监控告警后再调整
### 8.2 持久化渐进路径
当前仅内存状态副本, 进程崩溃后通过 SDK 全量查询恢复。
风险: 崩溃期间成交回报不补发, 需人工核对当日流水。
未来可扩展 SQLite, 订单状态变更即写日志。

### 8.3 submitAlgoOrFail 归属
属于 IBrokerGatewayEx 方法, 语义为不支持则返回错误。
TradeExecutionService 根据策略配置选择: 刚性 -> submitAlgoOrFail, 柔性 -> submitAlgoOrder 自动降级。

### 8.4 hasCapability(id) 管理
公共头文件定义 constexpr CapabilityId (CAP_ALGO_TWAP, CAP_SHORT_SELLING 等)。
实现中用 string_view 比较, BrokerCapability 位掩码作为快照, hasCapability 作为扩展。

### 8.5 模拟网关行为一致性
SimulatedBrokerGateway 必须模拟完整状态机, 基于 Bar OHLC/Volume 撮合, 注入可配置延迟(1-50ms)。
不可简化为一次性成交, 否则实盘/回测一致性被破坏。建议单独编写《模拟网关设计说明书》。

---

## 9. 最终评审结论

架构设计评审通过, 可进入实施阶段。建议实施顺序: 阶段3前先构建精简原型验证线程模型和SDK集成可行性。
