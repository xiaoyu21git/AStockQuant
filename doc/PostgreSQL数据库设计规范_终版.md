# PostgreSQL 量化数据库设计规范 — 终版

> **ℹ️ (2026-07)**: MySQL 已退役，本文档中 MySQL 对比内容为历史参考。PostgreSQL 为主库，本文档是当前权威规范。

## 设计原则

1. **对齐代码现实**：字段名、类型、主键必须与现有 C++ 代码的读取方式一致，不制造无意义的适配层
2. **一个事实一个来源**：`ref.symbol_info.symbol` 是标的唯一业务标识，全局用 `symbol_id bigint FK` 关联，避免字符串 JOIN
3. **不迁空表**：现存 MySQL 中 row_count=0 的表不迁移，用到再建
4. **预留不冗余**：当前无代码写入但属于量化基础框架的表（如 tick、orderbook、因子值、组合净值），建表结构但数据为空，避免未来返工重建
5. **TimescaleDB**：行情表全部 hypertable，原生压缩 + 自动分区

---

## 一、Schema 划分（7 个）

| Schema | 内容 | 说明 |
|---|---|---|
| `ref` | 标的、日历、行业分类、指数成分股 | 参考数据 |
| `mkt` | 日线、周线、月线、分钟线、Tick、订单簿、复权因子 | 行情时序（TimescaleDB） |
| `fund` | 财务指标日频、分红拆股 | 基本面 |
| `alpha` | 因子实例、模板、参数、分类、标签、因子值、信号 | 因子元数据 + 运行时 |
| `live` | 策略定义、订单、成交、持仓、风控参数 | 交易链路 |
| `port` | 组合净值、持仓明细 | 组合管理（预留） |
| `data` | 数据质量日志 | 数据治理（预留） |

---

## 二、核心表结构

### `ref` — 参考数据

#### `ref.symbol_info`
现存 MySQL: `symbol_info`，5825 行

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `id` | `bigserial` | **PK** | 全局唯一标的 ID |
| `symbol` | `varchar(20)` | **UNIQUE NOT NULL** | 如 `"000001.SZ"`、`"600000.SH"` |
| `name` | `varchar(100)` | NOT NULL | 中文简称 |
| `exchange` | `varchar(10)` | | SZSE / SHSE / BSE |
| `asset_class` | `varchar(20)` | NOT NULL | STOCK / ETF / INDEX / FUND |
| `list_date` | `date` | | 上市日期 |
| `delist_date` | `date` | | 退市日期 |
| `status` | `varchar(20)` | NOT NULL | ACTIVE / DELISTED / ST / \*ST / SUSPENDED |
| `industry` | `varchar(100)` | | 行业名称 |
| `created_at` | `timestamptz` | DEFAULT now() | |
| `updated_at` | `timestamptz` | DEFAULT now() | |

所有关联表（daily_bar、financial_indicator、index_constituents、factor_values、signals 等）用 `symbol_id bigint REFERENCES ref.symbol_info(id)` 替代 `symbol varchar(20)`。

#### `ref.trade_calendar`
现存 MySQL: **不存在**，新增

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `trade_date` | `date` | **PK** | |
| `is_trading_day` | `boolean` | DEFAULT true | 预留非交易日标记（半天市等） |
| `created_at` | `timestamptz` | DEFAULT now() | |

初始化：因 `trade_calendar` 在 `daily_bar` 之前建表，初始写入空表。PG 迁移完成后，执行：
```sql
INSERT INTO ref.trade_calendar SELECT DISTINCT trade_date FROM mkt.daily_bar ORDER BY trade_date;
```
后续 Python 日线脚本每写入一笔新日期，同步插入 `ref.trade_calendar`。

#### `ref.index_info`
现存 MySQL: `index_info`，12 行

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `symbol` | `varchar(20)` | **PK** | 指数代码，如 `"000001.SH"` |
| `name` | `varchar(100)` | NOT NULL | 指数名称 |
| `created_at` | `timestamptz` | DEFAULT now() | |

