# 🔒 AStock量化引擎 - 接口冻结文档

**版本**: v1.0  
**日期**: 2026-01-31  
**状态**: 🚨 FROZEN - 禁止修改  

本文档定义了AStock量化引擎的核心接口边界，在后续开发中这些接口将保持稳定，任何修改需要经过版本升级流程。

---

## 📐 架构层次与职责边界

```
┌─────────────────────────────────────────────────────────┐
│                   Python业务层                           │
│  - 策略逻辑 (Strategy)                                   │
│  - 数据获取 (DataProvider - akshare)                     │
│  - 回测引擎 (BacktestEngine)                             │
└───────────────────┬─────────────────────────────────────┘
                    │ Python绑定层 (pybind11)
┌───────────────────┴─────────────────────────────────────┐
│                   C++性能层                               │
│  - 数据库访问 (ConnectionPool, Repository)               │
│  - 事件分发 (EventBus - 未来)                            │
│  - 因子计算 (FastFactors - 未来)                         │
└─────────────────────────────────────────────────────────┘
```

### 职责划分

| 层次 | 组件 | 职责 | 语言 |
|------|------|------|------|
| **数据获取层** | DataProvider | 外部API调用、网络请求、JSON解析 | Python |
| **数据存储层** | Database (C++) | 高性能数据库读写、连接池管理 | C++ |
| **事件分发层** | EventBus | 事件订阅/发布、线程安全分发 | Python (临时) → C++ |
| **策略执行层** | Strategy | 业务逻辑、信号生成、风控 | Python |

---

## 🔌 接口定义

### 1. 数据库访问层 (C++)

#### 1.1 DatabaseConfig (冻结)

```cpp
struct DatabaseConfig {
    std::string host{"localhost"};
    int port{3306};
    std::string database{"astock_quant"};
    std::string username{"root"};
    std::string password;
    std::string charset{"utf8mb4"};
    size_t pool_size{10};
    size_t max_overflow{20};
    
    bool validate() const;
    std::string getConnectionUrl() const;
};
```

**Python接口**:
```python
@dataclass
class DatabaseConfig:
    host: str = "localhost"
    port: int = 3306
    database: str = "astock_quant"
    username: str = "root"
    password: str = ""
    pool_size: int = 10
```

#### 1.2 MarketDataRepository (冻结)

**核心方法签名**:

```cpp
class MarketDataRepository {
public:
    // Symbol Info
    bool saveSymbol(const SymbolInfo& symbol);
    std::optional<SymbolInfo> getSymbol(const std::string& symbol);
    std::vector<SymbolInfo> getAllSymbols(
        std::optional<SymbolType> symbol_type = std::nullopt,
        const std::string& status = "active");
    
    // Daily Bar
    size_t saveDailyBars(const std::vector<DailyBar>& bars);
    std::vector<DailyBar> getDailyBars(
        const std::string& symbol,
        std::time_t start_date,
        std::time_t end_date);
    std::optional<DailyBar> getLatestBar(const std::string& symbol);
    
    // Minute Bar
    size_t saveMinuteBars(const std::vector<MinuteBar>& bars);
    std::vector<MinuteBar> getMinuteBars(
        const std::string& symbol,
        std::time_t start_datetime,
        std::time_t end_datetime,
        int frequency = 1);
    
    // Transaction
    bool beginTransaction();
    bool commit();
    bool rollback();
};
```

**Python接口**:
```python
class Database:
    # Symbol
    def save_symbol(symbol: str, name: str, symbol_type: str, 
                   exchange: str = '', list_date: date = None) -> bool
    def get_symbol(symbol: str) -> Optional[dict]
    def get_all_symbols(symbol_type: str = None) -> List[dict]
    
    # Daily Bar
    def save_daily_bars(bars: List[dict]) -> int
    def get_daily_bars(symbol: str, start_date: str, end_date: str) -> List[dict]
    def get_latest_bar(symbol: str) -> Optional[dict]
    
    # Transaction
    def begin_transaction() -> bool
    def commit() -> bool
    def rollback() -> bool
```

