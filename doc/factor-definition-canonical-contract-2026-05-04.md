# 因子定义全流程统一契约

日期: 2026-05-04

目标: 把因子定义链路里的类型、参数、数据库字段名统一成一套单一事实源，后续删除兼容分支时按这份表一次改到底。

## 硬约束

1. 业务语义层只允许一套 canonical key/value，不再同时接受 camelCase、snake_case、中文类别名三套输入。
2. `factor_instance.full_config` 是运行期唯一真源，写入时只写 canonical key，不再双写别名键。
3. 关系型数据库物理列名继续保留 snake_case；只有 SQL schema 层允许 snake_case。
4. UI / QVariantMap / full_config / bridge / domain runtime 内部语义字段统一使用 camelCase key 和英文 canonical id。
5. 中文仅用于显示名，不再承担类型判断、参数判断、source table 推断。

## 统一范围

本契约覆盖以下链路：

1. UI / QML / QVariantMap 因子定义对象。
2. `FactorService` 写入 `factor_instance.full_config` 的 JSON 结构。
3. `FactorRepository` 在 `factors` 表与 QVariantMap 之间的映射。
4. `FactorInstanceManager` / `ConfigurableFactor` / `FactorBacktestController` 对因子类型和参数的读取。
5. 因子回测、domain sync、benchmark/test 中直接读取 `full_config` 的代码。

不在本次统一范围内：纯回测结果 DTO、进度 DTO、独立策略组合 JSON（如 `portfolio_factor_ids` / `factor_id`）。这些是另一条 schema，不属于因子定义链。

## 一、类型统一表

| 语义 | Canonical 字段 | Canonical 值 | 显示名 | 禁止继续出现的业务语义写法 |
| --- | --- | --- | --- | --- |
| 因子类型 | `factorType` | `value` | `价值因子` | `价值因子` 参与控制流 |
| 因子类型 | `factorType` | `momentum` | `动量因子` | `动量因子` 参与控制流 |
| 因子类型 | `factorType` | `size` | `规模因子` | `规模因子` 参与控制流 |
| 因子类型 | `factorType` | `quality` | `质量因子` | `质量因子` 参与控制流 |
| 因子类型 | `factorType` | `growth` | `成长因子` | `成长因子` 参与控制流 |
| 因子类型 | `factorType` | `dividend` | `红利因子` | `红利因子` 参与控制流 |
| 因子类型 | `factorType` | `technical` | `技术因子` | `技术因子` 参与控制流 |
| 因子类型 | `factorType` | `liquidity` | `流动性因子` | `流动性因子` 参与控制流 |
| 因子类型 | `factorType` | `macro` | `宏观因子` | `宏观因子` 参与控制流 |
| 因子类型 | `factorType` | `industry` | `行业因子` | `行业因子` 参与控制流 |
| 因子类型 | `factorType` | `sentiment` | `情绪因子` | `情绪因子` 参与控制流 |
| 因子类型 | `factorType` | `custom` | `自定义因子` | `自定义因子` / `自定义` 参与控制流 |
| 因子类型 | `factorType` | `lowVolatility` | `低波因子` | `low_volatility` / `低波动因子` 参与控制流 |

说明:

1. `factorType` 是唯一运行时类型字段。
2. `majorCategory` 是唯一显示类别字段，值固定为 `canonicalFactorDisplayName(factorType)` 的结果。
3. `factorType` 和 `majorCategory` 不再互相兜底；写入时必须同时完整生成。
4. 运行期内部若需要 C++ 字段名，可继续使用 `factorType` / `majorCategory`，但值必须是上表 canonical 语义。

## 二、顶层对象统一表

