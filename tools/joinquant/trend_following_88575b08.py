# -*- coding: utf-8 -*-
"""
多因子策略 88575b08 → 聚宽 (JoinQuant) 回测转换 (框架式)
============================================================
策略身份: 88575b08-386a-41ff-8cbd-91cabfabf70a
  策略名: 多因子-换手率稳定性
  行为:   MultiFactor — 引擎 MultiFactorStrategy 六阶段流水线, 语义与
          src/domain/strategy/src/MultiFactorStrategy.cpp 逐项对齐:
          ①截面 z-score (clamp[-3,3]) ②加权合成 composite ③持仓卖出
          (composite<0.2 或排名>40) ④Top-N 选股 (composite>0) ⑤等权
          1/n clamp[0.01,0.1] ⑥买入 (持仓更新目标权重 / 新标的限
          maxPositions-持仓数)
  因子层: 换手率稳定性单因子 (用户裁定 2026-08-15; ML 实例同为单因子)
  市场闸门: DB 3 市场模板 (ML 实例 862b0b8d): 熊市冻结 p99(regime==bear)
            + p97(breadth60<0.3 且 dd>=0.1) 冻结新开仓; p95 恐慌波动冻结
            已按用户裁定移除 (2026-08-15); 震荡精选 p84 / 牛市放行 p90
            为放行模板 (state_switch), 仅记日志不阻断
  择时闸门: 000300.XSHG MA20/MA60 五状态 (live 语义, 引擎级对所有策略生效)
  出场规则: 结构止盈 5 条件 (ML 实例 862b0b8d rule_profile 结构止盈
            summary): 前高85%减30 → 超涨MA110%减30 → 回撤6%清仓 →
            动能<50减30 → 支撑破位清仓 (有序第一命中, 减仓比例 0.3)
  风控层:   TimedCircuitBreaker (熔断器) + RiskEvaluator (每单风控)
            + EventRiskController (事件风控, 默认关闭) + Kelly 仓位建议输出

引擎依据:
  - 参数: config/strategy_862b0b8d_parameters_2026-08-15.json (ML 多因子实例)
  - 六阶段: src/domain/strategy/src/MultiFactorStrategy.cpp
  - 工厂: src/domain/strategy/src/NonFactorStrategy.cpp
  - 市场快照/出场规则变量: src/domain/strategy/rules/RuleVariableProvider.cpp
  - 市场模板: config/rules/templates/risk_market_*.json + exit_scale_out_take_profit.json
  - 择时闸门: src/domain/strategy/src/MarketTimingGate.cpp
  - 低波因子: src/domain/factor/src/LowVolFactor.cpp
  - 换手率稳定性: src/domain/factor/src/batch_technical_indicators.cpp
  - 熔断器: src/domain/strategy/src/TimedCircuitBreaker.cpp
  - 风控: src/domain/strategy/src/{RiskEvaluator,RiskManager,EventRiskSubscriber}.cpp
  - 凯利: src/domain/strategy/src/StrategyEngineFacade.cpp (回测后统计输出)

框架结构 (对应引擎分层, 便于后续换因子/改规则/调参数):
  ┌─ ① 配置层 STRATEGY ── 全部内部/外部参数, 数值与引擎 DB/模板对齐并注明来源
  ├─ ② 数据层 ────────── 股票池 / 增量面板滚动缓存 / 换手率批量缓存
  ├─ ③ 市场层 ────────── 市场快照 / 市场冻结闸门(p99+p97) / 放行模式(p90/p84) / 择时闸门
  ├─ ④ 因子层 ────────── FACTORS 因子注册表 + 截面 z-score 合成 (六阶段 1-2)
  ├─ ⑤ 信号层 ────────── 六阶段 3-6: 持仓卖出 / Top-N 选股 / 等权 / 买入
  ├─ ⑥ 出场规则层 ────── 结构止盈 5 条件有序表
  ├─ ⑦ 风控层 ────────── TimedCircuitBreaker / RiskEvaluator / EventRiskController
  ├─ ⑧ 执行层 ────────── 订单生成/过滤/成交记录/凯利统计
  └─ ⑨ 主循环 handle_data — 快照→闸门→熔断→因子→六阶段→出场→风控→下单

与 C++ 引擎实现的差异点/数据口径清单: doc/joinquant-conversion-notes.md
聚宽平台使用方法与调试指南: doc/joinquant-usage-guide.md

用法:
  1. 将本文件整体复制到聚宽"我的策略"
  2. 回测设置: 日频, 建议 2019-01-01 ~ 2026-07-31, 初始资金 1,000,000
  3. 全A 口径已用增量缓存加速 (首日预热后每日 3 次单行查询, 数据量约 45× 下降);
     快速冒烟可把 STRATEGY['universe_mode'] 改为 'hs300'
"""

import numpy as np
import pandas as pd
from collections import deque
# 注: get_price / order_target / order_target_value / log / g / set_* / OrderCost /
#     PriceRelatedSlippage 均为聚宽回测环境内置对象, 无需 import
# 数据接口必须用平台惯用形式 from jqdata import * (命名导入在新版沙箱会 ImportError);
# 提供: get_all_securities / get_index_stocks / get_fundamentals / query / valuation
from jqdata import *  # noqa: F401,F403

