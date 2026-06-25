# TradePanel 新交易组件设计方案

> 日期: 2026-06-26
> 状态: 设计阶段

---

## 一、架构原则

| 原则 | 说明 |
|------|------|
| **纯 QML 渲染** | 所有 UI 元素用 QML 声明式布局，属性绑定驱动更新 |
| **JS 只做纯计算** | 格式化、数值转换等纯函数置于 `.js` 文件（`.pragma library`），不参与 UI 创建 |
| **C++ 做业务** | 下单校验、价格计算、订单管理由 Bridge 层负责 |
| **属性注入** | 组件不主动拉数据，所有数据通过 property 从外部注入 |
| **单一职责** | TradePanel 只做下单，持仓/策略状态由页面层管理 |

---

## 二、组件树

```
TradePanel.qml                          ← 入口组件，管理 displayDensity 切换
│
├── TradeSymbolBar.qml                  ← 代码输入 + 名称 + 现价（始终可见）
│   ├── TextField (代码输入 + 搜索联想)
│   ├── Text (股票名称，来自 StockNameResolver)
│   ├── Text (现价)
│   └── Text (涨跌幅 + 涨跌额，红绿色)
│
├── TradeOrderSheet.qml                 ← 下单参数表单（compact/full 模式）
│   ├── ComboBox (价格类型: 市价/限价)
│   ├── RowLayout
│   │   ├── Button [-] 
│   │   ├── TextField (价格输入，限价时可用)
│   │   └── Button [+]
│   ├── RowLayout
│   │   ├── Repeater → Button (快捷数量: 1/4仓, 半仓, 全仓)
│   │   └── TextField (数量输入)
│   └── TradeFeePreview.qml             ← 子组件: 费用预估
│       ├── Text (预计金额)
│       ├── Text (佣金)
│       └── Text (印花税)
│
├── TradeQuickActions.qml               ← 买卖按钮（始终可见）
│   ├── Button "买入" (红色)
│   └── Button "卖出" (绿色)
│
└── TradePendingList.qml                ← 委托队列（full 模式可见）
    ├── ListView
    │   └── delegate: TradePendingRow.qml
    │       ├── Text (方向标签)
    │       ├── Text (代码)
    │       ├── Text (数量 @ 价格)
    │       ├── Text (状态)
    │       └── Button "撤单" (条件可见)
    └── Text "暂无委托" (空状态)
```

### 可选扩展（默认不加载）

```
TradeDepthPanel.qml                     ← 盘口五档
    ├── Text "买盘"
    ├── Repeater → 五档买盘行
    ├── Text "卖盘"
    └── Repeater → 五档卖盘行
```

---

## 三、组件 API 定义

### 3.1 TradePanel.qml

```qml
// ── 输入属性 ──
property string symbol                    // 外部绑定标的代码 "000001.SZ"
property var marketSnapshot               // {price, changePct, changeAmt, high, low, open, preClose}
property var pendingOrders                // 委托列表
property string displayDensity: "full"    // "mini" | "compact" | "full"
property bool showDepth: false            // 是否加载盘口扩展

// ── 订单校验与费率（P2 补全）──
property var accountSnapshot: null
property var feeRate: ({ commission: 0.0003, stampTax: 0.001, minCommission: 5.0 })
property string lastError: ""
property bool showError: false

// ── 输出信号 ──
signal orderRequested(var payload)
// payload: {side: "buy"|"sell", priceType: "market"|"limit", price: double, quantity: int}

signal cancelRequested(string orderId)

// ── 内部属性（由子组件双向绑定）──
property string priceType: "market"       // "market" | "limit"
property double orderPrice: 0.0
property int orderQuantity: 0
```

### 3.2 TradeSymbolBar.qml

```qml
// ── 输入 ──
property string symbol
property var marketSnapshot

// ── 输出 ──
signal symbolChanged(string newSymbol)    // 用户选择新标的
```

### 3.3 TradeOrderSheet.qml

```qml
// ── 输入 ──
property string priceType                 // 双向绑定
property double orderPrice                // 双向绑定
property int orderQuantity                // 双向绑定
property var marketSnapshot               // 用于参考价格
property string displayDensity            // 控制紧凑/完整布局

// ── 输出 ──
signal quickFillRequested(string level)   // 快捷填单: "quarter"|"half"|"full"
```

### 3.4 TradeQuickActions.qml

```qml
// ── 输入 ──
property string priceType
property double orderPrice
property int orderQuantity
property bool canSubmit: true             // 外部控制是否可下单

// ── 输出 ──
signal buyRequested()
signal sellRequested()
```