#### `ref.index_constituents`
现存 MySQL: `index_constituents`，694,847 行

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `index_symbol` | `varchar(20)` | NOT NULL | FK → `ref.index_info.symbol` |
| `stock_symbol_id` | `bigint` | NOT NULL | FK → `ref.symbol_info(id)` |
| `effective_date` | `date` | NOT NULL | 纳入日期 |
| `weight` | `numeric(10,6)` | | 权重 |
| `is_current` | `smallint` | DEFAULT 1 | 1=当前成分股 |

索引：`(index_symbol, stock_symbol_id)` UNIQUE

#### `ref.industry_classification`
现存 MySQL: `industry_classification`，0 行。预留。

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `symbol_id` | `bigint` | NOT NULL | FK → `ref.symbol_info(id)` |
| `industry_code` | `varchar(20)` | NOT NULL | |
| `industry_name` | `varchar(100)` | | |
| `standard` | `varchar(20)` | DEFAULT 'SW' | 分类标准：SW / GICS / CSI |
| `effective_date` | `date` | | |
| `end_date` | `date` | | |

---

### `mkt` — 行情时序（全部 TimescaleDB hypertable）

#### `mkt.daily_bar`
现存 MySQL: `daily_bar`，1068 万行，4.3GB。表结构与 MySQL 一致，只做类型映射和 schema 迁移。

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `symbol_id` | `bigint` | NOT NULL | FK → `ref.symbol_info(id)` |
| `trade_date` | `date` | NOT NULL | |
| `open` | `numeric(12,4)` | NOT NULL | |
| `high` | `numeric(12,4)` | NOT NULL | |
| `low` | `numeric(12,4)` | NOT NULL | |
| `close` | `numeric(12,4)` | NOT NULL | |
| `pre_close` | `numeric(12,4)` | | |
| `volume` | `bigint` | DEFAULT 0 | |
| `turnover` | `numeric(20,4)` | DEFAULT 0 | 成交额 |
| `change_pct` | `numeric(8,4)` | | 涨跌幅 % |
| `change_amt` | `numeric(12,4)` | | 涨跌额 |
| `amplitude` | `numeric(8,4)` | | 振幅 % |
| `turnover_rate` | `numeric(8,4)` | | 换手率 % |
| `pe_ratio` | `numeric(10,4)` | | 市盈率 |
| `pb_ratio` | `numeric(10,4)` | | 市净率 |
| `market_cap` | `numeric(20,4)` | | 总市值 |
| `circulating_market_cap` | `numeric(20,4)` | | 流通市值 |
| `pre_adjust_factor` | `numeric(20,8)` | | 前复权因子 |
| `post_adjust_factor` | `numeric(20,8)` | | 后复权因子 |
| `data_source` | `varchar(50)` | DEFAULT 'UNKNOWN' | |
| `created_at` | `timestamptz` | DEFAULT now() | |
| `updated_at` | `timestamptz` | DEFAULT now() | |

`(symbol_id, trade_date)` UNIQUE。

```sql
CREATE TABLE mkt.daily_bar (...);
SELECT create_hypertable('mkt.daily_bar', 'trade_date', chunk_time_interval => INTERVAL '1 year');
ALTER TABLE mkt.daily_bar SET (timescaledb.compress, timescaledb.compress_segmentby = 'symbol_id');
SELECT add_compression_policy('mkt.daily_bar', INTERVAL '90 days');
ALTER TABLE mkt.daily_bar ADD CONSTRAINT daily_bar_sid_date_uniq UNIQUE (symbol_id, trade_date);
CREATE INDEX ON mkt.daily_bar(trade_date);
CREATE INDEX ON mkt.daily_bar(symbol_id);
```

#### `mkt.weekly_bar`
现存 MySQL: `weekly_bar`，918,886 行。字段与 `daily_bar` 一致（含 OHLCV、volume、turnover、复权因子），`(symbol_id, trade_date)` UNIQUE。TimescaleDB hypertable。

