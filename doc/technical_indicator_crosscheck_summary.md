# Technical Indicator Cross-Check Summary

## Current status
- C++ export test passed.
- TA-Lib mode comparison: 113 / 113 passed.
- Formula mode comparison: 101 / 113 passed, 12 failed.

## Failed technical indicators in formula mode
- `ema`: 3 symbols failed.
- `kdj`: 3 symbols failed.
- `macd`: 3 symbols failed.
- `obv`: 3 symbols failed.

## Observed delta pattern
- `ema`: small absolute drift, about 0.00027 to 0.00072.
- `kdj`: large systematic gap, relative error about 28.57%.
- `macd`: C++ side is effectively zero while the formula side remains non-zero.
- `obv`: systematic gap, relative error about 29% to 32%.

## Root cause notes
- `ema`: the TA-Lib path uses `TA_EMA`, while the old formula path seeds from the first value and applies a manual recursive EMA. The two smoothing conventions produce a small but stable tail drift.
- `macd`: the TA-Lib path uses `TA_MACD` histogram output, while the old formula path builds fast/slow EMA lines manually and normalizes the last histogram value. The different seeding and normalization make the old formula stay slightly away from zero when the TA-Lib result has already converged.
- `kdj`: the TA-Lib path uses `TA_STOCH` with SMA smoothing for slow K/D, then converts to J. The old formula path uses a hand-rolled RSV -> K -> D recursion over the last 5 bars. That is a different smoothing model, so the gap is systematic.
- `obv`: the TA-Lib path uses the full-series `TA_OBV` cumulative line and then normalizes by average volume over the tail window, while the old formula path only sums the signed volume changes inside the tail window. That makes the normalized magnitude consistently different.

## Interpretation
- The TA-Lib replacement is aligned with the current C++ export results.
- The old formula backend still differs materially on `macd`, `kdj`, and `obv`.
- The remaining `ema` drift is small and consistent with different smoothing conventions.

## Completion Prompts
- `technical`: TA-Lib 模式输出必须与 C++ 在容差内一致。
- `momentum`: 动量因子必须使用 `adj_factor` 价格链，不能回退 `close`。
- `value`: 估值因子权重与指标选择必须真实改变结果。
- `quality`: 质量指标选择与阈值过滤必须真实进入计算。
- `size`: 规模指标选择必须真实切换字段。
- `lowvol`: 低波组件与权重必须真实进入计算。
- `growth`: 成长指标与权重必须真实进入计算。
- `liquidity`: 流动性指标与窗口必须真实进入计算。
- `dividend`: 红利指标与最低股息率过滤必须真实生效。