| 语义 | UI / QVariantMap | full_config JSON | DB 物理列 | Domain / Runtime 字段 | 当前待删别名 |
| --- | --- | --- | --- | --- | --- |
| 因子业务 ID | `factorId` | `metadata.factorId` | `factors.factor_id` / `factor_instance.factor_id` | `factorId` | `factor_id` 作为 QVariant/JSON key |
| 实例 ID | `instanceId` | 不写入 full_config，单独存列 | `factor_instance.instance_id` | `instanceId` | `instance_id` 作为因子定义 JSON key |
| 因子名称 | `factorName` | `factorName` | `factors.factor_name` | `name` / `factorName` | `factor_name` 作为 QVariant/JSON key |
| 显示名称 | `displayName` | `displayName` | `factors.display_name` | `instanceName` / `displayName` | `display_name` 作为 QVariant/JSON key |
| 类型 ID | `factorType` | `factorType` | 不单独落列，来自 full_config | `factorType` | `factor_type`, 中文类型名, `majorCategory` 兜底 |
| 显示类别 | `majorCategory` | `majorCategory` | `factors.major_category` | `majorCategory` | `major_category`, `factorType` 兜底 |
| 子类别 | `subCategory` | `subCategory` | `factors.sub_category` | `subCategory` | `sub_category` |
| 描述 | `description` | `description` | `factors.description` / `factor_instance.description` | `description` | 无 |
| 标签 | `tags` | `tags` 与 `metadata.tags` 二选一，最终保留一处 | `factor_tags.tag` | `tags` | 多处重复标签源 |
| 参数原始输入 | `parameters` | `parameters` | 不落 `factor_params` 真源 | `parameters` | `factor_params` 快照真源 |
| 运行计算配置 | 不直接传 | `calculation` | 不落列 | `calculation` | 顶层散落参数 |
| 数据需求 | 不直接传 | `dataRequirements` | 不落列 | `dataRequirements` | `data_requirements` |
| 边界规则 | 不直接传 | `boundaryRules` | 不落列 | `boundaryRules` | `boundary_rules` |

强制规则:

1. full_config 顶层只保留 camelCase: `factorType`, `majorCategory`, `displayName`, `factorName`, `parameters`, `calculation`, `dataRequirements`, `boundaryRules`, `metadata`。
2. `factor_instance` 表继续保留 snake_case 列名，因为这是物理 schema，不属于业务 key 兼容。
3. `factor_params` 不再作为因子参数真源，只能视为历史快照表，后续如无强依赖可逐步下线。

## 三、参数统一表

### 3.1 公共参数

| 语义 | Canonical key | 禁止继续出现的别名 |
| --- | --- | --- |
| 回看周期 | `lookbackPeriod` | `lookback`, `lookback_period` |
| 跳过最近天数 | `skipRecent` | `skip_recent` |
| 标准化 | `standardization` | 无 |
| 行业中性化 | `neutralizationEnabled` | `neutralization_enabled` |
| 是否使用成交量 | `useVolume` | `use_volume` |
| 是否对数变换 | `logTransform` | `log_transform` |
| 是否分位数 | `usePercentile` | `use_percentile` |
| 行业中性 | `industryNeutral` | `industry_neutral` |
| 交易成本 | `transactionCost` | `commissionRate`, `commission` |
| 滑点 | `slippageRate` | `slippage` |
| 无风险利率 | `riskFreeRate` | `risk_free_rate` |
| 基准代码 | `benchmarkSymbol` | `benchmark_symbol` |

### 3.2 各类型参数

