明白了。我会完全根据您文档中每个因子的文字描述，重新推导并写出正确的数学公式，而不是照搬原文中的文本表达式。同时严格按照原文保留 C++ 代码。

---

# 因子类型、指标、数学公式与 C++ 代码对照表

---

## 1. Value 因子

### 个性化指标
`bp`, `ep`, `dividend_yield`, `cf_p`

### 数学公式

**单指标原始得分：**

市净率倒数（BP）：
$$S_{bp}(s) = \frac{1}{P/B_s}$$

市盈率倒数（EP）：
$$S_{ep}(s) = \frac{1}{P/E_s}$$

股息率：
$$S_{div}(s) = D_s$$

现金流市值比：
$$S_{cf/p}(s) = \frac{OCF_s}{MV_s}$$

其中 $MV_s$ 为股票 $s$ 的总市值，$OCF_s$ 为经营性现金流，$D_s$ 为股息率。

**多指标加权合成：**

设选定的估值指标集合为 $\mathcal{M}$，指标 $m$ 的配置权重为 $w_m$。对于股票 $s$，定义有效指标集合为 $\mathcal{M}_s^{active} \subseteq \mathcal{M}$，表示该股票上实际有数据的指标。

最终得分：
$$\phi_{value}(s) = \frac{\sum_{m \in \mathcal{M}_s^{active}} w_m \cdot S_m(s)}{\sum_{m \in \mathcal{M}_s^{active}} w_m}$$

分母仅累加有效指标的权重。

### C++ 代码
```cpp
for (const auto& [symbol, score] : contribution.scores) {
    weightedScores[symbol] += score * contribution.weight;
    usedWeights[symbol] += contribution.weight;
}

for (const auto& [symbol, weightedScore] : weightedScores) {
    result.values[symbol] = weightedScore / usedWeights[symbol];
}
```

---

## 2. Size 因子

### 个性化指标
`market_cap`, `circulating_market_cap`, `total_assets`

### 数学公式

选取规模度量 $x_s$，可为总市值、流通市值或总资产。

当启用对数变换时，使用自然对数：
$$\phi_{size}(s) = -\ln\big(x_s\big)$$

当不启用对数变换时，直接取负：
$$\phi_{size}(s) = -x_s$$

因子的方向为：规模越小得分越高。对数变换使得得分对小市值区间的区分度更高。

### C++ 代码
```cpp
double SizeFactor::scoreFromRawValue(double rawValue) const {
    if (params_.logTransform) {
        return -std::log(rawValue);
    }
    return -rawValue;
}
```

---

## 3. Quality 因子

### 个性化指标
`roe`, `roa`, `gross_margin`, `operating_margin`, `earnings_quality`

### 数学公式

设 $s$ 为股票，$q$ 为选取的质量指标。

对于标准质量指标（ROE、ROA、毛利率、营业利润率），得分直接取自对应字段 $F_q(s)$：
$$\phi_q(s) = F_q(s)$$

对于收益质量指标，使用净资产收益率计算：
$$\phi_{eq}(s) = \frac{NI_s}{E_s}$$
其中 $NI_s$ 为净利润，$E_s$ 为股东权益。

**阈值过滤：**

所有得分需满足门槛条件：
$$\phi_q(s) \geq \theta$$

其中 $\theta = qualityThreshold$ 为配置的质量阈值。不满足条件的股票从横截面中剔除。

### C++ 代码
```cpp
if (metric == QStringLiteral("earnings_quality")) {
    const double factorValue = netProfit / equityIt->second;
    if (factorValue >= qualityThreshold) {
        result.values[symbol] = factorValue;
    }
} else {
    if (factorValue > 0.0 && factorValue >= qualityThreshold) {
        result.values[symbol] = factorValue;
    }
}
```

---

## 4. Momentum 因子

### 个性化指标与计算分支
四种类型：`simple`、`rank`、`normalized`、`exponential`

### 数学公式

**真实价格定义：**

