# 板块共振集成计划 v1（基于代码现状的修正版）

**状态**：🟢 有条件批准 — 待 Phase 0 验证通过后启动 Phase 1 编码

## Context

用户提出将板块共振深度植入 AI 因子的标签系统和决策系统。本计划基于对代码库的实际分析，识别当前能力与目标之间的差距，给出分阶段实施方案。本文档已经过正式评审并采纳了所有修正意见。

## 当前代码现状（关键发现）

### 已有能力
| 能力 | 状态 | 位置 |
|:---|:---|:---|
| `industry_code` 字段 | ✅ 已在 Arrow 缓存中（numeric，来自 `ref.symbol_info`） | `DataSourceRegistry.h:61` symbol_info_columns |
| `ref.industry_classification` 表 | ⚠️ PG 中表结构存在，**但 MySQL 时代为 0 行，需验证当前 PG 是否有数据** | `SchemaNames.h:10`, 导入脚本: `tools/import_sw_industry*.py` |
| 申万分类导入脚本 | ✅ 已有（akshare + 掘金两套），**但运行状态未知** | `tools/import_sw_industry.py`, `tools/import_sw_industry_juejin.py` |
| 日频行业相对收益 `industry_rel_ret` | ✅ train.py 已计算 | `train.py:39,255-261`（4 个 MARKET_FEATURES 之一） |
| 行业+市值中性化 | ✅ C++ 已实现 | `FactorNeutralizationUtils.h:13-155` |
| 分钟线原始数据 | ✅ PG `mkt.minute_bar` 有 **1 分钟** K 线（OHLCV+amount，**无 VWAP 列**） | `postgresql_schema_draft.sql:66-80`（draft 有 vwap，**生产表无**） |
| `HighFreqFactor` 三种方法 | ✅ 已实现（聪明钱/已实现矩/量价） | `HighFreqFactor.h/.cpp` — **仅用日聚合列，非真 5 分 K 线** |
| 资金流日频数据 | ✅ PG `fund.money_flow_daily` 有 20 列 | `money_flow_ddl.sql` |
| ONNX 推理链 | ✅ C++ 已实现 | `DLFactor.cpp` + `OnnxInference.cpp` + `FeatureTensorBuilder` |
| DL 训练管线 | ✅ Python train.py（12 原始特征 + 4 市场特征 + 6 派生特征） | `astock_engine/ai/train.py` |
| 板块热度实时监控 | ✅ QML `MarketDataBridge` 有 `sectorHeatThread`（30s 轮询掘金 SDK） | `MarketDataBridge.h/.cpp` |
| `EventRiskSubscriber` 板块风险 | ✅ 事件总线订阅板块级风险事件 + `capSectorExposure()` | `EventRiskSubscriber.h/.cpp` |
| 多标准行业字段常量 | ✅ 已定义（`sw_industry_1/2`, `citics_industry_1/2`, `gics_sector`） | `field_traits.h`, `FactorMetricConfig.h` `ConfigurableSectorType` |
| 归因分析板块拆解 | ✅ `AttributionAnalyzer` 按 sector 分组归因 | `AttributionAnalyzer.cpp` |
| 缠论买点系统 | ❌ **零代码** | 全代码库搜索无 `缠论`/`ChanLun`/`中枢`/`分型`/`买点` 关键词 |