| factorType | Canonical 参数 | 禁止继续出现的别名 |
| --- | --- | --- |
| `value` | `valuationMetrics`, `bpWeight`, `epWeight`, `dividendYieldWeight`, `cfPWeight`, `usePercentile`, `industryNeutral`, `standardization` | `valuationType`, `use_percentile`, `industry_neutral` |
| `momentum` | `window`, `type`, `priceType`, `useVolume`, `skipRecent` | `lookback_window`, `lookbackWindow`, `price_type`, `use_volume`, `skip_recent`, `momentumType` |
| `size` | `sizeMetric`, `logTransform`, `usePercentile`, `industryNeutral`, `standardization` | `size_metric`, `log_transform`, `use_percentile`, `industry_neutral` |
| `growth` | `growthMetrics`, `growthWeights`, `timeframe`, `lookbackPeriod`, `standardization`, `neutralizationEnabled` | `revenueGrowthWeight`, `netProfitGrowthWeight`, `deltaRoeWeight`, `sueWeight`, `lookback_period`, `neutralization_enabled` |
| `quality` | `metric`, `timeframe`, `qualityThreshold` | `qualityMetric`, `quality_threshold` |
| `dividend` | `dividendMetrics`, `metric`, `minDividendYield`, `timeframe` | `dividendMetric`, `min_dividend_yield` |
| `technical` | `technicalIndicators`, `indicatorType`, `window`, `rsiWindow`, `maWindow`, `emaWindow`, `bollWindow`, `bollStdDev`, `kdjWindow`, `kdjKPeriod`, `kdjDPeriod`, `atrWindow`, `macdFastPeriod`, `macdSlowPeriod`, `macdSignalPeriod`, `obvWindow`, `vwapWindow`, `volumeRatioWindow`, `turnoverStabilityWindow`, `turnoverStabilityMetric`, `technicalPriceType`, `lookbackPeriod`, `frequency`, `priceType`, `useVolume` | `indicator_type`, `indicatorTypes`, `lookback_period`, `price_type`, `technicalPriceType` 与 `priceType` 双轨 |
| `liquidity` | `liquidityMetric`, `window`, `lookbackPeriod`, `frequency` | `liquidity_metric`, `liquidityWindow`, `lookback_period` |
| `macro` | `macroDimensions`, `macroIndicators`, `macroMetric`, `macroFrequency`, `macroWindow` | `frequency`, `window` 作为宏观专用参数 |
| `industry` | `sectorType`, `industryMetric`, `window` | 无 |
| `sentiment` | `sentimentSource`, `metric`, `window`, `sentimentWeight` | `sentiment_source`, `sentiment_weight`, `sentimentWindow`, `lookbackDays`, `sentimentMetric` |
| `custom` | `expression`, `variables` | 无 |
| `lowVolatility` | `window`, `components`, `volatilityWeight`, `drawdownWeight`, `betaWeight` | `volatilityWindow` |

参数统一规则:

1. `parameters` 保存编辑态 canonical key。
2. `calculation` 保存运行态 canonical key；如果与 `parameters` 完全一致，可保留同名，不再引入 snake_case 镜像键。
3. 不允许“顶层一个键 + calculation 再来一份 + alias 再来一份”的三份并存。

## 四、数据库层统一表

| 表/载体 | 角色 | 保留字段风格 | 备注 |
| --- | --- | --- | --- |
| `factors` | 因子展示元数据 | snake_case 列名 | 通过 repository 映射到 camelCase QVariantMap |
| `factor_instance` | 因子实例真源 | snake_case 列名 | `full_config` 内部 JSON 按本契约统一成 camelCase |
| `factor_tags` | 标签关联 | snake_case 列名 | 对外仍映射为 `tags` |
| `factor_params` | 历史参数快照 | snake_case 列名 | 不再作为真源，不允许反向覆盖 full_config |
| `full_config` JSON | 运行配置真源 | camelCase key | 不再双写 snake_case key |

数据库层结论:

1. SQL schema 的 snake_case 不改。
2. Repository 作为唯一映射层，把 snake_case 列映射到 camelCase QVariantMap。
3. 任何 bridge/domain 代码不应直接依赖 `major_category`、`display_name`、`factor_id` 这种 QVariant key；只有 repository row 和 SQL query 可见这些名字。

## 五、当前代码热点与待删兼容点

