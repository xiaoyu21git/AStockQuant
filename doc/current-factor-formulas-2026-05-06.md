# 当前实现因子数学公式

这份文档是当前仓库里唯一保留的公式基线，直接对应 `src/domain/factor/src/` 下的运行时实现。

约定如下：

- 缺少必要字段或样本时，因子通常跳过该样本，不做隐式补值。
- 多指标合成时，分母只统计实际参与计算的有效权重。
- $clamp(x, a, b) = \max(a, \min(b, x))$。
- $\varepsilon$ 表示避免除零的小正数。

## 1. 公共规则

### 1.1 横截面 Z-Score

$$
z_s = \frac{x_s - \mu_x}{\sigma_x}
$$

### 1.2 百分位排名

$$
p_s = \frac{\#\{j \mid x_j < x_s\}}{N}
$$

当前实现对并列值取同一前序位置。

### 1.3 线性加权平均

$$
\phi(s) = \frac{\sum_{i \in A_s} w_i \cdot x_i(s)}{\sum_{i \in A_s} w_i}
$$

其中 $A_s$ 为样本 $s$ 上实际参与计算的指标集合。

---

## 2. Value 因子

指标：`bp`, `ep`, `dividend_yield`, `cf_p`

$$
S_{bp}(s) = \frac{1}{pb\_ratio(s)}
$$

$$
S_{ep}(s) = \frac{1}{pe\_ratio(s)}
$$

$$
S_{div}(s) = dividend\_yield(s)
$$

$$
S_{cf/p}(s) = \frac{operating\_cash\_flow(s)}{market\_cap(s)}
$$

合成公式：

$$
\phi_{value}(s) = \frac{\sum_{i \in A_s} w_i \cdot S_i(s)}{\sum_{i \in A_s} w_i}
$$

---

## 3. Size 因子

指标：`market_cap`, `circulating_market_cap`, `total_assets`

设规模原始值为 $x_s$。

启用对数变换时：

$$
\phi_{size}(s) = -\ln(x_s)
$$

未启用对数变换时：

$$
\phi_{size}(s) = -x_s
$$

---

## 4. Quality 因子

指标：`roe`, `roa`, `gross_margin`, `operating_margin`, `earnings_quality`

当前字段映射：

- `roe` -> `roe`
- `roa` -> `roa`
- `gross_margin` -> `gross_margin`
- `operating_margin` -> `operating_margin`
- `earnings_quality` -> `operating_cash_flow / net_profit`

因此：

$$
Q_i(s) = field_i(s)
$$

对于 `earnings_quality`：

$$
Q_{eq}(s) = \frac{operating\_cash\_flow(s)}{net\_profit(s)}
$$

阈值会先归一化：

$$
	heta_{eff} =
\begin{cases}
\frac{qualityThreshold}{100}, & qualityThreshold > 1 \\
qualityThreshold, & 0 < qualityThreshold \le 1 \\
0, & qualityThreshold \le 0
\end{cases}
$$

只有满足 $Q_i(s) \ge \theta_{eff}$ 的样本才保留。

---

## 5. Momentum 因子

类型：`simple`, `rank`, `normalized`, `exponential`

价格口径：

$$
P_t = close_t \cdot f_t
$$

其中 $f_t$ 由 `adjustPriceType` 选择，对应 `pre_adjust_factor` 或 `post_adjust_factor`。

设窗口长度为 $w$，跳过最近 $k$ 个交易日。

简单动量：

$$
M_s = \frac{P_{t-k} - P_{t-k-w}}{P_{t-k-w}}
$$

若启用成交量确认：

$$
\rho_s = clamp\left(\frac{V_t}{\bar V_w}, 0.5, 1.5\right)
$$

$$
M'_s = M_s \cdot \rho_s
$$

排名动量：

$$
M^{rank}_s = \frac{\#\{j \mid M_j < M_s\}}{N}
$$

标准化动量：

$$
M^{norm}_s =
\begin{cases}
\frac{M_s - \mu_M}{\sigma_M}, & \sigma_M > 0 \\
0, & \sigma_M = 0
\end{cases}
$$

指数动量先放大简单动量，再做同样的横截面标准化：

$$
M^{exp,raw}_s = M_s \cdot \left(1 + \frac{1}{\max(1, w)}\right)
$$

$$
M^{exp}_s =
\begin{cases}
\frac{M^{exp,raw}_s - \mu_{exp}}{\sigma_{exp}}, & \sigma_{exp} > 0 \\
0, & \sigma_{exp} = 0
\end{cases}
$$

---

## 6. Low Volatility 因子

组件：`volatility`, `drawdown`, `beta`

收益率序列：

$$
r_\tau = \frac{P_\tau - P_{\tau-1}}{P_{\tau-1}}
$$

波动率：

$$
\sigma_s = \sqrt{\frac{1}{N}\sum (r_\tau - \bar r)^2}
$$

最大回撤：

$$
NAV_\tau = NAV_{\tau-1}(1+r_\tau), \quad NAV_0 = 1
$$

$$
Peak_\tau = \max_{j \le \tau} NAV_j
$$

$$
MDD_s = \max_\tau \frac{Peak_\tau - NAV_\tau}{Peak_\tau}
$$

Beta：

$$
\beta_s = \frac{Cov(r_s, r_b)}{Var(r_b)}
$$

多样本时做横截面反向归一化：

$$
\bar x_i(s) = \frac{\max_j x_i(j) - x_i(s)}{\max_j x_i(j) - \min_j x_i(j)}
$$

仅当某一组件在当日横截面上至少有 2 个可比样本时，才计算该组件的反向归一化分数并参与合成。

若某组件当日可比样本数小于 2，则该组件当日不贡献分数。

若最终没有任何组件形成有效横截面分数，则该交易日记为空日，不产生低波因子值。

最终合成：

$$
\phi_{lowvol}(s) = \frac{\sum_i w_i \cdot \bar x_i(s)}{\sum_i w_i}
$$

---

## 7. Growth 因子

指标：`revenue_growth`, `net_profit_growth`, `delta_roe`, `sue`

营收同比增速：

$$
G_{rev}(s) = \frac{Revenue_t(s) - Revenue_{t-1}(s)}{|Revenue_{t-1}(s)|}
$$

净利润同比增速：

$$
G_{np}(s) = \frac{NetProfit_t(s) - NetProfit_{t-1}(s)}{|NetProfit_{t-1}(s)|}
$$

ROE 变化：

$$
G_{\Delta ROE}(s) = ROE_t(s) - ROE_{t-1}(s)
$$

SUE 使用最近 8 个季度 EPS 序列，按时间从旧到新记为：

$$
e_0, e_1, e_2, e_3, e_4, e_5, e_6, e_7
$$

先构造过去 3 个季节性 surprise：

$$
\Delta^{hist}_0 = e_4 - e_0
$$

$$
\Delta^{hist}_1 = e_5 - e_1
$$

$$
\Delta^{hist}_2 = e_6 - e_2
$$

当前 surprise 定义为：

$$
\Delta^{cur} = e_7 - e_3
$$

再计算历史均值和标准差：

$$
\mu_{hist} = \frac{\Delta^{hist}_0 + \Delta^{hist}_1 + \Delta^{hist}_2}{3}
$$

$$
\sigma_{hist} = \sqrt{\frac{1}{3}\sum_{i=0}^{2}(\Delta^{hist}_i - \mu_{hist})^2}
$$

$$
SUE_s =
\begin{cases}
\frac{\Delta^{cur} - \mu_{hist}}{\sigma_{hist}}, & \sigma_{hist} > 10^{-12}
\end{cases}
$$

若样本不足 8 个季度，或 $\sigma_{hist} \le 10^{-12}$，则该股票当日不产生 SUE 分数。

单指标分数可按 `zscore`, `minmax`, `percentile`, `rank`, `none` 标准化，再做加权平均：

$$
\phi_{growth}(s) = \frac{\sum_{i \in A_s} w_i \cdot G_i(s)}{\sum_{i \in A_s} w_i}
$$

---

## 8. Liquidity 因子

指标：`turnover_rate`, `volume`, `amplitude`, `amihud_illiquidity`

换手率：

$$
\phi_{to}(s) = \frac{1}{N}\sum_{k=1}^{N} turnover\_rate_{t-k+1}(s)
$$

成交量：

$$
\phi_{vol}(s) = \frac{1}{N}\sum_{k=1}^{N} volume_{t-k+1}(s)
$$

振幅：

$$
\phi_{amp}(s) = -\frac{1}{N}\sum_{k=1}^{N} amplitude_{t-k+1}(s)
$$

Amihud 非流动性：

$$
\lambda_t(s) = \frac{|P_t - P_{t-1}| / |P_{t-1}|}{volume_t(s)}
$$

$$
\phi_{illiq}(s) = -\frac{1}{N}\sum_{k=1}^{N} \lambda_{t-k+1}(s)
$$

---

## 9. Technical 因子

指标：`rsi`, `macd`, `ma`, `ema`, `boll`, `kdj`, `atr`, `obv`, `vwap`, `volume_ratio`, `turnover_stability`

等权平均：

$$
\phi_{tech}(s) = \frac{1}{n}\sum_{i=1}^{n} T_i(s)
$$

归一化平均：

$$
\phi_{tech}^{norm}(s) = \frac{\frac{1}{n}\sum_{i=1}^{n} T_i(s)}{\max\left(\varepsilon, \frac{1}{n}\sum_{i=1}^{n}|T_i(s)|\right)}
$$

各技术指标当前实现口径：

- RSI: $T_{rsi}(s) = clamp\left(\frac{RSI - 50}{50}, -1, 1\right)$
- MACD: $T_{macd}(s) = \tanh\left(\frac{H_t}{|P_t|}\right)$
- MA: $T_{ma}(s) = \tanh\left(\frac{P_t - MA_t}{|MA_t| + \varepsilon}\right)$
- EMA: $T_{ema}(s) = \tanh\left(\frac{P_t - EMA_t}{|EMA_t| + \varepsilon}\right)$
- BOLL: $T_{boll}(s) = \tanh\left(\frac{P_t - MB_t}{\max(\varepsilon, \sigma_t \cdot m)}\right)$
- KDJ: $T_{kdj}(s) = clamp\left(\frac{J_t - 50}{50}, -1, 1\right)$
- ATR: $T_{atr}(s) = clamp\left(-\frac{ATR_t}{|C_t| + \varepsilon}, -1, 1\right)$
- OBV: $T_{obv}(s) = \tanh\left(\frac{OBV_t}{\bar V \cdot N}\right)$
- VWAP: $T_{vwap}(s) = \tanh\left(\frac{P_t - VWAP_t}{|VWAP_t| + \varepsilon}\right)$
- Volume Ratio: $T_{vr}(s) = \tanh\left(\frac{V_t - \bar V_t}{|\bar V_t| + \varepsilon}\right)$
- Turnover Stability: $T_{ts}(s) = clamp\left(2 \cdot \left(1 - \frac{clamp(CV, 0, 2)}{2}\right) - 1, -1, 1\right)$

---

## 10. Dividend 因子

指标：`dividend_yield`, `payout_ratio`, `dividend_stability`

单指标直接取字段值，多指标取简单平均：

$$
\phi_{div}(s) = \frac{1}{n}\sum_{i=1}^{n} d_i(s)
$$

最低股息率阈值先归一化：

$$
	heta_{eff} =
\begin{cases}
\frac{minDividendYield}{100}, & minDividendYield > 1 \\
minDividendYield, & 0 < minDividendYield \le 1 \\
0, & minDividendYield \le 0
\end{cases}
$$

只有满足 $dividend\_yield(s) \ge \theta_{eff}$ 的样本才保留。

---

## 11. Industry 因子

指标：`industry_prosperity`, `industry_momentum`, `industry_concentration`

若窗口序列可用，则先取窗口均值：

$$
I_{eff}(s) = \frac{1}{N}\sum_{k=1}^{N} I_{t-k+1}(s)
$$

否则直接使用当日截面值：

$$
I_{eff}(s) = I_t(s)
$$

再乘以行业体系权重：

$$
\phi_{ind}(s) = I_{eff}(s) \cdot \gamma_{sector}
$$

典型权重：

- `sw_l1` = 1.0
- `sw_l2` = 0.85
- `citic_l1` = 0.95
- `citic_l2` = 0.80

---

## 12. Macro 因子

维度：`growth`, `inflation`, `credit`, `rates`, `policy`, `risk_appetite`

宏观因子不是直接取宏观字段均值，而是计算个股收益对宏观代理变化的敏感度。

股票收益序列：

$$
r_s(\tau) = \frac{P_\tau - P_{\tau-1}}{P_{\tau-1}}
$$

对每个宏观指标，取其代理时间序列并转换为收益或变化序列，再计算相关系数：

$$
\rho_{s,d,i} = corr(r_s, r_{m_i})
$$

结合方向和维度权重：

$$
a_{s,d,i} = \rho_{s,d,i} \cdot direction_i \cdot dimensionWeight_d
$$

最终结果是对所有有效项先求平均，再做一次 $\tanh$ 压缩：

$$
\phi_{macro}(s) = \tanh\left(\frac{1}{n_s}\sum_{(d,i) \in A_s} a_{s,d,i}\right)
$$

运行时要求 `macroDimensions` 和 `macroIndicators` 都显式提供。

---

## 13. Sentiment 因子

情绪因子使用 `sentimentMetric` 对应的规范字段，且字段来源必须是 `NEWS_SENTIMENT`。

若窗口序列可用：

$$
\phi_{sent}(s) = \frac{1}{N}\sum_{k=1}^{N} m_{s, t-k+1}
$$

若窗口序列不可用，则回退为当日截面值：

$$
\phi_{sent}(s) = m_{s,t}
$$

这里不存在默认代理拼接或额外回退模型。

---

## 14. Custom 因子

参数：`expression`, `variables[].name`, `variables[].field`

表达式先转为逆波兰表达式，再对每个样本求值：

$$
\phi_{custom}(s) = Eval(RPN(E), \{v_j \mapsto x_j(s)\})
$$

其中变量值满足：

$$
x_j(s) =
\begin{cases}
F_j(s), & \text{字段存在且样本有值} \\
	ext{无效}, & \text{否则}
\end{cases}
$$

约束：

- `expression` 不能为空。
- `variables` 不支持 `defaultValue` 兜底。
- 某个样本只要缺少任一表达式变量值，就会被跳过。

---

如需核对具体实现，以 `src/domain/factor/src/` 下对应因子文件为准。