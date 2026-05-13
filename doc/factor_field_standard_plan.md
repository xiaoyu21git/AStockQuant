# 因子字段统一标准表

## 目标

本表用于定义因子回测链路的唯一字段标准。原则只有一条：

- 只允许在外部源接入阶段做别名解析。
- 从“清洗输出到缓存”开始，后续所有阶段都只能使用同一个标准字段名。
- 支持检查通过的字段组合，必须能够进入预热、HistoricalView、因子计算、结果生成和最终展示。
- 不允许缓存一套名字、检查一套名字、运行时再换另一套名字。
- 不允许兜底、回退、代理替代和隐式兼容。

## 简化版结论

你要看的不是“所有字段永远都要存在”，而是两件事：

- 某个字段应不应该存在，取决于当前缓存集到底包含哪些数据类型。
- 某个字段一旦应该存在，从清洗输出开始到最终显示就必须始终是同一个名字。

也就是说：

- 如果当前缓存集只有日K线数据，那就只要求存在日K线标准字段，不要求财务字段。
- 如果当前缓存集同时有日K线和财务数据，那缓存里就应该同时有“日K线标准字段 + 财务标准字段”。
- 如果当前缓存集没有行业/情绪/政策这类数据，就不应该假装支持对应因子。
- 但只要某类数据已经进了缓存，就不能因为换了名字导致“明明有数据却显示不支持”。

## 按数据类型决定字段是否应该存在

| 数据类型 | 什么时候应该存在于缓存 | 缓存中应该使用的标准字段名 | 不应该再出现的链路字段名 | 当前结论 |
| --- | --- | --- | --- | --- |
| 日K线域 | 数据集包含 daily_bar 类行情数据时 | symbol, trade_date, open, high, low, close, pre_close, volume, turnover, change_amt, change_pct, amplitude, turnover_rate, adj_factor, market_cap, circulating_market_cap, pe_ratio, pb_ratio | date, turnover_amount, post_adjust_factor | 当前最乱的一层，必须统一 |
| 财务域 | 数据集包含 financial_indicator 类财务数据时 | total_assets, net_profit, equity, roe, roa, profit_margin, eps, total_revenue, operating_cash_flow，以及实际接入的其他财务字段 | revenue_growth 作为缓存字段名、用 total_revenue 冒充 operating_cash_flow | 大体已有数据，但 supportMap 口径有断点 |
| 行业分类域 | 数据集真的提供行业分类时 | industry_code | industry | 当前代码对行业因子/中性化要求 industry_code，但清洗层还停在 industry |
| 分红域 | 数据集真的提供分红数据时 | dividend_yield, payout_ratio, dividend_stability | 无 | 当前 dataset 18 不具备，所以不支持是对的 |
| 情绪域 | 数据集真的提供情绪表时 | sentiment_score, social_sentiment, investor_sentiment, market_sentiment | news_sentiment 作为缓存字段名 | 当前 dataset 18 不具备，所以不支持是对的 |
| 政策/另类/衍生域 | 数据集真的提供对应表时 | policy_score, hot_rank, basis_rate | policy_data, alternative_data, derivatives_data 作为字段名 | 这些是 sourceTable，不是字段名 |
| 清洗标签域 | 清洗规则输出标签时 | adjusted_price_applied, forward_filled, missing_value_filled 等 tag | 不能拿这些 tag 顶替业务字段 | tag 可以保留，但不得进入因子字段判断 |

## 你真正要改的阶段表

这张表只回答四个问题：当前这个阶段现在用什么名、应不应该改、改成什么、还是保留源列名只在入口映射。

| 字段语义 | S0 外部源/SQL 可接受名字 | S1 清洗输出现在是什么 | S2-S6 应统一成什么 | 哪些阶段要改 | 哪些阶段保持 |
| --- | --- | --- | --- | --- | --- |
| 交易日期 | trade_date, date, bar_time, report_date, ann_date 等 | trade_date | trade_date | 改 S1 清洗输出、S2-S6 任何还看 date 的地方 | 保留 S0 数据库列名 trade_date，只做入口映射 |
| 成交额 | turnover, amount | turnover | turnover | 改 S1 标准化名、S2 availableFields、S3-S6 所有使用方 | 保留 S0 源列 turnover/amount 作为别名输入 |
| 复权因子 | adj_factor, hfq_factor, post_adjust_factor | 目前未真正统一，dataset 常落成 post_adjust_factor | adj_factor | 改 S1 清洗输出、S2 availableFields、S3 supportMap、S4 warmup/HistoricalView、S5 Momentum/运行时 | 保留 S0 SQL 源列 post_adjust_factor 只作为入口映射 |
| 行业分类 | industry_code, sw_l1, citic_l1 | industry | industry_code | 改 S1 清洗输出、S2 availableFields、S3-S6 全链路使用方 | 保留 S0 原始行业列名作为输入别名 |
| 财务营收字段 | total_revenue | total_revenue | total_revenue | 不需要大改，保持 | 保留现状 |
| 经营现金流字段 | operating_cash_flow | operating_cash_flow | operating_cash_flow | 改 S3 supportMap，不允许再改写成 total_revenue | 保留 S1/S2/S5 现有正确用法 |
| 清洗标签 adjusted_price_applied | 无，这是规则标签 | adjusted_price_applied | adjusted_price_applied，但仅限 tag | 改 S3-S6，禁止把它当 adj_factor 之类业务字段 | 保留 tag 本身 |