# ═══════════════════════════════════════════════════════════════════════
# ① 配置层: 全部参数 (与引擎 DB/模板/代码常量对齐, 每项注明来源)
#    调试参数只改这里; 换因子改 FACTORS 注册表; 换出场规则改 EXIT_RULES
# ═══════════════════════════════════════════════════════════════════════
STRATEGY = {
    # ---- 多因子核心 (引擎 MultiFactorStrategy; 参数=ML 实例 config/strategy_862b0b8d_parameters_2026-08-15.json) ----
    'max_positions': 20,            # DB: maxPositions=20 (每日买入信号硬上限)
    'top_n': 50,                    # DB: factor_overlay.targetPositionCount=50 (Top-N 选股)
    'sell_threshold': 0.2,          # 引擎硬编码: composite < 0.2 → 持仓卖出
    'sell_rank_multiplier': 2.0,    # DB 默认 2.0: 排名 > max_positions×2=40 → 持仓卖出
    'min_composite_score': 0.0,     # DB: minimumCompositeScore=0 (且 composite>0 只做多)
    'max_weight_per_stock': 0.10,   # DB: maxWeightPerStock=0.1 (等权 1/n 后 clamp 上限)
    'min_weight_per_stock': 0.01,   # DB: minWeightPerStock=0.01 (clamp 下限)
    'rebalance_frequency': 0,       # DB: 0 → 每交易日评估
    'min_hold_days': 0,             # DB 默认 0 → 最少持有期不启用 (出场规则不再审核)
    # 权重方案: EQUAL 1/n (DB weightScheme=0, 唯一口径); sellThreshold 与
    # sellRankMultiplier 为引擎代码常量 (NonFactorStrategy.cpp), 非 DB 可配项

    # ---- 因子层 (DB factor_overlay: ML 实例单因子 ai选股; 本转换=换手率稳定性) ----
    # 用户裁定 (2026-08-15): 因子组合=换手率稳定性单因子 (合成评分=其截面 z 分; ML 实例同为单因子)。
    # 引擎 MultiFactorStrategy: 每因子全截面 z-score (总体std, clamp[-3,3]) →
    # composite=Σ(归一化权重×z); 缺任一因子值剔除该股票。低波因子保留在 FACTORS 注册表,
    # 未配权重 = 不执行 (compute_factor_scores 只执行配了权重的 name);
    # 以后加因子: 在此字典加 name: 相对权重 (自动归一化)
    'factor_weights': {'turnover_stability': 1.0},

    # ---- 低波因子 (factor_低波因子-贝塔标准252-窗口20天中性化开启_46079) ----
    'lowvol': {
        'window': 20,          # 波动率/回撤窗口 (DB: window=20)
        'beta_period': 5,      # 贝塔周期 (引擎 TA_BETA period=min(5,n))
        'benchmark': '000300.XSHG',
        # 组件权重: DB 中 volatilityWeight/drawdownWeight/betaWeight 全为 0
        # (导致该因子生产失效, factor_values 0 行)。
        # 本转换采用 UI 默认等权: 33.4% / 33.3% / 33.3%
        'component_weights': {'volatility': 0.334, 'drawdown': 0.333, 'beta': 0.333},
    },

    # ---- 换手率稳定性 (batch_technical_indicators.cpp 权威实现) ----
    'turnover': {
        'window': 60,    # 统计窗口: 60 个有效交易日
        'cv_cap': 2.0,   # score = 1 - clamp(CV, 0, cv_cap) / cv_cap
    },

    # ---- 涨跌停过滤 (管道 isAtLimitUp/Down: 1.098 / 0.902, 引擎对所有板块统一) ----
    'limit_up_ratio': 1.098,
    'limit_down_ratio': 0.902,

    # ---- 结构止盈出场 5 条件阈值 ----
    # 来源: ML 实例 862b0b8d rule_profile 结构止盈 summary
    # "前高85%减30->超涨MA110%减30->回撤6%清仓->动能<50减30->支撑破位清仓"
    # (模板 exit_scale_out_take_profit payload: reduce_ratio=0.3; 有序第一命中)
    'exit_rules': {
        'high_pressure_min_pnl_pct': 3.0,   # ① 前高85%减30: pnl >= 3% 且 close/60日高(不含今) >= 0.85
        'high_pressure_ratio': 0.85,
        'overheat_min_pnl_pct': 8.0,        # ② 超涨MA110%减30: pnl >= 8% 且 close/MA20 >= 1.1
        'overheat_ma20_ratio': 1.1,
        'drawdown_exit_pct': 6.0,           # ③ 回撤6%清仓: 距持仓高点回撤 >= 6%
        'momentum_exit_threshold': 50.0,    # ④ 动能<50减30: 趋势健康分 < 50
        'support_break_ma20_ratio': 0.98,   # ⑤ 支撑破位清仓: close/MA20 < 0.98
        'reduce_ratio': 0.3,                # 减仓比例 30% (模板 payload reduce_ratio=0.3, 与 summary "减30" 一致)
    },

    # ---- 市场快照 (RuleVariableProvider::computeMarketSnapshot) ----
    'market': {
        'bull_breadth': 0.55,    # kRegimeBullBreadth
        'bear_breadth': 0.35,    # kRegimeBearBreadth
        'drawdown_window': 60,   # 等权指数回撤窗口 (日)
        'ma120_deviation': 0.12, # 结构确认偏离阈值 (sideways 判定, 引擎默认态即 sideways)
        # ---- p90 牛市趋势放行 (risk_market_bull_trend_allow_entry 模板) ----
        'bull_trend_breadth': 0.58,         # breadth60 >= 0.58
        'bull_trend_index_ma120': 1.01,     # 指数收盘/MA120 >= 1.01
        'bull_trend_strength': 0.62,        # trendStrengthScore >= 0.62 (引擎口径=breadth60)
        # ---- p84 震荡市精选放行 (risk_market_sideways_selective_entry 模板) ----
        'sideways_min_breadth': 0.38,       # breadth60 ∈ [0.38, 0.58]
        'sideways_max_breadth': 0.58,
        'sideways_max_vol_shock': 0.58,     # volShock <= 0.58
        'sideways_max_drawdown': 0.1,       # 等权指数回撤 <= 0.1
    },

    # ---- 市场冻结闸门阈值 (risk_market_bear_freeze_entry 模板 payload) ----
    # 熊市冻结模板 3 条独立 OR 中: p99 熊市体制 + p97 准熊市退潮 生效;
    # p95 恐慌波动冻结 (volShock>=0.7) 已移除 (用户裁定 2026-08-15,
    # 回测中平静市 volShock≈1 恒冻结; volShock 仍计算并进 [评估] 日志, 仅作观察)
    'market_freeze': {
        'risk_freeze_breadth': 0.3,     # p97 风险冻结: breadth60 < 0.3
        'risk_freeze_drawdown': 0.1,    # 且 drawdown >= 0.1
    },

    # ---- 择时闸门 (MarketTimingGate::evaluateRules) ----
    'timing': {
        # 引擎中 atrPercent 恒 0.02 占位 / crossSectionalVol 恒 0
        # → 高波动状态永不触发; 本转换保留开关 (默认关闭=忠实引擎)
        'enable_high_vol_state': False,
        'atr_percent': 0.02,
        'cross_sectional_vol': 0.0,
    },

    # ---- 熔断器 (TimedCircuitBreaker.cpp Config) ----
    'circuit_breaker': {
        'enabled': True,            # 引擎回测接线: updateEndOfDay 每日调用 + isHalted 清仓
        'max_drawdown': 0.11,       # 峰值回撤 >= 11% → 熔断 (ML 实例 DB: maxDrawdownLimit=11)
        'halt_days': 5,             # 熔断持续交易日数
        'daily_loss_limit': 0.03,   # 单日亏损 >= 3% → 减仓至 reduce_to
        'reduce_to': 0.5,
        # 忠实引擎: checkIntraday (单日亏损检查) 在引擎中零调用者 → 默认关闭;
        # 打开后按设计意图: 单日亏 3% → 当日新买单目标仓位 ×0.5
        'enable_daily_loss_check': False,
        # 引擎缺陷: 熔断分支内不调用 updateEndOfDay → 倒计时永不递减 (rebalanceFrequency=0
        # 恒调仓日) = 永久熔断; 本转换按设计意图每日递减 (halt_days 日后恢复)
    },

    # ---- 每单风控 (RiskEvaluator::evaluateOrder + RiskConfig) ----
    # ML 实例 862b0b8d DB: stopLossPercent=10 / takeProfitPercent=20 → 启用浮亏/浮盈
    # 拒加仓检查; maxDrawdownLimit=creationParams.maxDrawdownLimit(99) → 永不触发, 其余全 0。
    # 修改数值即调整对应检查 (调试参数只需改 STRATEGY['risk'])
    'risk': {
        'signal_strength_min': 0.1,     # 信号强度阈值 (多因子 composite 可为负 → 卖出信号不审核买入侧, 仅买入侧校验)
        'order_size_limit_wan': 0.0,    # 单笔委托上限(万元), 0=禁用 (引擎该策略构建为 0)
        'slippage_limit_pct': 0.0,      # 滑点容忍(%), 回测 referencePrice 不注入 → 不检查
        'turnover_limit_wan': 0.0,      # 日成交额上限(万元), 回测不追踪 → 不检查
        'stop_loss_pct': 10.0,          # 浮亏达线拒加仓 (DB: stopLossPercent=10)
        'take_profit_pct': 20.0,        # 浮盈达线拒加仓 (DB: takeProfitPercent=20)
        'max_drawdown_limit_pct': 99.0, # 账户回撤达线拒买入 (引擎 creationParams=99)
        'breaker_level1_pct': 0.0,      # 一级熔断 (引擎该策略构建为 0)
        'breaker_level2_pct': 0.0,      # 二级熔断
        'breaker_level3_pct': 0.0,      # 三级熔断
        'max_position_pct': 0.0,        # 单票集中度上限 (引擎该策略构建为 0)
        'max_total_exposure_pct': 0.0,  # 总敞口上限 (引擎该策略构建为 0)
    },

    # ---- 事件风控 (EventRiskSubscriber) ----
    # 引擎数据源: live EventBus news.* 事件 (Python 新闻管线) — 聚宽回测无新闻数据流
    # → 默认关闭 = 忠实回测 (引擎回测路径同样无事件)。接入数据源后在 handle_data 中
    # 调用 event_risk.apply_event(...) 并打开此开关, 规则语义 (引擎原样):
    #   stock/liquidate → 立即清仓名单; stock/reduce_position → 单股限仓 2%
    #   sector/reduce_exposure → 行业限仓 30%; sector/reduce_position → 50%
    #   market/reduce_exposure(severity>=0.5) → 总敞口<=50%; market/alert → <=80%
    #   tags 立案调查 → 封禁开仓 + 止损<=5% + 限仓<=2%; ST警示 → 封禁 + 限仓<=2%
    #   政策负面 (sentiment<-0.7 且 confidence>0.8) → 总敞口<=80%
    #   T+1 自动解禁 (每交易日评估入口清空)
    'event_risk': {
        'enabled': False,
    },

    # ---- 股票池 ----
    # 'all_a': 全A (剔除 ST/退市/次新/北交所); 'hs300': 沪深300 快速冒烟
    'universe_mode': 'all_a',
    'min_list_days': 250,   # 剔除上市不足 250 自然日 (保证 160 行面板与 MA120 窗口充足)
}

LOOKBACK = 160                    # close/指数滚动缓存行数: 覆盖 MA120(121行) + 60日等权指数 + 冗余
HIGH_LOOKBACK = 61                # high 滚动缓存行数: 60 日高(不含今) + 当日
TURNOVER_BATCH_DAYS = 5           # 换手率批量查询间隔(交易日): 一次区间查询追加多日值
TRADING_DAYS_PER_YEAR = 250       # 年化波动 √250


# ═══════════════════════════════════════════════════════════════════════
# ② 数据层: 股票池 / 收盘面板 / 换手率缓存
# ═══════════════════════════════════════════════════════════════════════
def build_universe(context):
    """股票池: 全A 剔除 ST/退市/次新/北交所 (universe_mode='hs300' 时取沪深300 成分)"""
    mode = STRATEGY.get('universe_mode', 'all_a')
    today = context.current_dt.date()
    if mode == 'hs300':
        index_codes = set(get_index_stocks('000300.XSHG', date=today))
    else:
        index_codes = None
    start_limit = today - pd.Timedelta(days=STRATEGY['min_list_days'])
    sec = get_all_securities(['stock'], date=today)
    codes = []
    for code, row in sec.iterrows():
        if index_codes is not None and code not in index_codes:
            continue
        if code[:1] in ('4', '8') or code.startswith('92'):
            continue   # 北交所/老三板
        name = '%s%s' % (row.get('display_name', ''), row.get('name', ''))
        if 'ST' in name or '退' in name:
            continue
        ed = row.get('end_date')
        if ed is not None and pd.notna(ed):
            ed_d = ed.date() if hasattr(ed, 'date') else ed
            if ed_d < today:
                continue   # 已退市
        sd = row.get('start_date')
        if sd is not None and pd.notna(sd):
            sd_d = sd.date() if hasattr(sd, 'date') else sd
            if sd_d > start_limit:
                continue   # 次新
        codes.append(code)
    return codes


def _normalize_panel(df):
    """把 get_price 返回自适应归一化为标准面板: index=交易日, columns=股票代码, 纯 float 值。
    新版平台 panel=False 的返回布局随引擎版本变化 (社区实测多种口径), 支持:
      A. 宽表 columns=MultiIndex(field, code) → 取最后一层 (代码)
      B. 长表 index=MultiIndex(..., code) → 单字段列按代码层 unstack 成宽表
      C. 标准宽表 columns=代码 → 原样
      D. 维度列变体: date/time/code 作为数据列而非索引 (部分平台版本) → 先落索引
    归一化后强制数值清洗: datetime 列丢弃 (数值 rolling 对 datetime64 列抛
    NotImplementedError, 转时间戳会污染计算), 其余非数值列尝试 pd.to_numeric,
    无法转换则丢弃; 全部丢弃列均打印 [数据] warn 诊断, 绝不把非数值列交给下游。
    其余布局/清洗后列空返回 None (fetch_panels 打印诊断日志后当日跳过, 不静默不崩溃)"""
    if df is None:
        return None
    df = df.copy()
    # ── 维度列变体 D: 平台把 date/time/code 放数据列而非索引 ──
    date_col = next((c for c in ('date', 'time') if c in df.columns), None)
    if date_col is not None:
        if isinstance(df.index, pd.MultiIndex):
            df = df.drop(columns=[date_col])   # 索引已含时间维度, 数据列重复 → 丢弃
        else:
            df[date_col] = pd.to_datetime(df[date_col])
            df = df.set_index(date_col)
            if 'code' in df.columns:
                df = df.set_index('code', append=True)
    elif 'code' in df.columns:
        df = df.set_index('code', append=True)
    if isinstance(df.columns, pd.MultiIndex):
        df.columns = df.columns.get_level_values(-1)
    if isinstance(df.index, pd.MultiIndex):
        if df.shape[1] != 1:
            return None   # 长表应只有 1 个字段列, 多列说明布局未知
        wide = df.iloc[:, 0].unstack()
        if not isinstance(wide.index, pd.DatetimeIndex):
            wide = wide.T   # 代码层在外层 → unstack 后行=代码, 转置为行=日期
    else:
        wide = df
    # ── 数值清洗 (兜底): 非数值列转换或丢弃, 不静默不崩溃 ──
    for c in list(wide.columns):
        dt = wide[c].dtype
        if np.issubdtype(dt, np.number) or dt == np.bool_:
            continue
        if np.issubdtype(dt, np.datetime64):
            log.warn('[数据] 面板混入日期列, 已丢弃: 列=%r dtype=%s' % (c, dt))
            wide = wide.drop(columns=[c])
            continue
        try:
            wide[c] = pd.to_numeric(wide[c], errors='raise')
        except (TypeError, ValueError):
            log.warn('[数据] 面板含无法数值化的列, 已丢弃: 列=%r dtype=%s' % (c, dt))
            wide = wide.drop(columns=[c])
    if len(wide.columns) == 0:
        return None   # 清洗后无数据列
    return wide