仅使用复权价格。设 $c_t$ 为第 $t$ 个交易日的收盘价，$f_t$ 为复权因子：
$$p_t = c_t \cdot f_t$$

**窗口参数：**

设 $w$ 为回看窗口长度（参数 `window`），$k$ 为跳过的最近交易天数（参数 `skipRecent`）。

**简单动量 (simple)：**

计算跳过最近 $k$ 天之后的区间收益率：
$$M_s^{(simple)} = \frac{p_{t-k} - p_{t-k-w}}{p_{t-k-w}}$$

若启用成交量确认（`useVolume = true`），引入成交量调整因子：
$$\rho = \text{clamp}\left(\frac{V_t}{\bar{V}_w},\, 0.5,\, 1.5\right)$$

其中 $V_t$ 为当日成交量，$\bar{V}_w$ 为过去 $w$ 天的平均成交量，$\text{clamp}$ 将值限制在 $[0.5, 1.5]$ 区间：

$$\text{clamp}(x, a, b) = \max\big(a, \min(b, x)\big)$$

调整后的得分：
$$M_s^{(simple, vol)} = M_s^{(simple)} \cdot \rho$$

**Rank 动量 (rank)：**

对全市场简单动量值做排序百分位化：
$$M_s^{(rank)} = \frac{r_s - 1}{N - 1}$$

其中 $r_s$ 为股票 $s$ 的动量值在全市场从低到高的排名，$N$ 为总股票数。

**Normalized 动量 (normalized)：**

对简单动量做 Z-Score 标准化：
$$M_s^{(norm)} = \frac{M_s^{(simple)} - \mu_M}{\sigma_M}$$

其中 $\mu_M$ 和 $\sigma_M$ 分别为全市场动量的均值和标准差。

**Exponential 动量 (exponential)：**

先将简单动量按窗口长度进行放大：
$$M_s^{(exp, raw)} = M_s^{(simple)} \cdot \left(1 + \frac{1}{w}\right)$$

其中 $w$ 为窗口长度，短窗口获得更大的放大系数。然后再对放大后的值做标准化。

### C++ 代码
```cpp
series.push_back(HistoricalDataPoint{
    closeSeries[index].date,
    closeSeries[index].value * factorSeries[index].value
});

double momentum = (currentClose - previousClose) / previousClose;
```

---

## 5. Low Volatility 因子

### 个性化组件
`volatility`、`drawdown`、`beta`

### 数学公式

设有 $n$ 个选定的组件，每个组件 $i$ 的原始值为 $x_i(s)$。

**组件 1：波动率**

使用日收益率序列。设窗口长度为 $N$，第 $\tau$ 日的收益率为：
$$r_{\tau} = \frac{p_{\tau} - p_{\tau-1}}{p_{\tau-1}}$$

波动率为收益率的标准差：
$$\sigma_s = \sqrt{\frac{1}{N}\sum_{\tau = t-N+1}^{t} \big(r_{\tau} - \bar{r}\big)^2}$$

其中 $\bar{r} = \frac{1}{N}\sum_{\tau} r_{\tau}$ 为样本均值。原始得分取波动率本身：
$$x_{vol}(s) = \sigma_s$$

**组件 2：最大回撤**

窗口内净值序列为：
$$NAV_{\tau} = \prod_{j=t-N+1}^{\tau} (1 + r_j)$$

历史峰值为：
$$Peak_{\tau} = \max_{j \in [t-N+1, \tau]} NAV_j$$

最大回撤为：
$$x_{mdd}(s) = \max_{\tau} \frac{Peak_{\tau} - NAV_{\tau}}{Peak_{\tau}}$$

**组件 3：Beta**

设基准收益序列为 $r_b$，Beta 为收益率协方差与基准方差之比：
$$x_{\beta}(s) = \frac{\text{Cov}(r_s, r_b)}{\text{Var}(r_b)}$$

**组件合成：**

由于波动率、最大回撤和 Beta 都是"越小越好"的指标，先对每个组件做反向归一化：
$$\bar{x}_i(s) = \frac{\max_j x_i(j) - x_i(s)}{\max_j x_i(j) - \min_j x_i(j)}$$