## 按当前数据组合判断“应该支持还是不应该支持”

这一张表专门回答你说的那句：如果这个数据有财务和日K线，那缓存中就应该有财务相关字段和K线相关字段。

| 当前缓存集包含的数据类型 | 缓存中应该有的字段集合 | 因子支持判定应该怎么做 |
| --- | --- | --- |
| 只有日K线域 | 只检查日K线标准字段 | 技术、动量、流动性、规模、部分宏观可以支持；纯财务/分红/情绪不应支持 |
| 只有财务域 | 只检查财务标准字段 | 质量、成长、部分价值可以支持；技术、动量、流动性不应支持 |
| 日K线域 + 财务域 | 同时包含日K线标准字段和财务标准字段 | 技术、动量、流动性、规模、质量、成长、价值都应按统一字段正常支持 |
| 日K线域 + 财务域 + 行业域 | 再额外包含 industry_code | 上述因子外，行业因子和行业中性化变体才允许支持 |
| 日K线域 + 财务域 + 情绪/政策域 | 再额外包含对应情绪/政策标准字段 | 情绪/政策因子才允许支持 |

## 最直接的统一要求

可以压缩成下面 5 句：

- 缓存里有没有某字段，先看这个缓存集有没有对应数据类型，不要乱要求。
- 只要这个字段属于当前缓存集应该有的数据类型，它从 S1 开始就只能有一个标准名。
- supportMap 检查的字段名，必须和缓存 availableFields 的字段名完全一样。
- 运行时 hasField/getCrossSection 用的字段名，必须和 supportMap 完全一样。
- 最终 UI 展示的 requiredFields/missingFields，也必须还是这个名字，不能再换。

## 阶段统一命名表

| 阶段ID | 统一阶段名 | 允许出现的字段名形态 | 输出要求 | 当前代码控制点 | 当前问题 |
| --- | --- | --- | --- | --- | --- |
| S0 | 外部源字段 | 数据源原始列名/别名 | 仅作为输入，不进入缓存标准 | DataCleaningEngine 的 canonicalFieldKey/aliasedKeysForField | 目前允许别名解析，这一层保留 |
| S1 | 清洗标准化输出 | 标准字段名 | 清洗结果行内键名必须已标准化 | DataCleaningEngine 的 setCanonicalNumericField/setCanonicalStringField | 目前只对部分字段标准化，而且旧键可能仍残留在记录里 |
| S2 | 缓存字段清单 | 标准字段名 | dataset availableFields 必须完全等于标准字段集合 | DataFetchController 的 collectAvailableFields/buildCleanedDataSetInfo | 目前 availableFields 是“行内现存所有键”的并集，不保证只剩标准键 |
| S3 | 回测支持检查字段 | 标准字段名 | requiredFields 与 availableFields 必须同名精确匹配 | FactorRequirementInferenceUtils / FactorBacktestController supportMap | 目前存在别名口径不一致和特例改写 |
| S4 | 预热/HistoricalView 字段 | 标准字段名 | Warmup 输出到 Arrow/HistoricalView 后仍必须是标准字段名 | FactorBacktestWarmupUtils / FactorBacktestController | 目前 adj_factor 通过 post_adjust_factor 转译，未统一成单名 |
| S5 | 因子运行时字段 | 标准字段名 | getDataRequirements、hasField、getCrossSection 必须只认标准字段名 | 各 Factor / ConfigurableFactor | 目前部分运行时与检查器字段名不一致 |
| S6 | 结果/展示字段 | 标准字段名 | supportMap.requiredFields、missingFields、metadata 中回显的字段名必须仍是标准字段名 | FactorBacktestController / QML | 目前展示层跟随 supportMap，若上游错名会被原样带到 UI |

## 统一字段标准

