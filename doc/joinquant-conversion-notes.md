# 聚宽转换交付说明 — 多因子策略 88575b08

> 任务: 把策略类型从趋势跟踪改为**多因子**（与 C++ 引擎 MultiFactorStrategy 对齐，用户裁定 2026-08-15），连同市场出场/入场规则全部转换成聚宽 Python 代码，用于平台上回测验证。
> 交付日期: 2026-08-15

## 1. 交付物

| 文件 | 说明 |
|---|---|
| `tools/joinquant/trend_following_88575b08.py` | 聚宽回测代码（自包含，整体粘贴到聚宽"我的策略"即可；文件名保持不改，用户裁定） |
| `tools/joinquant/smoke_test_local.py` | 本地冒烟测试（桩 jqdata + 桩平台对象，无需聚宽平台；`python smoke_test_local.py`，20 组 174 断言） |
| `doc/joinquant-usage-guide.md` | 聚宽平台使用方法与调试指南 |
| 本说明文档 | 语义依据、与 C++ 引擎实现的差异清单、可调参数、已知限制 |

## 2. 使用方法

1. 打开聚宽 → 我的策略 → 新建策略，粘贴全部代码。
2. 回测设置: **日频**；建议区间 **2019-01-01 ~ 2026-07-31**；初始资金 **1,000,000**。
3. 全A 口径回测已用增量缓存加速（首日预热 close 160/high 61/指数 160 行，之后每日 3 次单行查询，见差异 #28）；快速冒烟可把 `STRATEGY['universe_mode']` 改为 `'hs300'`（沪深300 股票池）。

## 3. 策略语义概览（引擎侧依据）