其中 $\max_j$ 和 $\min_j$ 遍历当前横截面所有股票在组件 $i$ 上的取值。

使用组件权重 $w_i$ 合成最终得分：
$$\phi_{lowvol}(s) = \frac{\sum_i w_i \cdot \bar{x}_i(s)}{\sum_i w_i}$$

---

## 6. Growth 因子

### 个性化指标
`revenue_growth`、`net_profit_growth`、`delta_roe`、`sue`

### 数学公式

**营收同比增速：**
$$G_{rev}(s) = \frac{\text{Revenue}_s(t) - \text{Revenue}_s(t-1)}{|\text{Revenue}_s(t-1)|}$$

其中 $t-1$ 表示上一同期（如去年同期），分母取绝对值保证符号一致性。

**净利润同比增速：**
$$G_{np}(s) = \frac{\text{NetProfit}_s(t) - \text{NetProfit}_s(t-1)}{|\text{NetProfit}_s(t-1)|}$$

**ROE 变化：**
$$G_{\Delta ROE}(s) = \text{ROE}_s(t) - \text{ROE}_s(t-1)$$

**SUE（标准化意外盈余）代理：**

定义 EPS 变化序列（以 $N$ 为历史窗口长度）：
$$\Delta \epsilon_{\tau} = \text{EPS}_{\tau} - \text{EPS}_{\tau-1}$$

SUE 为最新一期 EPS 变化相对于历史窗口的标准化：
$$G_{SUE}(s) = \frac{\Delta \epsilon_t - \mu_{\Delta \epsilon}}{\sigma_{\Delta \epsilon}}$$

其中：
$$\mu_{\Delta \epsilon} = \frac{1}{N}\sum_{k=1}^{N} \Delta \epsilon_{t-k+1}$$
$$\sigma_{\Delta \epsilon} = \sqrt{\frac{1}{N}\sum_{k=1}^{N} (\Delta \epsilon_{t-k+1} - \mu_{\Delta \epsilon})^2}$$

**多指标合成：**

设选定的成长指标集合为 $\mathcal{G}$。每个指标 $g$ 的原始值 $G_g(s)$ 首先经过标准化（zscore / minmax / percentile / none）得到 $z_g(s)$。

最终得分为标准化后的加权平均，分母仅累加有效指标的权重：
$$\phi_{growth}(s) = \frac{\sum_{g \in \mathcal{G}_s^{active}} w_g \cdot z_g(s)}{\sum_{g \in \mathcal{G}_s^{active}} w_g}$$

### C++ 代码
```cpp
if (selection.metric == "revenue_growth") {
    metricScores = computeYoYScoreMap(selection.field);
} else if (selection.metric == "net_profit_growth") {
    metricScores = computeYoYScoreMap(selection.field);
} else if (selection.metric == "delta_roe") {
    metricScores = computeDifferenceScoreMap(selection.field);
} else if (selection.metric == "sue") {
    metricScores = computeSueProxyScoreMap();
}

combinedScores[symbol] += score * selection.weight;
activeWeightSums[symbol] += selection.weight;
```

---

## 7. Liquidity 因子

### 个性化指标
`turnover_rate`、`volume`、`amihud_illiquidity`、`amplitude`

### 数学公式

设窗口长度为 $N$。

**换手率（正向）：**
$$\phi_{liq}^{(to)}(s) = \frac{1}{N}\sum_{k=1}^{N} T_{s, t-k+1}$$

其中 $T_{s,\tau}$ 为股票 $s$ 在第 $\tau$ 日的换手率。换手率越高，得分越高。

**成交量（正向）：**
$$\phi_{liq}^{(vol)}(s) = \frac{1}{N}\sum_{k=1}^{N} V_{s, t-k+1}$$

$V_{s,\tau}$ 为成交量。成交量越高，得分越高。

**振幅（负向）：**

定义单日振幅为：
$$A_{s,\tau} = \frac{H_{s,\tau} - L_{s,\tau}}{C_{s,\tau-1}}$$