### 规则

- 从 S2 开始只允许使用“标准字段名”。
- S0 的数据库列名或第三方接口字段名只能在清洗入口和 SQL 读取入口被转换一次。
- 标记位与数值字段必须分开，不能混用。

### 字段标准表

| 标准字段名 | 允许的 S0 输入别名/源列 | 从 S2 到 S6 必须统一使用 | 当前断点 | 统一后要求 |
| --- | --- | --- | --- | --- |
| symbol | symbol, code, stock_code, ts_code | symbol | 当前基本一致 | 保持不变 |
| trade_date | trade_date, date, bar_time, report_date, publish_time, created_at, ann_date, announcement_date, disclosure_date | trade_date | 旧链路里有一部分还在看 date | 缓存、检查、运行时、结果统一为 trade_date；SQL 源列 trade_date 只在入口映射 |
| open | open | open | 当前基本一致 | 保持不变 |
| high | high | high | 当前基本一致 | 保持不变 |
| low | low | low | 当前基本一致 | 保持不变 |
| close | close | close | 当前基本一致 | 保持不变 |
| pre_close | pre_close, prev_close, preclose | pre_close | 当前基本一致 | 保持不变 |
| volume | volume, vol | volume | 当前基本一致 | 保持不变 |
| turnover | turnover, amount | turnover | 清洗、缓存 ready 检查、dataset 和其他链路统一使用 turnover | 统一标准名为 turnover；turnover_amount 不再作为链路字段名 |
| turnover_rate | turnover_rate, turn_rate, turnrate, 换手率 | turnover_rate | 当前基本一致 | 保持不变 |
| change_amt | change, chg, 涨跌额 | change_amt | 当前基本一致 | 保持不变 |
| change_pct | pct_chg, pct_change, changepercent | change_pct | 当前基本一致 | 保持不变 |
| amplitude | swing, 振幅 | amplitude | 当前基本一致 | 保持不变 |
| adj_factor | adj_factor, hfq_factor, post_adjust_factor | adj_factor | 清洗规则读取 adj_factor/hfq_factor/post_adjust_factor；warmup 从 daily_bar 取 post_adjust_factor；support check 要求 adj_factor；Momentum 运行时也要求 adj_factor；dataset 现存字段是 post_adjust_factor | 统一标准名改为 adj_factor；post_adjust_factor 只允许作为源列名存在于 SQL/原始输入层 |
| market_cap | market_cap, total_mv, total_market_cap, 总市值 | market_cap | 当前基本一致 | 保持不变 |
| circulating_market_cap | circulating_market_cap, circ_mv, float_market_cap, 流通市值 | circulating_market_cap | 当前基本一致 | 保持不变 |
| total_assets | total_assets | total_assets | 当前基本一致 | 保持不变 |
| pe_ratio | pe_ratio | pe_ratio | 当前基本一致 | 保持不变 |
| pb_ratio | pb_ratio | pb_ratio | 当前基本一致 | 保持不变 |
| roe | roe | roe | 当前基本一致 | 保持不变 |
| roa | roa | roa | 当前基本一致 | 保持不变 |
| profit_margin | profit_margin | profit_margin | 当前基本一致 | 保持不变 |
| net_profit | net_profit | net_profit | 当前基本一致 | 保持不变 |
| equity | equity | equity | 当前基本一致 | 保持不变 |
| eps | eps | eps | 当前基本一致 | 保持不变 |
| total_revenue | total_revenue | total_revenue | 当前 growth checker 已把 revenue_growth 归一到 total_revenue | 保持 total_revenue 为标准字段名 |
| operating_cash_flow | operating_cash_flow | operating_cash_flow | value 因子 CF/P 的 supportMap 目前把它错误改写成 total_revenue | 保持 operating_cash_flow，不允许在 supportMap 改写 |
| industry_code | industry_code, sw_l1, citic_l1 | industry_code | 清洗层当前只标准化到 industry；运行时中性化和行业因子普遍要求 industry_code | 统一标准名改为 industry_code；industry 不再作为因子链路字段名 |
| dividend_yield | dividend_yield | dividend_yield | 当前 dataset 18 不具备 | 缺字段时直接判不支持 |
| payout_ratio | payout_ratio | payout_ratio | 当前 dataset 18 不具备 | 缺字段时直接判不支持 |
| dividend_stability | dividend_stability | dividend_stability | 当前 dataset 18 不具备 | 缺字段时直接判不支持 |
| sentiment_score | sentiment_score | sentiment_score | 当前 dataset 18 不具备 | 缺字段时直接判不支持 |
| social_sentiment | social_sentiment | social_sentiment | 当前 dataset 18 不具备 | 缺字段时直接判不支持 |
| investor_sentiment | investor_sentiment | investor_sentiment | 当前 dataset 18 不具备 | 缺字段时直接判不支持 |
| market_sentiment | market_sentiment | market_sentiment | 当前 dataset 18 不具备 | 缺字段时直接判不支持 |
| policy_score | policy_score | policy_score | 当前 dataset 18 不具备 | 缺字段时直接判不支持 |
| hot_rank | hot_rank | hot_rank | 当前 dataset 18 不具备 | 缺字段时直接判不支持 |
| basis_rate | basis_rate | basis_rate | 当前 dataset 18 不具备 | 缺字段时直接判不支持 |
| adjusted_price_applied | 无；这是清洗标签，不是行情/因子字段 | 仅可作为 tag，不能进入 requiredFields/availableFields/hasField/getCrossSection | 当前已作为清洗 tag 存在，但不能被当成 adj_factor 替代 | 明确从因子字段标准中排除 |

