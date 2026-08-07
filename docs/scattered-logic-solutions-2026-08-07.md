# 散装逻辑消除 — 详细解决方案

> 配套 [scattered-logic-audit-2026-08-07.md](scattered-logic-audit-2026-08-07.md) | 2026-08-07

每个方案含：现状代码（精简）→ 目标代码 → 影响范围 → 验证方式

---

## Phase A: P0 安全修复（预计 16 小时）

### A1: 移除硬编码 DB 密码 (I1, I2)

**I1 — NativePgConnectionPool.cpp:**

现状：
```cpp
cfg.password = cfgMgr.get_app_config_string("pg.password", "astock123");  // 明文密码!
```

方案：
```cpp
// 不再提供默认密码，缺失时拒绝连接
std::string pw = cfgMgr.get_app_config_string("pg.password", "");
if (pw.empty()) {
    // 回退到环境变量
    const char* env = std::getenv("ASTOCK_PG_PASSWORD");
    if (!env || !*env) {
        INTERNAL_ERROR_STREAM("pg.password not configured and ASTOCK_PG_PASSWORD not set");
        return false;
    }
    pw = env;
}
cfg.password = pw;
```

**I2 — MarketDataServiceFactory.h:**

现状：
```cpp
cfg.port = 3306;         // MySQL 端口，错！
cfg.password = "123456a"; // 明文密码
```

方案：
```cpp
// 删除整个硬编码默认值，所有字段必须从 ConfigManager 读取
cfg.host     = cfgMgr.get_app_config_string("marketdata.pg.host", "127.0.0.1");
cfg.port     = cfgMgr.get_app_config_int("marketdata.pg.port", 5432);  // 修正：PG 端口
cfg.database = cfgMgr.get_app_config_string("marketdata.pg.database", "astock_quant");
cfg.username = cfgMgr.get_app_config_string("marketdata.pg.user", "");
cfg.password = cfgMgr.get_app_config_string("marketdata.pg.password", "");
// 密码为空时回退环境变量 ASTOCK_MARKETDATA_PG_PASSWORD
```

**影响文件**: `NativePgConnectionPool.cpp`, `MarketDataServiceFactory.h`
**验证**: 不配置密码时启动，应看到错误日志而非静默连接失败

---

### A2: recentOrders() 加读锁 (I8)