### 关键差距（已核实）
| 差距 | 严重度 | 说明 |
|:---|:---|:---|
| **板块分类数据可能为空** | 🔴 **前置阻塞** | `ref.industry_classification` 在 MySQL 时代为 0 行；导入脚本虽存在但运行状态未知。**必须先验证并补数据，否则所有板块功能无从谈起。** |
| **无板块 5 分钟 VWAP 指数** | 🔴 阻塞 | 当前板块级数据只有日频 `industry_code`（来自 `symbol_info`），无分钟级板块指数 |
| **资金流仅日频** | 🔴 阻塞 | GM SDK `stk_get_money_flow` 是日频 API，**无分钟级资金流**。分钟级"板块聪明钱"只能用日频资金流+分钟量价做替代 |
| **VWAP 列在生产表中不存在** | 🔴 阻塞 | draft schema 有 VWAP，但生产 `mkt.minute_bar` 只存 OHLCV+amount，需在查询时计算或新增列 |
| **分钟线是 1 分钟粒度，非 5 分钟** | 🟡 注意 | GM SDK 获取 `"60s"` 频率，`HighFreqFactor.barFrequency=5` 只是窗口缩放参数，不重采样 |
| **分钟线不在 Arrow 缓存** | 🟡 架构 | 分钟线仅在 PG，Arrow 缓存只存了日聚合列（`open_minute` 等 5 列）。真 5 分钟 K 线需从 PG 实时查 |
| **当前标签是截面排序，非缠论买点** | 🟡 架构 | `train.py:72-80` `to_rank_labels()` 做截面排名（Rank IC 评估），无缠论结构标签 |
| **无板块环境系数决策层** | 🟡 架构 | 当前 AI 因子输出直接参与策略信号，无板块环境乘法修正 |
| **缠论系统零基础** | ⚠️ 长期 | 全代码库无缠论代码，Phase 3 的"板块中枢"需从零搭建 |

### ⚠️ 数据频率不匹配（核心矛盾）

用户的方案假设所有数据（个股聪明钱、板块资金潮汐、板块宽度）在 **5 分钟级别** 可用，但实际：
- **资金流数据**：仅日频（GM SDK 日频 API），**没有分钟级资金流** → 板块"聪明钱"只能日频
- **分钟线数据**：PG 中是 **1 分钟**粒度（非 5 分钟），且**生产表无 VWAP 列** → 需自建 5 分钟聚合 + VWAP 计算
- **板块分类**：`industry_code` 来自 `symbol_info`（日频快照），可能缺少申万二级/概念板块等细分

**→ 方案需要做频率适配：Phase 1 做日频板块特征（立即可行），Phase 2+ 逐步引入分钟级。**

---

## 前置验证（Phase 0：动手前必须确认）

这些验证步骤必须在任何编码之前完成，否则可能导致方案推翻重来。

### V0-1: 验证 `ref.industry_classification` 表是否有数据
```sql
SELECT COUNT(*), standard FROM ref.industry_classification GROUP BY standard;
```
- 如果 0 行 → 运行 `tools/import_sw_industry_juejin.py`（推荐，掘金 SDK）或 `tools/import_sw_industry.py`（akshare）
- 预期：至少申万一级 ~30 个行业、申万二级 ~100+ 个行业

### V0-2: 验证 `industry_code` 在 `ref.symbol_info` 中的覆盖率
```sql
SELECT COUNT(*) AS total,
       COUNT(*) FILTER (WHERE industry_code IS NOT NULL AND industry_code != '') AS with_industry,
       COUNT(DISTINCT industry_code) AS distinct_codes
FROM ref.symbol_info
WHERE status = 'ACTIVE';
```
- 预期：with_industry / total > 90%

### V0-3: 验证 `mkt.minute_bar` 生产表结构
```sql
SELECT column_name, data_type FROM information_schema.columns
WHERE table_schema='mkt' AND table_name='minute_bar';
```
- 确认是否有 `vwap` 列 → 如果没有，Phase 1 的板块 VWAP 需在查询中计算

### V0-4: 验证板块日频聚合的性能（⚠️ 必须用生产环境副本跑 5 天）
```sql
EXPLAIN ANALYZE
SELECT ic.industry_code,
       COUNT(DISTINCT mb.symbol_id) AS stock_count,
       SUM(mb.close * mb.volume) / NULLIF(SUM(mb.volume), 0) AS vwap,
       COUNT(*) FILTER (WHERE mb.close > mb.open) * 1.0 / NULLIF(COUNT(*), 0) AS breadth
FROM mkt.minute_bar mb
JOIN ref.symbol_info si ON mb.symbol_id = si.id
LEFT JOIN ref.industry_classification ic ON ic.symbol_id = mb.symbol_id
    AND ic.standard = 'SW' AND ic.end_date IS NULL
WHERE mb.trade_ts::date BETWEEN '2026-08-03' AND '2026-08-07'  -- 覆盖最近 5 天
GROUP BY ic.industry_code, mb.trade_ts::date;
```
- 预期：单日 < 5 秒。如果 > 30 秒 → 启用 Plan B（物化视图，见下文）
- 如果性能不达标：板块聚合走**预计算物化视图**而非实时 GROUP BY

