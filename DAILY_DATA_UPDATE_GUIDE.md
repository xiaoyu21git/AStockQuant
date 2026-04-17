# 日线数据日终更新指南

## 目标

本指南用于规范 A 股 `daily_bar` 日线数据的日终更新方式，确保脚本只在“最近已收盘交易日”范围内执行一次性全量增量更新，避免把未收盘交易日误当成缺失数据。

当前主流程对应脚本：

- `tools/run_daily_update_pipeline.py`：一键串联执行更新与收口校验，适合作为计划任务入口。
- `tools/run_daily_update_pipeline.py --daily-close-profile`：推荐的日终自动模式，默认启用 latest/history 与主要日线回填，并允许可选步骤失败后继续执行。
- `tools/update_daily_data.py`：按最近已收盘交易日执行全量增量更新。
- `tools/check_data_latest.py`：检查数据库是否已更新到最近已收盘交易日。
- `tools/trading_day_utils.py`：交易日历、最近已收盘交易日、等待到收盘等公共工具。
- `tools/verify_daily_update.py`：更新完成后的收口校验脚本，输出落后股票样本并返回告警退出码。

## 运行原则

1. 更新目标日不是自然日“今天”，而是“最近已收盘交易日”。
2. 如果今天是交易日但尚未到收盘阈值，目标日自动回退到上一个交易日。
3. 默认对 `symbol_info` 中所有 `asset_class='STOCK'` 且 `status` 属于 `ACTIVE / ST / *ST / SUSPENDED / DELISTED` 的股票执行更新或特殊状态校验。
4. `ST / *ST / SUSPENDED / DELISTED` 不会被直接剔除；脚本会单独标记为“特殊状态股票”，其中退市股的目标交易日会自动收敛到退市日。
5. 不属于当前 A 股更新范围的代码会被单独标记为“跳过的非A股代码”，不计入未完成股票数。
6. 脚本运行结束后，会分别输出“普通落后股票”和“特殊状态股票”，便于区分真正的数据缺口与状态类待处理标的。

默认收盘阈值为 `15:30`，可通过命令行参数覆盖。

## 常用命令

### 0. 一键执行更新 + 校验

```powershell
python tools/run_daily_update_pipeline.py
```

推荐的日终自动模式：

```powershell
python tools/run_daily_update_pipeline.py --daily-close-profile --report-file logs/daily_update/latest.json
```

适用场景：

- 作为日常手工入口。
- 作为 Windows 计划任务的单一目标命令。
- 希望统一拿到更新和校验两个阶段的退出码。

常用变体：

```powershell
python tools/run_daily_update_pipeline.py --wait-until-close --close-time 15:40
python tools/run_daily_update_pipeline.py --target-date 2026-03-31
```

### 1. 立即更新到最近已收盘交易日

```powershell
python tools/update_daily_data.py
```

适用场景：

- 当天收盘后手工执行。
- 非交易日补齐最近一个交易日数据。
- 例行任务补跑。

### 2. 等到收盘后再自动执行一次

```powershell
python tools/update_daily_data.py --wait-until-close
```

适用场景：

- 提前启动脚本，由脚本阻塞到下一次收盘后再执行一次更新。

### 3. 指定目标交易日重跑

```powershell
python tools/update_daily_data.py --target-date 2026-03-31
```

适用场景：

- 回补某个指定交易日。
- 排查某一天数据缺失问题。

### 4. 自定义收盘判断时间

```powershell
python tools/update_daily_data.py --close-time 15:40
```

适用场景：

- 上游数据源通常在官方收盘后数分钟才稳定返回完整日线。

### 5. 检查数据库是否已更新到目标日

```powershell
python tools/check_data_latest.py
```

返回值：

- `True`：`daily_bar` 已更新到最近已收盘交易日。
- `False`：数据库仍落后于最近已收盘交易日。

### 6. 更新完成后的收口校验

```powershell
python tools/verify_daily_update.py
```

返回码：

- `0`：校验通过。
- `2`：数据库最大交易日未达到目标交易日，或仍有股票落后于目标交易日。

常用变体：