其中 $H$、$L$ 分别为最高价和最低价，$C$ 为收盘价。振幅越小代表越稳定，因此取负号使方向与得分正相关：
$$\phi_{liq}^{(amp)}(s) = -\frac{1}{N}\sum_{k=1}^{N} A_{s, t-k+1}$$

**Amihud 非流动性（负向）：**

单日非流动性指标为：
$$\lambda_{s,\tau} = \frac{|p_{\tau} - p_{\tau-1}| / |p_{\tau-1}|}{V_{s,\tau}}$$

该值越小代表流动性越好，因此取负均值使"更容易成交"的股票得分更高：
$$\phi_{liq}^{(illiq)}(s) = -\frac{1}{N}\sum_{k=1}^{N} \lambda_{s, t-k+1}$$

---

## 8. Technical 因子

### 个性化指标
`rsi`、`macd`、`ma`、`ema`、`boll`、`kdj`、`atr`、`obv`、`vwap`、`volume_ratio`、`turnover_stability`

### 多指标合成

设选定 $n$ 个技术指标，第 $i$ 个指标的得分为 $T_i(s) \in [-1, 1]$。

**等权平均模式 (`equal_weight`)：**
$$\phi_{tech}(s) = \frac{1}{n}\sum_{i=1}^{n} T_i(s)$$

**归一化平均模式 (`normalized_average`)：**

将等权平均除以绝对值的等权平均，进行平滑归一化：
$$\phi_{tech}^{(norm)}(s) = \frac{\frac{1}{n}\sum_{i=1}^{n} T_i(s)}{\max\left(\varepsilon,\ \frac{1}{n}\sum_{i=1}^{n} |T_i(s)|\right)}$$

其中 $\varepsilon > 0$ 为极小正数，防止除零。

---

### 各技术指标详细公式

各指标得分均映射到 $[-1, 1]$ 区间，正值表示偏强信号，负值表示偏弱信号。

---

#### RSI（相对强弱指标）

设窗口长度为 $N$。定义日内涨跌：
$$\Delta_{\tau}^+ = \begin{cases} p_{\tau} - p_{\tau-1}, & p_{\tau} > p_{\tau-1} \\ 0, & \text{otherwise} \end{cases}$$
$$\Delta_{\tau}^- = \begin{cases} p_{\tau-1} - p_{\tau}, & p_{\tau} < p_{\tau-1} \\ 0, & \text{otherwise} \end{cases}$$

平均涨幅和平均跌幅分别为：
$$\overline{\Delta^+} = \frac{1}{N}\sum_{\tau = t-N+1}^{t} \Delta_{\tau}^+$$
$$\overline{\Delta^-} = \frac{1}{N}\sum_{\tau = t-N+1}^{t} \Delta_{\tau}^-$$

RSI 原始值：
$$RSI = 100 - \frac{100}{1 + \overline{\Delta^+} / \overline{\Delta^-}}$$

得分映射（50 为中性点）：
$$T_{rsi}(s) = \text{clamp}\left(\frac{RSI - 50}{50},\ -1,\ 1\right)$$

---

#### MACD（指数平滑异同移动平均线）

设快线周期为 $n_f$，慢线周期为 $n_s$，信号线周期为 $n_{sig}$。

快线 EMA：
$$F_t = \alpha_f p_t + (1 - \alpha_f) F_{t-1},\quad \alpha_f = \frac{2}{n_f + 1}$$

慢线 EMA：
$$S_t = \alpha_s p_t + (1 - \alpha_s) S_{t-1},\quad \alpha_s = \frac{2}{n_s + 1}$$

MACD 线：
$$DIF_t = F_t - S_t$$

信号线：
$$DEA_t = \alpha_{sig} DIF_t + (1 - \alpha_{sig}) DEA_{t-1},\quad \alpha_{sig} = \frac{2}{n_{sig} + 1}$$

柱状线（Histogram）：
$$H_t = DIF_t - DEA_t$$