def _append_and_trim(old, new, max_rows):
    """滚动缓存追加: 按日期去重 (保留新值) 后裁剪到 max_rows 行; DataFrame 缺列填 NaN"""
    if hasattr(new, 'columns') and hasattr(old, 'columns'):
        new = new.reindex(columns=old.columns)
    combined = pd.concat([old, new])
    combined = combined[~combined.index.duplicated(keep='last')]
    return combined.tail(max_rows)


def _fetch_daily_row(universe, end, field):
    """拉取锚点日单行面板 (count=1): 归一化 + 行日期==锚点校验 (防御平台布局/日期异常);
    失败/日期不符 → [数据] warn + None (fetch_panels 当日跳过, 滚动缓存不动)"""
    try:
        raw = get_price(universe, end_date=end, count=1, frequency='daily',
                        fields=[field], fq='pre', panel=False)
        df = _normalize_panel(raw)
    except Exception as exc:
        log.warn('[数据] %s 面板获取失败: %s' % (field, exc))
        return None
    if df is None or df.empty:
        log.warn('[数据] %s 面板布局无法识别或为空: type=%s shape=%s' %
                 (field, type(raw).__name__, getattr(raw, 'shape', '?')))
        return None
    try:
        last_date = pd.Timestamp(df.index[-1]).strftime('%Y-%m-%d')
    except Exception:
        last_date = ''
    if last_date != end:
        log.warn('[数据] %s 面板日期与锚点不符: 行日期=%s 锚点=%s, 当日跳过' % (field, last_date, end))
        return None
    return df


def fetch_panels(universe, end):
    """
    增量数据层 (用户裁定 2026-08-15 速度优化): 决策数据与逐日全量拉取逐值相同
      - 首日预热: close LOOKBACK 行 / high 61 行 / 指数 LOOKBACK 行
      - 之后每日: count=1 单行面板追加到滚动缓存 (按日期去重 + 尾裁剪), 数据量约 45× 下降;
        单行日期 != 锚点 → [数据] warn + 当日跳过 (缓存不动, 次日重试)
      - pre_close 由 close 缓存派生 (close.iloc[-2], 锚点前收)
      - 停牌股 NaN 语义不变 (多股票面板平台强制日期对齐, 见下)
    返回 None 表示数据不可用 (当日跳过)
    面板口径: 新版平台弃用 panel=True (每次调用构造 pandas Panel 并告警,
    将来升级 pandas 后策略会失败, 见平台 UserWarning) → 全部 panel=False,
    返回列若为 MultiIndex (field, code) 由 _normalize_panel 取代码层
    停牌口径: 新版平台多股票查询禁止 skip_paused=True (面板日期对齐与跳停牌
    冲突, 报错"为了对齐日期, 不能跳过停牌") → 不传该参数, 停牌日个股为 NaN,
    下游信号/因子/出场/涨跌停过滤/缓存各处 np.isfinite/notna 检查自动跳过
    (与引擎"停牌跳过"语义一致)
    """
    cache = getattr(g, 'panel_cache', None)
    if cache is None:
        # ── 首日预热: 完整历史面板 (缓存未就绪前每日重试) ──
        try:
            raw = get_price(universe, end_date=end, count=LOOKBACK, frequency='daily',
                            fields=['close'], fq='pre', panel=False)
            close = _normalize_panel(raw)
            if close is None:
                log.warn('[数据] 收盘面板布局无法识别: type=%s shape=%s 列示例=%s 行索引示例=%s' %
                         (type(raw).__name__, getattr(raw, 'shape', '?'),
                          list(raw.columns)[:3], list(raw.index)[:2]))
                return None
        except Exception as exc:
            log.warn('[数据] 收盘面板获取失败: %s' % exc)
            return None
        if close.empty:
            log.warn('[数据] 收盘面板为空, 当日跳过')
            return None
        if len(close) < 66:
            log.warn('[数据] 收盘面板历史不足: %d 行 (需 >=66), 当日跳过' % len(close))
            return None
        try:
            high = _normalize_panel(get_price(universe, end_date=end, count=HIGH_LOOKBACK,
                                              frequency='daily', fields=['high'], fq='pre', panel=False))
            idx_df = get_price('000300.XSHG', end_date=end, count=LOOKBACK, frequency='daily',
                               fields=['close'], fq='pre', panel=False)
        except Exception as exc:
            log.warn('[数据] 面板获取失败: %s' % exc)
            return None
        if high is None:
            log.warn('[数据] 预热 high 面板不可用, 当日跳过')
            return None
        idx_close = idx_df['close'] if 'close' in idx_df.columns else idx_df.iloc[:, 0]
        g.panel_cache = dict(close=close, high=high.reindex(columns=close.columns), idx_close=idx_close)
    else:
        # ── 每日增量: 单行追加 close/high/指数 ──
        new_close = _fetch_daily_row(universe, end, 'close')
        if new_close is None:
            return None
        new_high = _fetch_daily_row(universe, end, 'high')
        if new_high is None:
            return None
        try:
            idx_df1 = get_price('000300.XSHG', end_date=end, count=1, frequency='daily',
                                fields=['close'], fq='pre', panel=False)
            idx1 = idx_df1['close'] if 'close' in idx_df1.columns else idx_df1.iloc[:, 0]
            idx_last = pd.Timestamp(idx1.index[-1]).strftime('%Y-%m-%d')
        except Exception as exc:
            log.warn('[数据] 指数面板获取失败: %s' % exc)
            return None
        if idx_last != end:
            log.warn('[数据] 指数面板日期与锚点不符: 行日期=%s 锚点=%s, 当日跳过' % (idx_last, end))
            return None
        cache['close'] = _append_and_trim(cache['close'], new_close, LOOKBACK)
        cache['high'] = _append_and_trim(cache['high'], new_high, HIGH_LOOKBACK)
        cache['idx_close'] = _append_and_trim(cache['idx_close'], idx1, LOOKBACK)
    cache = g.panel_cache
    if len(cache['close']) < 2:
        log.warn('[数据] 收盘面板历史不足 2 行, 当日跳过')
        return None
    return dict(close=cache['close'],
                high=cache['high'],
                pre_close=cache['close'].iloc[[-2]],   # 锚点前收 = 缓存倒数第二行 (省一次查询)
                idx_close=cache['idx_close'])


def _fetch_turnover_for_day(universe, day_str):
    """
    单日全市场换手率: get_fundamentals + query(valuation) — 聚宽在线回测沙箱
    官方标准估值接口 (原 get_valuation 为 JQData 本地 SDK 接口, 在线沙箱历史
    区间查询行为无文档保证, 且 4850只×5日 超平台 10000 行/次上限, 已弃用)。
    单日全A ~4850 行 < 10000 行限制。
    返回 {code: 换手率%} 或 None — 查询异常/返回空/全无有效值三条失败路径
    均显式 [数据] warn, 不静默。
    """
    try:
        q = query(valuation.code, valuation.turnover_ratio).filter(
            valuation.code.in_(list(universe)))
        df = get_fundamentals(q, date=day_str)
    except Exception as exc:
        log.warn('[数据] 换手率查询失败: 日期=%s 原因=%s' % (day_str, exc))
        return None
    if df is None or df.empty:
        log.warn('[数据] 换手率返回为空: 日期=%s 股票数=%d' % (day_str, len(universe)))
        return None
    if 'code' not in df.columns or 'turnover_ratio' not in df.columns:
        log.warn('[数据] 换手率面板列异常: 日期=%s 列=%s' % (day_str, list(df.columns)))
        return None
    out = {}
    for _, row in df.iterrows():
        tr = row['turnover_ratio']
        if not np.isfinite(tr):
            continue
        out[row['code']] = float(tr)
    if not out:
        log.warn('[数据] 换手率全无有效值: 日期=%s 行数=%d' % (day_str, len(df)))
        return None
    return out


def update_turnover_cache(anchor):
    """
    换手率缓存批量更新: 每 TURNOVER_BATCH_DAYS 个交易日把 (上次锚点, 本次锚点]
    区间内的每个交易日逐日查询入缓存 — 区间内交易日从收盘缓存索引枚举,
    逐日走 get_fundamentals 单日查询 (官方标准接口, 单日 ~4850 行不超限);
    按日期升序追加, deque 顺序=时间顺序。与逐日查询语义一致 (第 60 个有效值
    照旧在第 60 个交易日入池, 差异 #15 不变)。
    某日查询失败 → 区间起点停在该日 (已成功日不重复追加), 下一批从失败日重试
    (无数据空洞); 区间内无交易日 → 不推进起点。
    anchor = 决策数据锚点日期 (上一交易日; 当日盘中取当日换手率会被平台拦截)
    """
    if g.day_count % TURNOVER_BATCH_DAYS != 1 and getattr(g, 'next_turnover_start', None) is not None:
        return
    if getattr(g, 'next_turnover_start', None) is None:
        start = pd.Timestamp(anchor)
    else:
        start = pd.Timestamp(g.next_turnover_start)
    anchor_ts = pd.Timestamp(anchor)
    targets = [d for d in g.panel_cache['close'].index if start <= d <= anchor_ts]
    if not targets:
        log.warn('[数据] 换手率区间无交易日: [%s, %s], 当日跳过' %
                 (start.strftime('%Y-%m-%d'), anchor))
        return
    w = STRATEGY['turnover']['window']
    fetched = 0
    for d in targets:
        day_str = d.strftime('%Y-%m-%d')
        vals = _fetch_turnover_for_day(g.universe, day_str)
        if vals is None:
            g.next_turnover_start = day_str   # 失败日不推进, 下批从该日重试
            log.warn('[数据] 换手率批量中断于 %s (前 %d 日已入缓存), 下批从该日重试' %
                     (day_str, fetched))
            return
        for code, tr in vals.items():
            g.turnover_cache.setdefault(code, deque(maxlen=w)).append(tr)
        fetched += 1
    g.next_turnover_start = (anchor_ts + pd.Timedelta(days=1)).strftime('%Y-%m-%d')


