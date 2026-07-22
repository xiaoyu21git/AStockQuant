# 出场规则参数快照 (修改前)

> 记录于 2026-07-22，供后续对比/回滚

## exit_acceptance_breakdown.yaml

| 规则 | 参数 | 原值 |
|------|------|------|
| hard-stop-pnl-floor | 硬止损线 | **-12%** |
| breakdown-accelerate-exit | 亏损阈值 | **-5%** |
| breakdown-accelerate-exit | 跌破MA20幅度 | **3%** (0.97) |
| profit-trailing-stop | 利润门槛 | **12%** |
| profit-trailing-stop | 高点回撤触发 | **12%** |
| trend-damage-exit | 触发条件 | trend_damage_confirmed |
| support-break-exit | 跌破MA20幅度 | **1%** (0.99) |
| acceptance-collapse-exit | 承接评分阈值 | **< 20** |
| acceptance-weaken-reduce | 承接评分阈值 | **< 40** |
| acceptance-weaken-reduce | 日内抛压幅度 | **≥ 2%** |

## exit_scale_out_take_profit.yaml

| 规则 | 参数 | 原值 |
|------|------|------|
| profit-ma5-break-reduce | 浮盈门槛 | **3%** |
| profit-ma5-break-reduce | 减仓比例 | **30%** |
| profit-pressure-reduce | 浮盈门槛 | **3%** |
| profit-pressure-reduce | 接近前高比例 | **90%** |
| profit-pressure-reduce | 减仓比例 | **30%** |
| profit-overextend-reduce | 浮盈门槛 | **8%** |
| profit-overextend-reduce | 超MA20比例 | **110%** |
| profit-overextend-reduce | 减仓比例 | **30%** |