| 环节 | 语义 |
|---|---|
| 入场 | 多因子六阶段流水线（`MultiFactorStrategy.cpp`，参数=ML 实例 862b0b8d 快照）：阶段1 每因子全截面统计（总体方差 sumSq/n-mean²，invStd=std>eps?1/std:0，z=clamp((val-mean)×invStd,-3,3)，std≤eps→z=0，缺任一配置因子值剔除该股）；阶段2 composite=Σ(weights×z)（单因子权重1.0 → composite=z）；阶段4 composite≥minCompositeScore(0) 且严格 >0（只做多）→ 降序 Top-50；阶段5 等权 1/n；阶段6 targetWeight=clamp(w, minWeight 0.01, maxWeight 0.1)，maxNewBuys=maxPositions(20)-当前持仓数，新标的且 maxNewBuys≤0 跳过，已持仓更新目标权重，newBuys≥maxNewBuys 后 break（此后连持仓更新也不发） |
| 因子池 | 换手率稳定性**单因子**（用户裁定 2026-08-15）；composite=该因子截面 z-score clamp[-3,3]；缺因子值股票剔除 |
| 持仓卖出 | 引擎阶段3（仅遍历当前持仓）：composite < sellThreshold(0.2) → 卖出("score")；否则全截面排名(1起全体降序) > maxPositions×sellRankMultiplier(20×2=40) → 卖出("rank")；持仓缺因子值 → 无信号仓位保留 |
| 市场快照 | breadth60/20 = P(close>MA60/MA20)；等权指数 = 日收益等权平均累积（60日窗口从1.0起，峰值含1.0）；drawdown=1-idx/peak；volShock=min(1,vol5/vol60) 年化√250；**indexClose=等权指数末值（非外部指数序列）**，indexMa120=近120日指数水平均值，indexAboveMa120=indexClose/indexMa120（同源同尺度）；trendStrengthScore=breadth60（引擎别名）；regime: b60≥0.55→bull，b60≤0.35且b20≤0.35→bear，其余→sideways（`RuleVariableProvider::computeMarketSnapshot`） |
| 市场闸门 | **p99 熊市体制冻结**（regime==bear → 冻结新开仓）+ **p97 准熊市退潮冻结**（breadth60<0.3 且 drawdown≥0.1 → 冻结新开仓），第一命中即冻结；冻结日新标的禁买但持仓仍可更新目标权重（引擎 live blockNewBuys 语义）；**p90 牛市趋势放行**（bull && breadth60≥0.58 && indexAboveMa120≥1.01 && trendStrength≥0.62 → active）/ **p84 震荡市精选放行**（sideways && breadth60∈[0.38,0.58] && volShock≤0.58 && drawdown≤0.1 → selective_active）仅记日志不阻断（引擎 DB 模板同语义）。**p95 高波动冻结已按用户裁定移除（见差异 6a）**；趋势版管道宽度冻结随策略类型变更不再适用（regime==bear 与 p99 同条件） |
| 择时闸门 | 000300.SH: 进攻(above60+trendUp+上涨家数>0.5)→exp1.0；谨慎(above60+ratio<0.35)→0.5；防御(!above60&&above20)→0.2禁新；空仓(!above20&&!trendUp&&ratio<0.30)→0.0强制清仓；高波动(ATR>3%||截面vol>3%)→0.3禁新（引擎恒不触发）；默认→0.5（`MarketTimingGate`） |
| 出场规则 | **结构止盈 5 条件有序第一命中**（ML 实例 862b0b8d rule_profile 结构止盈 summary，模板 exit_scale_out_take_profit）：①前高85%减30（pnl≥3% 且 close/60日高(不含今)≥0.85）②超涨MA110%减30（pnl≥8% 且 close/MA20≥1.1）③回撤6%清仓（距持仓高点回撤≥6%）④动能<50减30（趋势健康分<50）⑤支撑破位清仓（close/MA20<0.98）；REDUCE=持仓×reduce_ratio(0.3)；trendHealthScore 四项各+25（MA20/MA60 斜率向上 + close>MA20/MA60，单项缺数据独立跳过，`RuleVariableProvider` kMaSlopeWindow=5） |
| 熔断器 | TimedCircuitBreaker: 峰值回撤≥**11%**（对齐 ML 实例 maxDrawdownLimit=11）→熔断5交易日（清仓+暂停交易），倒计时每日递减归零恢复（峰值重置）；单日亏损≥3%→减仓至50%（checkIntraday 引擎零调用者，转换中默认关闭） |
| 每单风控 | RiskEvaluator 6 步验证管道（信号强度/止损/止盈/账户回撤/单票集中度/总敞口/卖出可卖数）；**数值对齐 ML 实例 862b0b8d：stopLossPercent=10 / takeProfitPercent=20 / maxDrawdownLimit=99**（其余禁用）→ 浮亏≥10%或浮盈≥20%拒加仓 |
| 事件风控 | EventRiskSubscriber: stock 清仓/限仓、sector 行业限仓、market 总敞口收缩、tags 立案调查/ST 封禁、T+1 解禁；数据源为 live EventBus news 事件，回测无此数据流 → 默认关闭 |
| 凯利 | 回测后统计仅输出（胜率=盈利卖笔/总卖笔，赔率=avgWin/avgLoss，全凯=p-(1-p)/b，半凯=全凯×0.5），不参与仓位；卖出日输出滚动值 |
| 其他 | 涨跌停过滤（买1.098/卖0.902；评分/排名卖出跌停跳过，结构止盈出场不查跌停）；订单去重(code,side)先到先得（策略卖优先于结构止盈）；买单 Σ权重>1 等比压缩；目标不足1手→卖出全清/买入跳过 |

## 4. 与 C++ 引擎实现的差异清单