# ═══════════════════════════════════════════════════════════════════════
# ③ 市场层: 市场快照 / 市场冻结闸门(p99+p97) / 放行模式(p90/p84) / 择时闸门
# ═══════════════════════════════════════════════════════════════════════
def compute_market_snapshot(close):
    """
    市场快照 (RuleVariableProvider::computeMarketSnapshot 精确口径):
      breadth60/20 = P(close > MA60/MA20)  (窗口完整且 close>0 的股票计入分母)
      等权指数 = 日收益等权平均累积 (60 日窗口, 从 1.0 起; 峰值含 1.0 起点)
      drawdown  = 1 - idx/peak
      volShock  = min(1, vol5/vol60), 年化 √250, 总体标准差 (TA_STDDEV nbDev=1.0)
      indexMa120 = 近120日等权指数水平均值 ([1.0, cumprod(1+ret120)...] 共 121 个水平)
      indexAboveMa120 = 等权指数当前水平 / indexMa120 (p90 放行模板变量; 引擎 indexClose
                        同为股票面板等权指数末值, 同源同尺度, 非外部指数序列)
      trendStrength = breadth60 (引擎 RuleVariableProvider: trendStrengthScore=breadthAboveMa60Ratio)
      regime: breadth60>=0.55→bull; breadth60<=0.35且breadth20<=0.35→bear; 其余→sideways
      (引擎 结构确认层: 宽度∈(0.35,0.55) 且偏离MA120<12% 强制 sideways — 该区间
       默认已是 sideways, 分支无实际效果, 本转换省略; 全部变量同源于股票面板)
    """
    mkt = STRATEGY['market']
    ma60 = close.rolling(60, min_periods=60).mean()
    ma20 = close.rolling(20, min_periods=20).mean()
    last = close.iloc[-1]
    m60 = ma60.iloc[-1].notna() & (last > 0)
    m20 = ma20.iloc[-1].notna() & (last > 0)
    breadth60 = float((last[m60] > ma60.iloc[-1][m60]).mean()) if m60.any() else 0.0
    breadth20 = float((last[m20] > ma20.iloc[-1][m20]).mean()) if m20.any() else 0.0
    ret = close.pct_change()
    ew = ret.iloc[-mkt['drawdown_window']:].mean(axis=1, skipna=True).dropna()
    if len(ew) < 2:
        return None
    idx = (1.0 + ew).cumprod()
    peak = max(1.0, float(idx.max()))
    drawdown = 1.0 - float(idx.iloc[-1]) / peak
    vol5 = ew.iloc[-5:].std(ddof=0) * np.sqrt(TRADING_DAYS_PER_YEAR)
    vol60 = ew.std(ddof=0) * np.sqrt(TRADING_DAYS_PER_YEAR)
    vol_shock = min(1.0, vol5 / vol60) if vol60 > 1e-9 else 0.0
    ret120 = ret.iloc[-120:].mean(axis=1, skipna=True).dropna()
    if len(ret120) >= 1:
        levels = np.concatenate([[1.0], (1.0 + ret120).cumprod().values])
        index_ma120 = float(levels.mean())
    else:
        index_ma120 = 0.0
    index_above_ma120 = float(idx.iloc[-1]) / index_ma120 if index_ma120 > 0.0 else 0.0
    if breadth60 >= mkt['bull_breadth']:
        regime = 'bull'
    elif breadth60 <= mkt['bear_breadth'] and breadth20 <= mkt['bear_breadth']:
        regime = 'bear'
    else:
        regime = 'sideways'
    return dict(breadth60=breadth60, breadth20=breadth20, drawdown=drawdown,
                vol_shock=vol_shock, index_ma120=index_ma120,
                index_above_ma120=index_above_ma120,
                trend_strength=breadth60, regime=regime)


def compute_advance_ratio(close):
    """上涨家数占比: close_t > close_{t-1} 且两者 > 1e-9 (引擎口径)"""
    last = close.iloc[-1]
    prev = close.iloc[-2]
    valid = (last > 1e-9) & (prev > 1e-9)
    return float((last[valid] > prev[valid]).mean()) if valid.any() else 0.0


def _freeze_rules():
    """
    市场规则闸门有序规则表 (RuleGate::runRules 第一命中即返回, priority 降序):
      p99 熊市体制冻结: regime == bear                        → 冻结
      p97 准熊市退潮冻结: breadth60 < 0.3 且 drawdown >= 0.1  → 冻结
    (熊市冻结模板 risk_market_bear_freeze_entry 3 条独立 OR 中的前两条;
     p95 恐慌波动冻结 volShock>=0.7 已按用户裁定移除 2026-08-15)
    新增冻结规则: 在此列表按优先级插入 (priority, 名称, judge(snap)->bool)
    """
    fz = STRATEGY['market_freeze']

    def bear_freeze(snap):
        return snap['regime'] == 'bear'

    def risk_freeze(snap):
        return snap['breadth60'] < fz['risk_freeze_breadth'] and snap['drawdown'] >= fz['risk_freeze_drawdown']

    return [
        (99, '熊市体制冻结(p99)', bear_freeze),
        (97, '准熊市退潮冻结(p97)', risk_freeze),
    ]


def market_freeze(snap):
    """市场冻结闸门: 第一命中即返回 (True, 原因); 全不命中 → (False, '')"""
    for _priority, name, judge in _freeze_rules():
        if judge(snap):
            return True, name
    return False, ''


def market_allow_mode(snap):
    """
    市场放行模式 (ML 实例 862b0b8d 市场放行组 must_pass all 中两条 state_switch 模板):
      p90 牛市趋势放行: bull && breadth60>=0.58 && indexAboveMa120>=1.01
                        && trendStrength>=0.62 → 'active' (放行趋势类新增仓位)
      p84 震荡市精选放行: sideways && breadth60∈[0.38,0.58] && volShock<=0.58
                        && drawdown<=0.1 → 'selective_active' (仅放行精选确认类)
    两条均为放行模板 (result=state_switch), 无阻断效果 → 本转换仅记日志 mode, 不进闸门判定。
    (多因子策略引擎侧 ruleSetId=kRuleSetAllPass, 规则闸门整体旁路, 模板仅作配置事实保留)
    """
    mkt = STRATEGY['market']
    if snap['regime'] == 'bull' \
            and snap['breadth60'] >= mkt['bull_trend_breadth'] \
            and snap['index_above_ma120'] >= mkt['bull_trend_index_ma120'] \
            and snap['trend_strength'] >= mkt['bull_trend_strength']:
        return 'active'
    if snap['regime'] == 'sideways' \
            and mkt['sideways_min_breadth'] <= snap['breadth60'] <= mkt['sideways_max_breadth'] \
            and snap['vol_shock'] <= mkt['sideways_max_vol_shock'] \
            and snap['drawdown'] <= mkt['sideways_max_drawdown']:
        return 'selective_active'
    return ''


def compute_timing_gate(idx_close, advance_ratio):
    """
    择时闸门 (MarketTimingGate::evaluateRules, 000300.SH, live 语义):
      进攻:  above60 && trendUp && advanceRatio>0.5  → exposure 1.0, 允许新开仓
      谨慎:  above60 && advanceRatio<0.35            → exposure 0.5, 允许新开仓
      防御:  !above60 && above20                     → exposure 0.2, 禁止新开仓
      空仓:  !above20 && !trendUp && ratio<0.30      → exposure 0.0, 强制清仓
      高波动: atrPercent>0.03 || 截面vol>0.03        → exposure 0.3, 禁止新开仓
             (引擎 atrPercent 恒 0.02 占位 → 永不触发; 本转换保留开关默认关)
      默认:                                          → exposure 0.5, 允许新开仓
    trendUp = MA20 > MA60 且 MA20 > 5日前MA20 (引擎 sum20_5 = mean(close[t-5..t-24]))
    exposure 仅记录日志, 不下单缩放 (引擎 EOD 管道同样未用 exposure 缩放仓位)
    """
    t = STRATEGY['timing']
    if len(idx_close) < 65:
        return dict(exposure=0.5, allow_new=True, force_liquidate=False, state='数据不足')
    ma20 = idx_close.rolling(20, min_periods=20).mean()
    ma60 = idx_close.rolling(60, min_periods=60).mean()
    c = idx_close.iloc[-1]
    m20, m60 = ma20.iloc[-1], ma60.iloc[-1]
    m20_5ago = ma20.iloc[-6]
    above60 = c > m60
    above20 = c > m20
    trend_up = (m20 > m60) and (m20 > m20_5ago)
    if above60 and trend_up and advance_ratio > 0.5:
        return dict(exposure=1.0, allow_new=True, force_liquidate=False, state='进攻')
    if above60 and advance_ratio < 0.35:
        return dict(exposure=0.5, allow_new=True, force_liquidate=False, state='谨慎')
    if not above60 and above20:
        return dict(exposure=0.2, allow_new=False, force_liquidate=False, state='防御')
    if not above20 and not trend_up and advance_ratio < 0.30:
        return dict(exposure=0.0, allow_new=False, force_liquidate=True, state='空仓')
    if t['enable_high_vol_state'] and (t['atr_percent'] > 0.03 or t['cross_sectional_vol'] > 0.03):
        return dict(exposure=0.3, allow_new=False, force_liquidate=False, state='高波动')
    return dict(exposure=0.5, allow_new=True, force_liquidate=False, state='中性')


# ═══════════════════════════════════════════════════════════════════════
# ④ 因子层: 因子注册表 + 截面 z-score 合成 (MultiFactorStrategy 阶段1-2)
#    ★ 换因子/加因子指南: ①写 compute(close, idx_close)->{code:score}
#      ②在 FACTORS 注册 (name: compute 函数)
#      ③STRATEGY['factor_weights'] 加该 name 的权重 (相对权重自动归一化)
# ═══════════════════════════════════════════════════════════════════════
def compute_lowvol_scores(close, idx_close):
    """
    低波因子 (factorType 12, LowVolFactor.cpp):
      波动率: 20 收盘 → 19 收益率总体标准差 (TA_STDDEV nbDev=1.0)
      回撤:   20 日最大回撤 (窗口内峰值回撤)
      贝塔:   TA_BETA period=min(5,n) vs 000300.SH (5 对有效收益, 总体协方差/方差)
      各组件横截面反向归一化 (max-x)/(max-min) 后按权重加权
      (截面 max<=min → 全部 1.0, 引擎 normalizedInverseScore 口径)
      任一组件缺失 → 剔除该股票 (引擎: needsVolatility/needsDrawdown/needsBeta 全必须)
      注: DB 组件权重 0/0/0 生产失效 → 本转换用 UI 默认等权 (见 STRATEGY['lowvol'])
    """
    cfg = STRATEGY['lowvol']
    ret = close.pct_change()
    vol = ret.iloc[-(cfg['window'] - 1):].std(ddof=0)          # 19 收益总体标准差
    cw = close.iloc[-cfg['window']:]
    peak = cw.cummax()
    dd = ((peak - cw) / peak).max(axis=0)                       # 20 日最大回撤
    bret = idx_close.reindex(close.index).pct_change()
    beta = {}
    bp = cfg['beta_period']
    for code in close.columns:
        x = ret[code].iloc[-bp:]
        y = bret.iloc[-bp:]
        m = x.notna() & y.notna()
        xv, yv = x[m].values, y[m].values
        if len(xv) < bp or len(yv) < bp:
            beta[code] = np.nan
            continue
        xm, ym = xv.mean(), yv.mean()
        denom = float(((yv - ym) ** 2).sum())
        beta[code] = float(((xv - xm) * (yv - ym)).sum()) / denom if denom > 1e-12 else np.nan
    beta = pd.Series(beta, dtype=float)
    ok = vol.notna() & dd.notna() & beta.notna() & (close.iloc[-1] > 0)

    def inv_norm(s):
        if float(s.max()) - float(s.min()) <= 1e-12:
            return pd.Series(1.0, index=s.index)   # 引擎: maxValue<=minValue → 1.0
        return (s.max() - s) / (s.max() - s.min())

    cw_cfg = cfg['component_weights']
    score = (inv_norm(vol) * cw_cfg['volatility']
             + inv_norm(dd) * cw_cfg['drawdown']
             + inv_norm(beta) * cw_cfg['beta'])
    return {c: float(score[c]) for c in close.columns if ok[c]}