### V0-5: 验证板块成分股数量分布（新增）
```sql
SELECT ic.industry_code, COUNT(DISTINCT si.symbol) AS cnt
FROM ref.industry_classification ic
JOIN ref.symbol_info si ON ic.symbol_id = si.id
WHERE ic.standard = 'SW' AND ic.end_date IS NULL AND si.status = 'ACTIVE'
GROUP BY ic.industry_code
ORDER BY cnt ASC;
```
- 如果某些行业 < 5 只股票 → 该板块的 VWAP/宽度/振幅统计不稳定
- 在 S1 `sector_daily_columns` 中新增 `sector_is_reliable` 布尔列（cnt >= 5 → true），供决策层判断是否采用该板块信号

### ⚠️ Plan B：兜底方案（当 Phase 0 验证失败时启用）

**B1 — 手工兜底映射表**（当 `ref.industry_classification` 为空且导入脚本也无法运行时）：
- 准备一份 `symbol -> sw_industry_level1` 手工映射，至少覆盖沪深 300 成分股
- 来源：Wind/同花顺导出、公开数据手动整理、或从 akshare `stock_board_industry_cons_em()` 抓取
- 存入 `ref.industry_classification` 表作为**临时数据源**，确保 Phase 1 开发不被完全阻塞

**B2 — 物化视图预案**（当 V0-4 聚合性能 > 30 秒时启用）：
```sql
CREATE MATERIALIZED VIEW mkt.sector_daily_agg AS
SELECT mb.trade_ts::date AS trade_date,
       ic.industry_code,
       COUNT(DISTINCT mb.symbol_id) AS stock_count,
       SUM(mb.close * mb.volume) / NULLIF(SUM(mb.volume), 0) AS sector_vwap,
       COUNT(*) FILTER (WHERE mb.close > mb.open) * 1.0 / NULLIF(COUNT(*), 0) AS sector_breadth,
       (MAX(mb.high) - MIN(mb.low)) / NULLIF(AVG(mb.open), 0) AS sector_amplitude,
       SUM(mb.amount) AS sector_turnover
FROM mkt.minute_bar mb
JOIN ref.symbol_info si ON mb.symbol_id = si.id
LEFT JOIN ref.industry_classification ic ON ic.symbol_id = mb.symbol_id
    AND ic.standard = 'SW' AND ic.end_date IS NULL
WHERE ic.industry_code IS NOT NULL
GROUP BY mb.trade_ts::date, ic.industry_code;

CREATE UNIQUE INDEX idx_sector_daily_agg ON mkt.sector_daily_agg(trade_date, industry_code);
```
- 每日收盘后 `REFRESH MATERIALIZED VIEW CONCURRENTLY mkt.sector_daily_agg;`
- `MarketDataRepository::querySectorDailyAgg()` 走物化视图读取，而非实时 GROUP BY

---

## 修正后的分阶段方案

### Phase 1: 板块日频数据基础设施（v0.17.0 前半）

本阶段不引入分钟级新数据源，仅利用现有日频 Arrow 缓存 + PG 资金流 + `symbol_info.industry_code`，构建板块日频聚合特征。

#### 1.1 板块日频聚合表

**核心思路**：从现有数据计算板块级日频指标，注入 Arrow 缓存（与 money_flow 同模式）。