#### `mkt.monthly_bar`
现存 MySQL: `monthly_bar`，227,796 行。字段与 `daily_bar` 一致，`(symbol_id, trade_date)` UNIQUE。TimescaleDB hypertable。

#### `mkt.minute_bar`
现存 MySQL: `minute_bar`，7,084 行。仅迁移已存在数据。

| 列 | 类型 | 约束 |
|---|---|---|
| `symbol_id` | `bigint` | NOT NULL |
| `trade_ts` | `timestamptz` | NOT NULL |
| `open` | `numeric(12,4)` | |
| `high` | `numeric(12,4)` | |
| `low` | `numeric(12,4)` | |
| `close` | `numeric(12,4)` | |
| `volume` | `bigint` | |
| `amount` | `numeric(20,4)` | |

`(symbol_id, trade_ts)` UNIQUE。TimescaleDB hypertable（数据量增长后开压缩）。

#### `mkt.adjustment_factor`
现存 MySQL: `adjustment_factor`，400 万行。迁移，不改结构。

| 列 | 类型 | 约束 |
|---|---|---|
| `symbol_id` | `bigint` | NOT NULL |
| `trade_date` | `date` | NOT NULL |
| `pre_adjust_factor` | `numeric(20,8)` | |
| `post_adjust_factor` | `numeric(20,8)` | |

`(symbol_id, trade_date)` UNIQUE

> **复权因子关系**：`mkt.adjustment_factor` 是数据源（Python ETL 写入，400 万行），`daily_bar.pre_adjust_factor` / `daily_bar.post_adjust_factor` 由 `adjustment_factor` 在导入时填充。应用层（C++ `CachedMarketDataView`）只读 `daily_bar` 的因子字段，不直接查 `adjustment_factor`。单一写入点，避免数据不一致。

#### 不迁：`daily_bar_optimized`（冗余，381 万行重复数据），`v_daily_bar_compatible`（兼容视图）

---

### `fund` — 基本面

#### `fund.financial_indicator_daily`
现存 MySQL: `financial_indicator_daily`，441 万行。MySQL 已用 `symbol_id`，PG 直接映射。

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `symbol_id` | `bigint` | NOT NULL | FK → `ref.symbol_info(id)`，迁移时从原 MySQL `symbol_id` 直接映射 |
| `trade_date` | `date` | NOT NULL | |
| `report_date` | `date` | NOT NULL | 财报截止日 |
| `report_type` | `varchar(4)` | NOT NULL | Q1/Q2/Q3/Q4/FY |
| `effective_disclosure_date` | `date` | | 实际披露日 |
| `eps` | `numeric(10,4)` | | 每股收益 |
| `bps` | `numeric(10,4)` | | 每股净资产 |
| `roa` | `numeric(8,4)` | | 总资产收益率 |
| `roe` | `numeric(8,4)` | | 净资产收益率 |
| `profit_margin` | `numeric(8,4)` | | 净利润率 |
| `gross_margin` | `numeric(8,4)` | | 毛利率 |
| `operating_margin` | `numeric(8,4)` | | 营业利润率 |
| `debt_to_equity` | `numeric(8,4)` | | 负债权益比 |
| `current_ratio` | `numeric(8,4)` | | 流动比率 |
| `quick_ratio` | `numeric(8,4)` | | 速动比率 |
| `operating_cash_flow` | `numeric(20,4)` | | 经营活动现金流 |
| `investing_cash_flow` | `numeric(20,4)` | | 投资活动现金流 |
| `financing_cash_flow` | `numeric(20,4)` | | 筹资活动现金流 |
| `total_revenue` | `numeric(20,4)` | | 营业总收入 |
| `net_profit` | `numeric(20,4)` | | 净利润 |
| `total_assets` | `numeric(20,4)` | | 总资产 |
| `total_liabilities` | `numeric(20,4)` | | 总负债 |
| `equity` | `numeric(20,4)` | | 股东权益 |
| `dividend_yield` | `numeric(12,6)` | | 股息率 |
| `payout_ratio` | `numeric(12,6)` | | 分红率 |
| `dividend_stability` | `numeric(8,4)` | | 分红稳定性 |
| `created_at` | `timestamptz` | DEFAULT now() | |
| `updated_at` | `timestamptz` | DEFAULT now() | |