def compute_turnover_stability_scores(close, idx_close):
    """
    换手率稳定性 (batch_technical_indicators.cpp 权威实现):
      CV = 总体std60 / |mean60|  (TA_STDDEV nbDev=1.0 → ddof=0)
      score = 1 - clamp(CV, 0, cv_cap) / cv_cap ∈ [0,1]
    窗口不足 60 个有效日 → 不产出 (回测前 60 交易日该因子缺数据, 仅低波生效)
    """
    w = STRATEGY['turnover']['window']
    cap = STRATEGY['turnover']['cv_cap']
    out = {}
    for code, buf in g.turnover_cache.items():
        if len(buf) < w:
            continue
        vals = np.asarray(buf, dtype=float)
        mean = abs(float(vals.mean()))
        if mean <= 1e-12:
            continue
        cv = float(vals.std(ddof=0)) / mean
        out[code] = 1.0 - min(max(cv, 0.0), cap) / cap
    return out


# 因子注册表: name → compute(close, idx_close) -> {code: score}
# (换手率稳定性从 g.turnover_cache 读取, 与面板解耦)
FACTORS = {
    'lowvol': compute_lowvol_scores,
    'turnover_stability': compute_turnover_stability_scores,
}


def compute_factor_scores(close, idx_close):
    """执行全部已注册因子 (权重在 STRATEGY['factor_weights'], 未注册的 name 忽略)"""
    names = [n for n in STRATEGY['factor_weights'] if n in FACTORS]
    return {name: FACTORS[name](close, idx_close) for name in names}


def build_composite(factor_scores):
    """
    截面 z-score 合成 (MultiFactorStrategy::normalizeCrossSectional + computeCompositeScores):
      每因子全截面统计: mean / 总体方差 (sumSq/n - mean², 引擎口径), invStd=std>eps ? 1/std : 0
      z = clamp((x - mean) * invStd, -3, 3)    (std<=eps → z=0, 全部同分)
      composite = Σ(归一化权重 × z)             (单因子权重 1.0 → composite = z, 值域 [-3,3])
      任一因子截面缺失该股票 → 剔除 (引擎 requiredFactorCount 校验)
      任一因子截面为空 → composite 空 (提前返回; 预热期换手因子窗口不足时命中,
      避免对空数组算均值产生 NaN 警告)
    """
    fw_raw = STRATEGY['factor_weights']
    names = [n for n in fw_raw if n in factor_scores]
    if not names:
        return {}
    total_fw = sum(fw_raw[n] for n in names)
    fw = {n: fw_raw[n] / total_fw for n in names} if total_fw > 0 else {}
    stats = {}
    for name in names:
        vals = factor_scores[name]
        if not vals:
            # 任一因子截面为空 → 交集必为空 → composite 空 (引擎 requiredFactorCount 同语义);
            # 预热期换手因子 60 日窗口不足时每天命中, 提前返回不产生 NaN 统计
            log.info('[因子] %s 截面为空, 当日无候选' % name)
            return {}
        arr = np.asarray(list(vals.values()), dtype=float)
        n = len(arr)
        mean = float(arr.mean())
        variance = float((arr * arr).sum() / n) - mean * mean
        std = float(np.sqrt(variance)) if variance > 0.0 else 0.0
        stats[name] = (mean, 1.0 / std if std > 1e-12 else 0.0)
    both = set.intersection(*[set(v) for v in factor_scores.values()])
    composite = {}
    for code in both:
        s = 0.0
        for name in names:
            mean, inv_std = stats[name]
            z = min(max((factor_scores[name][code] - mean) * inv_std, -3.0), 3.0)
            s += z * fw[name]
        composite[code] = s
    return composite


# ═══════════════════════════════════════════════════════════════════════
# ⑤ 信号层: 六阶段 3-6 (MultiFactorStrategy::emitExitSignals / rankAndSelect /
#    allocateWeights / emitBuySignals), 阶段 1-2 在 ④ 因子层完成
# ═══════════════════════════════════════════════════════════════════════
def evaluate_multi_factor_signals(composite, holdings, allow_new, blocked):
    """
    MultiFactorStrategy 六阶段 3-6 语义:
      阶段3 持仓卖出: 持仓且 composite < sellThreshold(0.2) → 卖出;
                      或 全截面排名 > maxPositions×sellRankMultiplier(40) → 卖出
                      (排名=全体 composite 降序, 从 1 起; 持仓缺因子值 → 无信号, 仓位保留)
      阶段4 选股:     composite >= minCompositeScore(0) 且 > 0 (只做多) → 降序 Top-N(50)
      阶段5 权重:     EQUAL 1/n (n=入选数), clamp[minW=0.01, maxW=0.1]
      阶段6 买入:     已持仓 → 更新目标权重 (可加/减仓); 新标的仅当
                      maxPositions(20)-当前持仓数 > 0; 按分数降序, 新买入耗尽预算即停止
                      (引擎 break 语义: 预算耗尽后不再发任何买入信号, 含持仓更新)
      冻结日 (allow_new=False): 新标的禁止买入 (引擎 live 语义: blockNewBuys 跳过非持仓;
      持仓仍可更新目标权重 — live 管道不阻断)
      blocked: 事件风控封禁名单 (引擎 live 管道跳过该股票全部评估, 默认空)
    返回 (sells, buys):
      sells = [(code, score, reason)]                  reason: 'score' 评分跌破 / 'rank' 排名跌出
      buys  = [(code, score, target_weight, is_held)]
    """
    cfg = STRATEGY
    for b in blocked:
        composite.pop(b, None)   # 封禁股票不参与排名/选股/卖出评估 (引擎 live 跳过)
    ranked = sorted(composite.items(), key=lambda kv: -kv[1])   # composite 降序
    rank_of = {code: i + 1 for i, (code, _v) in enumerate(ranked)}
    rank_exit = int(cfg['max_positions'] * cfg['sell_rank_multiplier']) \
        if cfg['sell_rank_multiplier'] > 0 else 0
    # ── 阶段3: 持仓卖出 ──
    sells = []
    for code in holdings:
        sc = composite.get(code)
        if sc is None:
            continue   # 持仓缺因子值 → 引擎 scoreById 找不到 → 无卖出信号, 仓位保留
        if sc < cfg['sell_threshold']:
            sells.append((code, sc, 'score'))
        elif rank_exit > 0 and rank_of.get(code, 0) > rank_exit:
            sells.append((code, sc, 'rank'))
    # ── 阶段4: Top-N 选股 ──
    selected = [(c, v) for c, v in ranked
                if v >= cfg['min_composite_score'] and v > 0.0][:cfg['top_n']]
    # ── 阶段5+6: 等权 clamp + 买入 ──
    n = len(selected)
    raw_w = 1.0 / n if n > 0 else 0.0
    target_w = min(max(raw_w, cfg['min_weight_per_stock']), cfg['max_weight_per_stock'])
    max_new = cfg['max_positions'] - len(holdings)
    buys = []
    new_count = 0
    for code, sc in selected:
        held = code in holdings
        if not held:
            if not allow_new:
                continue   # 冻结/防御/高波动日: 新标的禁止买入 (引擎 live blockNewBuys 语义), 持仓更新不受影响
            if max_new <= 0:
                continue
        buys.append((code, sc, target_w, held))
        if not held:
            new_count += 1
            if new_count >= max_new:
                break   # 引擎: 新买入预算耗尽 → 停止全部买入信号 (含后续持仓更新)
    return sells, buys


# ═══════════════════════════════════════════════════════════════════════
# ⑥ 出场规则层: 结构止盈 5 条件 (ML 实例 862b0b8d rule_profile 结构止盈 summary)
#    "前高85%减30->超涨MA110%减30->回撤6%清仓->动能<50减30->支撑破位清仓"
#    有序第一命中; Reduce 减仓比例 0.3 (模板 payload reduce_ratio)
#    ★ 换规则/加规则指南: 在 _exit_rules() 列表按优先级插入
#      (priority, 名称, judge(d)->(action, reason)|None), 阈值在 STRATEGY['exit_rules']
# ═══════════════════════════════════════════════════════════════════════
def trend_health_score(pc, ma20_now, ma20_5ago, ma60_now, ma60_5ago):
    """
    趋势健康分 (RuleVariableProvider::trendHealthScore, 0-100):
      MA20 斜率向上 (MA20_t > MA20_{t-5})  +25   [引擎 kMaSlopeWindow=5, 严格大于]
      MA60 斜率向上 (MA60_t > MA60_{t-5})  +25
      收盘 > MA20                          +25
      收盘 > MA60                          +25
    单项数据缺失 → 该项不加分 (引擎单项跳过, 不整体失效)
    """
    s = 0
    if np.isfinite(ma20_now) and np.isfinite(ma20_5ago) and ma20_now > ma20_5ago:
        s += 25
    if np.isfinite(ma60_now) and np.isfinite(ma60_5ago) and ma60_now > ma60_5ago:
        s += 25
    if np.isfinite(ma20_now) and ma20_now > 0 and np.isfinite(pc) and pc > ma20_now:
        s += 25
    if np.isfinite(ma60_now) and ma60_now > 0 and np.isfinite(pc) and pc > ma60_now:
        s += 25
    return s


