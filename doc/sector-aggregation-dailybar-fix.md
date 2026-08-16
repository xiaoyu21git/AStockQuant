# 板块日频聚合修复方案：minute_bar 数据源 → daily_bar（2026-08-13）

状态：已确认方案 → 代码已改 → SQL 冒烟验证通过（含暴跌日口径验证）→ Release 编译通过 → dataset_5 重建 + 分年度验收通过（2026-08-13）→ 待 AutoDL 重训 dl_v12

## 1. 背景与根因（已核实证据）

v11（23 特征含 7 板块字段）回测远弱于 v9（16 特征无板块），且跌幅集中在 2025/2026。排查定位到板块聚合 SQL 的**数据源错误**：

- 板块日频聚合 `sqlSectorDailyAgg()` 的源表是 `mkt.minute_bar`
- minute_bar 2020-2025 仅 ~7.5% 行覆盖（AmiBroker CSV 导入 200~450 只），2026 实盘订阅后覆盖骤变
- 实测证据（dataset_3 分年度均值）：
  - `sector_is_reliable`（=当日有分钟数据的成分股≥5只）：2020-2025 均值 0.15~0.23 → 2026 年 0.958
  - `sector_amplitude`（=板块内所有分钟bar最高-最低/平均开盘）：2020-2025 约 1.05 → 2026 年 4.74
  - 同期**个股**振幅 3.3~4.5 两时代同量级 → 跳变不是行情，是覆盖口径变了
- 结论：训练时代（2020-2025）板块字段=400 只有分钟数据的稀疏代理（噪声），2026=另一套口径。
  模型训练与推理输入根本不是同一个物理量，这正是 2025 也输 v9、2026 崩塌的原因。

**修复方向（用户裁决：不允许回退，修数据和逻辑）**：聚合源改 `mkt.daily_bar`（日线 2020-2025 覆盖率：turnover 100%、pre_close 99.95%、amplitude 99.8%，两时代同口径）。

## 2. 收益口径的两个陷阱（设计与实测）

**陷阱 A：minute_bar 口径两时代覆盖分裂**（见上，sector_is_reliable 0.2→0.96）。

**陷阱 B：SUM(turnover)/SUM(volume) 环比在极端日失真**。首版新 SQL 用板块 VWAP 环比做收益，冒烟测试发现：2020-02-03（千股跌停日，全市场真实跌幅 -8.86%），VWAP 环比算出 **+12.6%** —— 跌停股无成交被排除在 VWAP 交易集合之外，方向直接反了。暴跌日正是板块轮动最关键的时点，此口径不可用。

**最终口径**：成分股**等权 change_pct 均值**（daily_bar.change_pct 为复权后涨跌幅，覆盖率 100%）：
- `sector_return` = AVG(成分股 change_pct)
- `market_return` = AVG(全市场 change_pct)
- `sector_relative_strength` = sector_return − market_return
- 异常行剔除：`pre_close <= 0`（坏前收）或 `|change_pct| > 25`（新股首日无涨跌幅限制）
- 冒烟验证（2020-02-03）：market_return = -8.88% ✓ 与真实一致；医药板块 +3.79%、相对强度 +12.68 ✓ 逆势暴涨故事正确

## 3. 最终 SQL 设计

### 3.1 新 sqlSectorDailyAgg()（已写入 DataSourceRegistry.h）

```sql
WITH sector_base AS (
 SELECT si.industry_code, db.trade_date,
  COUNT(*) AS stock_count,                                              -- 正常交易成分股数
  AVG(db.change_pct) AS sector_return,                                  -- 板块等权涨跌幅(%)
  COUNT(*) FILTER (WHERE db.close > db.open) * 1.0
    / NULLIF(COUNT(*), 0.0) AS sector_breadth,                          -- 成分股上涨家数占比
  AVG(db.amplitude) AS sector_amplitude,                                -- 成分股振幅均值
  SUM(db.turnover) AS sector_turnover                                   -- 板块总成交额(元)
 FROM mkt.daily_bar db
 JOIN ref.symbol_info si ON db.symbol_id = si.id
 WHERE db.trade_date BETWEEN '{start_date}' AND '{end_date}'
  AND si.industry_code IS NOT NULL
  AND db.pre_close > 0 AND db.change_pct BETWEEN -25 AND 25             -- 剔除新股首日/坏前收
 GROUP BY si.industry_code, db.trade_date
), market_base AS (
 SELECT db.trade_date, AVG(db.change_pct) AS market_return              -- 全市场等权涨跌幅
 FROM mkt.daily_bar db
 WHERE db.trade_date BETWEEN '{start_date}' AND '{end_date}'
  AND db.pre_close > 0 AND db.change_pct BETWEEN -25 AND 25
 GROUP BY db.trade_date
), sector_lagged AS (
 SELECT industry_code, trade_date, stock_count,
  sector_return, sector_breadth, sector_amplitude, sector_turnover,
  CASE WHEN stock_count >= 5 THEN 1.0 ELSE 0.0 END AS sector_is_reliable,
  AVG(sector_turnover) OVER (PARTITION BY industry_code ORDER BY trade_date
   ROWS BETWEEN 19 PRECEDING AND CURRENT ROW) AS avg20_turnover
 FROM sector_base
)
SELECT sl.industry_code, sl.trade_date, sl.stock_count, sl.sector_is_reliable,
 sl.sector_breadth, sl.sector_amplitude, sl.sector_turnover, sl.sector_return,
 sl.sector_return - mb.market_return AS sector_relative_strength,
 CASE WHEN sl.avg20_turnover > 0 AND sl.avg20_turnover IS NOT NULL
  THEN sl.sector_turnover / sl.avg20_turnover ELSE NULL END AS sector_turnover_ratio
FROM sector_lagged sl
JOIN market_base mb USING (trade_date)
ORDER BY sl.industry_code, sl.trade_date;
```