**产出列**（约 9 列，注入 Arrow 缓存，按 `(symbol, trade_date)` 与日线行对齐）：
```
sector_vwap_change        -- 板块 VWAP 加权涨跌幅（从 minute_bar 日聚合算 VWAP）
sector_breadth            -- 板块内上涨家数占比（close>open 家数 / 总家数）
sector_is_reliable        -- 板块统计是否可靠（成分股 >= 5 → true，否则该板块信号不采用）
sector_money_flow_net     -- 板块聪明钱净流入 = 个股 main_net_in 求和（注意：负值 = 主力净流出，环境系数应降权）
sector_money_flow_ratio   -- 板块聪明钱净流入 / 板块总成交额
sector_amplitude          -- 板块 VWAP 日内振幅（(high-low)/open）
sector_relative_strength  -- 板块相对大盘的涨跌幅差（基准：全市场等权 VWAP 指数或 000985.SH 中证全指，非沪深 300）
sector_concentration      -- 板块内 Top3 成交额 / 全板块成交额（NULLIF 防护，成分股 < 3 只时返回 NULL）
sector_turnover_ratio     -- 板块当日成交额 / 20日均成交额
```

**数据来源与 SQL**：
- VWAP/涨跌幅/宽度/振幅 → 从 `mkt.minute_bar` JOIN `ref.symbol_info`，按 `trade_ts::date` + `industry_code` GROUP BY
- ⚠️ VWAP 在查询中计算（`SUM(close*volume)/SUM(volume)`），生产表无 `vwap` 列
- 资金流 → 从 `fund.money_flow_daily` JOIN `ref.symbol_info`，按 sector 求和
- 大盘基准 → **全市场等权 VWAP 指数**或 **000985.SH 中证全指**（避免沪深 300 的大盘股偏差）
- ⚠️ **日期对齐**：`fund.money_flow_daily.trade_date` 与 `mkt.minute_bar.trade_ts::date` 统一使用 `date(col at time zone 'Asia/Shanghai')` 进行对齐，JOIN 条件明确写 `ON date_a = date_b`，不依赖隐式转换

**实现方式**：
- 新增 `MarketDataRepository::querySectorDailyAgg()` 方法 → 查 minute_bar 日聚合
- 新增 `MarketDataRepository::querySectorMoneyFlowAgg()` 方法 → 查 money_flow 聚合
- 新增 `DataSourceRegistry.h` 中 `sector_daily_columns` namespace
- 在 `RawMarketDataAssembler` 中注入 sector 列（仿照 money_flow 注入模式）

#### 1.2 相对 Alpha 标签（train.py 修改）

将标签从"绝对收益截面排名"改为"相对板块的超额收益截面排名"。

**当前**（`train.py:304`）：
```python
y_val_i = fut_c / now_c - 1.0  # 未来20日绝对收益率
```

**修改后**：
```python
stock_ret = fut_c / now_c - 1.0
sector_ret = sector_future_returns[industry_code].mean()  # 板块均值
alpha = stock_ret - sector_ret  # 个股超额收益
y_val_i = alpha  # 标签变为 Alpha，而非绝对收益
```

**影响**：
- train.py 数据加载新增 sector 聚合列（从 Arrow 缓存读取）
- 标签变成"预测谁跑赢板块"——这正是用户方案的核心价值
- 模型架构不变

#### 1.3 板块环境系数（决策层）

在 `FactorSignalProcessor.cpp` 中引入板块环境系数乘法修正。

**架构设计**：系数采用**可配置参数表**（从数据库或配置文件加载），支持按板块类型和行情状态分层设置。Phase 1 先用硬编码默认值，但代码架构上预留"外部参数注入"接口，避免回测过拟合后难以调参。

**参数表结构**（建议 PG 表或 JSON 配置）：
```json
{
  "sectorEnvCoeff": {
    "strong_resonance":  { "breadthMin": 0.7, "moneyFlowSign": "positive", "coeff": 1.3 },
    "weak_resonance":    { "breadthMax": 0.3, "moneyFlowSign": "negative", "coeff": 0.5 },
    "extreme_divergence": { "breadthMax": 0.3, "relStrengthMin": 0.02, "consecutiveBarsMin": 3, "coeff": 1.2 },
    "neutral":           { "coeff": 1.0 }
  }
}
```

**伪代码**：
```cpp
double sectorEnvCoeff = loadSectorEnvCoeff(sectorCode, marketState);
if (!sectorIsReliable) {
    sectorEnvCoeff = 1.0;  // 成分股 < 5 的板块，不采用板块信号
}
finalScore = aiFactorScore * sectorEnvCoeff;
```