**现状** — [TradeExecutionEngine.cpp:416-418](src/domain/trading/TradeExecutionEngine.cpp#L416-L418):
```cpp
const std::vector<TradeOrder>& TradeExecutionEngine::recentOrders() const noexcept {
    return m_impl->recentOrders;  // 无锁！写端在其他线程持有 mutex
}
```

**方案**: 改为值拷贝 + copy-under-lock：

```cpp
// .h — 声明改为返回值
[[nodiscard]] std::vector<TradeOrder> recentOrders() const noexcept;

// .cpp — 实现
std::vector<TradeOrder> TradeExecutionEngine::recentOrders() const noexcept {
    std::shared_lock lock(m_impl->recentOrdersMutex);  // 新增 shared_mutex
    return m_impl->recentOrders;
}
```

写端锁从 `std::mutex` 改为 `std::shared_mutex`（写用 `unique_lock`，读用 `shared_lock`）。当前只有 1 个读点（此 getter），改值拷贝后不需要 shared_mutex，用普通 `std::mutex` + `lock_guard` 即可。

**影响文件**: `TradeExecutionEngine.h`, `TradeExecutionEngine.cpp` (Impl 的 mutex 类型 + 所有写端 + 此读端)
**验证**: TSAN 构建 + 策略运行中不断调用 `recentOrders()` 不应报 data race

---

### A3: m_quoteCache 写入加锁 (I7)

**现状** — [GmSessionEngine.cpp:169-179](src/engine/src/GmSessionEngine.cpp#L169-L179):
```cpp
void GmSessionEngine::on_tick(const GmTickData& td) {
    GmQuote cached;
    cached.symbol = td.symbol;
    // ... populate ...
    m_quoteCache[td.symbol] = std::move(cached);  // 写入未持有 m_tickMutex!
}
```

`fetchQuote()` 读端持锁：
```cpp
std::lock_guard<std::mutex> lock(m_tickMutex);
auto it = m_quoteCache.find(symbol);
```

**方案**: 写入端也加锁：

```cpp
void GmSessionEngine::on_tick(const GmTickData& td) {
    GmQuote cached;
    cached.symbol = td.symbol;
    // ... populate ...
    {
        std::lock_guard<std::mutex> lock(m_tickMutex);
        m_quoteCache[td.symbol] = std::move(cached);
    }
}
```

**影响文件**: `GmSessionEngine.cpp` 单文件
**验证**: TSAN 构建 + tick 推送时并发调 `fetchQuote()`

---

### A4: BacktestResultRepository SQL 返回值检查 (I4, I14)

**I4 — saveFactorBacktest / saveDailySnapshot:**

现状：
```cpp
m_db.executeQuery(sql.str());
return true;  // 无论成功与否
```

方案：
```cpp
auto result = m_db.executeQuery(sql.str());
if (!result.isSuccess()) {
    INTERNAL_ERROR_STREAM("saveFactorBacktest failed: " << result.errorMessage());
    return false;
}
return true;
```

**I14 — ensureTables DDL 未检查:**

现状：
```cpp
m_db.executeQuery(kCreateStrategyBacktest);  // 忽略
m_db.executeQuery(kIdxStrategyBacktest);     // 忽略
m_tablesEnsured = true;
```

方案：逐条检查，任一失败则整体失败：
```cpp
bool BacktestResultRepository::ensureTables() {
    if (m_tablesEnsured) return true;
    auto r1 = m_db.executeQuery(kCreateStrategyBacktest);
    if (!r1.isSuccess()) {
        INTERNAL_ERROR_STREAM("CREATE TABLE strategy_backtest_results failed: " << r1.errorMessage());
        return false;
    }
    auto r2 = m_db.executeQuery(kIdxStrategyBacktest);
    if (!r2.isSuccess()) {
        INTERNAL_ERROR_STREAM("CREATE INDEX on strategy_backtest_results failed: " << r2.errorMessage());
        return false;
    }
    // ... 其余 DDL ...
    m_tablesEnsured = true;
    return true;
}
```

**影响文件**: `BacktestResultRepository.cpp`
**验证**: 模拟 DB 不可用场景 → ensureTables 应返回 false，后续写操作不应执行

---

### A5: 全局 EventBus 裸指针改 shared_ptr (I6)

**现状** — [GlobalEventBusRegistry.cpp:7-9](src/engine/src/GlobalEventBusRegistry.cpp#L7-L9):
```cpp
namespace {
EventBus* g_engine_event_bus = nullptr;
std::mutex g_engine_event_bus_mutex;
}

void register_engine_event_bus(EventBus* bus) {
    std::lock_guard lock(g_engine_event_bus_mutex);
    g_engine_event_bus = bus;
}

EventBus* get_engine_event_bus() {
    std::lock_guard lock(g_engine_event_bus_mutex);
    return g_engine_event_bus;  // 裸指针，无生命周期保证
}
```

**方案**:
```cpp
namespace {
std::shared_ptr<EventBus> g_engine_event_bus;
std::mutex g_engine_event_bus_mutex;
}

void register_engine_event_bus(std::shared_ptr<EventBus> bus) {
    std::lock_guard lock(g_engine_event_bus_mutex);
    g_engine_event_bus = std::move(bus);
}

std::shared_ptr<EventBus> get_engine_event_bus() {
    std::lock_guard lock(g_engine_event_bus_mutex);
    return g_engine_event_bus;  // shared_ptr，即使原持有者释放，调用方仍安全
}
```

**调用方改动**: `Trigger.cpp` 的 `g_trigger_publisher` 也需同样处理。所有使用处从 `EventBus*` 改为 `std::shared_ptr<EventBus>`。

**影响文件**: `GlobalEventBusRegistry.cpp`, `GlobalEventBusRegistry.h`, `Trigger.cpp`, `EngineImpl.cpp`（初始化处）
**验证**: Engine shutdown → 已获得 `shared_ptr` 的调用方访问不应 crash

---

### A6: ConfigManager public 成员改 private (I15)

**现状** — [ConfigManager.hpp:57-59](src/foundation/include/foundation/config/ConfigManager.hpp#L57-L59):
```cpp
public:
    std::map<std::string, ConfigNode::Ptr> snapshots_;
    std::vector<std::vector<ConfigChange>> change_history_;
```

**方案**: 改为 private，只暴露 const 访问器：
```cpp
public:
    [[nodiscard]] const std::map<std::string, ConfigNode::Ptr>& snapshots() const { return snapshots_; }
    [[nodiscard]] const std::vector<std::vector<ConfigChange>>& changeHistory() const { return change_history_; }
    [[nodiscard]] std::size_t changeHistorySize() const { return change_history_.size(); }

private:
    std::map<std::string, ConfigNode::Ptr> snapshots_;
    std::vector<std::vector<ConfigChange>> change_history_;
```

**排查外部访问**: `grep -rn "snapshots_\|change_history_"` 找出所有外部使用者，替换为访问器调用。

**影响文件**: `ConfigManager.hpp` + 所有外部访问者
**验证**: 编译通过 + 配置变更历史查询功能正常

---

## Phase B: P0 封装/结构（预计 24 小时）

### B1: GmSessionEngine public 成员 → private + friend (I5)

**现状**:
```cpp
public: // SessionStrategy needs direct access
    std::unique_ptr<void, StrategyDeleter> m_strategy;
    std::mutex m_tickMutex;
    std::unordered_map<std::string, int> m_tickRefCount;
    std::unordered_map<std::string, GmQuote>  m_quoteCache;
```

**方案**:

```cpp
// GmSessionEngine.h
class GmSessionEngine {
    friend class SessionStrategy;  // 唯一的内部使用者
public:
    // ... 原有 public API ...

    // 给 engine 层使用的访问器（不破坏封装）
    [[nodiscard]] std::mutex& tickMutex() { return m_tickMutex; }
    [[nodiscard]] const std::unordered_map<std::string, GmQuote>& quoteCache() const { return m_quoteCache; }

private:
    struct Impl { /* ... */ };
    std::unique_ptr<Impl> m_impl;

    std::unique_ptr<void, StrategyDeleter> m_strategy;
    std::mutex m_tickMutex;
    std::unordered_map<std::string, int> m_tickRefCount;
    std::unordered_map<std::string, GmQuote> m_quoteCache;
};
```

`SessionStrategy` 在 `GmSessionEngine.cpp` 的匿名 namespace 中，加 `friend` 后可直接访问 private 成员。

**影响文件**: `GmSessionEngine.h`, `GmSessionEngine.cpp`, `TradingSystem.cpp`（2 处 `gse.m_impl->sessionReady`）
**验证**: TradingSystem 改用 `isSessionReady()` 访问器 → 编译 + 功能不变

---

### B2: FactorBacktestBridge QVariantMap → typed DTO (I9)

**现状** — 250 行裸字符串 key 构造嵌套 map：
```cpp
QVariantMap metricItem;
metricItem["key"] = "annual_return";
metricItem["title"] = QStringLiteral("年化收益率");
metricItem["value"] = annualReturn;
metricItem["format"] = "percent";
// ... 重复 15+ 种不同的 metric
```

**方案**: 定义轻量 DTO struct：

```cpp
// src/ui/bridge/include/FactorBacktestTypes.h
#pragma once
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <vector>

namespace ui::bridge {

struct MetricItem {
    QString key;
    QString title;
    QString subtitle;
    QVariant value;
    QString format;       // "percent" | "decimal" | "integer" | "date"
    double goodThreshold{0.0};
    QString direction;    // "higher_better" | "lower_better"
    int tier{0};          // 0=core, 1=auxiliary, 2=optional
    QString units;

    [[nodiscard]] QVariantMap toMap() const {
        QVariantMap m;
        m["key"] = key;
        m["title"] = title;
        if (!subtitle.isEmpty()) m["subtitle"] = subtitle;
        m["value"] = value;
        m["format"] = format;
        if (goodThreshold != 0.0) m["goodThreshold"] = goodThreshold;
        if (!direction.isEmpty()) m["direction"] = direction;
        m["tier"] = tier;
        if (!units.isEmpty()) m["units"] = units;
        return m;
    }
};

struct FactorQualityReport {
    std::vector<MetricItem> coreMetrics;
    std::vector<MetricItem> optionalMetrics;
    std::vector<MetricItem> auxiliaryMetrics;
    // ...

    [[nodiscard]] QVariantMap toMap() const {
        QVariantMap m;
        m["coreMetrics"] = toMapList(coreMetrics);
        m["optionalMetrics"] = toMapList(optionalMetrics);
        // ...
        return m;
    }

private:
    static QVariantList toMapList(const std::vector<MetricItem>& items) {
        QVariantList list;
        for (const auto& item : items)
            list.append(item.toMap());
        return list;
    }
};

} // namespace ui::bridge
```

然后在 `processRunResult` 中：
```cpp
FactorQualityReport report;
report.coreMetrics.push_back(MetricItem{
    .key = "annual_return",
    .title = QStringLiteral("年化收益率"),
    .value = annualReturn,
    .format = "percent",
    .direction = "higher_better",
    .tier = 0
});
// ...

emit factorBacktestReady(report.toMap());
```

**影响文件**: 新增 `FactorBacktestTypes.h` + 修改 `FactorBacktestBridge.cpp`
**验证**: QML 端接收的 JSON 结构前后一致（Golden Master: 录制一份旧 output JSON, 对比新 output）

---

### B3: TradingBridges 订单 map → typed struct (I10)

**方案**: 定义 key 常量化头文件：

```cpp
// src/ui/bridge/include/OrderFieldKeys.h
#pragma once
#include <QString>

namespace ui::bridge::keys {
    inline const QString kBrokerOrderId  = QStringLiteral("brokerOrderId");
    inline const QString kSymbol         = QStringLiteral("symbol");
    inline const QString kSide           = QStringLiteral("side");
    inline const QString kPrice          = QStringLiteral("price");
    inline const QString kQuantity       = QStringLiteral("quantity");
    inline const QString kStatus         = QStringLiteral("status");
    inline const QString kRawStatus      = QStringLiteral("rawStatus");
    inline const QString kFilledQty      = QStringLiteral("filledQty");
    inline const QString kFilledPrice    = QStringLiteral("filledPrice");
    inline const QString kMessage        = QStringLiteral("message");
    inline const QString kStrategyId     = QStringLiteral("strategyId");
    inline const QString kSubmittedAt    = QStringLiteral("submittedAt");
    inline const QString kClientOrderId  = QStringLiteral("clientOrderId");
    inline const QString kAccepted       = QStringLiteral("accepted");
    inline const QString kOrderType      = QStringLiteral("orderType");
    inline const QString kPositionEffect = QStringLiteral("positionEffect");
    inline const QString kExchange       = QStringLiteral("exchange");
    inline const QString kCurrency       = QStringLiteral("currency");
    inline const QString kCreatedAt      = QStringLiteral("createdAt");
    inline const QString kUpdatedAt      = QStringLiteral("updatedAt");
}
```

然后在 `TradingBridges.cpp` 中全局替换：
```cpp
// 之前
entry["brokerOrderId"] = QString::fromStdString(order.brokerOrderId());

// 之后
entry[keys::kBrokerOrderId] = QString::fromStdString(order.brokerOrderId());
```

QML 端同步引用 key 常量（或在 QML 侧也定义常量，从 C++ 注入）。

**影响文件**: 新增 `OrderFieldKeys.h` + `TradingBridges.cpp` 全局替换
**验证**: 订单状态更新后 QML 端显示正常

---

### B4: Infrastructure Qt 类型解耦 (I3)

**问题**: `StrategyRepository.h` 用 `QString`/`QVariantMap`/`QDateTime` 作为接口类型。

**方案**: 分两步走，不破坏现有功能：

**Step 1**: 在基础设施层定义纯 C++ 等价类型：

```cpp
// src/infrastructure/include/strategy/StrategyDataTypes.h
#pragma once
#include <chrono>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace infrastructure::strategy {

using ParamValue = std::variant<bool, int, double, std::string>;
using ParamMap   = std::map<std::string, ParamValue>;

struct PersistedStrategyData {
    std::string id;
    std::string name;
    std::string type;
    std::string status;
    ParamMap params;
    ParamMap metadata;
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point updatedAt;
    // ...
};

// 转换函数 — 只在 bridge 层调用
class StrategyDataConverter {
public:
    [[nodiscard]] static QVariantMap toQtVariant(const PersistedStrategyData& data);
    [[nodiscard]] static PersistedStrategyData fromQtVariant(const QVariantMap& map);
    [[nodiscard]] static QDateTime toQDateTime(std::chrono::system_clock::time_point tp);
    [[nodiscard]] static std::chrono::system_clock::time_point fromQDateTime(const QDateTime& dt);
};

} // namespace infrastructure::strategy
```

**Step 2**: `StrategyRepository` 改为使用纯 C++ 类型，`StrategyBridge` 使用 `StrategyDataConverter` 做转换。

**影响文件**: `StrategyRepository.h`, `StrategyRepository.cpp`, `StrategyBridge.cpp`, `ResolvedStrategyBehaviorVariant.h`
**验证**: 策略列表加载、保存、参数解析功能全回归

---

### B5: QML 大文件拆分 (I11, I12, I13)

**B5.1 — BacktestResultPanel.qml (1329 行) → 拆分为:**

```
components/Backtest/
├── BacktestResultPanel.qml       (~200行, 主容器, 组合子组件)
├── BacktestMetricsGrid.qml       (~200行, 核心指标网格)
├── BacktestEquityChart.qml       (~250行, 权益曲线图)
├── BacktestDrawdownChart.qml     (~200行, 回撤图)
├── BacktestTradeList.qml         (~200行, 成交明细)
├── BacktestDailyPnL.qml          (~150行, 每日盈亏)
└── BacktestBenchmarkCompare.qml  (~150行, 基准对比)
```

**B5.2 — BacktestPage.qml (1071 行) → 拆分为:**
```
components/FactorWorkbench/Backtest/
├── BacktestPage.qml              (~200行)
├── BacktestConfigPanel.qml       (~250行, 回测参数配置)
├── BacktestProgressPanel.qml     (~200行, 进度展示)
├── BacktestSymbolSelector.qml    (~200行, 股票池选择)
└── BacktestPeriodPicker.qml      (~150行, 日期区间选择)
```

**B5.3 — DataSelectionPanel.qml (504 行) → 拆分为:**
```
components/DataAnalysis/
├── DataSelectionPanel.qml        (~150行)
├── DataSourceSelector.qml        (~150行)
├── DataSymbolFilter.qml          (~100行)
└── DataDateRangePicker.qml       (~100行)
```

**影响文件**: 如上 + 引用这些组件的 Page 文件
**验证**: 每个子组件可独立在 Qt Quick Designer 中预览

---

### B6: syncDailyRange 拆分 (I16)

**现状**: 140 行，3 个 pass 交织

**方案**:
```cpp
// Before: syncDailyRange() 140行
// After: 三个独立方法

bool PostMarketSyncService::syncDailyRange(
    const std::vector<std::string>& symbols,
    int tradingDay, bool force)
{
    auto gmMap = buildGmSymbolMap(symbols);  // 提取自 E18

    // Pass 1: 批量获取
    DailyRangeFetchResult result = fetchDailyBarRange(gmMap, tradingDay, force);
    if (!result.success) return false;

    // Pass 2: 重试失败项（可选）
    if (!result.failedSymbols.empty()) {
        retryFetchFailed(result, gmMap, tradingDay);
    }

    // Pass 3: 估值同步
    syncValuationForRange(gmMap, tradingDay);

    return true;
}

DailyRangeFetchResult PostMarketSyncService::fetchDailyBarRange(
    const GmSymbolMap& gmMap, int tradingDay, bool force)
{
    // ~60行: 原 fetch pass 逻辑
}

void PostMarketSyncService::retryFetchFailed(
    DailyRangeFetchResult& result, const GmSymbolMap& gmMap, int tradingDay)
{
    // ~30行: 原 retry pass 逻辑
}

void PostMarketSyncService::syncValuationForRange(
    const GmSymbolMap& gmMap, int tradingDay)
{
    // ~30行: 原 valuation pass 逻辑
}
```

**影响文件**: `PostMarketSyncService.cpp`, `PostMarketSyncService.h`
**验证**: 回测数据同步结果与拆分前一致

---

## Phase C: P1 重复消除（预计 40 小时）

### C1: 日期格式化统一 (E1)

**现状**: 20+ 处 `int y = date / 10000; ...; snprintf(buf, "%04d-%02d-%02d", ...)`

**方案**:

```cpp
// src/foundation/include/foundation/utils/DateUtils.h
#pragma once
#include <string>
#include <cstdio>

namespace foundation::utils {

/// @brief 将 YYYYMMDD 整数转为 "YYYY-MM-DD" 字符串
inline std::string formatTradingDay(int yyyymmdd) {
    char buf[16];
    int y = yyyymmdd / 10000;
    int m = (yyyymmdd / 100) % 100;  // 统一用此公式，修复 (yyyymmdd % 10000) / 100 不一致
    int d = yyyymmdd % 100;
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
    return std::string(buf);
}

/// @brief 将 "YYYY-MM-DD" 字符串转回 YYYYMMDD 整数
inline int parseTradingDay(const std::string& str) {
    int y, m, d;
    if (std::sscanf(str.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return 0;
    return y * 10000 + m * 100 + d;
}

/// @brief 分解 YYYYMMDD 为 (year, month, day)
struct DateParts { int year; int month; int day; };
inline DateParts decomposeDate(int yyyymmdd) {
    return {
        yyyymmdd / 10000,
        (yyyymmdd / 100) % 100,
        yyyymmdd % 100
    };
}

} // namespace foundation::utils
```

**替换模式**: 在所有 20+ 处替换为 `foundation::utils::formatTradingDay(date)`。

**影响文件**: 8+ 个 .cpp 文件（见审计报告 E1）
**验证**: 全量回测结果日期字段前后完全一致

---

### C2: DB 连接守卫 RAII (E16)

**现状**: 40+ 处手动模式：
```cpp
auto db = NativePgConnectionPool::instance().getConnection();
if (!db || !db->isOpen()) return;  // 或 return false
```

**方案**: 利用已有的 `ScopedConnection` 设计并推广：

```cpp
// src/infrastructure/include/database/ScopedConnection.h
#pragma once
#include <memory>
#include <stdexcept>

namespace infrastructure::database {

class ConnectionGuard {
public:
    ConnectionGuard() {
        auto& pool = NativePgConnectionPool::instance();
        m_db = pool.getConnection();
        if (!m_db || !m_db->isOpen()) {
            throw std::runtime_error("Failed to acquire database connection");
        }
    }

    ISqlDatabase* operator->() noexcept { return m_db.get(); }
    ISqlDatabase& operator*()  noexcept { return *m_db; }
    [[nodiscard]] ISqlDatabase* db() const noexcept { return m_db.get(); }

    /// @brief 返回 false（连接不可用）而不抛异常
    [[nodiscard]] bool isValid() const noexcept {
        return m_db && m_db->isOpen();
    }

private:
    std::shared_ptr<ISqlDatabase> m_db;  // shared_ptr — 归还连接池
};

} // namespace infrastructure::database
```

**使用**:
```cpp
// 方案 A: 抛异常版（适合不会恢复的路径）
auto conn = ConnectionGuard();
conn->executeUpdate(sql);

// 方案 B: 检查版（适合需要优雅降级的路径）
ConnectionGuard conn;
if (!conn.isValid()) return false;
conn->executeUpdate(sql);
```

**影响文件**: `OrderRecorder.cpp`(4 处), `EventBridgePoller.cpp`(1), `PostMarketSyncService.cpp`(40+), `StrategyRepository.cpp`(1)
**验证**: 模拟 DB 不可用 → 各调用点行为正确（异常传播 / 优雅降级）

---

### C3: toGmSymbol map 构造统一 (E18)

**现状**: 6 个方法各自重复：
```cpp
std::unordered_map<std::string, std::string> gmToSym;
std::vector<std::string> gmList;
for (const auto& sym : symbols) {
    std::string g = toGmSymbol(sym);
    if (!g.empty()) { gmToSym[g] = sym; gmList.push_back(g); }
}
```

**方案**:

```cpp
// PostMarketSyncService 私有方法
struct GmSymbolMapping {
    std::unordered_map<std::string, std::string> gmToSym;  // gmSymbol → internalSymbol
    std::vector<std::string> gmList;                        // gmSymbol 列表（传 gmsdk）

    [[nodiscard]] bool empty() const noexcept { return gmList.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return gmList.size(); }
};

GmSymbolMapping PostMarketSyncService::buildGmSymbolMap(
    const std::vector<std::string>& symbols) const
{
    GmSymbolMapping mapping;
    mapping.gmList.reserve(symbols.size());
    for (const auto& sym : symbols) {
        std::string g = engine::GmSessionEngine::toGmSymbol(sym);  // 使用权威方法
        if (!g.empty()) {
            mapping.gmToSym[g] = sym;
            mapping.gmList.push_back(std::move(g));
        }
    }
    return mapping;
}
```

**替换**: 6 个 sync 方法中删除重复代码，统一调用 `buildGmSymbolMap(symbols)`。

**影响文件**: `PostMarketSyncService.cpp`, `PostMarketSyncService.h`
**验证**: 各 sync 功能正常（symbol 数量无变化）

---

### C4: 批量 upsert → BulkUpserter 模板 (E19)

**方案**:

```cpp
// src/infrastructure/include/database/BulkUpserter.h
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace infrastructure::database {

class BulkUpserter {
public:
    using Params = std::vector<SqlParam>;

    BulkUpserter(ISqlDatabase* db, std::string sql,
                 std::size_t batchSize = 500)
        : m_db(db), m_sql(std::move(sql)), m_batchSize(batchSize)
    {
        m_batch.reserve(batchSize);
    }

    void push(Params&& params) {
        m_batch.push_back(std::move(params));
        if (m_batch.size() >= m_batchSize)
            flush();
    }

    void flush() {
        if (m_batch.empty()) return;
        for (const auto& p : m_batch)
            m_db->executeUpdate(m_sql, p);
        m_batch.clear();
    }

    ~BulkUpserter() { flush(); }

    [[nodiscard]] bool hasBatch() const noexcept { return !m_batch.empty(); }

private:
    ISqlDatabase* m_db;
    std::string m_sql;
    std::size_t m_batchSize;
    std::vector<Params> m_batch;
};

} // namespace infrastructure::database
```

**使用**:
```cpp
// Before: syncDaily 中 15 行手写 flush lambda
// After:
BulkUpserter upsert(db, "INSERT INTO mkt.daily_bar (...) VALUES (...) ON CONFLICT ...", 500);
for (const auto& bar : bars) {
    upsert.push({bar.symbol, bar.date, bar.open, ...});
}
// ~BulkUpserter 自动 flush 剩余
```

**影响文件**: `PostMarketSyncService.cpp`(5 处), 新增 `BulkUpserter.h`
**验证**: 数据同步量无变化 + 边界条件（空批、超大批）

---

### C5: fromGm/toGm/状态映射统一 (E2, E10)

**C5.1 — fromGm 系列:**

现状：3 个文件中 `fromGm()` 匿名 namespace 函数 + `GmSessionEngine::fromGmSymbol()` 已经存在。

方案：删除 `AccountEngine.cpp`, `GmSessionEngine.cpp`（内部未使用）, `TradeEngine.cpp` 中的匿名 namespace `fromGm`，全部调用 `GmSessionEngine::fromGmSymbol()`。（`OrderManager.cpp` 已在 Phase 1-5 中修复）

**C5.2 — toGmSide/toGmType:**

现状：
```cpp
// GmSessionEngine.cpp — 匿名 namespace
int toGmSide(OrderSide s) { return s == OrderSide::Buy ? 1 : 2; }
int toGmType(OrderType t) { return t == OrderType::Limit ? 1 : 2; }

// TradeEngine.cpp — 匿名 namespace（完全相同）
int toGmSide(OrderSide s) { return s == OrderSide::Buy ? 1 : 2; }
int toGmOrderType(OrderType t) { return t == OrderType::Limit ? 1 : 2; }
```

方案：提升到 `GmSessionEngine` 作为 public static 方法：
```cpp
// GmSessionEngine.h
[[nodiscard]] static int toGmSide(gmsdk::OrderSide s) noexcept;
[[nodiscard]] static int toGmOrderType(gmsdk::OrderType t) noexcept;
```

**C5.3 — 状态映射:**
`TradeExecutionEngine.cpp` 中已有匿名 namespace 的 `toOrderStatusValue()`，删除两个内联 switch（:441-448, :517-530）改用此函数。

**影响文件**: `GmSessionEngine.h/.cpp`, `TradeEngine.cpp`, `AccountEngine.cpp`, `TradeExecutionEngine.cpp`
**验证**: 编译通过 + 下单/状态回调正常

---

### C6: Schema 名常量化 (E20)

**方案**:

```cpp
// src/infrastructure/include/database/SchemaNames.h
#pragma once

namespace infrastructure::database::schema {

constexpr const char* kReference      = "ref";
constexpr const char* kMarket         = "mkt";
constexpr const char* kFundamental    = "fund";
constexpr const char* kAlpha          = "alpha";
constexpr const char* kLive           = "live";
constexpr const char* kPortfolio      = "port";
constexpr const char* kData           = "data";
constexpr const char* kPublic         = "public";

} // namespace infrastructure::database::schema
```

**替换策略**: 逐步替换 100+ 处 SQL 字符串。优先替换 DDL（低频），然后 DML（高频）。不改变 SQL 语义，仅把 `"mkt.daily_bar"` 替换为 `std::string(schema::kMarket) + ".daily_bar"`。

**影响文件**: 所有 repository + PostMarketSyncService
**验证**: 编译通过 + 全量 SQL 执行正常

---

### C7: Bridge 领域逻辑回迁 (E11)

**C7.1 — VaR 计算:**
```cpp
// RiskControlBridge.cpp — 删除此段
double varUsage = exposurePct * 1.49;

// 改为调用
double varUsage = domain::strategy::RiskManager::estimateVar(exposurePct);
```

在 `RiskManager` 中新增：
```cpp
/// @brief 简化 VaR 估算，1.49 ≈ 99% 置信度下的正态分位数（单尾 2.33 的缩放）
static double estimateVar(double exposurePct, double confidenceZ = 1.49);
```

**C7.2 — 价格舍入规则:**
```cpp
// 新增 domain::trading::roundPriceForBoard
// src/domain/trading/include/PositionUtils.h
inline double roundPriceForBoard(double price, const std::string& type) noexcept {
    if (type == "futures")
        return std::round(price);
    if (type == "options")
        return std::round(price * 10000.0) / 10000.0;
    // equities
    return std::round(price * 100.0) / 100.0;
}
```

Bridge 层调用：
```cpp
// Before
if (m == "futures") price = qRound(price);
else if (m == "options") price = qRound(price * 10000.0) / 10000.0;
else price = qRound(price * 100.0) / 100.0;

// After
price = domain::trading::roundPriceForBoard(price, type.toStdString());
```

**C7.3 — 技术指标回迁:**
`CandleDataModel.cpp` 中的 SMA/EMA/MACD/KDJ/RSI 完整实现提取为：
```cpp
// src/domain/market/include/TechnicalIndicators.h
namespace domain::market::indicators {
    [[nodiscard]] double sma(const std::vector<double>& closes, int period);
    [[nodiscard]] double ema(const std::vector<double>& closes, int period);
    struct MacdResult { double dif; double dea; double histogram; };
    [[nodiscard]] MacdResult macd(const std::vector<double>& closes, int fast, int slow, int signal);
    // ...
}
```

`CandleDataModel` 改为委托：
```cpp
double CandleDataModel::smaAt(int row, int period) const {
    auto closes = getCloseSeries(row, period);  // 只做数据提取
    return domain::market::indicators::sma(closes, period);  // 委托领域层计算
}
```

**影响文件**: `RiskControlBridge.cpp`, `TradingBridges.cpp`, `CandleDataModel.cpp`, `RiskManager.cpp/h`, `PositionUtils.h`, 新增 `TechnicalIndicators.h`
**验证**: K 线图指标数值不变

---

### C8: void* 改前向声明 (E3)

**现状** — 4 个类：
```cpp
void* m_strategy = nullptr;  // holds gmsdk ::Strategy pointer
// 使用: auto* s = static_cast<::Strategy*>(m_strategy);
```

**方案**: 使用前向声明 + unique_ptr：

```cpp
// GmSessionEngine.h (同样应用于 AccountEngine.h, TradeEngine.h, OrderManager.h)
// 前向声明 gmsdk 类型
namespace gmsdk {
    class Strategy;
}

class GmSessionEngine {
    // ...
    struct StrategyDeleter {
        void operator()(gmsdk::Strategy* p) const noexcept;
    };
    std::unique_ptr<gmsdk::Strategy, StrategyDeleter> m_strategy;
};
```

`StrategyDeleter::operator()` 定义在 `.cpp` 中包含 gmsdk 头文件的地方。

**影响文件**: `AccountEngine.h/.cpp`, `TradeEngine.h/.cpp`, `OrderManager.h/.cpp`, `GmSessionEngine.h/.cpp`
**验证**: 编译通过 + `static_cast<::Strategy*>` 调用点全部改为直接使用 `m_strategy.get()`

---

### C9: raw new/delete → unique_ptr (E5, E6)

**C9.1 — BacktestScheduler:**

现状：
```cpp
// .h
ResourceGovernor* m_governor = nullptr;
// .cpp
m_governor = new ResourceGovernor(memoryLimitBytes);
delete m_governor;
```

方案：
```cpp
// .h
std::unique_ptr<ResourceGovernor> m_governor;
// .cpp
m_governor = std::make_unique<ResourceGovernor>(memoryLimitBytes);
// 删除 delete — unique_ptr 析构自动处理
```

**C9.2 — DataCacheParquet:**

现状：
```cpp
using ArrowWriteToken = WriteSession*;
void finishArrowWrite(ArrowWriteToken token) { delete token; }
```

方案：
```cpp
// 定义自定义 deleter
struct WriteSessionDeleter {
    void operator()(WriteSession* p) const noexcept {
        // 清理 Arrow 资源
        delete p;
    }
};
using ArrowWriteToken = std::unique_ptr<WriteSession, WriteSessionDeleter>;

// finishArrowWrite 改为接受 unique_ptr&& 或直接删掉（析构自动触发）
// 调用方:
auto token = ArrowWriteToken(new WriteSession(...));
```

**影响文件**: `BacktestScheduler.h/.cpp`, `DataCacheParquet.h/.cpp`
**验证**: Valgrind/ASAN 不报 leak

---

### C10: static int 诊断 → atomic (E7)

**现状**: 10 处：
```cpp
static int genDiag = 0;
if (genDiag++ < 5) { LOG(...); }  // 非原子递增 → UB
```

**方案**:
```cpp
static std::atomic<int> genDiag{0};
if (genDiag.fetch_add(1, std::memory_order_relaxed) < 5) { LOG(...); }
```

**影响文件**: `OrderGenerator.cpp`, `MultiFactorStrategy.cpp`, `RuntimeFactorSvc.cpp`, `RiskManager.cpp`
**验证**: TSAN 不报 data race

---

### C11: 千行函数拆分 (E8)

**C11.1 — `backtest()` 1226 行:**

```cpp
// StrategyEngineFacade.h — 新增私有方法声明
private:
    struct BacktestContext {
        // 回测循环间共享的状态
        std::vector<std::string> symbols;
        int startDate, endDate;
        double initialCapital;
        // ...
    };

    void runBacktestLoop(BacktestContext& ctx);
    void processBacktestDay(BacktestContext& ctx, int date);
    std::vector<StrategySignal> generateBacktestSignals(const BacktestContext& ctx, int date);
    std::vector<FillResult> simulateFills(const std::vector<OrderRequest>& orders, int date);
    void applyBacktestRules(const BacktestContext& ctx, std::vector<StrategySignal>& signals);
    BacktestMetrics computeBacktestMetrics(const BacktestContext& ctx);
    std::string buildBacktestDiagnostics(const BacktestContext& ctx, const BacktestMetrics& metrics);
```

**C11.2 — `FactorBacktestOrchestrator::run()` 1098 行:**

拆分为: `setupDbFallback()`, `runChunkedCompute()`, `computeCrossSectionalStats()`, `calculateIC()`, `runSimulatedTrading()`, `buildJsonResult()`。

**影响文件**: `StrategyEngineFacade.h/.cpp`, `FactorBacktestOrchestrator.h/.cpp`
**验证**: 同一组参数回测两次，结果逐字段 diff（Golden Master）

---

### C12: 佣金/税率统一 (E9)

**方案**:

```cpp
// src/domain/trading/include/TradingCosts.h
#pragma once

namespace domain::trading {

struct TradingCosts {
    double commissionRate;   // 佣金率
    double minCommission;     // 最低佣金（元）
    double stampTaxRate;      // 印花税率（卖出时单向收取）
    double transferFeeRate;   // 过户费率

    /// @brief A 股默认费率
    static const TradingCosts& aStock() {
        static const TradingCosts c{0.0003, 5.0, 0.001, 0.00002};
        return c;
    }
};

} // namespace domain::trading
```

然后 PnlCalculator 和 BacktestFillSimulator 都引用 `TradingCosts::aStock()`。

**影响文件**: `PnlCalculator.h`, `BacktestFillSimulator.h`, 新增 `TradingCosts.h`
**验证**: 计算盈亏前后一致

---

### C13-C17: 其余 P1 项

| 编号 | 方案摘要 |
|------|---------|
| C13: toConfigNode | 提取 `ConfigVariantAdapter.h` 到 `src/ui/bridge/include/` |
| C14: FactorBacktestBridge INSERT 检查 | 同 A4 模式：检查 `executeUpdate` 返回值 |
| C15: Arrow ValueOrDie → 错误处理 | 改为 `if (!status.ok()) { emit error; return; }` |
| C16: safeStr SQL 转义补全 | BacktestResultRepository 引用 `MarketDataRepository` 的 `safeStr()` 或统一提成独立头文件 |
| C17: ThreadPoolExecutor 裸指针 | workers_ 声明周期改为在所有 thread join 后清理 |

---

## Phase D: P2/P3 技术债（预计 30 小时，择机）

### D1: 字符串路由 → enum map

**RuleConditionEvaluator::compileConditionImpl()** — 改 JSON op 字符串为 `constexpr` 映射：

```cpp
// 编译期 hash map (C++20 constexpr)
enum class ConditionOp : uint8_t {
    All, Any, Not, Truthy,
    Eq, Neq, Lt, Le, Gt, Ge,
    // ...
};

constexpr ConditionOp parseOp(std::string_view op) {
    // 用 FNV-1a hash → switch 分发（O(1) 且 constexpr）
    switch (hashString(op)) {
        case "all"_hash:    return ConditionOp::All;
        case "any"_hash:    return ConditionOp::Any;
        case "lt"_hash:     return ConditionOp::Lt;
        // ...
        default:            return ConditionOp::Truthy;  // 或 throw
    }
}
```

**影响**: `RuleConditionEvaluator.cpp`, `StrategyEngineFacade.cpp`（factorCombineMode）, `RiskEvaluator.cpp`（parseDirection）

### D2: EventBus 事件名常量化

```cpp
// src/engine/include/Event/EventTopics.h
namespace engine::event_topics {
    constexpr std::string_view kMarketTick           = "trading.market.tick";
    constexpr std::string_view kAccountUpdated       = "trading.account.updated";
    constexpr std::string_view kPositionUpdated      = "trading.position.updated";
    constexpr std::string_view kOrderUpdated         = "trading.order.updated";
    constexpr std::string_view kExecutionReport      = "trading.execution.report";
}
```

发布端: `EventFormat(engine::event_topics::kOrderUpdated, ...)`
订阅端: `bus->subscribe(engine::event_topics::kOrderUpdated, ...)`

### D3-D8: 快速参考

| 编号 | 内容 |
|------|------|
| D3 | 死代码清理 — 删除 `EventBusStats` 方法定义、`process_batch_events()`、未使用的 `ScopedConnection`、`Event::type_to_string()` |
| D4 | QML: `BaseQuantCard.qml` 中 `getStatusText/getCategoryIcon/getStatusColor/formatMetricValue` 提取为 JS 模块 |
| D5 | `MarketDataRepository` — 模板方法 `executeAndMap<T>(sql, mapper)` |
| D6 | `StoredStrategyBacktest` — 40+ 字段分组为 `PerformanceMetrics`, `RiskMetrics`, `StrategyParams` 子 struct |
| D7 | 硬编码配置值 → ConfigManager 读取（队列大小、重试次数、超时等） |
| D8 | 中文显示字符串 → 资源文件 `strings_zh.qm` |

---

## 实施依赖与并行策略

```
Phase A (P0 安全)
  ├─ A1 密码 ────────── 独立
  ├─ A2 读锁 ────────── 独立
  ├─ A3 quoteCache 锁 ─ 独立
  ├─ A4 DB 返回值 ───── 独立
  ├─ A5 EventBus ────── 独立
  └─ A6 ConfigManager ─ 独立
        │
        ▼  ← 全部可并行，涉及不同文件
        │
Phase B (P0 封装)
  ├─ B1 GmSessionEngine ── 独立
  ├─ B2 FactorBacktest ─── 独立
  ├─ B3 TradingBridges ─── 独立
  ├─ B4 Qt 解耦 ────────── 依赖 IStrategyRepository 接口（影响大）
  ├─ B5 QML 拆分 ───────── 独立
  └─ B6 syncDailyRange ─── 依赖 C3/C4（提取 buildGmSymbolMap + BulkUpserter 后）
        │
        ▼
Phase C (P1 重复消除)
  ├─ C1 日期格式化 ──── 独立，影响范围大
  ├─ C2 DB 守卫 ─────── 独立
  ├─ C3 GmSymbol map ── 独立
  ├─ C5 fromGm/toGm ─── 独立
  ├─ C8 void* ───────── 独立
  ├─ C9 unique_ptr ──── 独立
  ├─ C10 atomic ─────── 独立
  ├─ C12 佣金/税率 ──── 独立
  └─ C4/C6/C7/C11/C13-17 ─ 部分有依赖（如 B6 依赖 C3/C4）
        │
        ▼
Phase D — 全部独立，择机
```

**最大并行度**: Phase A 全部 6 项 + Phase C 前 8 项可同时推进（涉及不同文件/模块）。

---

## 风险控制

| 阶段 | 最大风险 | 控制措施 |
|------|---------|---------|
| A2/A3/A7/A10 | 锁引入死锁 | 锁顺序文档化 + TSAN 验证 |
| B4 | Qt 解耦破坏策略 CRUD | Step-by-step：先定义纯 C++ 类型→改 repository→改 bridge，每步验证 |
| C1 | 日期公式不一致 → 回测结果变化 | 先验证 `%04d-%02d-%02d` 与 `formatTradingDay` 产出逐字节一致 |
| C7 | 技术指标数值漂移 | 提取前录制 100 组 (input→output)，提取后对比 |
| C11 | backtest 拆分引入逻辑错误 | 拆分前后同一参数跑回测，结果逐行 diff |
| D1 | 字符串→enum 改漏 | 全项目 grep 原字符串确认无遗漏 |