得分通过双曲正切归一化：
$$T_{macd}(s) = \tanh\left(\frac{H_t}{|p_t|}\right)$$

---

#### MA（移动平均线）

$$MA_t(N) = \frac{1}{N}\sum_{k=0}^{N-1} p_{t-k}$$

得分衡量价格相对于均线的偏离：
$$T_{ma}(s) = \tanh\left(\frac{p_t - MA_t(N)}{|MA_t(N)| + \varepsilon}\right)$$

---

#### EMA（指数移动平均线）

$$EMA_t = \alpha p_t + (1 - \alpha) EMA_{t-1},\quad \alpha = \frac{2}{N + 1}$$

得分：
$$T_{ema}(s) = \tanh\left(\frac{p_t - EMA_t}{|EMA_t| + \varepsilon}\right)$$

---

#### BOLL（布林带）

中轨：
$$MB_t = \frac{1}{N}\sum_{k=0}^{N-1} p_{t-k}$$

标准差：
$$\sigma_t = \sqrt{\frac{1}{N}\sum_{k=0}^{N-1} (p_{t-k} - MB_t)^2}$$

设标准乘数为 $m$（通常为 2），得分衡量价格在布林带中的位置：
$$T_{boll}(s) = \tanh\left(\frac{p_t - MB_t}{\max(\varepsilon,\ \sigma_t \cdot m)}\right)$$

---

#### KDJ（随机指标）

设计算周期为 $N$，$K$ 线平滑周期为 $p_K$，$D$ 线平滑周期为 $p_D$。

窗口内最高价与最低价：
$$H_t^{(N)} = \max_{0 \leq k < N} H_{t-k}$$
$$L_t^{(N)} = \min_{0 \leq k < N} L_{t-k}$$

未成熟随机值 RSV：
$$RSV_t = 100 \cdot \frac{C_t - L_t^{(N)}}{H_t^{(N)} - L_t^{(N)}}$$

简化平滑（$K_{t-1}$ 和 $D_{t-1}$ 初始值均为 50）：
$$K_t = 50 + (RSV_t - 50) \cdot \frac{1}{p_K}$$
$$D_t = 50 + (K_t - 50) \cdot \frac{1}{p_D}$$
$$J_t = 3K_t - 2D_t$$

得分以 50 为中性点映射到 $[-1, 1]$：
$$T_{kdj}(s) = \text{clamp}\left(\frac{J_t - 50}{50},\ -1,\ 1\right)$$

---

#### ATR（平均真实波幅）

真实波幅：
$$TR_{\tau} = \max\Big(H_{\tau} - L_{\tau},\ |H_{\tau} - C_{\tau-1}|,\ |L_{\tau} - C_{\tau-1}|\Big)$$

平均真实波幅（窗口长度 $N$）：
$$ATR_t = \frac{1}{N}\sum_{k=0}^{N-1} TR_{t-k}$$

ATR 衡量波动幅度，数值越大代表波动越剧烈，取负号后使低波为正向：
$$T_{atr}(s) = \text{clamp}\left(-\frac{ATR_t}{|C_t| + \varepsilon},\ -1,\ 1\right)$$

---

#### OBV（能量潮）

累积规则：当收盘价高于前日时累加当日成交量，低于前日时减去成交量：
$$OBV_t = OBV_{t-1} + \Delta OBV_t$$

其中：
$$\Delta OBV_t = \begin{cases} +V_t, & C_t > C_{t-1} \\ -V_t, & C_t < C_{t-1} \\ 0, & C_t = C_{t-1} \end{cases}$$

设平均成交量为 $\bar{V}$，窗口计算周期为 $N$：
$$T_{obv}(s) = \tanh\left(\frac{OBV_t}{\bar{V} \cdot N}\right)$$

---

#### VWAP（成交量加权平均价格）

以 $N$ 为窗口长度：
$$VWAP_t = \frac{\sum_{k=0}^{N-1} p_{t-k} \cdot V_{t-k}}{\sum_{k=0}^{N-1} V_{t-k}}$$