### 3.2 新 sqlSectorConcentration()（已写入）

源表 minute_bar（历史 amount 全 NULL，字段死亡）→ daily_bar.turnover，Top3 成交额占比，逻辑同构。

### 3.3 语义对照（旧 minute 口径 → 新 daily 口径）

| 字段 | 旧语义（错误） | 新语义（成分股级，两时代同口径） |
|---|---|---|
| sector_vwap_change → **sector_return**（更名） | 分钟bar子集 vwap 环比 | 成分股等权 change_pct 均值（复权涨跌幅%） |
| sector_breadth | **分钟bar** close>open 占比 | **成分股**上涨家数占比 |
| sector_amplitude | 板块内所有bar的最高-最低/平均开盘（覆盖越多值越大） | AVG(成分股 amplitude)，与个股 amplitude 同单位 |
| sector_is_reliable | 有分钟数据的成分股≥5只（覆盖率镜子） | 正常交易成分股≥5只（两时代均约1，变为行业规模标记） |
| sector_relative_strength | 板块vwap_change − 等权板块均值（C++算） | sector_return − market_return（SQL 同构定义） |
| sector_turnover / turnover_ratio | minute amount 聚合（历史全NULL→字段死亡） | daily turnover 聚合，真实可用 |
| sector_concentration | minute amount Top3 占比（死亡） | daily turnover Top3 占比，真实可用 |
| sector_money_flow_net / ratio | fund.money_flow_daily（已是日频源） | **不改**（重建后复核分布） |

## 4. 已改代码清单

- **DataSourceRegistry.h**：names()/numeric() 更名 sector_return；sqlSectorDailyAgg 重写（daily_bar + change_pct 口径 + 异常行剔除 + relative_strength SQL 化）；sqlSectorConcentration 换 daily_bar
- **RawMarketDataAssembler.cpp**：删 dailyMarketAvgVwapChg 声明、删相对强弱市场均值计算块、删注入时相对强弱写块（SQL 直接产出，sectorIdx 按 names() 自动带入）
- **DLFactor.cpp**：appendRequiredField 更名 sector_return（加注释）
- **train.py**（astock_engine）：SECTOR_FIELDS 更名 sector_return（加注释）
- **MarketDataRepository.h**：注释同步（daily_bar / 返回列名）

字段**数量、顺序**不变（9 名字 → 9 名字，第 1 个更名）→ FeatureTensorBuilder 匹配逻辑零改动，仍 19 raw + 4 market = 23 维。

## 5. 执行步骤与状态

1. ✅ SQL 冒烟验证（含 2020-02-03 暴跌日口径验证）
2. ✅ 编译 Release 通过
3. ✅ 重建 dataset_5（cleaning:all_market，7,662,458 行，isBacktestReady）→ 分年度验收（2026-08-13 实测）：
   - sector_amplitude vs 个股 amplitude：4.04/4.07 (2020) … 4.48/4.48 (2026)，差 ≤0.03 ✅
   - sector_is_reliable：0.963~0.975 全区间平稳，无 0.2→0.96 级跳变 ✅
   - sector_return vs 个股 change_pct：每年差 <0.05 ✅
   - sector_relative_strength：0.000~0.006，归零 ✅
   - 板块列覆盖：96.6% (2020) → 100% (2026)，单调爬升源于 2020 新股/缺 industry_code 行 ✅
4. AutoDL 重训（train.py 已含 sector_return）→ 部署 dl_v12
5. 回测对比：基线 = v11 回测（年化 3.75%、同窗口约 9.5%、503 笔）；目标 = v9 时代（同窗口 15.3%、2024-2026 窗口 28.9%）并超过

## 6. 不改的东西

- sqlSectorMoneyFlowAgg（fund.money_flow_daily 日频源，仅验收时复核）
- minute_daily_columns / HighFreqFactor（分钟数据补录是独立课题）
- FeatureTensorBuilder（维度/顺序契约不变）
- 分钟补录任务（证券宝 AutoDL）与本次修复解耦

## 7. P2 后续迭代（本次不做）

- sector_concentration / sector_turnover_ratio 恢复进训练（daily turnover 已可用，经典板块轮动信号，需同步 train.py 字段表 + 3 处契约）
- sector_is_reliable 升级为参与率语义（当日成分数/20日滚动最大成分数）
- money_flow 口径复核（2025/2026 均值漂移 -2.0e8→-2.9e8，重建后观察）
