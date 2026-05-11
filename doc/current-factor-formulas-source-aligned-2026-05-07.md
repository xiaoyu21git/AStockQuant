# 当前实现因子数学公式源码对齐版（2026-05-07）

本文档只描述当前仓库里的实际实现口径，不等同于教材、论文或行业通用定义。用途是给后续对比、回归和文档修订提供一份固定基线。

## 0. 读法约定

- 这里的“当前实现”指 `src/domain/factor/src/` 下的运行时逻辑。
- 所有公式都默认在当前横截面或当前可用时间窗上计算。
- 若某个样本缺少必要数据，通常会被跳过，而不是强行补值。
- 加权合成时，分母只统计实际参与计算的有效权重。
- 本文中的 $P_t$ 表示复权后价格，$c_t$ 表示收盘价，$f_t$ 表示复权因子。
- $\varepsilon$ 表示很小的正数，代码里通常用于避免除零。
- $clamp(x, a, b) = \max(a, \min(b, x))$。

## 1. 公共标准化与合成规则

当前实现中的多指标因子，通常先得到单指标原始分数 $x_i(s)$，再按配置进行标准化或直接合成。

### 1.1 横截面 z-score

$$
z_s = \frac{x_s - \mu_x}{\sigma_x}
$$

其中 $\mu_x$ 和 $\sigma_x$ 是当前横截面上的均值和标准差。

### 1.2 百分位排名