得分：
$$T_{vwap}(s) = \tanh\left(\frac{p_t - VWAP_t}{|VWAP_t| + \varepsilon}\right)$$

---


#### 量比（Volume Ratio）

窗口内平均成交量：
$$\bar{V}_t = \frac{1}{N}\sum_{k=0}^{N-1} V_{t-k}$$

当前成交量相对于历史均值的偏离：
$$T_{vr}(s) = \tanh\left(\frac{V_t - \bar{V}_t}{|\bar{V}_t| + \varepsilon}\right)$$

---

#### 换手率稳定性（Turnover Stability）

设窗口内换手率序列为 $\{x_1, \dots, x_N\}$。

变异系数：
$$CV = \frac{\sigma_x}{|\bar{x}|}$$

其中 $\bar{x} = \frac{1}{N}\sum x_i$，$\sigma_x = \sqrt{\frac{1}{N}\sum (x_i - \bar{x})^2}$。

稳定性先映射到 $[0,1]$：
$$\eta = 1 - \frac{\text{clamp}(CV,\ 0,\ 2)}{2}$$

再线性变换到 $[-1, 1]$：
$$T_{ts}(s) = \text{clamp}\big(2\eta - 1,\ -1,\ 1\big)$$

CV 越小代表换手率越稳定，得分越接近 1。

---

## 9. Dividend 因子

### 个性化指标
`dividend_yield`、`payout_ratio`、`dividend_stability`

### 数学公式

单指标得分即为对应字段值：
$$\delta_y(s) = D_s \quad (\text{股息率})$$
$$\delta_p(s) = \theta_s \quad (\text{股利支付率})$$
$$\delta_s(s) = \sigma_s \quad (\text{分红稳定性})$$

多指标时取简单平均：
$$\phi_{div}(s) = \frac{1}{|\mathcal{D}|}\sum_{d \in \mathcal{D}} \delta_d(s)$$

其中 $\mathcal{D}$ 为选定的红利指标集合。

**最低股息率过滤：**

若配置了最低股息率阈值 $\theta_{min}$，先对其进行归一化：

当输入值大于 1 时视为百分比（除以 100）；否则保持原值：
$$\theta_{eff} = \begin{cases} \frac{\theta_{min}}{100}, & \theta_{min} > 1 \\ \theta_{min}, & 0 < \theta_{min} \leq 1 \\ 0, & \theta_{min} \leq 0 \end{cases}$$

满足条件的股票需通过过滤：
$$D_s \geq \theta_{eff}$$

不满足者剔除出横截面。

---

## 10. Industry 因子

### 个性化指标
`industry_prosperity`、`industry_momentum`、`industry_concentration`

### 数学公式

设选定的行业指标为 $I(s)$，对应的行业字段值直接取当前横截面；若窗口序列可用，则优先使用窗口均值。

**指标取值（窗口优先）：**

设窗口长度为 $N$，单日值为 $I_{\tau}(s)$：

$$I_{eff}(s) = \begin{cases} \frac{1}{N}\sum_{k=1}^{N} I_{t-k+1}(s), & \text{若窗口序列存在} \\ I_t(s), & \text{否则} \end{cases}$$

**行业体系缩放：**

设行业分类体系对应的缩放系数为 $\gamma_{sector}$：
$$\phi_{ind}(s) = I_{eff}(s) \times \gamma_{sector}$$

其中 $\gamma_{sector}$ 取值规则：

| 行业体系 | $\gamma_{sector}$ |
|----------|-------------------|
| `sw_l1`  | 1.0              |
| `sw_l2`  | 0.85             |
| `citic_l1` | 0.95           |
| `citic_l2` | 0.80           |

### C++ 代码
```cpp
const auto metricSeriesBySymbol = fetchBatchSeriesMap(effectiveContext, industryMetric, window);

for (const auto& [symbol, value] : metricValues) {
    double resolvedValue = value;
    const auto seriesIt = metricSeriesBySymbol.find(symbol);
    if (seriesIt != metricSeriesBySymbol.end()) {
        const double aggregatedValue = safeFiniteMean(seriesIt->second);
        if (std::isfinite(aggregatedValue)) {
            resolvedValue = aggregatedValue;
        }
    }
    resolvedValue *= sectorWeight;
    if (std::isfinite(resolvedValue)) {
        result.values[symbol] = resolvedValue;
    }
}
```