`(symbol_id, trade_date)` UNIQUE

> 每只股票每个交易日只有一条有效财务记录（最新已披露财报），`report_date` 和 `report_type` 为来源追溯字段。`(symbol_id, trade_date)` 唯一约束足够，无需加 `report_date`。

> 迁移时 MySQL 的 `financial_indicator_daily.symbol_id` 直接映射到 PG，无转换。

#### `fund.derivatives_data` / `fund.alternative_data` / `fund.policy_data` / `data.data_source_type` / `data.data_update_log` / `data.cleaned_dataset` / `data.cleaning_tasks` / `data.news_sentiment`
现存 MySQL 同名表，共 148 行。结构不变，迁入对应 schema（fund 或 data）。空表 `cleaning_results` 不迁。

---

### `alpha` — 因子元数据

全部来自现有 MySQL，结构不变，只做 schema 迁移和类型映射。

| PG 表 | MySQL 源 | 行数 | 说明 |
|---|---|---|---|
| `alpha.factor_instance` | `factor_instance` | 21 | 因子实例，`full_config jsonb` 是真源 |
| `alpha.factor_template` | `factor_template` | 2 | 因子模板 |
| `alpha.factor_parameter` | `factor_parameter` | 5 | 参数定义 |
| `alpha.factor_category` | `factor_category` | 5 | 分类 |
| `alpha.factor_tags` | `factor_tags` | 22 | 标签 |
| `alpha.factors` | `factors` | 21 | 因子基础信息 |
| `alpha.factor_combination` | `factor_combination` | 0 | 组合因子（空表） |
| `alpha.factor_params` | `factor_params` | 0 | 参数（空表） |
| `alpha.factor_performance` | `factor_performance` | 0 | 绩效（空表） |
| `alpha.factor_backtest_results` | `factor_backtest_results` | 0 | 回测结果（空表） |
| `alpha.user_factor` | `user_factor` | 0 | 用户因子（空表） |

> **因子值存储**：`alpha.factor_values` 表结构已建（见预留扩展），当前 C++ 代码在 `RuntimeFactorSvc` 内存计算不落库。后续接入时，EAV 模式按 `(instance_id, trade_date)` 查询单因子高效；多因子横向拼接需 `JOIN` 或 `crosstab`，是 EAV 固有 trade-off，非设计缺陷。

**`alpha.factor_instance` 详细字段：**

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `instance_id` | `varchar(100)` | **PK** | 实例唯一标识 |
| `factor_id` | `varchar(100)` | NOT NULL | FK → `alpha.factors` |
| `instance_name` | `varchar(200)` | NOT NULL | |
| `description` | `text` | | |
| `full_config` | `jsonb` | NOT NULL | **因子参数真源** |
| `status` | `varchar(20)` | DEFAULT 'ACTIVE' | ACTIVE / INACTIVE / TESTING |
| `creator` | `varchar(100)` | DEFAULT 'system' | |
| `created_at` | `timestamptz` | DEFAULT now() | |
| `updated_at` | `timestamptz` | DEFAULT now() | |

---

### `live` — 交易链路

