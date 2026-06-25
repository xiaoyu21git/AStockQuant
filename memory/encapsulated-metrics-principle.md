---
name: encapsulated-metrics-principle
description: 指标计算必须封装为独立函数，不能暴露裸算法
metadata:
  type: feedback
---

每个独立的指标计算和算法都应该封装为函数，输入参数 → 返回结果，不暴露计算过程。

**Why:** 如果算法逻辑散落在桥接层或回测循环里：(1) 桥接层可能不小心改动算法导致绩效结果不准确；(2) 回测和实盘各自手写同一套公式，一个修了另一个没修；(3) 无法复用。

**How to apply:** 所有指标公式（波动率、夏普、索提诺、卡玛、年化收益、最大回撤、胜率、盈亏比、Alpha/Beta/TE/IR 等）都应该是 `FactorBacktestMetricsCalculator`（或独立 `PerformanceMetrics` 类）的 public static 方法。回测和实盘都调同一套接口，算法只在一处定义。