| # | 差异点 | 引擎行为 | 转换行为 | 原因 |
|---|---|---|---|---|
| 1 | 低波因子组件权重 | DB 中 volatilityWeight/drawdownWeight/betaWeight 全为 0 → 因子生产失效（factor_values 0 行） | FACTORS 注册表保留 `compute_lowvol_scores` 实现，但未配权重 = 不执行 | 引擎内该因子实际不产出任何值，无从复刻；注册表保留便于启用 |
| 2 | 因子权重 | ML 实例 862b0b8d 为 AI 因子单因子 | 用户裁定（2026-08-15）：**仅用换手率稳定性**（`factor_weights={'turnover_stability': 1.0}`，composite=该因子截面 z）；启用/换因子只需改 `STRATEGY['factor_weights']` | 用户明确指示"只用换手率稳定性"；聚宽侧 AI 因子（ONNX）无从复刻 |
| 3 | 出场规则体系 | 趋势版 10 条模板规则（p97 硬止损/p94 破位加速/p92 利润保护/p91 趋势破坏/p88 承接崩塌/p86 破5日线/p84 预警减仓/p83 前高压力/p82 支撑破位/p80 超涨） | **用户裁定（2026-08-15）：删除全部 10 条**，改为 ML 实例 862b0b8d 结构止盈 5 条件 + 引擎 MultiFactorStrategy 阶段3 评分/排名卖出。承接强度分随之删除（唯一消费者已无）；前高阈值按 summary 口径 0.85（模板 JSON 为 0.9） | 策略类型从趋势跟踪改为多因子（本任务核心）；原 #3 p92 利润保护（引擎死规则 holdDays 恒0）与原 #4 p84 预警减仓（引擎单位 bug 永不触发）随 10 条规则一并删除 |
| 4 | Reduce 卖出比例 | 引擎 buildRuleExit 恒卖一半（held/2），忽略模板 payload 的 reduce_ratio 0.3 | **按 ML 模板意图 reduce_ratio=0.3**（"减30"，与 862b0b8d summary 一致） | 结构止盈 summary 明示"减30"，模板意图明确 |
| 5 | 市场闸门集合 | 趋势版 4 条闸门（p99/p97/p95/管道宽度冻结）+ 规则闸门旁路差异 | **用户裁定（2026-08-15）：DB 3 个市场模板**——p99+p97 冻结保留，p90/p84 放行模板 → `market_allow_mode` 仅记日志 mode（引擎 DB 模板同语义）；p95 与管道宽度冻结移除（见 6a）；冻结日禁新开仓但持仓更新保留（引擎 live blockNewBuys 语义） | 对齐 ML 实例 862b0b8d 市场闸门组（3 个市场模板） |
| 6 | 闸门语义 | 市场冻结在引擎回测真实阻断买单（Facade L2177）；择时 allowNewEntries 在 backtest 路径成交后才删单（cosmetic，不回滚），live 管道才真阻断 | 市场冻结与引擎回测一致（冻结日跳过非持仓评估）；择时按 **live 语义**：allowNewEntries=false 或冻结 → 跳过非持仓股票评估；持仓在池仍可加仓（live 管道 L377 同语义） | 实盘行为才是策略设计意图；市场冻结双路径一致 |
| 6a | 高波动冻结（已移除） | 引擎 volShock=min(1, vol5/vol60)（5日/60日等权指数波动比），p95 阈值 ≥0.7 → 冻结 | **用户裁定（2026-08-15）：移除 p95 规则**（`_freeze_rules` 表项与 `vol_shock_freeze` 配置键删除；volShock 仍计算并进 `[评估]` 日志，仅作观察） | 回测中平静市 vol5≈vol60 → volShock≈1 封顶 → 冻结几乎天天触发拦截新开仓，用户要求去掉 |
| 7 | 涨跌停阈值 | 引擎对所有板块统一 1.098/0.902 | 忠实保留（创业板/科创板实际 20% 限制也按 10% 过滤）；评分/排名卖出跌停跳过、结构止盈出场不查跌停（引擎规则出场同语义） | 忠实引擎 |
| 8 | 股票池 | 引擎为清洗后全A池 | 全A 剔除 ST/退市/次新(<250自然日)/北交所 | 聚宽侧近似，保证窗口数据充足 |
| 9 | 中性化/滞后 | 因子配置 neutralizationEnabled/laggedEnabled=true 但引擎**未实现** | 不实现（当日数据、无中性化） | 引擎本身未实现 |
| 10 | 标准化 | standardization=1（z-score） | 不单独实现；composite 已含截面 z-score | 引擎 MultiFactorStrategy 同口径 |
| 11 | 换手率数据源 | 引擎日线视图 turnover 字段 | 聚宽 get_price 无换手率 → get_fundamentals(query(valuation)) turnover_ratio 单日查询（在线沙箱官方标准接口；get_valuation 为 JQData 本地 SDK 接口且区间查询 4850只×5日 超平台 10000 行/次上限，已弃用），缓存 60 个有效日（停牌日缺省） | 平台数据口径 |
| 12 | 成交时点 | 引擎 14:50 决策、收盘成交 | 聚宽日频 09:30 决策（锚点=上一交易日收盘面板）、当日收盘成交 | 平台撮合口径（见 #25） |
| 13 | 交易成本 | 实盘按通道 | 佣金双边万2.5（最低5元）+ 卖出印花税0.1% + 双边滑点0.1%（PriceRelatedSlippage） | 可调，见 initialize |
| 14 | 复权 | 引擎数据集 | 聚宽 fq='pre' 前复权全链一致 | 平台口径 |
| 15 | 换手因子预热 | 无（引擎数据集有历史） | 回测前 60 交易日换手因子无数据 → **因子层全空 → 候选池空 → 无买入**（差异 #2 仅用换手因子后，前 60 交易日策略零开仓） | 缓存从回测开始累积 |
| 16 | 熔断器 | 引擎 circuitBreaker 参数随实例 | 已移植 TimedCircuitBreaker（峰值回撤/倒计时/清仓/单日减仓全语义），**回撤线对齐 ML 实例 862b0b8d maxDrawdownLimit=11**（趋势版为 15） | 参数对齐裁定实例 |
| 17 | 买单资金不足 | 引擎按手数截断（lots==0 跳过） | 聚宽按可用现金自动约束（整手） | 等价 |
| 18 | 买入压缩 | 压缩所有买单数量 ×1/Σ权重 | 等比压缩目标金额；压缩后加仓目标低于现仓 → 跳过该单 | 防 order_target_value 意外卖出，微差异 |
| 19 | 择时 exposure | 引擎 EOD 管道记录但未用于仓位缩放 | 同样仅记录日志、不缩放仓位 | 忠实引擎 |
| 20 | 熔断倒计时 | **引擎缺陷**: 熔断分支内不调 updateEndOfDay + rebalanceFrequency=0 恒调仓日 → 倒计时永不递减 = 永久熔断 | 按设计意图每日递减，halt_days 日后恢复（峰值重置为当前净值） | 模板/代码意图明确，引擎为隐性缺陷 |
| 21 | 每单风控 | ML 实例 862b0b8d: stopLossPercent=10 / takeProfitPercent=20 / maxDrawdownLimit=11 | 完整 6 步验证管道 + **数值对齐 ML 实例：stop=10 / take=20 / 账户回撤线=99**（趋势版构建值全 0/99 橡皮图章）；买单路径审核（卖单数量已按持仓截断，卖出侧检查等价） | 用户裁定风控参数对齐 862b0b8d 实例；账户回撤线 99=永不触发（引擎 maxDrawdownLimit=11 由熔断器承担，见 #16） |
| 22 | 事件风控 | live EventBus news.* 事件驱动（Python 新闻管线） | 结构层移植（applyEventTags/清仓名单/行业限仓/T+1 解禁语义原样），默认关闭；接入数据源后在 handle_data 调 apply_event 并打开开关 | 聚宽回测无新闻数据流；引擎回测路径同样无事件 |
| 23 | 盘中持仓巡检 | live tick 驱动 patrolPositions（盘中逐 tick 巡检事件清仓名单） | 不移植（日频回测无盘中 tick） | 频率模型不同，无对应物 |
| 24 | 凯利输出时机 | 回测结束后一次性统计输出 | 卖出日输出滚动值（统计口径一致） | 信息性输出，不参与仓位 |
| 25 | 决策数据锚点 T-1 | 引擎 14:50 用当日 in-progress 日线决策 | 用上一交易日收盘面板决策（`context.previous_date`），订单当日收盘成交。信号识别/出场/冻结/择时整体延迟 1 个交易日（买卖均延迟，无系统偏向）；涨跌停过滤口径=上一交易日状态；换手率缓存锚点 T-1；凯利盈亏价按 T-1 收盘近似 | **新平台硬约束**：日频回测 handle_data 固定 09:30 运行一次 + avoid_future_data 禁止取当日 close（平台口径已查证）；用户裁定方案A（2026-08-15） |
| 26 | 停牌面板口径 | 引擎数据链停牌日无该股数据（序列缺省） | 多股票面板不传 `skip_paused`（新平台禁止面板+跳停牌组合）；停牌日个股为 NaN → 下游信号/因子/出场/涨跌停过滤各处 `np.isfinite` 检查跳过 | **新平台硬约束**；跳过语义与引擎一致，仅表示形式从"缺行"变"NaN 行" |
| 27 | 面板查询接口 | 引擎无面板概念（DB 数据链） | `panel=True` 已被新版平台弃用（每次调用构造 pandas Panel 并告警，平台称将来升级 pandas 后策略会失败）→ 全部 get_price 改 `panel=False`；返回布局随平台引擎版本变化（社区实测多种口径），`_normalize_panel` 自适应五形态：A 宽表 MultiIndex (field, code) 列取代码层 / B 长表 (date, code) 行索引 unstack 成宽表 / C 标准宽表原样（防御性副本）/ D 维度列变体（date/time/code 作为数据列而非索引，部分引擎版本）先落索引再 unstack / E 索引已含时间且带重复 date 数据列 → 丢弃重复列；归一化后强制数值清洗：datetime64 列丢弃 + warn、其余非数值列 pd.to_numeric 失败则丢弃 + warn、清洗后列全空 → None；布局无法识别 → 打印 `[数据] 收盘面板布局无法识别` 诊断日志（type/shape/列示例/行索引示例）当日跳过，不静默不崩溃 | 平台 UserWarning 明确建议 panel=False；旧面板格式下信号层 `code not in close.columns` 对字符串 code 恒成立 → 全候选跳过 = 零成交；布局未知时报错 `TypeError: ufunc 'isfinite' not supported`（值非标量），已加标量保护与布局诊断；实测第三种形态：平台把 date/code 作数据列返回 → datetime64 列混入面板 → 数值 rolling 抛 `NotImplementedError: ops for Rolling for this dtype datetime64[ns]`（修复：维度列识别 + 数值清洗兜底） |
| 28 | 增量数据层（速度优化） | 引擎 EOD 数据链逐日从 DB 读取 | 首日预热 close 160 行/high 61 行/指数 160 行，之后每日 **3 次** count=1 单行面板追加到滚动缓存（按日期去重 + 尾裁剪，缓存行数不变）；pre_close 由 close 缓存倒数第二行派生（省一次查询）；单行日期≠锚点 → `[数据] ... 日期与锚点不符` warn + 当日跳过（缓存不动次日重试）；换手率每 5 交易日批量补拉区间内各交易日（区间交易日从收盘缓存索引枚举，逐日 get_fundamentals 单日查询；某日失败 → 起点停在该日、已成功日不重复，下一批从失败日重试，无数据空洞；查询异常/返回空/全无有效值均 `[数据]` warn 不静默） | 用户裁定速度优化（2026-08-15）：出场层仅需 close/high/指数三份数据，volume/low/open 查询已随 10 条旧出场规则删除 → 每日查询数由趋势版 6 次再降为 3 次；决策数据与逐日全量拉取逐值相同（缓存末行=锚点 T-1），策略语义/参数/闸门零改动 |
| 29 | indexAboveMa120 口径 | 引擎 snapshot.indexClose=股票面板等权指数末值（非外部指数序列），indexMa120=近120日指数水平均值 → 比值同源同尺度（`RuleVariableProvider.cpp` L1233/L1262-1277） | **同样用等权指数末值/指数均值**（非沪深300绝对点位）。若误用外部指数绝对价（~4000）/等权指数（~1.0）会出现比值恒 >>1.01 的尺度错配（p90 指数条件形同虚设），实现已按引擎同源口径修正 | 忠实引擎口径 |