def _exit_rules():
    """
    持仓出场规则有序表 (结构止盈 5 条件, 优先级第一命中; 来源=ML 实例 862b0b8d
    rule_profile 结构止盈 summary, 模板 exit_scale_out_take_profit payload
    reduce_ratio=0.3; 引擎侧多因子规则闸门旁路, 本转换按模板意图实现):
      ① 前高85%减30:    pnl >= 3% 且 close/60日高(不含今) >= 0.85 → REDUCE
      ② 超涨MA110%减30: pnl >= 8% 且 close/MA20 >= 1.1            → REDUCE
      ③ 回撤6%清仓:     距持仓高点回撤 >= 6%                       → EXIT
      ④ 动能<50减30:    趋势健康分 < 50                            → REDUCE
      ⑤ 支撑破位清仓:   close/MA20 < 0.98                          → EXIT
    REDUCE 实际卖出 = 持仓 × exit_rules['reduce_ratio'] (0.3, 与 summary "减30" 一致)
    """
    e = STRATEGY['exit_rules']

    def r1(d):
        if d['pnl'] >= e['high_pressure_min_pnl_pct'] and np.isfinite(d['high60']) \
                and d['high60'] > 0 and d['close'] / d['high60'] >= e['high_pressure_ratio']:
            return ('REDUCE', '前高85%减30')
        return None

    def r2(d):
        if d['ma20'] > 0 and d['pnl'] >= e['overheat_min_pnl_pct'] \
                and d['close'] / d['ma20'] >= e['overheat_ma20_ratio']:
            return ('REDUCE', '超涨MA110%减30')
        return None

    def r3(d):
        if d['holding_high'] > 0 \
                and (1.0 - d['close'] / d['holding_high']) >= e['drawdown_exit_pct'] / 100.0:
            return ('EXIT', '回撤6%清仓')
        return None

    def r4(d):
        if d['health'] is not None and d['health'] < e['momentum_exit_threshold']:
            return ('REDUCE', '动能<50减30')
        return None

    def r5(d):
        if d['ma20'] > 0 and d['close'] / d['ma20'] < e['support_break_ma20_ratio']:
            return ('EXIT', '支撑破位清仓')
        return None

    return [
        (1, '前高压力', r1),
        (2, '超涨', r2),
        (3, '回撤', r3),
        (4, '动能', r4),
        (5, '支撑破位', r5),
    ]


def position_exit_rule(d):
    """出场规则第一命中 (action, reason); 全不命中 → (None, '')"""
    for _priority, _name, judge in _exit_rules():
        hit = judge(d)
        if hit is not None:
            return hit
    return None, ''


# ═══════════════════════════════════════════════════════════════════════
# ⑦ 风控层: 熔断器 / 每单风控 / 事件风控 / 凯利统计
# ═══════════════════════════════════════════════════════════════════════
class TimedCircuitBreaker:
    """
    引擎 TimedCircuitBreaker 移植 (设计意图语义; 引擎缺陷见 STRATEGY 注释):
      峰值回撤 >= max_drawdown → 熔断 halt_days 交易日 (清仓 + 暂停交易)
      倒计时逐日递减, 归零 → 恢复 (峰值重置为当前净值)
      单日亏损 >= daily_loss_limit → daily_reduced (targetExposure = reduce_to)
      [引擎接线事实: checkIntraday 零调用者 / targetExposure 零消费 / live 路径惰性]
    """

    def __init__(self, cfg):
        self._cfg = cfg
        self._peak_equity = 0.0
        self._trading_halted = False
        self._halt_days_remaining = 0
        self._daily_reduced = False
        self._daily_start_equity = 0.0

    def check_intraday(self, current_equity):
        """盘中检查 (引擎 backtest 不调用; 由 enable_daily_loss_check 开关决定是否启用)"""
        if self._trading_halted:
            return False
        if current_equity > self._peak_equity:
            self._peak_equity = current_equity
        if self._peak_equity > 0.0:
            drawdown = 1.0 - current_equity / self._peak_equity
            if drawdown >= self._cfg['max_drawdown']:
                self._trading_halted = True
                self._halt_days_remaining = self._cfg['halt_days']
                return True
        if not self._daily_reduced and self._daily_start_equity > 0.0:
            daily_loss = 1.0 - current_equity / self._daily_start_equity
            if daily_loss >= self._cfg['daily_loss_limit']:
                self._daily_reduced = True
                return True
        return False

    def update_eod(self, end_of_day_equity):
        """日终更新 (引擎 backtest 每个正常交易日调用)"""
        if end_of_day_equity > self._peak_equity:
            self._peak_equity = end_of_day_equity
        if self._trading_halted:
            self._halt_days_remaining -= 1
            if self._halt_days_remaining <= 0:
                self._trading_halted = False
                self._peak_equity = end_of_day_equity   # 重置峰值, 以当前净值重新开始
        self._daily_start_equity = end_of_day_equity
        self._daily_reduced = False

    def is_halted(self):
        return self._trading_halted

    def remaining_halt_days(self):
        return self._halt_days_remaining

    def is_daily_reduced(self):
        return self._daily_reduced

    def target_exposure(self):
        """目标敞口系数: 熔断 0.0 / 单日减仓 reduce_to / 正常 1.0"""
        if self._trading_halted:
            return 0.0
        if self._daily_reduced:
            return self._cfg['reduce_to']
        return 1.0


class RiskEvaluator:
    """
    引擎 RiskEvaluator::evaluateOrder 6 步验证管道移植 (config 驱动):
      基础字段 → 大盘回撤保护(引擎回测不注入→跳过) → 信号强度 → 三级熔断停止(不注入→跳过)
      → 涨跌停/滑点(回测 referencePrice 不注入→跳过) → 买入侧(止损/止盈/账户回撤/三级浮动熔断/
      单票集中度/总敞口) → 卖出侧(可卖持仓/数量上限) → 单笔金额/滑点/日成交额
    引擎 88575b08 构建值 (STRATEGY['risk']) 全 0 或 99 → 回测中等于橡皮图章;
    修改数值即启用对应检查 (调试参数只需改 STRATEGY['risk'])
    """

    @staticmethod
    def evaluate_order(ri):
        """
        ri: dict(symbol, price, quantity, is_buy, signal_strength, total_asset,
                 market_value, symbol_market_value, symbol_return_pct,
                 closeable_quantity, current_drawdown_pct)
        返回 (approved: bool, code: str, description: str)
        """
        r = STRATEGY['risk']
        # ── 步骤 1: 基础字段 (回测恒过: 策略已绑定/激活/持仓快照就绪/交易时段) ──
        if not ri.get('symbol') or not np.isfinite(ri['price']) or ri['price'] < 0:
            return False, 'PriceInvalid', '价格无效'
        # ── 信号强度 (引擎阈值 0.1; 策略 min score 0.3 → 恒过) ──
        if ri.get('signal_strength', 0.0) < r['signal_strength_min']:
            return False, 'SignalStrengthTooWeak', '信号强度过弱 (阈值: %.2f)' % r['signal_strength_min']
        if ri['is_buy']:
            # ── 止损检查: 浮亏达线拒加仓 (引擎 88575b08: 0=禁用, 由规则模板接管) ──
            if r['stop_loss_pct'] > 0.0 and ri.get('symbol_return_pct', 0.0) < 0.0 \
                    and abs(ri['symbol_return_pct']) >= r['stop_loss_pct']:
                return False, 'StopLossTriggered', '止损触发: 当前回撤已超过止损线'
            # ── 止盈检查 ──
            if r['take_profit_pct'] > 0.0 and ri.get('symbol_return_pct', 0.0) > 0.0 \
                    and ri['symbol_return_pct'] >= r['take_profit_pct']:
                return False, 'TakeProfitTriggered', '止盈触发: 当前盈利已达到止盈线'
            total_asset = ri.get('total_asset', 0.0)
            proposed = ri['price'] * max(1, ri['quantity'])
            dd = ri.get('current_drawdown_pct', 0.0)
            # ── 账户最大回撤 (引擎 88575b08: 99 → 永不触发) ──
            if r['max_drawdown_limit_pct'] > 0.0 and dd < 0.0 \
                    and abs(dd) >= r['max_drawdown_limit_pct']:
                return False, 'MaxDrawdownExceeded', '最大回撤超限'
            # ── 三级浮动熔断: breaker3 > breaker2 > breaker1 ──
            if r['breaker_level3_pct'] > 0.0 and dd < 0.0 and abs(dd) >= r['breaker_level3_pct']:
                return False, 'BreakerLevel3', '三级熔断触发 (Level 3)'
            if r['breaker_level1_pct'] > 0.0 and dd < 0.0 and abs(dd) >= r['breaker_level1_pct']:
                return False, 'BreakerLevel1', '一级熔断触发 (Level 1)'
            if r['breaker_level2_pct'] > 0.0 and dd < 0.0 and abs(dd) >= r['breaker_level2_pct']:
                return False, 'BreakerLevel2', '二级熔断触发 (Level 2)'
            # ── 单票集中度 ──
            if total_asset > 0.0 and r['max_position_pct'] > 0.0:
                combined = ri.get('symbol_market_value', 0.0) + proposed
                if (combined / total_asset) * 100.0 > r['max_position_pct']:
                    return False, 'PositionConcentrationExceeded', '单标的持仓集中度超限'
            # ── 总敞口 ──
            if total_asset > 0.0 and r['max_total_exposure_pct'] > 0.0:
                new_pct = (ri.get('market_value', 0.0) + proposed) / total_asset * 100.0
                if new_pct > r['max_total_exposure_pct']:
                    return False, 'TotalExposureExceeded', '总敞口超限'
        else:
            # ── 卖出侧: 可卖持仓/数量上限 (生成时已按持仓截断 → 恒过) ──
            if ri.get('closeable_quantity', 0) <= 0:
                return False, 'NoSellablePosition', '无可卖持仓'
            if ri['quantity'] > ri.get('closeable_quantity', 0):
                return False, 'SellQuantityExceedsHolding', '卖出数量超过可卖持仓'
        # ── 单笔金额上限 (0=禁用) ──
        notional = proposed if ri['is_buy'] else ri['price'] * max(1, ri['quantity'])
        if r['order_size_limit_wan'] > 0.0 and notional > r['order_size_limit_wan'] * 10000.0:
            return False, 'OrderSizeExceeded', '单笔订单金额超限'
        return True, 'None', '基础风控校验通过'