---

## 11. Macro 因子

### 个性化维度
`growth`、`inflation`、`credit`、`rates`、`policy`、`risk_appetite`

### 个性化指标
`industrial_added_value_yoy`、`manufacturing_pmi`、`gdp_yoy`、`cpi_yoy`、`ppi_yoy`、`m2_yoy`、`social_financing_stock_yoy`、`m1_m2_spread`、`ten_year_bond_yield`、`shibor_3m`、`lpr_1y`、`reserve_requirement_ratio`、`aa_credit_spread`、`vix_proxy`

### 数学公式

宏观因子不直接使用宏观字段值作为得分，而是通过计算个股对宏观变量的敏感度来构建因子。

**股票收益序列：**

设宏观窗口长度为 $W$（参数 `macroWindow`）：
$$r_s(\tau) = \frac{p_{\tau} - p_{\tau-1}}{p_{\tau-1}},\quad \tau = t-W+1, \dots, t$$

**宏观代理变化序列：**

对于宏观指标 $M$，其第 $\tau$ 期的变化率为：
$$r_M(\tau) = \frac{M_{\tau} - M_{\tau-1}}{|M_{\tau-1}| + \varepsilon}$$

**敏感度计算：**

对每只股票 $s$、每个宏观指标 $i$ 和维度 $d$，计算两个序列的皮尔逊相关系数：
$$\rho_{s,i} = \frac{\text{Cov}\big(r_s, r_{M_i}\big)}{\sigma_{r_s} \cdot \sigma_{r_{M_i}}}$$

其中 $\sigma_{r_s}$ 和 $\sigma_{r_{M_i}}$ 分别为股票收益和宏观指标变化在窗口内的标准差。

考虑指标的方向性 $\delta_i \in \{-1, +1\}$ 和维度权重 $\omega_d$：
$$\psi_{s,d,i} = \tanh\big(\rho_{s,i} \times \delta_i \times \omega_d\big)$$

其中 $\omega_d$ 表示该维度的相对重要性，典型取值：$\omega_{growth} = 1.2$、$\omega_{inflation} = 0.9$、$\omega_{rates} = 0.95$、$\omega_{risk\_appetite} = 1.1$。

$\tanh$ 压缩函数使结果保持在 $(-1, 1)$ 区间，避免极端值。

**多指标合成：**

对所有选定的维度和指标取简单平均：
$$\phi_{macro}(s) = \frac{1}{|\mathcal{D}| \cdot |\mathcal{I}|} \sum_{d \in \mathcal{D}} \sum_{i \in \mathcal{I}} \psi_{s,d,i}$$

其中 $\mathcal{D}$ 为选定的维度集合，$\mathcal{I}$ 为选定的指标集合。宏观因子运行时必须显式提供这两个参数，否则直接报错。

### C++ 代码
```cpp
if (selectedDimensions.isEmpty() || selectedIndicators.isEmpty()) {
    result.dataStatus = CalculationResult::createError(
        "宏观因子必须显式提供 macroDimensions 和 macroIndicators").dataStatus;
    result.metadata.set("error",
        json_helper::toJsonValue("宏观因子必须显式提供 macroDimensions 和 macroIndicators"));
    return result;
}
```

```cpp
result.metadata.set("macroDimensions",
    json_helper::toJsonValue(selectedDimensions.join(",").toStdString()));
result.metadata.set("macroIndicators",
    json_helper::toJsonValue(selectedIndicators.join(",").toStdString()));
result.metadata.set("macroMode",
    json_helper::toJsonValue("proxy_sensitivity"));
```

---

## 12. Sentiment 因子

### 个性化来源与指标映射