$$
p_s = \frac{\#\{j \mid x_j < x_s\}}{N}
$$

这里 $N$ 是横截面样本数。当前实现对并列值取同一前序位置。

### 1.3 线性加权平均

$$
\phi(s) = \frac{\sum_i w_i \cdot x_i(s)}{\sum_{i \in A_s} w_i}
$$

其中 $A_s$ 是样本 $s$ 上实际有值并参与计算的指标集合。

---

## 2. Value 因子

### 2.1 个性化指标

`bp`, `ep`, `dividend_yield`, `cf_p`

### 2.2 单指标公式

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

代码里会跳过非有限值和非正样本，因此这些指标本质上都是“质量合格后才参与”的原始值。

### 2.3 合成公式

$$
\phi_{value}(s) = \frac{\sum_{i \in A_s} w_i \cdot S_i(s)}{\sum_{i \in A_s} w_i}
$$

---

## 3. Size 因子

### 3.1 个性化指标

`market_cap`, `circulating_market_cap`, `total_assets`

### 3.2 单指标公式

设规模原始值为 $x_s$。

当启用对数变换时：

$$
\phi_{size}(s) = -\ln(x_s)
$$

未启用对数变换时：

$$
\phi_{size}(s) = -x_s
$$

规模越小，得分越高。

---

## 4. Quality 因子

### 4.1 个性化指标

`roe`, `roa`, `gross_margin`, `operating_margin`, `earnings_quality`

### 4.2 指标映射

- `gross_margin` 实际映射到 `profit_margin`
- `operating_margin` 实际映射到 `profit_margin`
- `earnings_quality` 直接按净利润与权益计算

### 4.3 单指标公式

对于 `roe`、`roa`、`profit_margin`，直接使用字段值：

$$
Q_i(s) = field_i(s)
$$

对于 `earnings_quality`：

$$
Q_{eq}(s) = \frac{net\_profit(s)}{equity(s)}
$$

### 4.4 阈值归一化

配置阈值 `qualityThreshold` 的有效值为：

$$
\theta_{eff} =
\begin{cases}
\frac{qualityThreshold}{100}, & qualityThreshold > 1 \\
qualityThreshold, & 0 < qualityThreshold \le 1 \\
0, & qualityThreshold \le 0
\end{cases}
$$

只有满足 $Q_i(s) \ge \theta_{eff}$ 的样本才会进入合成。

### 4.5 合成公式

$$
\phi_{quality}(s) = \frac{\sum_{i \in A_s} w_i \cdot Q_i(s)}{\sum_{i \in A_s} w_i}
$$

---

## 5. Momentum 因子

### 5.1 个性化类型

`simple`, `rank`, `normalized`, `exponential`

### 5.2 价格口径

当前实现只接受 canonical `priceType = adj_factor`，并要求历史视图同时提供 `close` 和 `adj_factor`。

$$
P_t = c_t \cdot f_t
$$

其中 $c_t$ 为收盘价，$f_t$ 为复权因子。

### 5.3 简单动量

设窗口长度为 $w$，跳过最近 $k$ 个交易日，则：

$$
M_s = \frac{P_{t-k} - P_{t-k-w}}{P_{t-k-w}}
$$

如果启用了成交量确认，还会乘以成交量修正因子：

$$
\rho_s = clamp\left(\frac{V_t}{\bar V_w}, 0.5, 1.5\right)
$$

$$
M'_s = M_s \cdot \rho_s
$$

### 5.4 排名动量

当前实现按简单动量值做升序排序，再用前序位置归一化：

$$
M^{rank}_s = \frac{\#\{j \mid M_j < M_s\}}{N}
$$

### 5.5 标准化动量

$$
M^{norm}_s =
\begin{cases}
\frac{M_s - \mu_M}{\sigma_M}, & \sigma_M > 0 \\
0, & \sigma_M = 0
\end{cases}
$$

### 5.6 指数动量

指数动量并不是单独的一套收益定义，而是先把简单动量统一放大，再做同样的横截面标准化：

$$
M^{exp}_s = \frac{\left(M_s \cdot \left(1 + \frac{1}{\max(1, w)}\right)\right) - \mu_{exp}}{\sigma_{exp}}
$$

若标准差为 0，则结果为 0。

---

## 6. Low Volatility 因子

### 6.1 个性化组件

`volatility`, `drawdown`, `beta`

### 6.2 波动率

设收益率序列为：

$$
r_\tau = \frac{P_\tau - P_{\tau-1}}{P_{\tau-1}}
$$

波动率为：

$$
\sigma_s = \sqrt{\frac{1}{N}\sum_{\tau = t-N+1}^{t}(r_\tau - \bar r)^2}
$$

### 6.3 最大回撤

令净值序列由收益率累乘得到：

$$
NAV_\tau = NAV_{\tau-1}(1+r_\tau), \quad NAV_0 = 1
$$

滚动峰值为：

$$
Peak_\tau = \max_{j \le \tau} NAV_j
$$

最大回撤为：

$$
MDD_s = \max_\tau \frac{Peak_\tau - NAV_\tau}{Peak_\tau}
$$

### 6.4 Beta

$$
\beta_s = \frac{Cov(r_s, r_b)}{Var(r_b)}
$$

其中 $r_b$ 为基准收益序列。

### 6.5 组件方向与合成

LowVol 的三个组件都属于“越小越好”。

多样本时采用横截面反向归一化：

$$
\bar x_i(s) = \frac{\max_j x_i(j) - x_i(s)}{\max_j x_i(j) - \min_j x_i(j)}
$$

单样本时直接退化为：

$$
\bar x_i(s) = -x_i(s)
$$

最终合成：

$$
\phi_{lowvol}(s) = \frac{\sum_i w_i \cdot \bar x_i(s)}{\sum_i w_i}
$$

---

## 7. Growth 因子

### 7.1 个性化指标

`revenue_growth`, `net_profit_growth`, `delta_roe`, `sue`

### 7.2 营收同比增速

$$
G_{rev}(s) = \frac{Revenue_t(s) - Revenue_{t-1}(s)}{|Revenue_{t-1}(s)|}
$$

### 7.3 净利润同比增速

$$
G_{np}(s) = \frac{NetProfit_t(s) - NetProfit_{t-1}(s)}{|NetProfit_{t-1}(s)|}
$$

### 7.4 ROE 变化

$$
G_{\Delta ROE}(s) = ROE_t(s) - ROE_{t-1}(s)
$$

### 7.5 SUE 代理

当前实现不是分析师预期惊喜，而是 EPS 序列差分的标准化代理。

设 EPS 序列为 $e_0, e_1, e_2, \dots$，其中 $e_0$ 是最新值。设变化项总数为 $m$，也就是差分序列的长度。先构造变化序列：

$$
\Delta e_i = e_i - e_{i+1}
$$

若只有一个变化项，则：

$$
SUE_s = \Delta e_0
$$

若至少有两个变化项，则：

$$
\mu_{\Delta e} = \frac{1}{m-1}\sum_{i=1}^{m-1} \Delta e_i
$$

$$
\sigma_{\Delta e} = \sqrt{\frac{1}{m-1}\sum_{i=1}^{m-1}(\Delta e_i - \mu_{\Delta e})^2}
$$

$$
SUE_s =
\begin{cases}
\frac{\Delta e_0 - \mu_{\Delta e}}{\sigma_{\Delta e}}, & \sigma_{\Delta e} > 10^{-12} \\
\Delta e_0, & \sigma_{\Delta e} \le 10^{-12}
\end{cases}
$$

### 7.6 合成公式

$$
\phi_{growth}(s) = \frac{\sum_{i \in A_s} w_i \cdot G_i(s)}{\sum_{i \in A_s} w_i}
$$

---

## 8. Liquidity 因子

### 8.1 个性化指标

`turnover_rate`, `volume`, `amplitude`, `amihud_illiquidity`

### 8.2 公式

当前实现对选定指标做窗口均值；其中 `amplitude` 和 `amihud_illiquidity` 取负向，以表示“越小越好”。

$$
\phi_{to}(s) = \frac{1}{N}\sum_{k=1}^{N} turnover\_rate_{t-k+1}(s)
$$

$$
\phi_{vol}(s) = \frac{1}{N}\sum_{k=1}^{N} volume_{t-k+1}(s)
$$

$$
\phi_{amp}(s) = -\frac{1}{N}\sum_{k=1}^{N} amplitude_{t-k+1}(s)
$$

Amihud 非流动性按窗口均值取负：

$$
\lambda_{t}(s) = \frac{|P_t - P_{t-1}| / |P_{t-1}|}{volume_t(s)}
$$

$$
\phi_{illiq}(s) = -\frac{1}{N}\sum_{k=1}^{N} \lambda_{t-k+1}(s)
$$

---

## 9. Technical 因子

### 9.1 个性化指标

`rsi`, `macd`, `ma`, `ema`, `boll`, `kdj`, `atr`, `vwap`, `volume_ratio`, `obv`, `turnover_stability`

### 9.2 合成模式

#### equal_weight

$$
\phi_{tech}(s) = \frac{1}{n}\sum_{i=1}^{n} T_i(s)
$$

#### normalized_average

$$
\phi_{tech}^{norm}(s) = \frac{\frac{1}{n}\sum_{i=1}^{n} T_i(s)}{\max\left(\varepsilon, \frac{1}{n}\sum_{i=1}^{n}|T_i(s)|\right)}
$$

### 9.3 各指标公式

#### RSI

$$
RSI = 100 - \frac{100}{1 + \overline{\Delta^+}/\overline{\Delta^-}}
$$

$$
T_{rsi}(s) = clamp\left(\frac{RSI - 50}{50}, -1, 1\right)
$$

#### MACD

$$
EMA_t = \alpha P_t + (1-\alpha)EMA_{t-1}, \quad \alpha = \frac{2}{N+1}
$$

$$
DIF_t = EMA^{fast}_t - EMA^{slow}_t
$$

$$
DEA_t = EMA^{signal}(DIF_t)
$$

$$
H_t = DIF_t - DEA_t
$$

$$
T_{macd}(s) = \tanh\left(\frac{H_t}{|P_t|}\right)
$$

#### MA

$$
MA_t = \frac{1}{N}\sum_{k=0}^{N-1} P_{t-k}
$$

$$
T_{ma}(s) = \tanh\left(\frac{P_t - MA_t}{|MA_t| + \varepsilon}\right)
$$

#### EMA

$$
T_{ema}(s) = \tanh\left(\frac{P_t - EMA_t}{|EMA_t| + \varepsilon}\right)
$$

#### BOLL

其中 $m$ 为布林带倍数参数。

$$
MB_t = \frac{1}{N}\sum_{k=0}^{N-1} P_{t-k}
$$

$$
\sigma_t = \sqrt{\frac{1}{N}\sum_{k=0}^{N-1}(P_{t-k} - MB_t)^2}
$$

$$
T_{boll}(s) = \tanh\left(\frac{P_t - MB_t}{\max(\varepsilon, \sigma_t \cdot m)}\right)
$$

#### KDJ

窗口内最高价和最低价分别记为：

$$
H_t^{(N)} = \max_{0 \le k < N} H_{t-k}
$$

$$
L_t^{(N)} = \min_{0 \le k < N} L_{t-k}
$$

$$
RSV_t = 100 \cdot \frac{C_t - L_t^{(N)}}{H_t^{(N)} - L_t^{(N)}}
$$

$$
K_t = 50 + \frac{RSV_t - 50}{p_K}
$$

$$
D_t = 50 + \frac{K_t - 50}{p_D}
$$

$$
J_t = 3K_t - 2D_t
$$

$$
T_{kdj}(s) = clamp\left(\frac{J_t - 50}{50}, -1, 1\right)
$$

#### ATR

$$
TR_t = \max\left(H_t - L_t, |H_t - C_{t-1}|, |L_t - C_{t-1}|\right)
$$

$$
ATR_t = \frac{1}{N}\sum_{k=0}^{N-1} TR_{t-k}
$$

$$
T_{atr}(s) = clamp\left(-\frac{ATR_t}{|C_t| + \varepsilon}, -1, 1\right)
$$

#### VWAP

$$
VWAP_t = \frac{\sum_{k=0}^{N-1} P_{t-k}V_{t-k}}{\sum_{k=0}^{N-1} V_{t-k}}
$$

$$
T_{vwap}(s) = \tanh\left(\frac{P_t - VWAP_t}{|VWAP_t| + \varepsilon}\right)
$$

#### volume_ratio

$$
\bar V_t = \frac{1}{N}\sum_{k=0}^{N-1} V_{t-k}
$$

$$
T_{volume\_ratio}(s) = \tanh\left(\frac{V_t - \bar V_t}{|\bar V_t| + \varepsilon}\right)
$$

#### OBV

设归一化所用的平均成交量为 $\bar V$，窗口长度为 $N$。

$$
OBV_t = OBV_{t-1} + \Delta OBV_t
$$

$$
\Delta OBV_t =
\begin{cases}
 +V_t, & C_t > C_{t-1} \\
 -V_t, & C_t < C_{t-1} \\
 0, & C_t = C_{t-1}
\end{cases}
$$

$$
T_{obv}(s) = \tanh\left(\frac{OBV_t}{\bar V \cdot N}\right)
$$

#### turnover_stability

设换手率窗口序列的均值和标准差分别为 $\bar x$、$\sigma_x$：

$$
CV = \frac{\sigma_x}{|\bar x|}
$$

$$
T_{turnover\_stability}(s) = clamp\left(2\cdot\left(1 - \frac{clamp(CV, 0, 2)}{2}\right) - 1, -1, 1\right)
$$

---

## 10. Dividend 因子

### 10.1 个性化指标

`dividend_yield`, `payout_ratio`, `dividend_stability`

### 10.2 公式

当前实现对选定红利指标做简单平均：

$$
\phi_{div}(s) = \frac{1}{n}\sum_{i=1}^{n} d_i(s)
$$

最低股息率阈值 `minDividendYield` 也会先做百分比归一化：

$$
\theta_{eff} =
\begin{cases}
\frac{minDividendYield}{100}, & minDividendYield > 1 \\
minDividendYield, & 0 < minDividendYield \le 1 \\
0, & minDividendYield \le 0
\end{cases}
$$

只有满足 $dividend\_yield(s) \ge \theta_{eff}$ 的样本才保留。

---

## 11. Industry 因子

### 11.1 个性化指标

当前实现按选定行业指标计算，通常是单指标或单字段窗口均值。

### 11.2 公式

若窗口序列可用，则先取均值：

$$
I_{eff}(s) = \frac{1}{N}\sum_{k=1}^{N} I_{t-k+1}(s)
$$

否则直接使用当前截面值：

$$
I_{eff}(s) = I_t(s)
$$

再乘以行业体系权重：

$$
\phi_{ind}(s) = I_{eff}(s) \cdot \gamma_{sector}
$$

当前实现中的典型权重为：

| 行业体系 | $\gamma_{sector}$ |
|---|---:|
| `sw_l1` | 1.0 |
| `sw_l2` | 0.85 |
| `citic_l1` | 0.95 |
| `citic_l2` | 0.80 |

---

## 12. Macro 因子

### 12.1 个性化维度与指标

当前实现要求 `macroDimensions` 和 `macroIndicators` 都显式提供。

### 12.2 公式

设股票收益序列为 $r_s$，宏观代理序列为 $r_{m_i}$。对每个维度 $d$、指标 $i$，先算相关系数：

$$
\rho_{s,d,i} = corr(r_s, r_{m_i})
$$

再结合方向和维度权重：

$$
a_{s,d,i} = \rho_{s,d,i} \cdot direction_i \cdot dimensionWeight_d
$$

最后对所有有效项取平均并做 $tanh$ 压缩：

$$
\phi_{macro}(s) = \tanh\left(\frac{1}{n_s}\sum_{(d,i)\in A_s} a_{s,d,i}\right)
$$

这不是直接取宏观字段均值，而是“个股收益对宏观代理变化的敏感度”模型。

---

## 13. Sentiment 因子

### 13.1 个性化来源

当前实现先把来源映射到规范化指标，再计算窗口均值或当前截面值。

### 13.2 公式

若窗口序列可用，则：

$$
\phi_{sent}(s) = \frac{1}{N}\sum_{k=1}^{N} m_{s,t-k+1}
$$

若窗口序列不可用，则回退为当前截面值：

$$
\phi_{sent}(s) = m_{s,t}
$$

这里没有“默认表达式”或“代理情绪拼接”这类隐藏回退。

---

## 14. Custom 因子

### 14.1 个性化参数

`expression`, `variables[].name`, `variables[].field`, `variables[].defaultValue`

### 14.2 公式

自定义因子由表达式 $E$ 和变量映射共同决定：

$$
\phi_{custom}(s) = Eval\big(RPN(E), \{v_j \mapsto x_j(s)\}\big)
$$

其中变量值满足：

$$
x_j(s) =
\begin{cases}
F_j(s), & \text{字段存在且样本有数据} \\
d_j, & \text{字段缺失但配置了默认值} \\
\text{无效}, & \text{否则}
\end{cases}
$$

表达式 $E$ 不能为空；如果为空，运行时直接报错，不存在默认表达式回退。

---

## 15. 与“标准教科书公式”的主要差异

1. Momentum 只认 `adj_factor` 复权路径，没有 `close` 兜底。
2. Growth 的 `sue` 是 EPS 差分序列的标准化代理，不是分析师预期惊喜。
3. LowVol 的单样本分支会退化成 `-rawValue`。
4. Macro 用的是“股票收益 vs 宏观代理变化”的相关性敏感度，不是直接的宏观字段均值。
5. Sentiment 不做默认代理拼接，只在窗口均值和当日截面值之间切换。
6. Custom 因子只做 RPN 求值，不存在隐式表达式。

---

## 16. 源码索引

- `src/domain/factor/src/ConfigurableFactor.cpp`
- `src/domain/factor/src/MomentumFactor.cpp`
- `src/domain/factor/src/ValueFactor.cpp`
- `src/domain/factor/src/QualityFactor.cpp`
- `src/domain/factor/src/SizeFactor.cpp`
- `src/domain/factor/src/LowVolFactor.cpp`
- `src/ui/bridge/src/FactorService.cpp`
- `src/ui/bridge/include/FactorRequirementInferenceUtils.h`

---

> 说明：本文档是“当前实现快照”。如果后续代码有 canonical 字段、标准化规则、窗口口径或参数名变化，优先先对照这份文档再改业务文档。