| 文件 | 当前现象 | 目标改动 |
| --- | --- | --- |
| `src/ui/bridge/src/FactorService.cpp` | `resolveFactorTypeId()` / `resolveDisplayCategory()` 同时吃 `factorType`、`factor_type`、`majorCategory`、`major_category`、`calculation.type` | 改成只吃 canonical key；把缺失字段视为写入错误，而不是继续兜底 |
| `src/ui/bridge/src/FactorService.cpp` | `buildDomainConfigObject()` 仍双写 `factor_type` | 删除 `factor_type`，只写 `factorType` |
| `src/domain/factor/src/ConfigurableFactor.cpp` | `loadConfig()` 仍读取 `factor_type` 和 `majorCategory` 兜底 | 改成只读取 `factorType`，缺失则配置非法 |
| `src/ui/bridge/src/FactorService.cpp` | 搜索/分类读取仍把 `majorCategory` / `major_category` / `factorType` / `factor_type` 混在一起 | 读取侧统一只看 canonical QVariantMap 键 |
| `src/ui/bridge/src/FactorService.cpp` | `canonicalizeParameterAliases()` 继续吞多套参数别名 | 迁移完成后保留一套 canonical key，删除 alias 折叠 |
| `src/ui/bridge/src/FactorService.cpp` | `normalizeCalculationParameters()` 内部仍大量写 snake_case 参数 | 全部改成 canonical camelCase 参数名 |
| `src/ui/bridge/src/FactorBacktestController.cpp` | 多处从 `full_config` 读取后继续接受旧字段 | 改成只消费 canonical full_config 结构 |
| `src/domain/factor/src/FactorInstanceManager.cpp` | `resolveFactorType()` 仍对历史 full_config 结构做兜底 | 在 write path 收口后删兼容读取 |
| `src/infrastructure/src/database/FactorRepository.cpp` | 物理列到 QVariantMap 已经是单向 camelCase 映射 | 保留，不再向上冒 snake_case QVariant key |
| `tests/run_factor_full_market_benchmark.cpp` | SQL 里同时读取 `$.factorType` 和 `$.factor_type` | 改成只读取 `$.factorType` |
| `tests/test_factor_backtest_regression.cpp` | 大量样本同时混用 `factorType` / `factor_type` / `majorCategory` | 样本全部切成 canonical 契约，旧键单独保留“迁移前”测试再整体删除 |

## 六、一步到位改造顺序

为了避免“改一半才能靠编译发现缺口”，必须按下面顺序收敛：

1. 先固定契约常量。
   - `FactorTypeUtils.h` 固定 canonical type id 与 canonical display name。
   - 新增统一 schema/contract helper，集中声明 full_config 顶层 key 和参数 canonical key。
2. 只改写路径。
   - `FactorService::buildDomainConfigObject()`
   - `syncFactorDefinitionToDomain()`
   - 所有 add/update/save cache 路径
   - 目标: 新写出的 full_config 只含 canonical key。
3. 再改运行时读路径。
   - `ConfigurableFactor::loadConfig()`
   - `FactorInstanceManager::resolveFactorType()`
   - `FactorBacktestController` 的 full_config 读取点
   - benchmark / regression 读取点
4. 再删桥接 alias。
   - `resolveFactorTypeId()` / `resolveDisplayCategory()` 的多键兜底
   - `canonicalizeParameterAliases()` 的历史 alias
   - `normalizeCalculationParameters()` 的 snake_case 出口
5. 最后统一测试与诊断脚本。
   - regression fixtures
   - benchmark SQL JSON_EXTRACT
   - 文档 / 示例 / SQL seed

## 七、改造完成后的判定标准

满足以下条件才算完成，不再保留兼容分支：

1. `full_config` 中不再出现 `factor_type`、`major_category`、`display_name`、`sub_category`、`sentiment_source`、`lookback_period` 等业务 alias key。
2. `ConfigurableFactor`、`FactorService`、`FactorBacktestController`、benchmark/test 不再读取这些 alias key。
3. 运行期 `factorType` 只接受 canonical 英文 id。
4. 中文类别名只出现在 `majorCategory` 显示字段和 UI 文案，不参与控制流。
5. focused build + regression 通过后，再做全局 grep，确认旧 key 仅剩 SQL 列名和历史文档。

## 八、建议验证顺序

每批删除兼容后，至少执行：

1. `Build_CMakeTools(target=test_eventsystem)`
2. `test_eventsystem --gtest_filter=FactorBacktestRegressionTest.BuildFactorSupportMap*`
3. `test_eventsystem --gtest_filter=FactorBacktestRegressionTest.ValidateFactorData*`
4. 相关 benchmark / strategy regression 中直接读 `full_config` 的切片
5. 最后 grep 旧 key，确认只剩 SQL schema 和迁移文档

## 九、最终目标格式示例