---

### Phase 2: 板块分钟级特征（v0.17.0 后半 / v0.18.0）

本阶段引入分钟级板块指数实时计算。**注意：由于资金流仅日频，分钟级"板块聪明钱"只能用日频资金流+分钟量价替代。**

#### 2.1 板块 5 分钟 K 线 VWAP 实时计算

**技术现实**：
- `mkt.minute_bar` 存储**1 分钟** K 线（`"60s"` 频率），**生产表无 `vwap` 列**
- 需要：1 分钟 → 5 分钟重采样 + VWAP 计算（`SUM(close*volume)/SUM(volume)`）
- ⚠️ 重采样必须按交易时间每 5 分钟切分（9:30-9:35, 9:35-9:40...），**不能简单用 `HighFreqFactor.barFrequency=5` 缩放参数**——那是窗口缩放参数，不产生真正的 5 分钟 K 线

**方案**：
- 在 `DataCache` 中新增 `MinuteBarCache` 分区（按月存储，Arrow IPC 格式）
- 当日分钟线实时缓存 + 历史分钟线按需从 PG 回填
- 5 分钟重采样在 `MinuteBarCache` 加载时完成（在 C++ 侧实现 5 分钟切分逻辑）
- 因子计算时从 `MinuteBarCache` 按 symbol+date 批量加载 5 分钟 K 线，在内存中构建板块 VWAP 指数

#### 2.2 板块共振特征（注入 FeatureTensorBuilder）

新增 15-20 维板块共振特征。**日频版本在 Phase 1 中已注入 Arrow 缓存，Phase 2 新增的是分钟级特征：**

```cpp
// 分钟级新增字段（需从 MinuteBarCache 实时计算）
"relative_strength_5m",      // 个股5分钟涨幅 - 板块5分钟涨幅
"sector_breadth_5m",         // 板块内上涨家数占比(每5分钟快照)
"sector_rank_pct_5m",        // 个股在板块内的涨幅排名分位(0~1)
"sector_corr_20bar",         // 个股-板块收益率相关系数(滚动20根5分钟K线)
"sector_vwap_deviation",     // 当前价格偏离板块VWAP的百分比

// 日频特征（来自 Phase 1 的 Arrow 缓存列，作为分钟模型的上下文）
"sector_money_flow_net",     // 板块聪明钱日频(最近N日)
"sector_breadth_daily",      // 板块宽度(日频尾盘)
```

**实现**：`FeatureTensorBuilder` 需要扩展以支持分钟级数据源（`MinuteBarCache`），当前只支持 `HistoricalView`（日频 Arrow 缓存）。

#### 2.3 板块中枢位置（缠论适配）⚠️ 移至 Phase 3a

原方案要求对板块指数做缠论中枢识别。**当前代码库无任何缠论代码。**

此功能已从 Phase 2 移出到 Phase 3a，Phase 2 仅做分钟级量价特征计算。详见 Phase 3a 章节。

---

### Phase 3a: 缠论基础特征离线注入（v0.18.0）

⚠️ **工作量修正**：缠论核心库从原估 500-800 行修正为 **~1500-2000 行**。原因：跳空、涨停、一字板等 A 股特有边界情况在"包含关系处理 + 分型识别"阶段就需要大量防御代码；缠论参数（分型包含的 K 线根数、笔的成笔条件、线段破坏判定规则）在实盘中必须可配置，增加复杂度。

**方案**：
- 纯 Python 离线计算（不在 C++ 实时推理中做缠论）
- 实现：包含关系处理 → 顶底分型 → 笔 → 线段 → 中枢
- 结果以特征列注入 Arrow 缓存（与 Phase 1 同模式）：
  - `sector_zhongshu_position` — 当前价格在板块中枢上/下/内部的标准化位置
  - `sector_zhongshu_strength` — 板块中枢的支撑/压力强度
  - `stock_chan_buy_point` — 个股缠论买点类型（0=无, 1=一买, 2=二买, 3=三买）
- 这些特征先给日频模型用（相当于多了一组高阶因子），验证有效后再进入 Phase 3b

