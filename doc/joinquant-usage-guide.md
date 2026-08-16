# 聚宽平台使用与调试指南 — trend_following_88575b08（多因子）

> 对应策略文件: `tools/joinquant/trend_following_88575b08.py`
> 目的: 在聚宽平台验证/调参/换因子/改规则, 后续与 C++ 引擎 MultiFactorStrategy 对照。
> 策略类型已由趋势跟踪改为**多因子**（用户裁定 2026-08-15）: 因子组合=换手率稳定性单因子, 出场=结构止盈(ML 实例 862b0b8d 模板)+多因子评分/排名卖出, 市场闸门=DB 3 个市场模板(p99/p97 冻结 + p90/p84 放行模式)。

## 1. 平台操作（首次上手）

1. 打开 [www.joinquant.com](https://www.joinquant.com)，注册/登录（免费账号即可跑日频回测）。
2. 顶部导航 → **我的策略** → **新建策略**，命名（如 `trend_following_88575b08`），语言选 **Python3**。
3. 把 `trend_following_88575b08.py` **整体粘贴**覆盖编辑器默认模板（文件自包含：平台内置对象 `get_price / log / g / order_* / set_*` 无需 import，唯一的外部 import 是 `jqdata` 模块——平台已提供）。代码已按**新版平台**（jqboson 引擎）适配：`from jqdata import *` + `handle_data(context, data)` 双参签名。
4. 保存。

## 2. 回测设置

| 设置项 | 建议值 | 说明 |
|---|---|---|
| 回测频率 | **日** | 引擎为日频策略，必须日频 |
| 时间区间 | 2019-01-01 ~ 2026-07-31 | 起始越早，换手因子预热占比越小 |
| 初始资金 | 1,000,000 | 与引擎一致 |
| 股票池 | 全A（默认） | 全A 回测耗时较长；冒烟可把 `STRATEGY['universe_mode']` 改为 `'hs300'` |

点击 **编译运行** → 等出结果。数据层已用增量缓存加速（用户裁定 2026-08-15）：首日预热 close 160/high 61/指数 160 行，之后每日 3 次 count=1 单行查询；换手率每 5 交易日批量补拉区间内各交易日（get_fundamentals 单日查询，单日全A ~4850 行 < 平台 10000 行/次上限）；若仍超时（平台单次回测有运行时间上限）：
- 缩短区间（如近 3 年）；
- 或 `universe_mode='hs300'` 快速冒烟；
- 不要用分钟频率（策略按日设计，分钟会巨慢且语义不符）。

## 3. 结果解读

- **收益图/回撤图**: 与引擎基线对比时注意差异清单（doc/joinquant-conversion-notes.md），重点差异：**决策数据锚点=上一交易日（信号延迟 1 日，差异 #25）**、换手因子前 60 日缺数据、佣金滑点近似、买卖按当日收盘价。
- **交易明细**: 每笔成交含佣金/滑点/税费明细。
- **日志输出**: 每日 `[评估]` 行含 breadth/回撤/volShock/regime/择时状态/冻结原因/放行模式；`[买入]/[卖出]/[规则出场]` 行含 score/目标金额/规则名；`[凯利]` 行滚动输出胜率/赔率/全凯/半凯；`[组合]` 行每 20 日或有交易时输出持仓与净值。日志是调参的第一手证据——先看冻结原因/放行模式再看信号。

## 4. 调参数（只改 STRATEGY 区）

全部参数集中在文件顶部 `STRATEGY` 字典，每项注明引擎来源（ML 实例 862b0b8d 参数快照/DB 模板 payload/代码常量）。常用调试项：

| 想验证什么 | 改哪 |
|---|---|
| 入选/买入灵敏度 | `min_composite_score`（默认 0，只做多）、`top_n`（入选池截断, 默认 50） |
| 持仓评分卖出松紧 | `sell_threshold`（默认 0.2，持仓 composite 跌破即卖）、`sell_rank_multiplier`（默认 2.0，全截面排名 > max_positions×2 卖出） |
| 更大/更小仓位 | `max_weight_per_stock`（单票上限, 默认 0.1）、`max_positions`（默认 20） |
| 换手因子权重 | `factor_weights`（当前 `{'turnover_stability': 1.0}` 仅换手因子；加因子如 `{'lowvol': 1.0, 'turnover_stability': 0.5}` → 归一化后 2/3 : 1/3） |
| 结构止盈松紧 | `exit_rules.*`（5 条件阈值：前高 0.85/超涨 1.1/回撤 6%/动能 50/支撑 0.98 + reduce_ratio 0.3） |
| 涨跌停过滤 | `limit_up_ratio` / `limit_down_ratio`（创业板/科创板可改 1.198/0.802 混合口径） |
| 市场冻结松紧 | `market_freeze.*`（p99 熊市体制冻结 / p97 准熊市退潮冻结 两条；p95 高波动冻结已按用户裁定移除，见 FAQ） |
| 市场放行模式阈值 | `market.*`（p90 牛市趋势放行 / p84 震荡市精选放行；仅记日志不阻断） |
| 熔断器 | `circuit_breaker.*`（max_drawdown=0.11 对齐 ML 实例；enabled=False 关闭） |
| 每单风控 | `risk.*`（对齐 ML 实例 862b0b8d：stop_loss_pct=10 / take_profit_pct=20 / max_drawdown_limit_pct=99） |

**调参流程**: 先跑 `python tools/joinquant/smoke_test_local.py` 确保语法/基础逻辑不被改坏 → 再上平台小区间快跑 → 看日志定位 → 全区间。

## 5. 换因子 / 加因子

因子层是注册表模式，三步：

1. **写 compute 函数**（在 ④ 因子层区域）：
   ```python
   def compute_my_factor_scores(close, idx_close):
       # 输入: 收盘面板 (rows×codes) + 指数收盘 Series; 输出: {code: score}
       # 惯例: score 越大越好 (composite 用截面 z-score, 方向无所谓)
       return {c: float(...) for c in close.columns if ...}
   ```
2. **注册**（`FACTORS` 表加一行）：
   ```python
   FACTORS = {
       'lowvol': compute_lowvol_scores,
       'turnover_stability': compute_turnover_stability_scores,
       'my_factor': compute_my_factor_scores,   # ← 新增
   }
   ```
3. **配权重**（`STRATEGY['factor_weights']` 加该名字，相对权重自动归一化；缺任一配置因子值的股票在合成时剔除）。

需要新数据字段时（如换手率那样聚宽 get_price 没有的字段），参考 `update_turnover_cache` 的缓存模式：每 5 交易日批量补拉区间内交易日（get_fundamentals 单日查询，官方标准接口），逐日按序累积 deque（某日失败 → 起点停在该日，下批重试且已成功日不重复），compute 函数从缓存读。

## 6. 改 / 加出场规则

出场规则层是有序表（优先级第一命中），两步：

1. 在 `_exit_rules()` 列表按优先级插入：
   ```python
   def r_my_rule(d):   # d: dict(close, pnl, ma20, high60, holding_high, health)
       return ('REDUCE', '我的规则(pXX)') if <条件> else None
   ```
2. 阈值放 `STRATEGY['exit_rules']`（禁止散落魔法数字）。

市场冻结规则同理改 `_freeze_rules()` 列表（priority 降序第一命中），阈值在 `STRATEGY['market_freeze']`；放行模式改 `market_allow_mode()` 内 p90/p84 模板（仅记日志），阈值在 `STRATEGY['market']`。

## 7. 常见问题

| 现象 | 原因/处理 |
|---|---|
| `ImportError: cannot import name ...` | 新版沙箱不支持 jqdata 命名导入，已改 `from jqdata import *`（平台规范形式），重新粘贴最新文件 |
| `handle_data() takes 1 positional argument but 2 were given` | 新版平台（jqboson）按 `handle_data(context, data)` 双参调用，已适配；`data` 当日快照本策略不使用（面板统一走 get_price） |
| `avoid_future_data=True，get_price取天行情时，盘中不能取当日的close字段数据` | 新版平台日频回测固定 09:30 运行一次、当日 close 属未来数据（平台口径）。已按方案A把决策数据锚点改为上一交易日（`context.previous_date`），订单当日收盘成交——信号延迟 1 日是平台硬约束下的忠实适配，非 bug |
| 日志频繁 `冻结: 高波动冻结(p95)` | 该规则已按用户裁定移除（2026-08-15），重新粘贴最新文件后不再出现；`[评估]` 行仍打印 volShock 值仅作观察。此为与引擎的刻意偏差（引擎仍有 p95，见差异 6a） |
| `get_price 取多只股票数据时, 为了对齐日期, 不能跳过停牌` | 新版平台多股票面板禁止 `skip_paused=True`（日期对齐与跳停牌冲突，平台口径）。已改为不传该参数：停牌日个股返回 NaN，下游信号/因子/出场各处 `np.isfinite` 检查自动跳过（与引擎"停牌跳过"同语义），重新粘贴最新文件 |
| `不建议继续使用panel` / `Panel is deprecated` 告警 | 新版平台弃用 `panel=True`（每次调用构造 pandas Panel 并告警，将来升级 pandas 后策略会失败）。已全部迁移 `panel=False`（平台推荐路径）——旧面板格式下信号层列名判断失效会零成交（差异 #27），重新粘贴最新文件后告警消失 |
| `TypeError: ufunc 'isfinite' not supported` | `panel=False` 的返回布局随平台引擎版本变化（宽表 MultiIndex 列/长表行索引两种口径）。已做布局自适应归一化 + 值标量保护；若日志出现 `[数据] 收盘面板布局无法识别`（附 type/shape/列示例/行索引示例），把该行日志发回分析，重新粘贴最新文件 |
| `NotImplementedError: ops for Rolling for this dtype datetime64[ns] are not implemented` | 第三种平台布局：date/code 作为**数据列**而非索引返回（面板混入 datetime64 列，数值滚动无法处理）。已支持维度列变体归一化 + 归一化后强制数值清洗（datetime64 列丢弃并打 `[数据] 面板混入日期列` warn，无法数值化列同样丢弃+告警，绝不静默不崩溃），重新粘贴最新文件 |
| 前 60 交易日零开仓 | 换手缓存从回测开始累积（差异 #15）；当前因子层仅换手率稳定性（用户裁定）→ 前 60 交易日候选池为空、无买入，正常。不想空窗可提前 60 交易日开跑或启用低波因子 |
| 回测超时/被限制 | 数据层已增量缓存（首日预热后每日 3 次单行查询），仍超时则缩短区间或 `universe_mode='hs300'` |
| 无任何买单 | 按日志分类定位（数据失败路径已全部显式 `[数据]` warn，不再静默）：①无 `[评估]` 行但有 `[数据]` warn → 按 warn 内容处理（面板为空/历史不足/布局无法识别/换手率查询失败或返回为空）；②有 `[评估]` 行但多为"冻结: xxx" → 市场/择时闸门拦截（冻结日禁新开仓，引擎 live blockNewBuys 同语义，看冻结原因）；③有 `[评估]` 行放行、`[组合]` 池正常但无 `[买入]` → 策略正常表现：多因子买入条件是 composite>0 且全截面 Top-50（全市场评分不佳时不买）+ 涨跌停过滤 + 目标不足 1 手跳过。若连 warn 都看不到，检查平台日志面板的级别过滤（需显示 INFO/WARNING） |
| 熔断后 5 日无交易 | 设计意图（引擎回测中该倒计时有缺陷=永久熔断，本转换按意图恢复） |
| 持仓被清仓 | 两条评分卖出路径（引擎 MultiFactorStrategy 阶段3）：持仓 composite < sell_threshold(0.2) → 评分卖出；全截面排名 > max_positions×sell_rank_multiplier(40) → 排名卖出。另有结构止盈 5 条件（EXIT 全清/REDUCE 减 30%）。先看日志 `[卖出]` 行的 reason 再调对应阈值 |

## 8. 数据口径速查

- 复权: `fq='pre'` 前复权全链一致；`use_real_price=True` 拆分除权还原。
- 成本: 佣金双边万 2.5（最低 5 元）+ 卖出印花税 0.1% + `PriceRelatedSlippage(0.001)` 双边约 0.1% 滑点（`initialize` 内可调）。
- 成交: 日频 `handle_data` 收盘决策、当日收盘价成交（引擎 14:50 决策/收盘成交的等价物）。
- 停牌: 多股票面板不传 `skip_paused`（新平台禁止该组合，见常见问题）；停牌日个股为 NaN → 下游 `np.isfinite` 检查自动跳过（引擎同语义）。
- 面板: 全部 `get_price` 用 `panel=False`（新平台弃用 `panel=True`，见常见问题）；返回布局自适应归一化为标准宽表（MultiIndex (field, code) 列取代码层 / 长表 (date, code) 行索引 unstack / date、code 作数据列的维度列变体先落索引），归一化后强制数值清洗（datetime64/无法数值化列丢弃 + `[数据]` warn），布局无法识别或清洗后无列时打诊断日志当日跳过。
- 增量缓存（用户裁定 2026-08-15 速度优化）: 首日预热 close 160/high 61/指数 160 行，之后每日 3 次 count=1 单行追加滚动缓存（按日期去重 + 尾裁剪），pre_close 由 close 缓存倒数第二行派生；单行日期≠锚点 → `[数据] ... 日期与锚点不符` warn + 当日跳过。出场层仅需 close/high/指数三份数据（volume/low/open 查询已随 10 条旧出场规则删除）。
- 换手率（因子层）: 聚宽 get_price 无换手率字段 → `get_fundamentals(query(valuation))` 单日查询（在线沙箱官方标准接口；原 get_valuation 为 JQData 本地 SDK 接口，且 4850只×5日 区间查询超平台 10000 行/次上限，已弃用）。每 5 交易日批量补拉区间内交易日，逐日按序追加 deque（60 有效日窗口）；查询异常/返回空/全无有效值均 `[数据]` warn 不静默，某日失败 → 起点停在该日下批重试（无数据空洞）。