class EventRiskController:
    """
    引擎 EventRiskSubscriber 移植 (结构层, 默认关闭 = 忠实回测)
    引擎数据源: live EventBus news.* 事件 (Python 新闻管线) — 聚宽回测无新闻数据流。
    接入数据源后在 handle_data 中调用 apply_event(...) 并打开 STRATEGY['event_risk']['enabled']。
    规则语义 (引擎 EventRiskSubscriber::onFinancialEvent + applyEventTags 原样):
      stock/liquidate → 立即清仓名单 (引擎由 live 盘中巡检消费)
      stock/reduce_position → 单股限仓 2% (引擎写入 RiskManager maxPositionPercent)
      sector/reduce_exposure|reduce_position → 行业限仓 30%/50%
      market/reduce_exposure(severity>=0.5) → 总敞口<=50%; market/alert → <=80%
      立案调查 → 封禁开仓 + 止损<=5% + 限仓<=2%; ST警示 → 封禁 + 限仓<=2%
      政策负面 (sentiment<-0.7 且 confidence>0.8) → 总敞口<=80%
    T+1 解禁: 每交易日评估入口 clear_blocked()
    """

    def __init__(self, risk_cfg_dict):
        self._risk = risk_cfg_dict          # 直接修改 STRATEGY['risk'] (引擎写 RiskManager 单例同效)
        self._blocked = set()               # 封禁开仓名单
        self._liquidated = set()            # 立即清仓名单
        self._sector_limits = {}            # 行业 → 限仓 %
        self._per_symbol_override = {}      # (原止损, 原限仓) 备份 (预留)

    def apply_event(self, level, action, symbols=None, sector_codes='', product_id='',
                    severity=0.0, tags=None, event_type='', sentiment=0.0, confidence=0.0):
        """事件入口 (引擎 onFinancialEvent 语义; tags: {'立案调查': 'true', ...})"""
        if level == 'stock' and symbols:
            for sym in symbols:
                if action == 'liquidate':
                    self._liquidated.add(sym)
                    log.warn('[事件风控] 个股利空清仓: %s severity=%s' % (sym, severity))
                elif action == 'reduce_position':
                    self._tighten_position_limit(sym, 2.0)
        elif level == 'sector':
            codes = sector_codes or product_id
            if codes:
                cap = 30.0 if action == 'reduce_exposure' else (50.0 if action == 'reduce_position' else None)
                if cap is not None:
                    for code in str(codes).split(','):
                        if code and (code not in self._sector_limits or self._sector_limits[code] > cap):
                            self._sector_limits[code] = cap
                            log.warn('[事件风控] 行业限仓: %s -> %s%%' % (code, cap))
        elif level == 'market':
            if action == 'reduce_exposure' and severity >= 0.5:
                self._risk['max_total_exposure_pct'] = min(self._risk['max_total_exposure_pct'], 50.0)
                log.warn('[事件风控] 系统性风险: 总敞口降至50%% severity=%s' % severity)
            elif action == 'alert':
                self._risk['max_total_exposure_pct'] = min(self._risk['max_total_exposure_pct'], 80.0)
                log.info('[事件风控] 市场预警: 总敞口降至80%')
        # ── 事件标签规则 (applyEventTags) ──
        tags = tags or {}
        if symbols and event_type:
            for sym in symbols:
                if tags.get('立案调查') == 'true':
                    self._blocked.add(sym)
                    if sym not in self._per_symbol_override:
                        self._per_symbol_override[sym] = (self._risk['stop_loss_pct'],
                                                          self._risk['max_position_pct'])
                    self._risk['stop_loss_pct'] = min(self._risk['stop_loss_pct'], 5.0)
                    self._risk['max_position_pct'] = min(self._risk['max_position_pct'], 2.0)
                if tags.get('ST警示') == 'true':
                    self._blocked.add(sym)
                    if sym not in self._per_symbol_override:
                        self._per_symbol_override[sym] = (self._risk['stop_loss_pct'],
                                                          self._risk['max_position_pct'])
                    self._risk['max_position_pct'] = min(self._risk['max_position_pct'], 2.0)
        if sentiment < -0.7 and confidence > 0.8 and event_type == 'news.policy':
            self._risk['max_total_exposure_pct'] = min(self._risk['max_total_exposure_pct'], 80.0)

    def _tighten_position_limit(self, symbol, max_pct):
        old = self._risk['max_position_pct']
        self._risk['max_position_pct'] = min(self._risk['max_position_pct'], max_pct)
        if self._risk['max_position_pct'] < old:
            log.warn('[事件风控] 单股限仓: %s %s%% -> %s%%' % (symbol, old, self._risk['max_position_pct']))

    def clear_blocked(self):
        """T+1 解禁: 每交易日评估入口调用 (引擎 evaluateEndOfDay 入口清空)"""
        self._blocked.clear()
        self._liquidated.clear()
        self._sector_limits.clear()

    def blocked(self):
        return self._blocked

    def liquidated(self):
        return self._liquidated

    def sector_limits(self):
        return self._sector_limits


def kelly_report(journal):
    """
    凯利公式仓位建议 (引擎 backtest() 回测后统计, 仅信息输出不参与仓位):
      f* = p - (1-p)/b, b = avgWin/avgLoss; 胜率=盈利卖笔数/总卖笔数
    引擎口径: totalFills 仅计卖出笔, totalProfit/totalLoss 按卖出盈亏累计
    返回 None 表示数据不足
    """
    sells = [t for t in journal if not t['is_buy']]
    if not sells:
        return None
    wins = [t for t in sells if t['pnl'] > 0]
    losses = [t for t in sells if t['pnl'] <= 0]
    if not wins or not losses:
        return None
    total_profit = sum(t['pnl'] for t in wins)
    total_loss = -sum(t['pnl'] for t in losses)
    if total_loss <= 0.0:
        return None
    win_rate = float(len(wins)) / float(len(sells))
    odds = (total_profit / float(len(wins))) / (total_loss / float(len(losses)))
    full_kelly = win_rate - (1.0 - win_rate) / odds
    return dict(win_rate=win_rate, odds=odds, full_kelly=full_kelly, half_kelly=full_kelly * 0.5)


# ═══════════════════════════════════════════════════════════════════════
# ⑧ 执行层: 清仓 / 订单风控输入构建 / 成交记录
# ═══════════════════════════════════════════════════════════════════════
def liquidate_all(context, reason, journal):
    """强制清仓全部持仓 (择时空仓/熔断/事件清仓), 记录成交日志"""
    n = 0
    for code, p in list(context.portfolio.positions.items()):
        if p.closeable_amount > 0:
            order_target_value(code, 0)
            _record_sell(journal, code, p.closeable_amount, p.avg_cost, context.current_dt.date())
            n += 1
    if n:
        log.info('[%s] 强制清仓 %d 只' % (reason, n))
    return n


def _record_sell(journal, code, qty, avg_cost, day):
    """卖出成交记录 (价格以当日收盘近似; 引擎按真实成交, 凯利为统计口径近似)"""
    if qty <= 0:
        return
    price = last_close_cache.get(code, 0.0)
    if not np.isfinite(price) or price <= 0:
        return
    journal.append(dict(day=day, code=code, is_buy=False, qty=qty,
                        price=price, pnl=(price - avg_cost) * qty))


def build_risk_input(context, code, price, qty, score, is_buy, symbol_return_pct, peak_equity):
    """构建 RiskEvaluator 输入 (引擎 Facade backtest L2236-2266 同口径)"""
    total_asset = context.portfolio.total_value
    mv = total_asset - context.portfolio.cash
    p = context.portfolio.positions.get(code)
    held = p.closeable_amount if p is not None else 0
    dd_pct = (peak_equity - total_asset) / peak_equity * 100.0 if peak_equity > 0 else 0.0
    return dict(symbol=code, price=price, quantity=qty, is_buy=is_buy,
                signal_strength=score, total_asset=total_asset,
                market_value=mv, symbol_market_value=price * held,
                symbol_return_pct=symbol_return_pct, closeable_quantity=held,
                current_drawdown_pct=-dd_pct)


# 当日收盘价缓存 (成交记录用, 每次 handle_data 数据面板就绪后更新)
last_close_cache = {}


# ═══════════════════════════════════════════════════════════════════════
# ⑨ 主循环 (引擎 EOD 管道语义: 快照→闸门→熔断→因子→信号→出场→风控→下单)
# ═══════════════════════════════════════════════════════════════════════
def initialize(context):
    # 基准与真实价格 (use_real_price: 拆分除权还原, avg_cost 真实)
    set_benchmark('000300.XSHG')
    set_option('use_real_price', True)
    set_option('avoid_future_data', True)
    # 交易成本: 佣金双边万2.5 (最低 5 元), 卖出印花税 0.1% (引擎实盘成本以其通道为准, 见文档)
    set_order_cost(OrderCost(open_tax=0.0, close_tax=0.001, open_commission=0.00025,
                             close_commission=0.00025, min_commission=5.0), type='stock')
    set_slippage(PriceRelatedSlippage(0.001))   # 双边约 0.1% 滑点
    log.set_level('order', 'error')             # 订单逐笔成交日志静默, 保留策略日志

    g.universe = build_universe(context)
    g.turnover_cache = {}    # code → deque(最近 60 个有效日换手率)
    g.holding_highs = {}     # code → 入场以来最高收盘 (结构止盈 回撤6% 用)
    g.day_count = 0
    g.journal = []           # 成交日志 (凯利统计)
    g.breaker = TimedCircuitBreaker(STRATEGY['circuit_breaker'])
    g.breaker.update_eod(context.portfolio.total_value)   # 峰值初始化 = 初始资金
    g.event_risk = EventRiskController(STRATEGY['risk'])
    g.peak_equity = context.portfolio.total_value
    log.info('多因子策略 88575b08 初始化: 股票池 %d 只, Top-N %d, 最大持仓 %d, 模式=%s' %
             (len(g.universe), STRATEGY['top_n'], STRATEGY['max_positions'],
              STRATEGY['universe_mode']))


