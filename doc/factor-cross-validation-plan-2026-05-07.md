# 因子交叉验证计划（2026-05-07）

## 目标

验证当前 C++ 实现与 Python 参照实现是否在同一数学口径上。

Python 侧优先使用 TA-Lib；TA-Lib 不支持的指标，使用 [current-factor-formulas-2026-05-06.md](current-factor-formulas-2026-05-06.md) 中的公式实现。

## 分层判定

### 层次一：公认数学定义

适用：RSI、MACD、布林带、ATR、OBV、Beta、Z-Score、相关系数。

判定方式：手工推导公式、TA-Lib、C++ 三方对照。

### 层次二：参考实现

适用：KDJ、VWAP、Percentile 等无唯一教科书定义但常见实现稳定的指标。

判定方式：以主流参考实现或文档公式为准，再与 C++ 对照。

### 层次三：自定义公式

适用：Growth 的 SUE proxy、Macro 敏感度、Exponential 动量、换手率稳定性等。

判定方式：只验证边界行为、单调性、不变性和样本一致性，不强行判唯一正确。

## 样本要求

- 使用固定样本，不允许 C++ 和 Python 各自重新抽样。
- 样本包含同一批 symbol、同一批交易日、同一批字段。
- 至少包含 10 个交易日的价格与成交量序列。
- 至少包含 2 到 3 只股票用于横截面标准化和百分位比较。
- 财务指标、宏观字段、情绪字段、行业字段需要在同一日期对齐。

## 对照字段

优先覆盖当前已验证可用的指标字段：

- Momentum: `simple`, `rank`, `normalized`, `exponential`
- Value: `bp`, `ep`, `dividend_yield`, `cf_p`
- Quality: `roe`, `roa`, `gross_margin`, `operating_margin`, `earnings_quality`
- Size: `market_cap`, `circulating_market_cap`, `total_assets`
- LowVol: `volatility`, `drawdown`, `beta`
- Growth: `revenue_growth`, `net_profit_growth`, `delta_roe`, `sue`
- Liquidity: `turnover_rate`, `volume`, `amplitude`, `amihud_illiquidity`
- Technical: `rsi`, `macd`, `ma`, `ema`, `boll`, `kdj`, `atr`, `vwap`, `volume_ratio`, `obv`, `turnover_stability`
- Dividend: `dividend_yield`, `payout_ratio`, `dividend_stability`
- Industry: `industry_metric`
- Macro: `macro_sensitivity`
- Sentiment: `sentiment_metric`
- Custom: `custom_expression`

## 输出表

每行一个“指标 + 参数 + symbol + date”组合，字段如下：

- `factor_type`
- `indicator`
- `symbol`
- `date`
- `params`
- `cpp_value`
- `python_value`
- `abs_error`
- `rel_error`
- `pass`
- `note`

## 判定规则

- 公认定义指标：与原始定义一致者为正确。
- 参考实现指标：与参考实现一致者为正确。
- 自定义指标：只判定行为是否合理，不强求唯一正确。

## 当前结论

- C++ 导出基线与 Python 参照实现已统一到同一 synthetic_v1 样本。
- 比较逻辑仅保留双方都实际输出的重叠行，排除阈值/覆盖面导致的非公式差异。
- 最新结果为 113/113 全通过，说明当前可比范围内没有公式差异。

## 实施顺序

1. 先做统一样本生成器。
2. 再做 C++ 导出结果。
3. 再做 Python 参照结果。
4. 最后做表格比较和阈值判定。