```powershell
python tools/verify_daily_update.py --sample-limit 50
python tools/verify_daily_update.py --target-date 2026-03-31
```

## 推荐的自动化方式

建议不要让脚本常驻轮询，而是使用操作系统计划任务在每日收盘后固定时间触发一次。

Windows 任务计划程序建议：

1. 触发时间设置为交易日的 `15:35` 或 `15:40`。
2. 程序使用当前 Python 解释器。
3. 参数填写：

```powershell
tools/run_daily_update_pipeline.py --daily-close-profile --report-file logs/daily_update/latest.json
```

4. 起始目录设置为项目根目录。
5. 如需更保守地等待上游稳定，可把参数改为：

```powershell
tools/run_daily_update_pipeline.py --daily-close-profile --close-time 15:40 --report-file logs/daily_update/latest.json
```

如果计划任务运行时点不稳定，也可以直接配置：

```powershell
tools/run_daily_update_pipeline.py --daily-close-profile --wait-until-close --close-time 15:40 --report-file logs/daily_update/latest.json
```

这会让任务先启动，再阻塞到下一次收盘阈值后执行更新，并在更新结束后自动做收口校验。

## 输出与校验

`tools/update_daily_data.py` 执行结束后会输出以下统计：

- `success_symbols`：成功处理的股票数。
- `failed_symbols`：接口调用失败的股票数。
- `incomplete_symbols`：返回数据最大交易日尚未达到目标交易日的股票样本数。
- `skipped_non_a_share_symbols`：因代码不属于当前 A 股更新范围而被跳过的数量。
- `fetched_rows`：抓取到的总行数。
- `written_rows`：写入或更新数据库的总行数。
- `partial_write_symbols`：整股批量写入失败后，通过逐行降级成功保住部分数据的股票数。

`tools/run_daily_update_pipeline.py --report-file ...` 会额外写出 JSON 摘要，包含：

- 每个步骤的命令与退出码。
- 是否为关键步骤或可选步骤。
- 最终状态是 `success`、`partial_success` 还是 `failed`。

如果 `incomplete_symbols` 非零，说明本次运行虽然已执行，但上游数据源可能尚未完全就绪，不应直接视为“当日数据已全部到齐”。

建议在任务后增加一次检查：

```powershell
python tools/check_data_latest.py
```

如需把告警直接交给计划任务或外部调度系统判断，推荐改用：

```powershell
python tools/verify_daily_update.py
```

这个脚本会在校验失败时返回非零退出码，并打印落后股票样本。
非 A 股代码会单独列出，但不会计入落后股票数。

必要时可结合数据库查询核对最大交易日：

```sql
SELECT MAX(trade_date) FROM daily_bar;
```

## 数据来源与范围说明

- 当前日线更新脚本使用 AKShare 的 A 股日线接口。
- 股票范围来自数据库 `symbol_info` 表中的活跃股票。
- 脚本会按每只股票当前已落库的最大 `trade_date` 做增量抓取，不重复全表重写。

## 典型运维流程

### 日常更新

1. 计划任务在收盘后触发 `python tools/run_daily_update_pipeline.py`。
2. 检查日志中是否出现大量 `failed_symbols`、`incomplete_symbols` 或落后样本。

### 异常补跑

1. 先执行 `python tools/check_data_latest.py` 判断数据库是否落后。
2. 若仅个别日期缺失，执行 `python tools/update_daily_data.py --target-date YYYY-MM-DD`。
3. 若收盘后接口返回仍不完整，可延后时间并重跑，例如 `--close-time 15:40`。
4. 重跑后执行 `python tools/verify_daily_update.py --target-date YYYY-MM-DD` 确认是否补齐。

## 相关实现文件

- [tools/run_daily_update_pipeline.py](tools/run_daily_update_pipeline.py)
- [tools/update_daily_data.py](tools/update_daily_data.py)
- [tools/check_data_latest.py](tools/check_data_latest.py)
- [tools/verify_daily_update.py](tools/verify_daily_update.py)
- [tools/trading_day_utils.py](tools/trading_day_utils.py)