## 5. 可调参数（STRATEGY 区）

| 键 | 默认 | 说明 |
|---|---|---|
| max_positions | 20 | 持仓上限/新买单截断；排名卖出阈值=max_positions×sell_rank_multiplier |
| top_n | 50 | 入选池截断（引擎 targetPositionCount） |
| sell_threshold | 0.2 | 持仓评分卖出线（composite < 即卖） |
| sell_rank_multiplier | 2.0 | 排名卖出倍数（全截面排名 > max_positions×该值 → 卖出） |
| min_composite_score | 0.0 | 入选门槛（且严格 >0 只做多，引擎 minimumCompositeScore=0） |
| weight_scheme | 'equal' | 等权 1/n（引擎 weightScheme=0 EQUAL） |
| min_weight_per_stock / max_weight_per_stock | 0.01 / 0.10 | 目标权重 clamp（引擎 minWeightPerStock/maxWeightPerStock） |
| factor_weights | turnover_stability 1.0 | 因子权重（自动归一化；用户裁定仅用换手率稳定性，低波保留注册表未启用） |
| turnover.window / cv_cap | 60 / 2.0 | 换手稳定性窗口与 CV 顶盖 |
| limit_up_ratio / limit_down_ratio | 1.098 / 0.902 | 涨跌停过滤阈值 |
| exit_rules.* | 结构止盈 5 条件阈值 | high_pressure_min_pnl_pct=3.0 / high_pressure_ratio=0.85 / overheat_min_pnl_pct=8.0 / overheat_ma20_ratio=1.1 / drawdown_exit_pct=6.0 / momentum_exit_threshold=50.0 / support_break_ma20_ratio=0.98 / reduce_ratio=0.3 |
| market.* | p90/p84 放行阈值 | bull_trend_breadth=0.58 / bull_trend_index_ma120=1.01 / bull_trend_strength=0.62；sideways_min_breadth=0.38 / sideways_max_breadth=0.58 / sideways_max_vol_shock=0.58 / sideways_max_drawdown=0.1 |
| market_freeze.* | 0.3 / 0.1 | p99 熊市体制冻结（regime==bear）/ p97 准熊市退潮冻结（risk_freeze_breadth / risk_freeze_drawdown）；p95 已按用户裁定移除 |
| timing.* | enable_high_vol_state=False | 择时高波动状态开关（引擎恒不触发） |
| circuit_breaker.* | enabled=True / 0.11 / 5 / 0.03 / 0.5 / enable_daily_loss_check=False | 熔断器（回撤线对齐 ML 实例 0.11/熔断天数/单日亏损线/减仓比例/单日亏损检查开关） |
| risk.* | stop=10 / take=20 / 回撤线 99 | 每单风控（对齐 ML 实例 862b0b8d：浮亏≥10%或浮盈≥20%拒加仓，账户回撤线 99=永不触发） |
| event_risk.enabled | False | 事件风控开关（回测无新闻数据流，默认关闭） |
| universe_mode | all_a | 股票池: all_a 或 hs300 |
| min_list_days | 250 | 次新剔除阈值（自然日） |
| LOOKBACK / HIGH_LOOKBACK | 160 / 61 | 滚动缓存行数（模块常量，非 STRATEGY；预热与尾裁剪上限，见差异 #28） |
| TURNOVER_BATCH_DAYS | 5 | 换手率批量查询间隔（交易日，模块常量，见差异 #28） |