#### `live.strategy`
现存 MySQL: `strategy`，2 行

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `strategy_id` | `varchar(128)` | **PK** | UUID |
| `strategy_code` | `varchar(128)` | UNIQUE NOT NULL | 如 `TBR_趋势突破_20260622...` |
| `metadata_json` | `jsonb` | NOT NULL | name/behaviorKind/factorIds/description |
| `strategy_identity_json` | `jsonb` | NOT NULL | |
| `version` | `varchar(64)` | | |
| `author` | `varchar(128)` | | |
| `language` | `varchar(32)` | DEFAULT 'PYTHON' | |
| `status` | `varchar(32)` | DEFAULT 'ACTIVE' | |
| `parameters` | `jsonb` | | 策略参数 |
| `performance_metrics` | `jsonb` | | |
| `runtime_json` | `jsonb` | | |
| `created_at` | `timestamptz` | DEFAULT now() | |
| `updated_at` | `timestamptz` | DEFAULT now() | |

#### `live.backtest_config` / `live.backtest_result` / `live.backtest_summary` / `live.strategy_backtest_results`
现存 MySQL 同名表。迁 schema，若原表有 `symbol` 列需同步改为 `symbol_id bigint REFERENCES ref.symbol_info(id)`。

#### `live.daily_position`
现存 MySQL: `daily_position`，0 行。预留。

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `position_id` | `bigserial` | **PK** | |
| `summary_id` | `integer` | NOT NULL | FK → 组合摘要 |
| `trade_date` | `date` | NOT NULL | |
| `symbol_id` | `bigint` | NOT NULL | FK → `ref.symbol_info(id)` |
| `position` | `integer` | DEFAULT 0 | 持仓数量 |
| `avg_cost` | `numeric(12,4)` | NOT NULL | 成本价 |
| `market_value` | `numeric(15,4)` | NOT NULL | 市值 |
| `floating_pnl` | `numeric(15,4)` | DEFAULT 0 | 浮动盈亏 |
| `realized_pnl` | `numeric(15,4)` | DEFAULT 0 | 实现盈亏 |
| `created_at` | `timestamptz` | DEFAULT now() | |

`(summary_id, trade_date, symbol_id)` UNIQUE。

#### `live.daily_equity_snapshots`
现存 MySQL: `daily_equity_snapshots`，0 行。预留。

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `id` | `varchar(36)` | **PK** | UUID |
| `strategy_id` | `varchar(64)` | NOT NULL | FK → `live.strategy` |
| `trade_date` | `date` | NOT NULL | MySQL 原列名 `snap_date`，迁移时重命名 |
| `total_asset` | `double precision` | DEFAULT 0 | 总资产 |
| `daily_return` | `double precision` | DEFAULT 0 | 日收益率 |

`(strategy_id, trade_date)` UNIQUE。

#### `live.trade_record`
现存 MySQL: `trade_record`，0 行。预留。

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `id` | `bigserial` | **PK** | |
| `strategy_id` | `varchar(128)` | | FK → `live.strategy` |
| `symbol_id` | `bigint` | NOT NULL | FK → `ref.symbol_info(id)` |
| `order_id` | `varchar(64)` | | 交易所委托编号 |
| `side` | `varchar(4)` | NOT NULL | BUY / SELL |
| `price` | `numeric(12,4)` | | |
| `quantity` | `bigint` | | |
| `filled_qty` | `bigint` | | |
| `avg_fill_price` | `numeric(12,4)` | | |
| `fee` | `numeric(12,4)` | | |
| `status` | `varchar(20)` | DEFAULT 'NEW' | NEW / PARTIAL / FILLED / CANCELLED / REJECTED |
| `reject_reason` | `text` | | |
| `created_at` | `timestamptz` | DEFAULT now() | |
| `updated_at` | `timestamptz` | DEFAULT now() | |