def handle_data(context, data):
    # 新版聚宽 (jqboson) 签名: handle_data(context, data), data 为当日 bar 快照。
    # 本策略面板数据统一走 get_price (与引擎 EOD 数据链口径一致), data 不使用
    # 数据锚点 (方案A, 用户裁定 2026-08-15): 新版平台日频回测 handle_data 固定
    # 09:30 运行一次, avoid_future_data 禁止取当日 close → 决策数据用上一交易日
    # 收盘面板, 订单在当日收盘成交 (平台日频撮合)。与引擎差异 = 信号识别/出场/
    # 冻结/择时整体延迟 1 个交易日 (买卖均延迟, 无系统偏向), 见差异清单 #25
    g.day_count += 1
    day = context.current_dt.date()                     # 交易日 T (成交/记录日期)
    end = context.previous_date.strftime('%Y-%m-%d')    # 决策数据锚点 = 上一交易日

    # ---------- 1. 数据面板 ----------
    panels = fetch_panels(g.universe, end)
    if panels is None:
        return
    close = panels['close']
    high = panels['high']
    pre_close, idx_close = panels['pre_close'], panels['idx_close']
    global last_close_cache
    last_close_cache = {}
    for c, v in close.iloc[-1].items():   # 标量保护: 布局异常时 v 可能非标量 → 跳过不崩溃
        if not np.isscalar(v):
            continue
        try:
            fv = float(v)
        except (TypeError, ValueError):
            continue
        if np.isfinite(fv):
            last_close_cache[c] = fv

    # ---------- 2. 换手率缓存 (锚点 = 上一交易日) ----------
    update_turnover_cache(end)

    # ---------- 3. 市场快照 / 市场规则闸门 / 放行模式 / 择时闸门 ----------
    snap = compute_market_snapshot(close)
    if snap is None:
        log.warn('[数据] 市场快照数据不足 (等权指数有效样本<2), 当日跳过')
        return
    advance_ratio = compute_advance_ratio(close)
    timing = compute_timing_gate(idx_close, advance_ratio)
    frozen, freeze_reason = market_freeze(snap)
    allow_mode = market_allow_mode(snap)
    allow_new = (not frozen) and timing['allow_new']
    log.info('[评估] T=%s 锚点=%s breadth60=%.3f breadth20=%.3f dd=%.3f volShock=%.3f regime=%s | 择时=%s exp=%.1f | %s%s' %
             (day.strftime('%Y-%m-%d'), end, snap['breadth60'], snap['breadth20'], snap['drawdown'],
              snap['vol_shock'], snap['regime'], timing['state'], timing['exposure'],
              ('冻结: %s' % freeze_reason) if frozen else '放行',
              (' [%s]' % allow_mode) if allow_mode else ''))

    # ---------- 4. 事件风控: T+1 解禁 + 事件清仓名单 ----------
    event_enabled = STRATEGY['event_risk']['enabled']
    if event_enabled:
        g.event_risk.clear_blocked()
        event_liquidate = set(g.event_risk.liquidated())
        if event_liquidate:
            n = 0
            for code in list(event_liquidate):
                p = context.portfolio.positions.get(code)
                if p is not None and p.closeable_amount > 0:
                    order_target_value(code, 0)
                    _record_sell(g.journal, code, p.closeable_amount, p.avg_cost, day)
                    g.holding_highs.pop(code, None)
                    n += 1
            if n:
                log.info('[事件风控] 事件清仓 %d 只' % n)

    # ---------- 5. 熔断器 (引擎 backtest: isHalted → 全清 + 跳过交易) ----------
    equity_now = context.portfolio.total_value
    if STRATEGY['circuit_breaker']['enabled'] and g.breaker.is_halted():
        liquidate_all(context, '熔断', g.journal)
        g.holding_highs.clear()
        # 引擎缺陷: 熔断分支不更新 → 倒计时永不递减; 本转换按设计意图每日递减
        g.breaker.update_eod(context.portfolio.total_value)
        log.info('[熔断] 熔断中 (剩余 %d 交易日), 当日仅清仓不交易' % g.breaker.remaining_halt_days())
        return

    # ---------- 6. 择时空仓状态 → 强制清仓, 当日结束 ----------
    if timing['force_liquidate']:
        liquidate_all(context, '择时', g.journal)
        g.holding_highs.clear()
        g.breaker.update_eod(context.portfolio.total_value)
        return

    # ---------- 7. 因子 → 截面 z 合成 → 六阶段信号 ----------
    holdings = {code: p for code, p in context.portfolio.positions.items() if p.closeable_amount > 0}
    blocked = set(g.event_risk.blocked()) if event_enabled else set()
    factor_scores = compute_factor_scores(close, idx_close)
    composite = build_composite(factor_scores)
    sells, buys = evaluate_multi_factor_signals(composite, holdings, allow_new, blocked)

    equity = context.portfolio.total_value
    last_close = close.iloc[-1]
    pre_today = pre_close.iloc[-1]
    sold_codes = set()
    full_closes = set()

    # ---------- 8. 策略卖出 (阶段3: composite<sellThreshold 或 排名>40 → 全清) ----------
    for code, score, reason in sells:
        if code not in holdings:
            continue   # 无持仓的卖出信号 → 引擎 sizer 丢弃
        pc = last_close.get(code)
        if not np.isfinite(pc) or pc <= 0:
            continue   # 停牌/无价
        pre = pre_today.get(code)
        if np.isfinite(pre) and pc <= pre * STRATEGY['limit_down_ratio']:
            continue   # 跌停: 策略卖单不卖 (管道过滤)
        p = holdings[code]
        order_target_value(code, 0)
        full_closes.add(code)
        _record_sell(g.journal, code, p.closeable_amount, p.avg_cost, day)
        log.info('[卖出] %s %s → 全清 score=%.3f' %
                 (code, ('评分跌破阈值' if reason == 'score' else '排名跌出前%d' % (STRATEGY['max_positions'] * STRATEGY['sell_rank_multiplier'])), score))
        sold_codes.add(code)

    # ---------- 9. 结构止盈出场 (持仓逐只, 5 条件第一命中; 无涨跌停过滤) ----------
    ma20 = close.rolling(20, min_periods=20).mean()
    ma60 = close.rolling(60, min_periods=60).mean()
    high60 = high.iloc[-61:-1].max(axis=0)          # 60 日高, 不含今日 (recentHigh(60,1))
    m20_now, m20_5ago = ma20.iloc[-1], ma20.iloc[-6]
    m60_now, m60_5ago = ma60.iloc[-1], ma60.iloc[-6]

    for code, p in list(holdings.items()):
        if code in sold_codes:
            continue   # 订单去重 (code,side) 先到先得: 策略卖单优先于规则出场 (OrderGenerator)
        pc = last_close.get(code)
        if not np.isfinite(pc) or pc <= 0:
            continue
        g.holding_highs[code] = max(g.holding_highs.get(code, pc), pc)   # 持仓高点更新 (回撤6% 用, 自入场起)
        avg = p.avg_cost
        if avg <= 0:
            continue
        pnl = (pc - avg) / avg * 100.0
        m20v = m20_now.get(code)
        if not np.isfinite(m20v) or m20v <= 0:
            continue
        d = dict(close=pc, pnl=pnl, ma20=m20v,
                 high60=high60.get(code, np.nan),
                 holding_high=g.holding_highs.get(code, 0.0),
                 health=trend_health_score(pc, m20v, m20_5ago.get(code, np.nan),
                                           m60_now.get(code, np.nan), m60_5ago.get(code, np.nan)))
        action, reason = position_exit_rule(d)
        if action == 'EXIT':
            order_target_value(code, 0)
            full_closes.add(code)
            _record_sell(g.journal, code, p.closeable_amount, p.avg_cost, day)
            log.info('[规则出场] %s %s pnl=%.1f%%' % (code, reason, pnl))
        elif action == 'REDUCE':
            sell_qty = int(round(p.closeable_amount * STRATEGY['exit_rules']['reduce_ratio'] / 100.0)) * 100
            if 0 < sell_qty < p.closeable_amount:
                order_target(code, p.closeable_amount - sell_qty)
                _record_sell(g.journal, code, sell_qty, p.avg_cost, day)
                log.info('[规则出场] %s %s → 减仓 %d 股 (剩 %d)' %
                         (code, reason, sell_qty, p.closeable_amount - sell_qty))

    # ---------- 10. 买入 (阶段6: 新开仓限 20-持仓数 / 持仓更新目标权重; 等权 clamp, 无等比压缩) ----------
    # 熔断器单日亏损检查 (设计意图; 引擎 checkIntraday 零调用者 → 默认关闭)
    if STRATEGY['circuit_breaker']['enable_daily_loss_check']:
        g.breaker.check_intraday(equity_now)
    exposure_scale = g.breaker.target_exposure()   # 引擎 targetExposure 零消费 → 恒 1.0 (除非开关打开)
    live_buys = []
    risk_rejected = 0
    for code, score, w, is_held in buys:
        if code in sold_codes or code in full_closes:
            continue   # 当日已卖出的持仓不再买回 (策略卖单优先)
        pc = last_close.get(code)
        if not np.isfinite(pc) or pc <= 0:
            continue
        pre = pre_today.get(code)
        if np.isfinite(pre) and pc >= pre * STRATEGY['limit_up_ratio']:
            continue   # 涨停: 不买 (管道过滤)
        p = holdings.get(code)
        cur_value = p.closeable_amount * pc if p is not None else 0.0
        target_value = w * equity * exposure_scale
        if target_value < pc * 100:
            if not is_held:
                continue   # 新开仓不足 1 手 (引擎 backtest lots==0 → 跳过)
            if target_value < cur_value:
                order_target_value(code, 0)   # 持仓目标减至不足 1 手 → 全清 (引擎 targetQty=0 lots)
                full_closes.add(code)
                _record_sell(g.journal, code, p.closeable_amount, p.avg_cost, day)
                log.info('[卖出] %s 目标权重不足1手 → 全清 score=%.3f' % (code, score))
                continue
        if target_value <= cur_value:
            continue   # 引擎 buyDelta: targetQty<=currentQty → 无订单 (减仓目标等于/高于现仓不下单)
        # 每单风控审核 (引擎 backtest 每笔成交前 evaluateOrder; 仅买入侧)
        ret_pct = (pc - p.avg_cost) / p.avg_cost * 100.0 if p is not None and p.avg_cost > 0 else 0.0
        qty = int(target_value / pc / 100.0) * 100
        ri = build_risk_input(context, code, pc, qty, score, True, ret_pct, g.peak_equity)
        approved, rc, desc = RiskEvaluator.evaluate_order(ri)
        if not approved:
            risk_rejected += 1
            log.info('[风控] %s 拒绝买入 (%s): %s' % (code, rc, desc))
            continue
        order_target_value(code, target_value)
        if not is_held:
            g.holding_highs.setdefault(code, pc)
        live_buys.append((code, score, w, target_value))
        log.info('[买入] %s score=%.3f w=%.4f 目标 %.0f 元%s' %
                 (code, score, w, target_value, '' if is_held else ' (新开仓)'))
    if risk_rejected:
        log.info('[风控] 当日风控拒单 %d 笔' % risk_rejected)

    # ---------- 11. 收盘清理: 全清持仓的持仓高点记录 ----------
    for code in full_closes:
        g.holding_highs.pop(code, None)

    # ---------- 12. 熔断器日终更新 (引擎 backtest 正常交易日调用) ----------
    g.breaker.update_eod(context.portfolio.total_value)
    if context.portfolio.total_value > g.peak_equity:
        g.peak_equity = context.portfolio.total_value

    # ---------- 13. 凯利仓位建议 (引擎回测后统计; 卖出日输出滚动值) ----------
    kr = kelly_report(g.journal)
    if kr is not None and len(g.journal) > 0 and not g.journal[-1]['is_buy']:
        log.info('[凯利] 胜率=%.1f%% 赔率=%.2f 全凯=%.2f%% 半凯(建议)=%.2f%%' %
                 (kr['win_rate'] * 100.0, kr['odds'], kr['full_kelly'] * 100.0, kr['half_kelly'] * 100.0))

    # ---------- 14. 组合摘要 (每 20 日或有交易时输出) ----------
    if g.day_count % 20 == 0 or live_buys or sold_codes or full_closes:
        log.info('[组合] 持仓 %d 只 | 总资产 %.0f | 现金 %.0f | 评分 %d 只' %
                 (len(context.portfolio.positions), equity, context.portfolio.cash, len(composite)))