#### 1.3 数据模型 (冻结)

```cpp
enum class SymbolType { STOCK, FUTURE, ETF, INDEX };

struct SymbolInfo {
    std::string symbol;
    std::string name;
    SymbolType symbol_type;
    std::string exchange;
    std::time_t list_date;
    std::string status;
};

struct DailyBar {
    std::string symbol;
    std::time_t trade_date;
    double open, high, low, close;
    double pre_close;
    double volume, turnover;
    double change_pct, amplitude, turnover_rate;
    double pe_ratio, pb_ratio, market_cap;
};

struct MinuteBar {
    std::string symbol;
    std::time_t datetime;
    int frequency;  // 1/5/15/30/60
    double open, high, low, close;
    double volume, turnover;
};
```

---

### 2. EventBus接口层 (Python - 临时冻结)

#### 2.1 EventBus核心接口

```python
class EventBus:
    """事件总线 - 当前使用Pure Python实现"""
    
    def subscribe(self, event_type: str, handler: Callable, priority: int = 0) -> str:
        """订阅事件"""
        pass
    
    def unsubscribe(self, event_type: str, subscription_id: str) -> bool:
        """取消订阅"""
        pass
    
    def publish(self, event_type: str, data: Any) -> int:
        """发布事件，返回处理器数量"""
        pass
    
    def publish_async(self, event_type: str, data: Any):
        """异步发布"""
        pass
```

**标准事件类型** (冻结):
```python
EVENT_TYPES = {
    # 市场数据事件
    'market.bar.daily',       # 日线数据
    'market.bar.minute',      # 分钟线数据
    'market.tick',            # Tick数据
    'market.depth',           # 深度行情
    
    # 交易信号事件
    'signal.buy',             # 买入信号
    'signal.sell',            # 卖出信号
    'signal.close',           # 平仓信号
    
    # 订单事件
    'order.new',              # 新订单
    'order.filled',           # 订单成交
    'order.cancelled',        # 订单取消
    
    # 系统事件
    'system.start',           # 系统启动
    'system.stop',            # 系统停止
    'system.error',           # 系统错误
}
```

**Event数据结构** (冻结):
```python
@dataclass
class Event:
    type: str           # 事件类型
    data: dict          # 事件数据
    timestamp: float    # 时间戳
    source: str         # 事件源
    metadata: dict      # 元数据
```

---

### 3. 数据提供者接口层 (Python)

#### 3.1 BaseDataProvider (冻结)

```python
class BaseDataProvider(ABC):
    """数据提供者基类"""
    
    @abstractmethod
    def get_price_data(self, symbol: str, start_date: str, end_date: str, 
                      frequency: str = 'daily') -> pd.DataFrame:
        """获取价格数据
        
        Returns:
            DataFrame with columns: [date, open, high, low, close, volume]
        """
        pass
    
    @abstractmethod
    def get_fundamental_data(self, symbol: str, date: str) -> Dict:
        """获取基本面数据"""
        pass
```

**DataFrame标准格式** (冻结):
```python
# 日线数据
{
    'symbol': str,           # 标的代码
    'trade_date': str,       # 交易日期 'YYYY-MM-DD'
    'open': float,           # 开盘价
    'high': float,           # 最高价
    'low': float,            # 最低价
    'close': float,          # 收盘价
    'pre_close': float,      # 前收盘价
    'volume': float,         # 成交量
    'turnover': float,       # 成交额
    'change_pct': float,     # 涨跌幅%
}
```

---

### 4. 策略接口层 (Python)

#### 4.1 BaseStrategy (冻结)