#### `live.risk_config`
新增。对应 C++ `RiskConfig::defaults()` 和 `RiskConfigService`。

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `id` | `bigserial` | **PK** | |
| `strategy_id` | `varchar(128)` | UNIQUE | FK → `live.strategy`，NULL=全局默认（仅一行） |
| `order_size_limit_wan` | `numeric(14,2)` | DEFAULT 500 | 单笔上限（万元） |
| `slippage_limit_pct` | `numeric(8,4)` | DEFAULT 2.0 | 滑点限制 % |
| `turnover_limit_wan` | `numeric(14,2)` | DEFAULT 5000 | 日成交额上限（万元） |
| `stop_loss_pct` | `numeric(8,4)` | DEFAULT 10.0 | 止损 % |
| `take_profit_pct` | `numeric(8,4)` | DEFAULT 20.0 | 止盈 % |
| `max_drawdown_pct` | `numeric(8,4)` | DEFAULT 12.0 | 最大回撤 % |
| `max_position_pct` | `numeric(8,4)` | DEFAULT 15.0 | 单只最大仓位 % |
| `max_total_exposure_pct` | `numeric(8,4)` | DEFAULT 67.0 | 最大总敞口 % |
| `breaker_l1_pct` | `numeric(8,4)` | DEFAULT 5.0 | 一级熔断 % |
| `breaker_l2_pct` | `numeric(8,4)` | DEFAULT 8.0 | 二级熔断 % |
| `breaker_l3_pct` | `numeric(8,4)` | DEFAULT 12.0 | 三级熔断 % |
| `updated_at` | `timestamptz` | DEFAULT now() | |

---

## 三、预留扩展（表结构现在就建，数据后续填入）

这些表当前为空或 C++ 未写入，但表结构按设计建好，避免未来返工。

### `mkt.ticks` — Tick 逐笔数据

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `symbol_id` | `bigint` | NOT NULL | FK → `ref.symbol_info(id)` |
| `ts` | `timestamptz` | NOT NULL | |
| `price` | `numeric(12,4)` | NOT NULL | |
| `volume` | `bigint` | | |
| `bid_price1` ~ `bid_price5` | `numeric(12,4)` | | 买 1-5 价 |
| `ask_price1` ~ `ask_price5` | `numeric(12,4)` | | 卖 1-5 价 |
| `bid_vol1` ~ `bid_vol5` | `bigint` | | 买 1-5 量 |
| `ask_vol1` ~ `ask_vol5` | `bigint` | | 卖 1-5 量 |
| `trade_direction` | `varchar(1)` | | B/S/N（买/卖/中性） |

`(symbol_id, ts)` UNIQUE。TimescaleDB hypertable。

### `mkt.orderbook_snapshots` — 订单簿快照

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `symbol_id` | `bigint` | NOT NULL | FK → `ref.symbol_info(id)` |
| `ts` | `timestamptz` | NOT NULL | |
| `bids` | `jsonb` | | `[[price, qty], ...]` |
| `asks` | `jsonb` | | `[[price, qty], ...]` |

`(symbol_id, ts)` UNIQUE。TimescaleDB hypertable。

### `alpha.factor_values` — 因子值（EAV 模式）

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `symbol_id` | `bigint` | NOT NULL | FK → `ref.symbol_info(id)` |
| `trade_date` | `date` | NOT NULL | |
| `instance_id` | `varchar(100)` | NOT NULL | FK → `alpha.factor_instance.instance_id` |
| `value` | `double precision` | | 因子值 |

`(instance_id, trade_date, symbol_id)` UNIQUE。不使用宽表——EAV 模式字段增删灵活，不锁表。

```sql
CREATE TABLE alpha.factor_values (
    symbol_id bigint NOT NULL,
    trade_date date NOT NULL,
    instance_id varchar(100) NOT NULL,
    value double precision,
    CONSTRAINT fk_fv_symbol FOREIGN KEY (symbol_id) REFERENCES ref.symbol_info(id),
    CONSTRAINT fk_fv_instance FOREIGN KEY (instance_id) REFERENCES alpha.factor_instance(instance_id),
    UNIQUE (instance_id, trade_date, symbol_id)
);
SELECT create_hypertable('alpha.factor_values', 'trade_date', chunk_time_interval => INTERVAL '1 year');
```