### 3.5 TradePendingList.qml

```qml
// ── 输入 ──
property var orders                        // 委托列表

// ── 输出 ──
signal cancelRequested(string orderId)
```

### 3.6 TradeDepthPanel.qml

```qml
// ── 输入 ──
property string symbol
property var depthSnapshot                // {bids: [{price, volume}], asks: [{price, volume}]}
```

---

## 四、数据流

```
外部（TradingPage / Dashboard）
  │
  ├─ symbol          ← 用户选择 / 页面绑定
  ├─ marketSnapshot  ← Bridge.MarketDataBridge
  ├─ pendingOrders   ← Bridge.TradeExecutionBridge
  │
  ▼
TradePanel.qml
  │
  ├─ symbol → TradeSymbolBar.symbol
  │    └─ symbolChanged → TradePanel.symbol (回传)
  │
  ├─ marketSnapshot → TradeSymbolBar.marketSnapshot
  │                 → TradeOrderSheet.marketSnapshot
  │
  ├─ priceType ↔ TradeOrderSheet.priceType
  ├─ orderPrice ↔ TradeOrderSheet.orderPrice
  ├─ orderQuantity ↔ TradeOrderSheet.orderQuantity
  │
  ├─ TradeQuickActions
  │    ├─ buyRequested → TradePanel 构建 payload → orderRequested(payload)
  │    └─ sellRequested → 同上
  │
  └─ pendingOrders → TradePendingList.orders
       └─ cancelRequested → TradePanel.cancelRequested(id)
```

**关键原则**：TradePanel 内部子组件之间通过属性绑定通信，不通过 JS 函数调用。父级（TradingPage）通过 `onOrderRequested` 信号接收下单请求，转发给 C++ Bridge。

---

## 五、displayDensity 三档布局

### 5.1 mini（宽度 < 280px）

```
┌─────────────────────────┐
│ TradeSymbolBar          │  ← 代码 + 现价，单行
├─────────────────────────┤
│ [买入]          [卖出]   │  ← QuickActions 水平排列
└─────────────────────────┘
```

- SymbolBar 只显示代码和现价，隐藏名称和涨跌幅
- QuickActions 按钮缩小，不带额外文字
- OrderSheet 隐藏

### 5.2 compact（宽度 280-500px）

```
┌─────────────────────────────┐
│ TradeSymbolBar              │  ← 名称 + 代码 + 现价 + 涨跌
├─────────────────────────────┤
│ 市价/限价 [▼]  ¥[____]  [+][-] │  ← OrderSheet 单行
│ 数量 [1/4][1/2][全] [____]    │
│ 预估 ¥xxx  佣金 ¥x  印花 ¥x   │  ← FeePreview
├─────────────────────────────┤
│ [买入]              [卖出]   │
└─────────────────────────────┘
```

### 5.3 full（宽度 > 500px）

```
┌──────────────────────────────────┐
│ TradeSymbolBar                   │  ← 完整信息，含今开昨收最高最低
├──────────────────────────────────┤
│ TradeOrderSheet                  │  ← 完整表单，宽松间距
│   价格类型: 市价 [▼]  限价 [▼]    │
│   价格:  [25.68]  [-] [+]        │
│   数量:  [1/4仓] [半仓] [全仓]    │
│         [1000]  ￥25,680         │
│   费用: 佣金￥7.70  印花税￥25.68  │
│   预计: ￥25,713.38              │
├──────────────────────────────────┤
│ [ 买 入 ]        [ 卖 出 ]      │
├──────────────────────────────────┤
│ TradePendingList                 │  ← 委托队列
│  ● BUY 000001.SZ 1000@25.68 已报 │
│  ● SELL 600000.SH 500@18.50 部成 │
└──────────────────────────────────┘
│ TradeDepthPanel (showDepth=true) │  ← 可选，底部附加
└──────────────────────────────────┘
```

---

## 六、JS 工具函数（.pragma library）

文件：`trading/TradeUtils.js`

```js
.pragma library

// 纯函数，仅做数据转换，不触碰 QML 组件

function formatPrice(price, digits) { ... }        // 价格格式化
function formatAmount(amount) { ... }               // 金额格式化 "1.23万" "123.45亿"
function formatPercent(pct) { ... }                 // 百分比 "+1.23%"
function computeFee(price, qty, rate) { ... }       // 费用计算 → {commission, tax, total}
function orderStatusLabel(status) { ... }           // 状态码 → 中文标签
function priceColor(changePct) { ... }              // 涨跌颜色
```

---

## 七、C++ Bridge 依赖