| 来源 (`sentimentSource`) | 对应的规范化指标 (`metric`) |
|---------------------------|-----------------------------|
| `news`                   | `sentiment_score`          |
| `social`                 | `social_sentiment`         |
| `analyst`                | `investor_sentiment`       |
| `market`                 | `market_sentiment`         |
| `policy`                 | `policy_score`             |
| `alternative`            | `hot_rank`                 |
| `derivatives`            | `basis_rate`               |

### 数学公式

设选取的来源对应规范化指标为 $m$，窗口长度为 $N$（参数 `window`）。

情绪因子优先使用滚动窗口内的平均值（反映"一段时间内的整体情绪"）：
$$\phi_{sent}(s) = \frac{1}{N}\sum_{k=1}^{N} m_{s, t-k+1}$$

若窗口序列数据不可用，回退为当日截面值：
$$\phi_{sent}(s) = m_{s,t}$$

这是同一规范化指标的两种可用取值路径。

### C++ 代码
```cpp
const QString source = normalizeSentimentSource(
    QString::fromStdString(params_.sentimentSource));
const QString metric = normalizedMetric().isEmpty()
    ? sentimentMetricForSource(source)
    : normalizedMetric();
```

```cpp
const auto directMetricMap = currentFieldCrossSection(effectiveContext, metric);
const auto metricSeriesBySymbol = fetchBatchSeriesMap(effectiveContext, metric, window);
```

```cpp
if (!std::isfinite(resolvedValue)) {
    const auto directIt = directMetricMap.find(symbol);
    if (directIt != directMetricMap.end()) {
        resolvedValue = directIt->second;
    }
}
```

---

## 13. Custom 因子

### 个性化参数
`expression`、`variables[].name`、`variables[].field`、`variables[].defaultValue`

### 数学公式

自定义因子由用户提供的表达式 $E$ 和变量绑定列表 $\mathcal{V}$ 完全决定。

**变量绑定：**

对每个变量 $v_j \in \mathcal{V}$，其在股票 $s$ 上的取值 $x_j(s)$ 由字段名 `field_j` 绑定确定：
$$x_j(s) = \begin{cases} F_j(s), & \text{若字段 } \text{field}_j \text{ 存在且股票 } s \text{ 有数据} \\ d_j, & \text{若字段缺失但配置了默认值 } d_j = \text{defaultValue}_j \\ \text{无效}, & \text{否则} \end{cases}$$

其中 $F_j(s)$ 为股票 $s$ 在字段 $\text{field}_j$ 上的取值。

**表达式求值：**

表达式 $E$ 先转换为逆波兰表达式（RPN），然后对每只股票 $s$ 代入变量值：
$$\phi_{custom}(s) = \text{eval}\big(\text{RPN}(E), \{v_1 \mapsto x_1(s), \dots, v_m \mapsto x_m(s)\}\big)$$

等价于显式函数形式：
$$\phi_{custom}(s) = f\big(x_1(s), x_2(s), \dots, x_m(s)\big)$$

其中 $f$ 为用户定义的任意表达式函数。

**约束条件：**

表达式 $E$ 不能为空，若为空则运行时直接报错，不存在默认表达式回退。所有变量均需有合法取值（字段数据或默认值），否则该股票在本次计算中记为无效。

### C++ 代码
```cpp
const QString resolvedExpression = expression.trimmed();
if (resolvedExpression.isEmpty()) {
    if (errorMessage) {
        *errorMessage = QStringLiteral("自定义因子必须显式提供 expression");
    }
    return results;
}
```

```cpp
const QStringList rpn = factor::custom_expression::toRpn(
    resolvedExpression.toLower(), &parseError);
const auto evaluated = factor::custom_expression::evaluateRpn(
    rpn, variableMap, &evalError);
```

```cpp
result.values = evaluateCustomExpression(
    effectiveContext,
    QString::fromStdString(params_.expression),
    symbols,
    &errorMessage);
```

---

以上公式均基于文档中描述的计算逻辑独立推导得出，而非原文文本的直接复制。每个因子的数学表达力求完整准确地反映 C++ 运行时的实际计算过程。