### `alpha.signals` — 策略信号

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `signal_id` | `bigserial` | **PK** | |
| `strategy_id` | `varchar(128)` | NOT NULL | FK → `live.strategy` |
| `symbol_id` | `bigint` | NOT NULL | FK → `ref.symbol_info(id)` |
| `signal_ts` | `timestamptz` | DEFAULT now() | 信号生成时间 |
| `direction` | `varchar(4)` | NOT NULL | BUY / SELL |
| `strength` | `double precision` | | 0-1 信号强度 |
| `score` | `double precision` | | 因子综合评分 |
| `weight` | `double precision` | | 目标权重 |
| `expire_ts` | `timestamptz` | | 信号过期时间 |

### `port.nav_daily` — 组合净值

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `strategy_id` | `varchar(128)` | NOT NULL | FK → `live.strategy` |
| `trade_date` | `date` | NOT NULL | |
| `nav` | `numeric(20,4)` | | 净值 |
| `cash` | `numeric(20,4)` | | 现金 |
| `daily_return` | `numeric(12,8)` | | 日收益率 |
| `cum_return` | `numeric(12,8)` | | 累计收益率 |
| `drawdown` | `numeric(12,8)` | | 回撤 |

`(strategy_id, trade_date)` UNIQUE。

### `port.allocations` — 组合持仓明细

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `strategy_id` | `varchar(128)` | NOT NULL | |
| `trade_date` | `date` | NOT NULL | |
| `symbol_id` | `bigint` | NOT NULL | FK → `ref.symbol_info(id)` |
| `weight` | `numeric(8,4)` | | 权重 |
| `market_value` | `numeric(20,4)` | | 市值 |

`(strategy_id, trade_date, symbol_id)` UNIQUE。

### `data.data_quality_log` — 数据质量日志

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `id` | `bigserial` | **PK** | |
| `table_name` | `varchar(100)` | NOT NULL | |
| `symbol_id` | `bigint` | | FK → `ref.symbol_info(id)` |
| `trade_date` | `date` | | |
| `field_name` | `varchar(100)` | | |
| `issue_type` | `varchar(20)` | NOT NULL | MISSING / OUTLIER / STALE / INCONSISTENT |
| `severity` | `varchar(10)` | DEFAULT 'WARN' | INFO / WARN / ERROR |
| `description` | `text` | | |
| `resolved` | `boolean` | DEFAULT false | |
| `created_at` | `timestamptz` | DEFAULT now() | |

### PostGIS 扩展

```sql
CREATE EXTENSION IF NOT EXISTS postgis;
```

不建地理表，只装扩展，用到的项目自然能用。

---

## 四、不迁/不建的内容

| 内容 | 原因 |
|---|---|
| MySQL 空表（20+ 张） | 无数据且已有 PG 替代表 |
| `daily_bar_optimized` | 冗余表，381 万行与 `daily_bar` 重复 |
| `v_daily_bar_compatible` | MySQL 兼容视图，PG 不需要 |
| `v_daily_bar_wide` | 依赖 `daily_bar_optimized`，且价格 `/10000.0` 缩放逻辑废弃 |
| `cleaned_daily_bar` | 含乱码且功能与 `daily_bar` 重复 |
| `cfg` schema | 单张 `live.risk_config` 足以覆盖配置需求 |
| Citus 分布式 | 单机足够，结构兼容但不安装——需要时只用改连接串 |

---

## 五、视图（PG 重写）

| PG 视图 | 来源 | 改动 |
|---|---|---|
| `mkt.v_daily_bar` | MySQL `v_daily_bar` | `mkt.daily_bar d JOIN ref.symbol_info s ON d.symbol_id = s.id` |
| `mkt.v_current_constituents` | MySQL `v_current_constituents` | schema 前缀修正 |
| `live.v_backtest_overview` | MySQL `v_backtest_overview` | schema 前缀修正 |
| `alpha.v_factor_availability` | MySQL `factor_availability_view` | schema 前缀修正 |
| `mkt.v_data_availability` | MySQL `data_availability_view` | `COUNT(0)` → `COUNT(*)` |

