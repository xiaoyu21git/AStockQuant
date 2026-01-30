# 测试模块体系全景图

## 📊 整体架构

```
AStockQuantEngine 测试体系
│
├─ 🏗️ C++ 核心层测试 (tests/)
│   ├─ test_eventsystem.cpp          [C++事件系统核心]
│   ├─ test_eventbus_cpp_units.cpp   [C++ EventBus单元测试]
│   ├─ test_event_domain_integration.cpp [领域集成]
│   └─ test_event_stress.cpp         [压力测试 - 769K events/sec]
│
├─ 🐍 Python 业务层测试 (astock_engine/tests/)
│   ├─ test_eventbus.py              [EventBus基础]
│   ├─ test_eventbus_comprehensive.py [核心功能 - 25个测试]
│   ├─ test_eventbus_business_scenarios.py [业务场景 - 15个测试]
│   └─ test_eventbus_python_cpp_interop.py [Python/C++互操作]
│
└─ 🔗 集成测试 (根目录)
    ├─ test_python_cpp_link.py       [Python-C++链路测试]
    ├─ test_e2e_dataflow.py          [端到端数据流]
    └─ test_full_dataflow_链路.py    [完整业务数据流] ⭐ 最新
```

---

## 🎯 测试目的分层

### 第一层：C++ 核心验证
**目的**：确保底层事件系统的正确性和高性能

| 测试文件 | 测试对象 | 核心目标 | 性能指标 |
|---------|---------|---------|---------|
| `test_eventsystem.cpp` | EventSystem | 事件创建、分发、订阅机制 | 基准 |
| `test_eventbus_cpp_units.cpp` | EventBus C++接口 | API正确性、线程安全 | 单元级 |
| `test_event_domain_integration.cpp` | 领域模型集成 | 事件在业务模型中的使用 | 功能 |
| `test_event_stress.cpp` | 高压力场景 | 稳定性、性能极限 | **769K events/sec** |

**运行方式**：
```bash
cd build
cmake --build . --target test_eventsystem --config Debug
./bin/Debug/test_eventsystem.exe
```

---

### 第二层：Python EventBus 功能验证
**目的**：确保Pure Python EventBus实现正确（当C++不可用时的fallback）

#### 📁 astock_engine/tests/

| 测试文件 | 测试数量 | 测试重点 | 对应模块 |
|---------|---------|---------|---------|
| **test_eventbus.py** | 6个 | 基础导入、创建、类型 | `core/eventbus.py` |
| **test_eventbus_comprehensive.py** | 25个 | 发布订阅、优先级、错误处理、线程安全 | `core/eventbus_simple.py` |
| **test_eventbus_business_scenarios.py** | 15个 | 市场数据、订单流程、策略执行 | 业务逻辑集成 |
| **test_eventbus_python_cpp_interop.py** | 18个 | Python调用C++ EventBus | `_native.pyd` (如果存在) |

**测试覆盖矩阵**：

```
功能维度                 | comprehensive | business | interop
-----------------------|--------------|----------|--------
事件创建                | ✓            |          |
发布订阅                | ✓            | ✓        | ✓
优先级队列              | ✓            |          |
错误处理                | ✓            | ✓        |
线程安全                | ✓            |          |
业务场景                |              | ✓        |
C++互操作               |              |          | ✓
```

**运行方式**：
```bash
# 单独运行
pytest astock_engine/tests/test_eventbus_comprehensive.py -v

# 运行所有
pytest astock_engine/tests/ -v
```

---

### 第三层：数据流链路验证
**目的**：验证从数据获取到策略执行的完整业务流程

#### 📁 根目录集成测试

| 测试文件 | 测试目的 | 涉及模块 | 状态 |
|---------|---------|---------|-----|
| **test_python_cpp_link.py** | Python数据 → C++ EventBus连通性 | data providers + C++ EventBus | ⚠️ 阻塞(EventFormat编译) |
| **test_e2e_dataflow.py** | 端到端：数据 → 处理 → 输出 | data + eventbus + strategies | ✓ 通过(Pure Python) |
| **test_full_dataflow_链路.py** | 完整业务链路：数据 → EventBus → 策略 → 信号 | 全系统 | ✅ **已验证通过** |

**数据流测试层次**：

```
test_full_dataflow_链路.py  ← 最高层：完整业务验证
         ↓
  [数据源模拟] → [EventBus] → [策略引擎] → [信号输出]
         ↑             ↑            ↑           ↑
    市场数据    Pure Python   简单策略     交易信号
                18K evt/s     条件触发    BUY/SELL
```

