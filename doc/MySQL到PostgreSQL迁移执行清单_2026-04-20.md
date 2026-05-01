# MySQL 到 PostgreSQL 迁移执行清单（2026-04-20）

## 1. 迁移目标

当前 MySQL 仍是现网业务库，PostgreSQL 18 已安装但尚未建业务表。本清单用于把现有 MySQL 业务数据迁移到 PostgreSQL，并在迁移完成后将 PostgreSQL 作为唯一主库。

迁移策略：

- 迁移期允许双库并行，但只用于对账、补迁和回退。
- 运行时业务逻辑不使用双主库。
- 不做长期兼容兜底。
- 旧表只在迁移窗口内保留，不作为最终主结构。

## 2. 迁移前置条件

1. MySQL 现有数据冻结到一个明确时间点。
2. PostgreSQL 18 已可连接，`postgres` 超级用户可用。
3. 已确认 MySQL 中至少以下核心表有有效数据：
   - `symbol_info`
   - `daily_bar`
   - `financial_indicator`
4. 已确认当前因子参数真源是 `factor_instance.full_config`。
5. 已确认 `daily_bar_optimized` 与 `daily_bar` 属于同一数据域的重复表达，不应同时作为长期主表。

## 3. 迁移阶段

### 阶段 0：冻结与盘点

目标：锁定源库状态和对象清单。

执行项：

- 导出 MySQL 现有 schema。
- 导出表统计、行数、索引、视图和触发器。
- 记录所有表的字段、主键、唯一键、外键。
- 标记冗余候选表。

### 阶段 1：PostgreSQL 目标 schema 建模

目标：先定目标结构，再迁数据。

执行项：

- 建立 `core`、`market`、`factor`、`backtest`、`cleaning`、`archive` schema。
- 创建主数据表。
- 创建时序主表。
- 创建结果表。
- 创建必要索引和唯一键。

### 阶段 2：主数据迁移

目标：先迁不常变的数据。

执行项：

- `symbol_info`
- `exchange_info`
- `industry_classification`
- `data_source_type`
- `factor_category`
- `factor_template`
- `factor_parameter`

校验项：

- 记录数一致。
- 唯一键无冲突。
- 字段枚举值一致。

### 阶段 3：时序数据迁移

目标：先迁回测刚需数据。

执行项：

- `daily_bar`
- `financial_indicator`
- `minute_bar`
- `news_sentiment`
- `policy_data`
- `alternative_data`
- `derivatives_data`

校验项：

- 记录数一致或可解释差异。
- 关键日期范围一致。
- 主键唯一。
- 抽样比对价格和财务字段。

### 阶段 4：因子运行域迁移

目标：迁移因子定义、实例、标签、快照。

执行项：

- `factors`
- `factor_instance`
- `factor_tags`
- `factor_runtime_snapshot`

校验项：

- `factor_instance.full_config` 作为真源保持完整。
- 旧 `factor_params` 仅作历史参考，不参与主写。

### 阶段 5：回测与清洗结果迁移

目标：迁移历史结果和审计链路。

执行项：

- `backtest_config`
- `backtest_result`
- `backtest_summary`
- `factor_performance`
- `cleaning_tasks`
- `cleaning_results`
- `data_quality_report`

校验项：

- 任务链路完整。
- 结果表可追溯到配置和输入数据。
- 统计字段和摘要字段一致。

### 阶段 6：兼容层收口

目标：只保留迁移窗口内视图。

执行项：

- 保留 `v_daily_bar_compatible` 类视图用于过渡。
- 停止新增依赖旧快照表的业务逻辑。
- 停止新增对 MySQL 的主链路写入。

### 阶段 7：切换与回退窗口

目标：完成主库切换。

执行项：

- 业务读切到 PostgreSQL。
- 业务写切到 PostgreSQL。
- MySQL 只保留只读对账。
- 保留一段固定回退窗口。

## 4. 迁移优先级

### 第一批

- `symbol_info`
- `daily_bar`
- `financial_indicator`

### 第二批

- `factor_instance`
- `factors`
- `factor_tags`

### 第三批

- `backtest_config`
- `backtest_summary`
- `cleaning_tasks`
- `cleaning_results`

### 第四批

- `minute_bar`
- `news_sentiment`
- `policy_data`
- `alternative_data`
- `derivatives_data`

### 第五批

- `daily_bar_optimized`
- `factor_params`
- 兼容视图

## 5. 校验规则

### 5.1 结构校验

- 表名、主键、唯一键、索引与目标 schema 一致。
- 每张表的字段类型、空值约束和默认值符合目标设计。

### 5.2 数据校验

- 行数校验。
- 关键日期范围校验。
- 抽样字段值校验。
- 外键引用完整性校验。

### 5.3 功能校验

- 因子读取可以在 PostgreSQL 中完成。
- 回测可以从 PostgreSQL 中完成。
- 清洗结果可以从 PostgreSQL 中查询。
- 旧 MySQL 不再参与主功能路径。

## 6. 禁止项

1. 禁止在产品代码里继续新增 MySQL 和 PostgreSQL 双写分支。
2. 禁止继续新增基于旧快照表的兜底读取。
3. 禁止让兼容视图成为永久业务依赖。
4. 禁止在迁移后继续扩充 MySQL 的主业务表。

## 7. 迁移完成标准

满足以下条件即可视为迁移第一阶段完成：

1. PostgreSQL 已建好核心 schema。
2. 主数据和回测刚需时序已完成迁移。
3. 因子运行链路改读 PostgreSQL。
4. 回测链路改读 PostgreSQL。
5. MySQL 仅保留历史对账和回退窗口。