### 9.1 UI / QVariantMap

```json
{
  "factorId": "factor_growth_quality",
  "instanceId": "factor_growth_quality",
  "factorName": "成长质量因子",
  "displayName": "成长质量综合",
  "factorType": "growth",
  "majorCategory": "成长因子",
  "subCategory": "盈利成长",
  "description": "测试描述",
  "tags": ["成长因子", "盈利"],
  "parameters": {
    "growthMetrics": ["revenue_growth", "net_profit_growth"],
    "growthWeights": [60, 40],
    "timeframe": "quarterly",
    "lookbackPeriod": 252,
    "neutralizationEnabled": false,
    "standardization": "zscore"
  }
}
```

### 9.2 factor_instance.full_config

```json
{
  "factorType": "growth",
  "majorCategory": "成长因子",
  "factorName": "成长质量因子",
  "displayName": "成长质量综合",
  "description": "测试描述",
  "tags": ["成长因子", "盈利"],
  "parameters": {
    "growthMetrics": ["revenue_growth", "net_profit_growth"],
    "growthWeights": [60, 40],
    "timeframe": "quarterly",
    "lookbackPeriod": 252,
    "neutralizationEnabled": false,
    "standardization": "zscore"
  },
  "calculation": {
    "growthMetrics": ["revenue_growth", "net_profit_growth"],
    "growthWeights": [60, 40],
    "timeframe": "quarterly",
    "lookbackPeriod": 252,
    "neutralizationEnabled": false,
    "standardization": "zscore"
  },
  "dataRequirements": {
    "required": ["total_revenue", "net_profit"],
    "sourceTable": "financial_indicator"
  },
  "boundaryRules": {
    "minDataPoints": 2
  },
  "metadata": {
    "factorId": "factor_growth_quality",
    "creator": "system",
    "tags": ["成长因子", "盈利"]
  }
}
```

这两个对象之间不允许再出现第二套命名规则。

  ## 十、2026-05-04 收敛结论补充

  ### 10.1 已删除的 legacy 面

  1. `src/ui/bridge/include/FactorParamController.h` 与 `src/ui/bridge/src/FactorParamController.cpp` 已删除；当前创建页主链只保留 `FactorService` + schema 驱动路径。
  2. `src/domain/model` 中的历史业务封装已删除；`Bar.h` 已迁移到 `src/domain/types/include/Bar.h`，不再继续维护 old strategy/factor/model 接口。
  3. `src/domain/market/include/repository` 下未接入构建、仅互相引用的旧仓储接口已删除。

  ### 10.2 `domain_types` 当前角色

  1. `Bar.h` 已迁移到 `src/domain/types/include/Bar.h`，新的 `domain_types` 作为唯一基础类型载体，不再沿用 `domain_model` 命名。
  2. `domain_types` 当前只承载跨模块共享的基础行情类型，供真正直接消费 `Bar` 基础类型的模块使用。
  3. 后续若某个 target 不直接包含 `Bar.h`，就不应再直链 `domain_types`。

  ### 10.3 本次继续删除的冗余直链

  以下目标已移除对旧 `domain_model` 的直接链接，改为只通过真实依赖链获取所需能力，或切换到新的 `domain_types`：

  1. `src/domain/CMakeLists.txt` 中的 `domain`
  2. `src/domain/backtest/CMakeLists.txt` 中的 `domain_backtest` 与 `test_factor_backtest`
  3. `src/infrastructure/CMakeLists.txt` 中的 `infrastructure`
  4. `src/app/CMakeLists.txt` 中的 `astockquantapp`、`astockquantapp-exe`、`astockquant_tradeprobe`

  判定规则:

  1. 只有真正直接 `#include "Bar.h"` 的模块保留 `domain_types`。
  2. 其余 target 一律通过更上游的真实域模块依赖获取传递能力，不再保留历史习惯性直链。

## 十一、补充规则

1. 不允许使用 QString 做类型判断，QString 仅允许用于调试输出。
2. QVariantMap 仅允许作为 QML 输入参数和返回到 QML 的输出载荷，桥接输出对象名统一为 StrategyManage。