## 明确禁止的跨阶段改名

| 当前混乱写法 | 禁止原因 | 统一后处理 |
| --- | --- | --- |
| trade_date 和 date 混用 | 清洗标准和缓存准入口径不一致 | 统一只保留 trade_date |
| turnover -> turnover_amount，但 dataset / ready 检查 /其他链路看 turnover | 同一字段两套名字 | 统一只保留 turnover |
| adj_factor 在不同阶段被写成 post_adjust_factor | 缓存、检查、运行时字段名不一致 | 统一只保留 adj_factor，post_adjust_factor 只留在源层 |
| adjusted_price_applied 被误当作 adj_factor 替代 | 标签与数值字段混用 | adjusted_price_applied 只能是 tag |
| value: cf_p 把 operating_cash_flow 改写成 total_revenue | 支持检查与真实运行依赖不一致 | 统一只保留 operating_cash_flow |
| industry 与 industry_code 混用 | 行业展示标签与行业分类字段语义不同 | 统一只保留 industry_code 作为因子字段 |

## 因子准入表

下面的“明确支持”含义是：同一组标准字段在 S2-S6 全部同名可达，supportMap 通过后必须能回测。

| 因子类别 | 必需标准字段 | 当前 dataset 18 是否具备字段 | 当前代码是否全链路同名 | 结论 |
| --- | --- | --- | --- | --- |
| 技术 | close/open/high/low/volume/turnover_rate，按指标取子集 | 是 | 大体一致 | 明确支持 |
| 动量 | close + pre_adjust_factor + post_adjust_factor | 数据层面有，且清洗与缓存层已同时保留前后复权 | 是 | 支持检查必须同时要求前后复权；计算层仍由回测引擎只选其一 |
| 价值 BP/EP | pb_ratio 或 pe_ratio | 是 | 是 | 明确支持 |
| 价值 CF/P | market_cap + operating_cash_flow | 是 | 否，supportMap 仍会改名 | 修正前不允许标记为支持 |
| 规模 | market_cap / circulating_market_cap / total_assets | 是 | 是 | 明确支持 |
| 质量 | roe / roa / profit_margin / net_profit + equity | 是 | 是 | 明确支持 |
| 成长 | total_revenue / net_profit / roe / eps | 是 | 基本一致 | 明确支持 |
| 红利 | dividend_yield / payout_ratio / dividend_stability | 否 | 否 | 不支持 |
| 行业 | industry_code，行业因子另加自身指标 | 否 | 否 | 不支持 |
| 任意行业中性化变体 | industry_code + market_cap | 否 | 否 | 不支持 |
| 宏观代理 | close/turnover_rate/pe_ratio/pb_ratio/volume，按指标取子集 | 是 | 基本一致 | 明确支持 |
| 情绪/政策/另类/衍生 | sentiment_score / social_sentiment / investor_sentiment / market_sentiment / policy_score / hot_rank / basis_rate | 否 | 否 | 不支持 |

## 后续改造的验收标准

- 清洗输出记录里不再保留进入 S2 之后的旧别名键。
- dataset availableFields 只包含标准字段名和明确允许的 tag。
- supportMap.requiredFields 与运行时 getDataRequirements/hasField/getCrossSection 使用完全相同的字段名。
- Warmup/Arrow/HistoricalView 暴露的字段名与 supportMap.requiredFields 完全一致。
- UI 展示的 requiredFields 和 missingFields 不再出现源列名、代理名、历史兼容名。
- 任何字段一旦 supportMap 判定 supported=true，必须能够直接进入回测；否则就是实现错误。