| Bridge | 方法 | 用途 |
|--------|------|------|
| `MarketDataBridge` | `resolveInstrument(symbol)` | 获取行情快照 |
| `TradeExecutionBridge` | `submitBridgeOrder(payload)` | 提交委托 |
| `TradeExecutionBridge` | `cancelOrder(orderId)` | 撤单 |
| `StockNameResolver` | `displayName(symbol)` | 代码 → "名称 代码" |
| `SymbolSearchModel` | `search(keyword)` | 代码输入联想 |

**不依赖**：`PositionAccountBridge`、`TradingConnectionConfigService`、持仓数据。

---

## 八、文件清单

| 文件 | 预估行数 | 说明 |
|------|---------|------|
| `TradePanel.qml` | ~100 | 入口，密度切换，信号转发 |
| `TradeSymbolBar.qml` | ~150 | 代码搜索 + 行情摘要 |
| `TradeOrderSheet.qml` | ~200 | 价格/数量表单 + FeePreview |
| `TradeQuickActions.qml` | ~60 | 买卖按钮 |
| `TradePendingList.qml` | ~120 | 委托队列 |
| `TradePendingRow.qml` | ~80 | 单条委托行 |
| `TradeDepthPanel.qml` | ~120 | 盘口五档（可选） |
| `trading/TradeUtils.js` | ~80 | 纯工具函数 |
| **合计** | **~910** | vs 当前 TradeFormPanel 2376 + 部分 TradingPage 3832 |

---

## 九、实施顺序

| 阶段 | 内容 | 产出 |
|------|------|------|
| **P1** | `TradeSymbolBar` + `TradeQuickActions` + `TradePanel`(mini) | 能输入代码、看现价、市价买卖 |
| **P2** | `TradeOrderSheet` + `TradeFeePreview` + `TradeUtils.js` | 限价单、快捷数量、费用预估 |
| **P3** | `TradePendingList` + `TradePendingRow` | 委托查看和撤单 |
| **P4** | `displayDensity` 三档自适应 | 同一组件在 Dashboard 和 TradingPage 都能用 |
| **P5** | `TradeDepthPanel`（可选） | 盘口五档扩展 |

---

## 十、预留属性（P1 阶段预埋接口，P2/P3 阶段补全）

以下属性在 P1 阶段先声明但不实现完整逻辑，避免后续破坏接口兼容性：

```qml
// ── 快捷填单依赖（P2 补全）──
property var accountSnapshot: null
// { side: "buy"|"sell", availableCash: 0, availableShares: 0 }
// 父页面按当前买卖方向动态注入。TradePanel 不主动拉取，
// 只传给 TradeUtils.js 用于计算 1/4仓/半仓/全仓。

// ── 费用计算费率（P2 补全）──
property var feeRate: ({ commission: 0.0003, stampTax: 0.001, minCommission: 5.0 })
// 由父页面从 Bridge/配置注入，传给 TradeUtils.js 的 computeFee()。

// ── 错误反馈（P2 补全）──
property string lastError: ""
property bool showError: false
// 父页面在下单失败时设置，TradePanel 底部显示红色文本，3 秒自动清除。
```

---

## 十一、P2/P3 阶段细节增强

### 11.1 下单前的本地校验职责

采用**属性变化时实时触发**模式（而非点击按钮时才校验）。`orderPrice`、`orderQuantity` 变化 → C++ `OrderValidationService` 实时计算 → 更新 `validationError` 属性 → QML 据此禁用按钮、显示提示。按钮的 `canSubmit` 绑定校验结果。

### 11.2 买卖方向切换时的状态重置

纯 QML 逻辑，不上升到 C++：`orderRequested` 发出后，或用户切换买卖方向时，清空 `orderPrice`、`orderQuantity`，重置 `priceType` 为市价。

### 11.3 费用计算的费率来源

`TradeUtils.js` 的 `computeFee(price, qty, rate)` 中的 `rate` 参数来自 `TradePanel.feeRate` 属性，由父页面从 Bridge 或配置注入，不硬编码在 JS 中。

### 11.4 错误状态的显示渠道

父页面在下单回调失败时设置 `TradePanel.lastError` 和 `showError = true`，TradePanel 底部显示红色错误文本，3 秒定时器自动清除。

---

## 十二、与现有 TradingFormPanel 的共存策略

新组件命名为 `TradePanel`（区别于现有 `TradingFormPanel`）。先在 Dashboard 侧边栏用 mini 模式验证，再逐步替换 TradingPage 中的旧面板。旧代码保留不删，直到新组件在 full 模式下完全覆盖所有功能。