**验证结果（2026-01-31）**：
```
✅ 已处理事件: 3个
✅ 生成信号: 1个 (RB2401 - BUY @ 4200.0)
✅ EventBus性能: 18,485 events/sec (Pure Python)
✅ 完整链路: 数据 → EventBus → 策略 → 信号 ✓
```

---

## 🔍 模块依赖关系

### 核心模块

```
astock_engine/
├── core/                      [事件总线核心]
│   ├── eventbus.py           → 统一入口
│   ├── eventbus_simple.py    → Pure Python实现 (18K/s)
│   └── eventbus_unified.py   → 智能选择器(C++/Python)
│
├── data/                      [数据提供层]
│   ├── base_provider.py      → 抽象接口
│   ├── stock_provider.py     → A股数据 (akshare)
│   ├── futures_provider.py   → 期货数据
│   └── news_provider.py      → 新闻情绪
│
├── strategies/                [策略层]
│   ├── base_strategy.py      → 策略基类
│   ├── multi_factor_strategy.py
│   ├── commodity_strategy.py
│   ├── futures_arbitrage_strategy.py
│   └── sentiment_strategy.py
│
├── risk/                      [风控层]
│   └── __init__.py           → VaR、CVaR、止损
│
└── anomaly/                   [异常检测]
    └── __init__.py           → 价格/成交量异常
```

### 测试目标映射

| 模块 | 单元测试 | 集成测试 | 端到端测试 |
|------|---------|---------|-----------|
| **EventBus** | test_eventbus_comprehensive | test_eventbus_business_scenarios | test_full_dataflow |
| **Data Providers** | (需添加) | test_python_cpp_link | test_e2e_dataflow |
| **Strategies** | (需添加) | test_eventbus_business_scenarios | test_full_dataflow |
| **Risk** | (需添加) | - | - |
| **Anomaly** | (需添加) | - | - |

---

## 📌 当前测试重点

### ✅ 已完成验证

1. **C++ EventBus核心** (769K events/sec)
   - test_event_stress.cpp ✓

2. **Pure Python EventBus** (18K events/sec)
   - test_eventbus_comprehensive.py ✓ (25个测试)
   - test_eventbus_business_scenarios.py ✓ (15个测试)

3. **完整数据流链路**
   - test_full_dataflow_链路.py ✓ (最新验证 2026-01-31)

### ⚠️ 待完成

1. **C++ EventBus Python绑定**
   - EventFormat.cpp编译问题 (30+错误)
   - eventbus_native.pyd编译

2. **模块单元测试**
   - Data Providers单元测试
   - Strategies单元测试
   - Risk模块测试

3. **性能基准测试**
   - Python策略性能测试
   - 数据提供者性能测试

---

## 🎯 测试策略建议

### 短期（开发阶段）
✅ **使用Pure Python EventBus** - 已验证可用
```python
# test_full_dataflow_链路.py 已证明链路打通
from astock_engine.core.eventbus import EventBus
```

### 中期（功能完善）
🔧 **添加模块单元测试**
```bash
# 为每个业务模块添加pytest测试
astock_engine/data/tests/test_futures_provider.py
astock_engine/strategies/tests/test_commodity_strategy.py
```

### 长期（性能优化）
🚀 **切换C++ EventBus**
- 修复EventFormat.cpp编译
- 完成C++绑定编译
- 性能提升42倍 (18K → 769K events/sec)

---

## 📖 相关文档

| 文档 | 内容 |
|------|------|
| [CPP_PYTHON_EVENTBUS_INTEGRATION.md](../CPP_PYTHON_EVENTBUS_INTEGRATION.md) | C++集成方案和问题分析 |
| [astock_engine/tests/INDEX.md](../astock_engine/tests/INDEX.md) | EventBus测试详细索引 |
| [astock_engine/tests/TEST_GUIDE.md](../astock_engine/tests/TEST_GUIDE.md) | 测试执行手册 |
| [EVENTBUS_ARCHITECTURE.md](../EVENTBUS_ARCHITECTURE.md) | EventBus架构设计 |

---

## 🚀 快速测试命令

```bash
# 验证完整数据流（推荐）
python test_full_dataflow_链路.py

# Python EventBus功能测试
pytest astock_engine/tests/test_eventbus_comprehensive.py -v

# C++ 性能测试
cd build && ./bin/Debug/test_event_stress.exe

# 运行所有Python测试
pytest astock_engine/tests/ -v --tb=short
```

---

**总结**：当前系统测试体系完整，**完整数据流链路已打通**（test_full_dataflow_链路.py验证通过），可以进行策略开发和业务逻辑开发。C++ EventBus集成是后续性能优化项。