---

### Phase 3b: 分钟 AI 模型 + 双周期耦合（v0.19.0+，视 3a 验证结果延后评估）

#### 3b.1 分钟级 label 生成

对 5 分钟 K 线序列做 Alpha 标签（未来 3 根 5 分钟 K 线的超额收益）：
- **日频资金流替代分钟级聪明钱**：用当日板块资金流方向作为"板块资金态度"
- 分钟量价替代：用 `relative_strength_5m` + `sector_breadth_5m` 判断"个股是否在领涨"

```python
alpha_3bar = stock_3bar_ret - sector_3bar_ret
if alpha_3bar > 0.005 and daily_money_flow_net > 0:
    label = +1  # 龙头买点
elif alpha_3bar < -0.005 and daily_money_flow_net < 0:
    label = -1  # 跟风溃败
else:
    label = 0   # 随波逐流（中性）
```

#### 3b.2 双周期融合

- 日频 AI 因子 → `dailyScore`：决定"今天要不要在这个板块里买"
- 分钟 AI 因子 → `minuteScore`：决定"现在是不是入场时机"
- 融合层：`finalConfidence = dailyScore * 0.6 + minuteScore * 0.4 * sectorEnvCoeff`

---

## 改动清单（Phase 1 可执行任务）

### P0 阻塞项（Phase 1 必须完成）

| ID | 任务 | 文件 | 说明 |
|:---|:---|:---|:---|
| S1 | `sector_daily_columns` namespace | `DataSourceRegistry.h` | 定义 8 个板块日频列的 names()/numeric()/sqlSelect() |
| S2 | `MarketDataRepository::querySectorDailyAgg()` | `MarketDataRepository.h/.cpp` | 从 minute_bar 按 sector 日聚合 VWAP/涨跌幅/宽度/振幅 |
| S3 | `MarketDataRepository::querySectorMoneyFlowAgg()` | `MarketDataRepository.h/.cpp` | 从 money_flow_daily 按 sector 日聚合资金流 |
| S4 | `RawMarketDataAssembler` 板块注入块 | `RawMarketDataAssembler.cpp` | 仿照 money_flow 注入模式，注入 sector 列到日频 Arrow |
| S5 | dataTypes 检测：增量 + 全量 | `DataCleaningServiceRefactored.cpp` + `DataFetchController.cpp` | 仿照 money_flow 的 P0-3/P0-4 检测逻辑 |
| S6 | train.py Alpha 标签修改 | `astock_engine/ai/train.py` | 标签从绝对收益改为相对板块超额收益 + 加载 sector 列 |
| S7 | `FeatureTensorBuilder` 新增 sector 字段 | `FeatureTensorBuilder.h/.cpp` | 建议新增**动态列注册机制**（而非硬编码 FEATURE_FIELDS），后续 Phase 2/3 新增特征时只需改配置，不需重编译 C++ 核心 |
| S8 | 板块环境系数决策层 | `FactorSignalProcessor.cpp` | 引入 sectorEnvCoeff 乘法修正 AI 因子得分 |

### P1 优化项

| ID | 任务 | 说明 |
|:---|:---|:---|
| S9 | `DataCacheAdapter` sector 查询去重 | 将内嵌 SQL 迁移到 MarketDataRepository（与 money_flow P1-7 同模式） |
| S10 | 板块分类数据校验脚本 | Python 验证 industry_code 覆盖率 + 板块成分股数量分布 |

### 不改的文件

- `DLFactor.h/.cpp` — 模型架构不变，仅输入字段扩展（通过 FeatureTensorBuilder）
- `HighFreqFactor.h/.cpp` — Phase 2 再改
- `OnnxInference.cpp` — 输入维度自动从 schema 推断
- `DataSourceRegistry.h` 中 kline/financial/money_flow 等已有 namespace — 不改

---

## 与当前 v0.16.0 管线任务的调度

当前 v0.16.0 正在执行 money_flow 接入增量更新管线（`doc/money-flow-pipeline-integration.md` 9 项任务，**P0-1 已开始但暂停，等待用户指令继续**）。