```python
class BaseStrategy(ABC):
    """策略基类"""
    
    @abstractmethod
    def on_bar(self, bar: dict):
        """处理Bar数据
        
        Args:
            bar: 包含OHLCV的dict
        """
        pass
    
    @abstractmethod
    def on_tick(self, tick: dict):
        """处理Tick数据"""
        pass
    
    def generate_signal(self, signal_type: str, symbol: str, 
                       price: float, quantity: int, **kwargs) -> dict:
        """生成交易信号 (标准格式)"""
        return {
            'type': signal_type,      # 'buy' / 'sell' / 'close'
            'symbol': symbol,
            'price': price,
            'quantity': quantity,
            'timestamp': datetime.now().timestamp(),
            'strategy': self.__class__.__name__,
            **kwargs
        }
```

---

## 🔄 数据流向 (冻结)

```
┌──────────────┐
│ DataProvider │  Python - akshare获取外部数据
│  (Python)    │
└──────┬───────┘
       │ dict/DataFrame
       ↓
┌──────────────┐
│   Database   │  C++ - 高性能存储
│    (C++)     │
└──────┬───────┘
       │ 查询/回调
       ↓
┌──────────────┐
│  EventBus    │  Python (临时) - 事件分发
│  (Python)    │
└──────┬───────┘
       │ Event对象
       ↓
┌──────────────┐
│   Strategy   │  Python - 策略逻辑
│  (Python)    │
└──────┬───────┘
       │ Signal
       ↓
┌──────────────┐
│   OrderMgr   │  未来实现
│  (C++/Py)    │
└──────────────┘
```

---

## 📊 性能指标 (基准)

| 组件 | 指标 | 当前值 | 目标值 |
|------|------|--------|--------|
| **EventBus** (Python) | 事件/秒 | 18,485 | 50,000 |
| **EventBus** (C++) | 事件/秒 | 769,000 | 1,000,000 |
| **Database** (C++) | 插入/秒 | TBD | 100,000 |
| **Database** (C++) | 查询延迟 | TBD | <5ms |

---

## 🚦 变更管理

### 允许的变更
- ✅ 新增接口方法（向后兼容）
- ✅ 新增可选参数（默认值）
- ✅ 内部实现优化
- ✅ 性能改进

### 禁止的变更
- ❌ 修改已有方法签名
- ❌ 删除已有方法
- ❌ 修改返回值类型
- ❌ 修改参数顺序
- ❌ 修改事件类型名称
- ❌ 修改数据结构字段

### 版本升级流程
1. 提交变更申请（附带影响分析）
2. 技术评审
3. 更新文档和版本号
4. 通知所有依赖方
5. 提供迁移指南

---

## 📝 依赖清单

### Python依赖 (冻结)
```
pandas >= 1.5.0
numpy >= 1.23.0
akshare >= 1.10.0
pybind11 >= 2.10.0
```

### C++依赖 (冻结)
```
C++17 标准
MySQL Connector/C++ >= 8.0
pybind11 >= 2.10.0
```

### 数据库 (冻结)
```
MySQL >= 8.0
Database: astock_quant
Tables: symbol_info, daily_bar, minute_bar, tick_data
```

---

## 🔍 测试要求

每个冻结接口必须有：
- ✅ 单元测试（覆盖率 > 80%）
- ✅ 集成测试
- ✅ 性能基准测试
- ✅ 边界情况测试

---

## 📅 冻结历史

| 版本 | 日期 | 变更内容 | 负责人 |
|------|------|----------|--------|
| v1.0 | 2026-01-31 | 初始冻结：数据库层、EventBus、数据流 | GitHub Copilot |

---

## 🎯 后续规划

### Phase 2 - EventBus升级
- 目标：从Python迁移到C++实现
- 接口：保持Python接口不变
- 性能：从18K提升到769K events/sec
- 时间：待EventFormat.cpp问题修复

### Phase 3 - 因子计算
- 新增C++因子库（FastFactors）
- Python绑定层
- 接口设计待定（不影响现有接口）

### Phase 4 - 订单管理
- OrderManager实现
- 与Strategy层集成
- 接口设计待定

---

**本文档受版本控制，任何修改需要提交PR并经过审核流程。**