## 6. 已知限制

1. **回测耗时**: 已用增量缓存加速（差异 #28）：首日预热全量面板，之后每日 3 次 count=1 单行查询 + 每 5 交易日批量补拉换手率（逐日 get_fundamentals 单日查询）；仍嫌慢可切 hs300 模式快速冒烟。
2. **换手因子预热**: 回测开始后 60 个交易日无换手率稳定性数据；当前因子层仅此一个因子（差异 #2）→ 前 60 交易日候选池为空、零开仓。
3. **事件风控无数据源**: 聚宽回测无新闻事件流，事件风控默认关闭（引擎回测路径同样无事件）。
4. **择时 exposure 不缩放仓位**（见差异 #19，引擎同样）。
5. **引擎熔断缺陷已按设计意图修复**（见差异 #20）：引擎回测中熔断分支不递减倒计时 = 永久熔断；本转换倒计时每日递减。若想对比引擎原始行为，把 `circuit_breaker.enabled` 设为 False 再手动模拟。
6. 聚宽回测订单以当日收盘价成交（与引擎 14:50 决策/收盘成交对齐），滑点与佣金为固定近似值。
7. **决策数据滞后 1 个交易日**（差异 #25）：新平台日频回测固定 09:30 运行、禁取当日 close，决策锚点为上一交易日收盘面板。信号/出场整体延迟 1 日，与引擎直接对比时需知悉。