### 调度决策：串行，不并行

> **不要并行推进，而是串行完成 money_flow 管线后再开启板块共振 Phase 1。**

**原因**：
1. money_flow 的 `RawMarketDataAssembler` 注入模式、dataTypes 检测逻辑、`MarketDataRepository` 查询模式——板块共振 Phase 1 要**完全复用**。如果 money_flow 还没调完接口，板块共振的代码写出来也要回头改
2. 两个任务同时修改 `DataSourceRegistry.h` 和 `RawMarketDataAssembler.cpp`，会产生大量合并冲突
3. **Phase 0 验证可以在 money_flow 开发期间并行执行**（不冲突），验证通过 + money_flow 管线合入后，全力推进 Phase 1 编码

### 推荐执行顺序

```
现在 → money_flow 管线（9项任务）完成
      │
      ├─（并行）Phase 0 验证（5项SQL验证 + Plan B准备）
      │
      ↓
money_flow 合入验证
      ↓
Phase 0 验证通过确认
      ↓
Phase 1 编码（S1-S10）
      ↓
Phase 1 验证（AUC > 0.65 相对板块 Alpha 预测目标）
      ↓
Phase 2（分钟级特征 + MinuteBarCache）
      ↓
Phase 3a（缠论离线特征注入）
      ↓
Phase 3b（分钟 AI 模型 + 双周期融合，视 3a 结果延后评估）
```

---

## 频率适配说明

用户原方案假设所有数据在 5 分钟级别可用，但实际：
1. **资金流** 仅日频 → Phase 1 用日频资金流做板块聚合，Phase 2 用分钟 VWAP+volume 做替代
2. **板块宽度** → Phase 1 从分钟线日聚合（单值/日），Phase 2 做到每 5 分钟更新
3. **板块中枢** → Phase 1 不做（需要缠论识别+分钟线全量加载），Phase 3 在 Python 离线计算
4. **Alpha 标签** → Phase 1 用日频超额收益做截面排名标签，Phase 3 做分钟级（未来 3 根 5 分钟 K 线）

**最小可行产品（MVP）**：Phase 1 完成后，日频 AI 因子就能区分"龙头"和"杂毛"，让模型学会预测"相对板块的超额收益"而非"绝对收益"。

---

## 方案修正总结

| 对比项 | 用户原方案（纯分钟级） | 本修正方案（按频率渐进） |
|:---|:---|:---|
| **板块指数** | 5分钟 VWAP 实时计算 | Phase 1: 日频聚合（从1分钟线GROUP BY） → Phase 2: 5分钟重采样+实时 |
| **资金流频率** | 分钟级板块聪明钱 | **不可行**（GM SDK仅日频） → 日频资金流 + 分钟量价替代 |
| **特征维度** | 40-45维（含分钟板块共振） | Phase 1: 日频22→约31维（+9板块列含可靠性标记） → Phase 2: +15分钟维 |
| **标签逻辑** | 未来3根5分钟K线Alpha | Phase 1: **日频Alpha**（未来20日超额收益排名） → Phase 3b: 分钟级 |
| **缠论适配** | 板块中枢上下沿引力 | Phase 1/2: **不做**（零基础设施） → Phase 3a: Python离线缠论特征注入 → Phase 3b: 分钟AI融合 |
| **决策层** | 板块环境系数(0~1.5) | Phase 1: **可做**（日频板块系数，可配置参数表，`FactorSignalProcessor` 乘法修正） |
| **前置条件** | 无 | Phase 0: 5项验证 + Plan B（手工映射表 + 物化视图） |
| **可行性** | 直接落地有 4 个阻塞 | Phase 1 可立即落地（无新数据依赖） |
| **AUC 目标** | > 0.65（个股相对板块 Alpha 预测） | Phase 1 日频 Alpha 预测目标（当前绝对收益 Rank IC ~0.27） |

**关键改动**：将用户的"分钟级全量方案"拆解为 Phase 1 → 2 → 3a → 3b 四个阶段，每个阶段产出可验证的增量价值。Phase 1 的 MVP 不需要任何新数据源——所有数据都已在 PG 中可用。