废弃：`v_daily_bar_wide`（依赖 `daily_bar_optimized`），`cleaned_daily_bar`（含乱码且功能重复）

---

## 六、索引策略

```sql
-- mkt.daily_bar
CREATE UNIQUE INDEX ON mkt.daily_bar(symbol_id, trade_date);
CREATE INDEX ON mkt.daily_bar(trade_date);

-- fund.financial_indicator_daily
CREATE UNIQUE INDEX ON fund.financial_indicator_daily(symbol_id, trade_date);

-- ref.index_constituents
CREATE UNIQUE INDEX ON ref.index_constituents(index_symbol, stock_symbol_id);
CREATE INDEX ON ref.index_constituents(stock_symbol_id);

-- [建议] mkt.daily_bar 覆盖索引：CachedMarketDataView 批量加载时只扫索引不读堆
CREATE INDEX ON mkt.daily_bar(trade_date)
    INCLUDE (symbol_id, open, high, low, close, volume, pre_adjust_factor, post_adjust_factor);

-- 其余表 PRIMARY KEY 即聚簇索引，不需要额外索引
```

---

## 七、与用户方案的差异说明

| 用户原方案 | 终版决策 | 理由 |
|---|---|---|
| 9 个 schema | 7 个 schema | `fund` 和 `live` 已覆盖业务需求，保留 `port/data` 作为预留 |
| `symbol_id` 用 int 做 PK | `symbol` varchar(20) UNIQUE + `id` bigserial PK | 标准化，减少 JOIN 开销 |
| `factor_values` 宽表 | EAV + 空表预留 | 表结构建好但不写入，需要时 C++ 直接接入 |
| `cfg` schema | `live.risk_config` 单表替代 | 配置项少，不需要独立 schema |
| Citus 分布式 | 兼容但不引入 | 单机足够 |
| `continuous_contract_map` | 不加 | 无期货交易代码 |

---

## 八、C++ 代码同步修改要点

`symbol` → `symbol_id` 切换后，以下 C++ 代码需同步修改：

| 文件 | 改动 |
|---|---|
| `CachedMarketDataView::fromSqlRows` | 读取 `symbol_id` 而非 `symbol`，通过 JOIN `symbol_info` 填充 `m_symbolStrings` |
| `CachedMarketDataView::fromDailyBarRows` | 同上 |
| `MarketDataRepository::queryAllMarketDailyBar` | SELECT 中加入 `symbol_id` |
| `RuntimeFactorSvc::setLiveMarketView` | 符号解析器映射 `symbol_id → symbol` |
| `RuntimeFactorSvc::getValues` | 传入 `symbol_id`，通过解析器获取 symbol 字符串后调 `computeSingleDate` |
| `StrategyBridge` — 订单转发 | `m_resolver(symbol_id)` → symbol 字符串 |
| `MarketDataAdapter::resolveInstrumentId` | 返回 `symbol_id` 而非从 symbol 字符串解析的 uint32 |

> 核心原则：PG 存储层用 `symbol_id`，C++ 内存中通过 `symbol_info` 缓存一次映射（`symbol_id ↔ symbol`），因子/策略计算仍用 symbol 字符串，不影响算法逻辑。

---

## 九、建表顺序

1. `ref.symbol_info` → `ref.trade_calendar` → `ref.index_info` → `ref.index_constituents` → `ref.industry_classification`
2. `mkt.daily_bar` → `mkt.weekly_bar` → `mkt.monthly_bar` → `mkt.minute_bar` → `mkt.adjustment_factor`
3. `fund.financial_indicator_daily`
4. `alpha.*`（10 张因子表）
5. `live.strategy` → `live.backtest_*` → `live.trade_record` → `live.daily_position` → `live.daily_equity_snapshots` → `live.risk_config`
6. 视图 `mkt.v_daily_bar`、`mkt.v_current_constituents`、`live.v_backtest_overview`、`alpha.v_factor_availability`、`mkt.v_data_availability`
