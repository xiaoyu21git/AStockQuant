# Engine 模块设计说明

## 1. 模块定位
Engine 是系统的运行内核（Runtime Kernel）。
**职责**：组织时间推进、数据分发、事件调度与模块协作，完成一次可重复执行的运行过程。

**不做**：策略决策、账户结算、指标计算、数据存储、脚本绑定。

---
## 2. 输入 / 输出
**输入**：
- MarketData（domain/data 抽象）
- Strategy 接口（domain/strategies）
- Clock（engine）
- EngineConfig（由 app 构建并注入）

**输出**：
- Event（Market / Signal / Trade）
- BacktestResult（仅原始事实）

---
## 3. 依赖规则（必须遵守）
- 允许：engine → domain（接口/数据结构）、foundation
- 禁止：engine → app / analysis / storage / Python
- 单向：engine → domain，domain 不反向依赖 engine

---
## 4. 目录结构（定稿）
```
engine/
├─ include/engine/
│  ├─ Engine.h
│  ├─ BacktestEngine.h
│  ├─ EngineConfig.h
│  ├─ EngineContext.h
│  ├─ Event.h
│  ├─ EventBus.h
│  └─ BacktestResult.h
├─ src/
│  ├─ BacktestEngine.cpp
│  ├─ EventBus.cpp
│  └─ EngineContext.cpp
└─ CMakeLists.txt
```

---
## 5. 核心接口
### Engine
- init / run / stop
- 不持有业务对象

### EngineContext
- 运行期共享对象（provider / strategies / event_bus / current_time）

### Event / EventBus
- 描述“发生了什么”
- 解耦模块通信

### BacktestEngine
- 执行循环：Clock → Data → Event → Strategy
- 不处理业务含义

---
## 6. 配置策略
- Engine **只使用**配置，不解析文件
- app 负责读取配置并构建 EngineConfig

---
## 7. 扩展点
- 运行模式：Backtest / Realtime（继承 Engine）
- 时间机制：IClock
- 事件类型：新增 Event
- 语言绑定：Adapter（不进 engine）

---
## 8. 稳定 API（冻结）
- Engine / EngineConfig / EngineContext / Event / EventBus

---
## 9. 禁区清单
- engine/account（禁止）
- engine 内做结算/分析（禁止）
- engine 依赖 Python（禁止）