## 7. 本地冒烟测试

`tools/joinquant/smoke_test_local.py`（需 numpy + pandas，无需聚宽平台）：

```bash
python tools/joinquant/smoke_test_local.py
```

20 个测试组 / 183 项断言：市场快照（新字段 indexAboveMa120 等权指数同源口径 + trendStrength=breadth60 别名 + 上行→bull/下行→bear/样本不足→None）、上涨家数、市场冻结（仅 p99+p97；volShock 不再冻结/宽度冻结移除）、市场放行模式（p90/p84 含边界/越界/bear 不匹配）、择时 5 状态、低波因子（注册未配权）、换手稳定性、截面合成 build_composite（总体方差口径 z/clamp[-3,3]/std=0→z=0/缺因子剔除/权重归一化/未配权因子不执行/空截面提前返回+日志）、多因子信号（评分<0.2 卖出/0.2 边界不卖/排名>40 卖出/持仓缺值保留/仅>0/Top-50/等权 clamp[0.01,0.1]/maxNewBuys 预算与 break 截断/冻结日禁新开仓持仓更新保留/封禁名单）、趋势健康分（四项+25/NaN 跳过）、结构止盈 5 条件（有序第一命中 + reduce 0.3 + 浮点边界说明）、熔断器（回撤熔断 0.11/倒计时恢复/单日减仓）、每单风控（ML 实例默认 10/20/99 + 改阈值启用）、凯利统计、事件风控、股票池剔除、全流程（首日换手截面分化 → 目标股 z>0 买入 → 次日截面同分 z=0 → 评分跌破阈值全清）、停牌 NaN 跳过链（快照/上涨家数/低波/信号全部正确跳过）、面板布局归一化（五形态 + 数值清洗）、数据失败路径无静默（预热历史不足/空面板/增量锚点不符/增量查询异常/换手逐日查询+中断续传+失败日重试不重复 + 查询次数契约：预热 3 次 close160/high61/指数160，增量日 3 次 count=1）。改参数/换因子/改规则后先跑冒烟再上平台。
