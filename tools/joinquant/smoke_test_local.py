# -*- coding: utf-8 -*-
"""
本地冒烟测试 — trend_following_88575b08.py (多因子策略, 无需聚宽平台)
====================================================================
方法: 桩 jqdata 模块 + 桩平台内置对象 (log/g/order_*/set_*),
      importlib 从文件路径加载策略模块, 函数级 + 全流程验证。
运行: python smoke_test_local.py  (需要 numpy + pandas)
用途: 调整 STRATEGY 参数 / 换因子 / 改出场规则后回归验证。

覆盖语义 (与引擎 MultiFactorStrategy 六阶段 / 结构止盈 / DB 市场模板对齐):
  - build_composite: 截面 z (总体方差口径 sumSq/n-mean², clamp[-3,3],
    std=0→z=0, 缺任一因子剔除, 权重归一化)
  - evaluate_multi_factor_signals: 持仓 composite<0.2 卖出 / 排名>40 卖出 /
    持仓缺值保留 / 仅 composite>0 且 Top-50 / 等权 1/n clamp[0.01,0.1] /
    maxNewBuys 预算耗尽 break (含持仓更新截断) / 冻结日禁新开仓 / 封禁名单跳过
  - trend_health_score 四项 +25 / 结构止盈 5 条件有序第一命中
    (0.85/1.1/6%/50/0.98, 减仓比例 0.3)
  - market_freeze 仅 p99+p97 (p95 与管道宽度冻结已按用户裁定移除) /
    market_allow_mode p90/p84 放行模式 (仅日志)
  - compute_market_snapshot: breadth/regime/volShock + 新字段
    indexAboveMa120 (等权指数同源口径) / trendStrength=breadth60
  - 全流程确定性几何: 首日换手率截面分化 → 目标股 z>0 买入 (等权 0.1);
    次日全市场换手率同值 → 截面 std=0 → z=0 → 持仓评分跌破阈值全清
  - 数据层: 预热 3 次查询 (close160/high61/指数160), 之后每日 3 次 count=1
    单行查询 (低/open/volume 查询已随 10 条旧出场规则删除)
"""
import os
import sys
import types
import importlib.util
from collections import deque
from types import SimpleNamespace

import numpy as np
import pandas as pd

HERE = os.path.dirname(os.path.abspath(__file__))
MOD_PATH = os.path.join(HERE, 'trend_following_88575b08.py')

# ─────────────────────────────────────────────────────────────────────
# 桩 jqdata 模块
# ─────────────────────────────────────────────────────────────────────
_jqdata = types.ModuleType('jqdata')

_sec_extra = {}   # 测试可注入: code → dict 覆盖


def _stub_get_all_securities(types_, date=None):
    rows = {}
    for i in range(60):
        code = '%06d.XSHE' % (1000 + i)
        rows[code] = dict(display_name='股票%d' % i, name='',
                          start_date=pd.Timestamp('2010-01-01'), end_date=pd.NaT)
    # 边界样例: ST / 退市 / 北交所 / 次新
    rows['000060.XSHE'] = dict(display_name='ST股票', name='', start_date=pd.Timestamp('2010-01-01'), end_date=pd.NaT)
    rows['000061.XSHE'] = dict(display_name='某退', name='', start_date=pd.Timestamp('2010-01-01'), end_date=pd.NaT)
    rows['830001.BJ'] = dict(display_name='北交所股', name='', start_date=pd.Timestamp('2010-01-01'), end_date=pd.NaT)
    rows['000062.XSHE'] = dict(display_name='次新股', name='', start_date=pd.Timestamp('2026-01-01'), end_date=pd.NaT)
    for code, extra in _sec_extra.items():
        rows[code] = extra
    return pd.DataFrame.from_dict(rows, orient='index')


def _stub_get_index_stocks(index, date=None):
    return ['%06d.XSHE' % (1000 + i) for i in range(10)]


_turnover_override = {}   # code → 当日换手率 (全流程测试注入)
_last_query_codes = []    # query(valuation).filter(in_) 捕获的股票列表


class _StubQuery:
    """桩 query 对象: filter 无操作 (股票列表捕获在 _stub_code_in)"""

    def __init__(self, fields):
        del fields   # 桩无需字段, 仅保持与平台 query(*fields) 签名一致

    def filter(self, cond):
        del cond
        return self


def _stub_query(*fields):
    return _StubQuery(fields)


def _stub_code_in(codes):
    _last_query_codes[:] = list(codes)
    return None


_valuation_stub = SimpleNamespace(code=SimpleNamespace(in_=_stub_code_in),
                                  turnover_ratio='turnover_ratio')


def _stub_get_fundamentals(q, date=None):
    rows = {}
    for code in _last_query_codes:
        tr = _turnover_override.get(code, 1.5)
        rows[code] = dict(code=code, turnover_ratio=tr)
    return pd.DataFrame.from_dict(rows, orient='index')


_jqdata.get_all_securities = _stub_get_all_securities
_jqdata.get_index_stocks = _stub_get_index_stocks
_jqdata.get_fundamentals = _stub_get_fundamentals
_jqdata.query = _stub_query
_jqdata.valuation = _valuation_stub
# 与平台 jqdata 模块一致: 显式 __all__ 支持 from jqdata import *
_jqdata.__all__ = ['get_all_securities', 'get_index_stocks', 'get_fundamentals',
                   'query', 'valuation']
sys.modules['jqdata'] = _jqdata

# ─────────────────────────────────────────────────────────────────────
# 加载策略模块 + 桩平台对象
# ─────────────────────────────────────────────────────────────────────
spec = importlib.util.spec_from_file_location('tf88575b08', MOD_PATH)
mod = importlib.util.module_from_spec(spec)
sys.modules['tf88575b08'] = mod
spec.loader.exec_module(mod)

LOOKBACK = mod.LOOKBACK

LOG_LINES = []
mod.log = SimpleNamespace(
    info=lambda *a, **k: LOG_LINES.append('[I] ' + ' '.join(str(x) for x in a)),
    warn=lambda *a, **k: LOG_LINES.append('[W] ' + ' '.join(str(x) for x in a)),
    set_level=lambda *a, **k: None,
)
mod.set_benchmark = lambda *a, **k: None
mod.set_option = lambda *a, **k: None
mod.set_order_cost = lambda *a, **k: None
mod.set_slippage = lambda *a, **k: None

g = SimpleNamespace()
mod.g = g

ORDER_LOG = []   # (op, code, value/qty)


def _stub_order_target_value(code, value):
    ORDER_LOG.append(('target_value', code, value))


def _stub_order_target(code, qty):
    ORDER_LOG.append(('target', code, qty))


mod.order_target_value = _stub_order_target_value
mod.order_target = _stub_order_target

RESULTS = []


def check(name, cond, detail=''):
    RESULTS.append((name, bool(cond), detail))
    if not cond:
        print('FAIL: %s %s' % (name, detail))


# ─────────────────────────────────────────────────────────────────────
# 面板/夹具生成
# ─────────────────────────────────────────────────────────────────────
def panel_close(codes, rows, rng=None, base=10.0, vol=0.02):
    """随机游走收盘面板: rows×len(codes)"""
    rng = rng or np.random.default_rng(42)
    steps = rng.normal(0.0, vol, size=(rows, len(codes)))
    prices = base * np.cumprod(1.0 + steps, axis=0)
    idx = pd.date_range('2025-01-01', periods=rows, freq='B')
    return pd.DataFrame(prices, index=idx, columns=list(codes))


def exit_d(**kw):
    """出场规则输入夹具: 默认全部 5 条件不命中"""
    d = dict(close=10.0, pnl=0.0, ma20=10.0, high60=10.0, holding_high=10.0, health=100.0)
    d.update(kw)
    return d


# ═════════════════════════════════════════════════════════════════════
# 1. 市场快照 (等权指数同源口径)
# ═════════════════════════════════════════════════════════════════════
def test_snapshot():
    rng = np.random.default_rng(7)
    close = panel_close(['%06d.XSHE' % (1000 + i) for i in range(30)], 160, rng)
    snap = mod.compute_market_snapshot(close)
    check('snapshot 非空', snap is not None)
    if snap is not None:
        check('breadth 范围', 0.0 <= snap['breadth60'] <= 1.0 and 0.0 <= snap['breadth20'] <= 1.0)
        check('regime 枚举', snap['regime'] in ('bull', 'bear', 'sideways'), snap['regime'])
        check('drawdown>=0', snap['drawdown'] >= 0.0)
        check('volShock 范围 0-1', 0.0 <= snap['vol_shock'] <= 1.0, snap['vol_shock'])
        check('trendStrength=breadth60 (引擎别名口径)',
              abs(snap['trend_strength'] - snap['breadth60']) < 1e-12)
        check('indexAboveMa120>0 (等权指数同源口径)', snap['index_above_ma120'] > 0.0,
              snap['index_above_ma120'])
    # 上行市场 → bull
    up = panel_close(['%06d.XSHE' % (1000 + i) for i in range(30)], 160,
                     rng=np.random.default_rng(1), vol=0.0)
    up = up.mul(pd.Series(np.linspace(1.0, 2.0, 160), index=up.index), axis=0)
    snap2 = mod.compute_market_snapshot(up)
    check('上行市场 regime=bull', snap2['regime'] == 'bull', snap2['regime'])
    # 下行市场 → bear
    down = panel_close(['%06d.XSHE' % (1000 + i) for i in range(30)], 160,
                       rng=np.random.default_rng(1), vol=0.0)
    down = down.mul(pd.Series(np.linspace(2.0, 1.0, 160), index=down.index), axis=0)
    snap3 = mod.compute_market_snapshot(down)
    check('下行市场 regime=bear', snap3['regime'] == 'bear', snap3['regime'])
    # 等权指数有效样本 <2 → None (handle_data 层打 warn)
    tiny = pd.DataFrame({'000001.XSHE': [10.0, 10.1]},
                        index=pd.date_range('2025-01-01', periods=2, freq='B'))
    check('等权指数样本不足 → None', mod.compute_market_snapshot(tiny) is None)


# 2. 上涨家数占比
def test_advance_ratio():
    rng = np.random.default_rng(3)
    close = panel_close(['%06d.XSHE' % (1000 + i) for i in range(30)], 160, rng)
    ratio = mod.compute_advance_ratio(close)
    check('ratio 范围', 0.0 <= ratio <= 1.0)


# 3. 市场冻结闸门 (仅剩 p99 熊市体制 + p97 准熊市退潮; p95 与管道宽度冻结已按用户裁定移除 2026-08-15)
def test_market_freeze():
    f = mod.market_freeze
    check('p99 熊市体制冻结', f(dict(regime='bear', breadth60=0.3, breadth20=0.3, drawdown=0.05)) == (True, '熊市体制冻结(p99)'))
    check('p97 准熊市退潮冻结', f(dict(regime='sideways', breadth60=0.25, breadth20=0.6, drawdown=0.15)) == (True, '准熊市退潮冻结(p97)'))
    check('p97 回撤<0.1 不冻结', f(dict(regime='sideways', breadth60=0.25, drawdown=0.05)) == (False, ''))
    check('p95 高波动冻结已移除: 高 volShock 不再冻结', f(dict(regime='sideways', breadth60=0.5, drawdown=0.02, vol_shock=0.8)) == (False, ''))
    check('宽度冻结(管道)已移除', f(dict(regime='sideways', breadth60=0.3, drawdown=0.02)) == (False, ''))
    check('放行', f(dict(regime='sideways', breadth60=0.5, drawdown=0.02)) == (False, ''))
    check('bull 放行(橡皮图章)', f(dict(regime='bull', breadth60=0.7, drawdown=0.01)) == (False, ''))


# 4. 市场放行模式 (p90 牛市趋势放行 / p84 震荡市精选放行; 仅记日志不阻断)
def test_market_allow_mode():
    am = mod.market_allow_mode
    base = dict(breadth60=0.5, breadth20=0.5, drawdown=0.02, vol_shock=0.4,
                index_above_ma120=1.0, trend_strength=0.5, regime='sideways')
    check('p90 牛市趋势放行 → active',
          am(dict(base, regime='bull', breadth60=0.7, index_above_ma120=1.05, trend_strength=0.7)) == 'active')
    check('p90 breadth<0.58 不放行',
          am(dict(base, regime='bull', breadth60=0.57, index_above_ma120=1.05, trend_strength=0.7)) == '')
    check('p90 指数低于MA120 不放行',
          am(dict(base, regime='bull', breadth60=0.7, index_above_ma120=1.005, trend_strength=0.7)) == '')
    check('p90 趋势强度<0.62 不放行',
          am(dict(base, regime='bull', breadth60=0.7, index_above_ma120=1.05, trend_strength=0.61)) == '')
    check('p84 震荡市精选 → selective_active', am(dict(base)) == 'selective_active')
    check('p84 breadth 上下界含',
          am(dict(base, breadth60=0.38)) == 'selective_active'
          and am(dict(base, breadth60=0.58)) == 'selective_active')
    check('p84 breadth 越界不放行',
          am(dict(base, breadth60=0.59)) == '' and am(dict(base, breadth60=0.37)) == '')
    check('p84 volShock 越界不放行', am(dict(base, vol_shock=0.59)) == '')
    check('p84 回撤越界不放行', am(dict(base, drawdown=0.11)) == '')
    check('bear 不匹配任何放行模板', am(dict(base, regime='bear')) == '')


# 5. 择时闸门五状态
def test_timing_gate():
    tg = mod.compute_timing_gate
    rising = pd.Series(np.linspace(10.0, 20.0, 70))
    r = tg(rising, 0.7)
    check('进攻', r['state'] == '进攻' and r['exposure'] == 1.0 and r['allow_new'], r)
    r = tg(rising, 0.3)
    check('谨慎', r['state'] == '谨慎' and r['exposure'] == 0.5 and r['allow_new'], r)
    # 防御: 40日10 → 24日9 → 今收9.3 (65行; close 在 MA20 上 MA60 下)
    u = pd.Series([10.0] * 40 + [9.0] * 24 + [9.3])
    r = tg(u, 0.8)
    check('防御', r['state'] == '防御' and not r['allow_new'] and not r['force_liquidate'], r)
    # 空仓: 持续下行
    down = pd.Series(np.linspace(12.0, 8.0, 70))
    r = tg(down, 0.2)
    check('空仓', r['state'] == '空仓' and r['force_liquidate'], r)
    # 数据不足
    r = tg(pd.Series([1.0, 2.0]), 0.5)
    check('数据不足', r['state'] == '数据不足' and r['allow_new'], r)


# 6. 低波因子 (已注册未配权, 不参与 composite)
def test_lowvol():
    idx = pd.date_range('2025-01-01', periods=40, freq='B')
    calm = np.array([10.0] * 40, dtype=float)
    wild = 10.0 * np.cumprod(1.0 + np.random.default_rng(5).normal(0, 0.05, 40))
    close = pd.DataFrame({'000001.XSHE': calm, '000002.XSHE': wild}, index=idx)
    bench = pd.Series(np.linspace(10, 11, 40), index=idx)
    scores = mod.compute_lowvol_scores(close, bench)
    check('低波两只都有分', set(scores) == {'000001.XSHE', '000002.XSHE'}, scores)
    if '000001.XSHE' in scores and '000002.XSHE' in scores:
        check('平稳股得分更高', scores['000001.XSHE'] > scores['000002.XSHE'], scores)


# 7. 换手率稳定性
def test_turnover_stability():
    mod.g.turnover_cache = {
        'A': deque([1.5] * 60, maxlen=60),          # 恒定 → CV=0 → 1.0
        'B': deque(list(np.linspace(0.1, 3.0, 60)), maxlen=60),   # 波动大 → 低分
        'C': deque([1.0] * 30, maxlen=60),          # 窗口不足 → 不产出
    }
    s = mod.compute_turnover_stability_scores(None, None)
    check('恒定换手=1.0', s.get('A') == 1.0, s)
    check('波动换手低于恒定', 'B' in s and s['B'] < s['A'], s)
    check('窗口不足不产出', 'C' not in s)


# 8. 截面 z-score 合成 (MultiFactorStrategy 阶段1-2)
def test_build_composite():
    saved = dict(mod.STRATEGY['factor_weights'])
    try:
        # 单因子 (默认权重 turnover_stability=1.0): composite = z
        fs = {'turnover_stability': {'A': 1.0, 'B': 0.6, 'C': 0.2}}
        comp = mod.build_composite(fs)
        # 总体方差口径: mean=0.6, var=(1+0.36+0.04)/3-0.6²=0.106667 → std=0.326599
        mean_ref = (1.0 + 0.6 + 0.2) / 3.0
        std_ref = ((1.0 + 0.36 + 0.04) / 3.0 - mean_ref ** 2) ** 0.5
        zA = (1.0 - mean_ref) / std_ref
        check('单因子 composite=z (总体方差口径)',
              comp is not None and abs(comp['A'] - zA) < 1e-9
              and abs(comp['B'] - 0.0) < 1e-9 and abs(comp['C'] + zA) < 1e-9, comp)
        # clamp[-3,3]: 15×0 + 1×15 → 原始 z=3.873 → 3.0
        fs2 = {'turnover_stability': dict([('Z%d' % i, 0.0) for i in range(15)] + [('BIG', 15.0)])}
        comp2 = mod.build_composite(fs2)
        check('z 上界 clamp 到 3.0', abs(comp2['BIG'] - 3.0) < 1e-12, comp2['BIG'])
        # 15×15 + 1×0 → 原始 z=-3.873 → -3.0
        fs3 = {'turnover_stability': dict([('Z%d' % i, 15.0) for i in range(15)] + [('LOW', 0.0)])}
        comp3 = mod.build_composite(fs3)
        check('z 下界 clamp 到 -3.0', abs(comp3['LOW'] + 3.0) < 1e-12, comp3['LOW'])
        # 截面无分化 (std=0) → z=0
        fs4 = {'turnover_stability': {'A': 0.5, 'B': 0.5}}
        comp4 = mod.build_composite(fs4)
        check('截面无分化 (std=0) → z=0', abs(comp4['A']) < 1e-12 and abs(comp4['B']) < 1e-12, comp4)
        # 空截面 (预热期换手因子 60 日窗口不足 → 因子返回空 dict): 提前返回 {} + [因子] 日志, 不打 NaN 警告
        LOG_LINES.clear()
        check('因子截面为空 → 空 composite', mod.build_composite({'turnover_stability': {}}) == {})
        check('空截面 → [因子] 日志提示', any('[因子] turnover_stability 截面为空' in l for l in LOG_LINES), LOG_LINES)
        LOG_LINES.clear()
        # 多因子: 权重归一化 (1:3) + 缺任一因子剔除
        mod.STRATEGY['factor_weights'] = {'f1': 1.0, 'f2': 3.0}
        fs5 = {'f1': {'A': 2.0, 'B': 0.0}, 'f2': {'A': 0.0, 'B': 2.0}}
        comp5 = mod.build_composite(fs5)
        # f1: mean=1,std=1 → zA=1,zB=-1; f2: zA=-1,zB=1 → A=0.25-0.75=-0.5, B=-0.25+0.75=0.5
        check('多因子权重归一化合成', abs(comp5['A'] + 0.5) < 1e-12 and abs(comp5['B'] - 0.5) < 1e-12, comp5)
        fs6 = {'f1': {'A': 2.0, 'B': 0.0, 'C': 1.0}, 'f2': {'A': 0.0, 'B': 2.0}}
        comp6 = mod.build_composite(fs6)
        check('缺任一因子剔除该股票', set(comp6) == {'A', 'B'}, comp6)
        check('无因子输入 → 空 composite', mod.build_composite({}) == {})
    finally:
        mod.STRATEGY['factor_weights'].clear()
        mod.STRATEGY['factor_weights'].update(saved)
    # 生产路径: compute_factor_scores 仅执行已配权因子 (lowvol 已注册未配权 → 不执行)
    mod.g.turnover_cache = {'A': deque([1.5] * 60, maxlen=60)}
    fs7 = mod.compute_factor_scores(None, None)
    check('未配权重因子不执行', set(fs7) == {'turnover_stability'} and 'lowvol' not in fs7, fs7)


# 9. 六阶段 3-6 信号 (持仓卖出 / Top-N 选股 / 等权 clamp / 买入预算与 break)
def test_multi_factor_signals():
    ev = mod.evaluate_multi_factor_signals
    # ── 阶段3: 评分跌破阈值卖出 ──
    comp = {'A': 1.0, 'B': 0.5, 'H': 0.1}
    sells, buys = ev(dict(comp), {'H': None}, True, set())
    check('composite<0.2 → score 卖出', sells == [('H', 0.1, 'score')], sells)
    comp2 = {'A': 1.0, 'H': 0.2}
    sells2, _ = ev(dict(comp2), {'H': None}, True, set())
    check('composite==0.2 不卖 (严格小于)', sells2 == [], sells2)
    # ── 排名 > maxPositions×sellRankMultiplier(40) 卖出 ──
    many = {('X%02d' % i): float(45 - i) for i in range(44)}   # 44 只分数高于 H
    many['H'] = 0.5
    for i in range(5):
        many['Y%d' % i] = 0.4 - i * 0.05                        # 5 只低于 H → H 排名 45
    sells3, _ = ev(dict(many), {'H': None}, True, set())
    check('排名>40 → rank 卖出 (唯一持仓)', sells3 == [('H', 0.5, 'rank')], sells3)
    # ── 持仓缺因子值 → 无卖出信号, 仓位保留 ──
    comp4 = {'A': 1.0, 'B': 0.5}
    sells4, buys4 = ev(dict(comp4), {'M': None}, True, set())
    check('持仓缺因子值无卖出信号 (仓位保留)', all(s[0] != 'M' for s in sells4), sells4)
    check('缺因子值持仓不进入买入信号', all(b[0] != 'M' for b in buys4), buys4)
    # ── 阶段4: 仅 composite>0 (只做多) ──
    mix = {'A': 1.0, 'B': 0.5, 'Z': 0.0, 'N': -0.5}
    sells6, buys6 = ev(dict(mix), {}, True, set())
    check('composite<=0 不入选 (只做多)', len(buys6) == 2 and all(b[0] in ('A', 'B') for b in buys6), buys6)
    # ── Top-50 截断 + 买入按分数降序 ──
    pos60 = {('P%02d' % i): float(60 - i) for i in range(60)}   # 60..1 全正
    sells5, buys5 = ev(dict(pos60), {}, True, set())
    check('买入信号<=20 截断 (maxPositions 硬上限)', len(buys5) == 20, len(buys5))
    check('买入按分数降序', all(buys5[i][1] >= buys5[i + 1][1] for i in range(len(buys5) - 1)), buys5[:3])
    check('第51名 (score=10) 排除在 Top-50 外', all(b[0] != 'P50' for b in buys5), buys5)
    # ── 阶段5: 等权 1/n + clamp[0.01, 0.1] ──
    one = {'S': 1.0}
    _, buys7 = ev(dict(one), {}, True, set())
    check('1只入选 → w=clamp(1/1)=0.1', len(buys7) == 1 and abs(buys7[0][2] - 0.1) < 1e-12, buys7)
    twenty = {('T%02d' % i): float(20 - i) for i in range(20)}
    _, buys8 = ev(dict(twenty), {}, True, set())
    check('20只入选 → w=1/20=0.05', len(buys8) == 20 and abs(buys8[0][2] - 0.05) < 1e-12, buys8)
    many200 = {('M%03d' % i): float(200 - i) for i in range(200)}
    _, buys9 = ev(dict(many200), {}, True, set())
    check('200只 → Top-50 入选 w=1/50=0.02', len(buys9) == 20 and abs(buys9[0][2] - 0.02) < 1e-12, buys9)
    saved_top = mod.STRATEGY['top_n']
    mod.STRATEGY['top_n'] = 200
    try:
        _, buys9b = ev(dict(many200), {}, True, set())
        check('w 下限 clamp=0.01 (1/n<0.01)', len(buys9b) == 20 and abs(buys9b[0][2] - 0.01) < 1e-12, buys9b)
    finally:
        mod.STRATEGY['top_n'] = saved_top
    # ── 阶段6: maxNewBuys 预算耗尽 break (持仓更新同被截断) ──
    hold19 = dict([('H%d' % i, None) for i in range(18)] + [('H', None)])
    comp_b = {'A': 3.0, 'B': 2.0, 'H': 1.0}
    sells_b, buys_b = ev(dict(comp_b), hold19, True, set())
    check('max_new=1: 仅发最高分新标的', [b[0] for b in buys_b] == ['A'], buys_b)
    check('break 截断持仓更新 (H 在排名末尾不发)', all(b[0] != 'H' for b in buys_b), buys_b)
    check('预算场景无卖出信号', sells_b == [], sells_b)
    # ── 冻结日 (allow_new=False): 新标的禁止买入, 持仓更新保留 ──
    comp_f = {'A': 3.0, 'B': 2.0, 'H': 1.0}
    sells_f, buys_f = ev(dict(comp_f), {'H': None}, False, set())
    check('冻结日禁新开仓 (引擎 live blockNewBuys)', len(buys_f) == 1 and all(b[0] == 'H' for b in buys_f), buys_f)
    check('冻结日持仓更新保留 (is_held=True)', buys_f[0][3] is True, buys_f)
    check('冻结日持仓无卖出', sells_f == [], sells_f)
    # ── 封禁名单: 不参与排名/选股/卖出评估 ──
    comp_g = {'A': 3.0, 'X': 2.0}
    sells_g, buys_g = ev(dict(comp_g), {}, True, {'X'})
    check('封禁股票跳过 (买卖均无)', all(b[0] != 'X' for b in buys_g) and all(s[0] != 'X' for s in sells_g),
          (buys_g, sells_g))


# 10. 趋势健康分 (RuleVariableProvider::trendHealthScore)
def test_trend_health():
    h = mod.trend_health_score
    check('四项全满足=100', h(10.5, 10.2, 10.0, 10.1, 9.9) == 100)
    check('斜率持平不加分 (严格大于)', h(10.5, 10.2, 10.2, 10.1, 10.1) == 50)
    check('收盘低于均线不加分 (仅斜率两项)', h(10.0, 10.2, 10.0, 10.1, 10.0) == 50)
    check('部分满足=75 (斜率一项+收盘两项)', h(10.5, 10.2, 10.2, 10.1, 10.0) == 75)
    check('单项缺数据跳过 (NaN 不加分不崩溃)', h(10.5, np.nan, 10.0, np.nan, 9.9) == 0)


# 11. 结构止盈出场 5 条件有序第一命中 (ML 实例 862b0b8d 结构止盈 summary)
def test_exit_rules():
    r = mod.position_exit_rule
    check('默认夹具不触发', r(exit_d()) == (None, ''))
    check('① 前高85%减30', r(exit_d(pnl=5.0, close=8.6, high60=10.0)) == ('REDUCE', '前高85%减30'))
    check('① 边界 0.85 含', r(exit_d(pnl=3.0, close=8.5, high60=10.0)) == ('REDUCE', '前高85%减30'))
    check('① pnl<3% 不触发 (其余规则也不命中)',
          r(exit_d(pnl=2.9, close=8.5, high60=10.0, ma20=8.0, holding_high=8.5)) == (None, ''))
    check('① 优先于 ② (同命中取前高)',
          r(exit_d(pnl=10.0, close=9.5, ma20=8.0, high60=10.0, holding_high=9.5)) == ('REDUCE', '前高85%减30'))
    check('② 超涨MA110%减30', r(exit_d(pnl=9.0, close=11.2, ma20=10.0, high60=20.0, holding_high=11.2)) == ('REDUCE', '超涨MA110%减30'))
    check('② 边界 1.1 含', r(exit_d(pnl=8.0, close=11.0, ma20=10.0, high60=20.0, holding_high=11.0)) == ('REDUCE', '超涨MA110%减30'))
    # 注: 不测 9.4/10.0 精确边界 — 二进制浮点下 1-9.4/10=0.059999... < 0.06
    # (实现为 >= 0.06 边界含, 但精确边界值在浮点下不可判定, 用明确越线值验证)
    check('③ 回撤6.5%清仓', r(exit_d(close=9.35, ma20=10.0, high60=10.0, holding_high=10.0)) == ('EXIT', '回撤6%清仓'))
    # 回撤 5.9% 未达线; ma20=9.0 使 ⑤(close/ma20<0.98) 同样不命中 → 全不触发 (纯测 6% 阈值)
    check('③ 回撤5.9% 不触发', r(exit_d(close=9.41, ma20=9.0, high60=10.0, holding_high=10.0)) == (None, ''))
    check('④ 动能<50减30', r(exit_d(health=25.0)) == ('REDUCE', '动能<50减30'))
    check('④ 边界 50 不触发 (严格小于)', r(exit_d(health=50.0)) == (None, ''))
    check('⑤ 支撑破位清仓', r(exit_d(close=9.7, ma20=10.0, high60=10.0, holding_high=9.7, health=100.0)) == ('EXIT', '支撑破位清仓'))
    check('⑤ 边界 0.98 不触发 (严格小于)', r(exit_d(close=9.8, ma20=10.0, holding_high=9.8)) == (None, ''))
    check('③ 优先于 ④ (有序第一命中)',
          r(exit_d(close=9.35, ma20=10.0, high60=10.0, holding_high=10.0, health=25.0)) == ('EXIT', '回撤6%清仓'))


# 12. 熔断器
def test_circuit_breaker():
    cfg = dict(max_drawdown=0.15, halt_days=5, daily_loss_limit=0.03, reduce_to=0.5)
    cb = mod.TimedCircuitBreaker(cfg)
    cb.update_eod(100.0)
    check('初始未熔断', not cb.is_halted())
    # 峰值回撤 16% → 熔断
    check('回撤>=15% 触发', cb.check_intraday(84.0) is True)
    check('熔断中', cb.is_halted())
    check('targetExposure=0', cb.target_exposure() == 0.0)
    # 倒计时递减 → 恢复
    for i in range(4):
        cb.update_eod(84.0)
        check('熔断第%d日未恢复' % (i + 1), cb.is_halted())
    cb.update_eod(84.0)
    check('第5日恢复', not cb.is_halted())
    check('恢复后峰值重置', cb.target_exposure() == 1.0)
    # 单日亏损检查
    cb2 = mod.TimedCircuitBreaker(cfg)
    cb2.update_eod(100.0)
    check('单日亏3%触发减仓', cb2.check_intraday(97.0) is True)
    check('dailyReduced exposure=0.5', cb2.target_exposure() == 0.5)
    cb2.update_eod(97.0)
    check('次日重置 dailyReduced', cb2.target_exposure() == 1.0)
    # 已熔断时 checkIntraday 不再触发
    cb3 = mod.TimedCircuitBreaker(cfg)
    cb3.update_eod(100.0)
    cb3.check_intraday(80.0)
    check('熔断后 check 返回 False', cb3.check_intraday(70.0) is False)


# 13. 每单风控 (默认配置=ML 实例 862b0b8d: 止损10% 止盈20% 账户回撤99 其余禁用)
def test_risk_evaluator():
    ev = mod.RiskEvaluator.evaluate_order
    base = dict(symbol='000001.XSHE', price=10.0, quantity=100, is_buy=True,
                signal_strength=0.5, total_asset=1000000.0, market_value=500000.0,
                symbol_market_value=0.0, symbol_return_pct=0.0, closeable_quantity=0,
                current_drawdown_pct=0.0)
    ok, rc, _ = ev(dict(base))
    check('默认配置买入放行', ok, rc)
    ok, rc, _ = ev(dict(base, signal_strength=0.05))
    check('信号强度<0.1 拒绝', not ok and rc == 'SignalStrengthTooWeak', rc)
    # ML 实例默认: stopLossPercent=10 / takeProfitPercent=20
    ok, rc, _ = ev(dict(base, symbol_return_pct=-12.0))
    check('浮亏>=10% 拒加仓 (ML 默认)', not ok and rc == 'StopLossTriggered', rc)
    ok, rc, _ = ev(dict(base, symbol_return_pct=-5.0))
    check('浮亏未达线放行', ok, rc)
    ok, rc, _ = ev(dict(base, symbol_return_pct=25.0))
    check('浮盈>=20% 拒加仓 (ML 默认)', not ok and rc == 'TakeProfitTriggered', rc)
    ok, rc, _ = ev(dict(base, symbol_return_pct=15.0))
    check('浮盈未达线放行', ok, rc)
    ok, rc, _ = ev(dict(base, current_drawdown_pct=-15.0))
    check('账户回撤 99% 线永不触发 (ML 默认)', ok, rc)
    saved = dict(mod.STRATEGY['risk'])
    mod.STRATEGY['risk']['max_position_pct'] = 15.0
    ok, rc, _ = ev(dict(base, symbol_market_value=90000.0, quantity=10000))  # 9万+10万=19% > 15%
    check('单票集中度超限拒绝', not ok and rc == 'PositionConcentrationExceeded', rc)
    mod.STRATEGY['risk']['max_total_exposure_pct'] = 67.0
    ok, rc, _ = ev(dict(base, market_value=600000.0, quantity=10000))  # 60万+10万=70% > 67%
    check('总敞口超限拒绝', not ok and rc == 'TotalExposureExceeded', rc)
    mod.STRATEGY['risk']['max_drawdown_limit_pct'] = 12.0
    ok, rc, _ = ev(dict(base, current_drawdown_pct=-15.0))
    check('账户回撤超限拒绝', not ok and rc == 'MaxDrawdownExceeded', rc)
    # 卖出侧: 数量不超可卖
    ok, rc, _ = ev(dict(base, is_buy=False, quantity=100, closeable_quantity=200))
    check('卖出数量合法放行', ok, rc)
    ok, rc, _ = ev(dict(base, is_buy=False, quantity=300, closeable_quantity=200))
    check('卖出超可卖拒绝', not ok and rc == 'SellQuantityExceedsHolding', rc)
    mod.STRATEGY['risk'].clear()
    mod.STRATEGY['risk'].update(saved)


# 14. 凯利统计
def test_kelly():
    journal = [
        dict(is_buy=True), dict(is_buy=False, pnl=100.0),
        dict(is_buy=False, pnl=50.0), dict(is_buy=False, pnl=-100.0),
        dict(is_buy=True), dict(is_buy=False, pnl=-50.0),
    ]
    kr = mod.kelly_report(journal)
    check('凯利报告生成', kr is not None, kr)
    if kr:
        check('胜率=2/4', abs(kr['win_rate'] - 0.5) < 1e-9, kr)
        check('半凯=全凯/2', abs(kr['half_kelly'] - kr['full_kelly'] / 2.0) < 1e-9, kr)
        # 全凯 = 0.5 - 0.5/1 = 0 (avgWin=75, avgLoss=75)
        check('全凯=0 (赔率1)', abs(kr['full_kelly'] - 0.0) < 1e-9, kr)
    check('无卖出无报告', mod.kelly_report([dict(is_buy=True)]) is None)
    check('全胜无报告', mod.kelly_report([dict(is_buy=False, pnl=10.0)]) is None)


# 15. 事件风控
def test_event_risk():
    saved = dict(mod.STRATEGY['risk'])
    er = mod.EventRiskController(mod.STRATEGY['risk'])
    er.apply_event('stock', 'liquidate', symbols=['000001.XSHE'], severity=0.7)
    check('liquidate 名单', '000001.XSHE' in er.liquidated())
    er.apply_event('sector', 'reduce_exposure', sector_codes='SW1,SW2')
    check('行业限仓 30%', er.sector_limits() == {'SW1': 30.0, 'SW2': 30.0}, er.sector_limits())
    er.apply_event('sector', 'reduce_position', sector_codes='SW1')
    check('行业限仓收紧取小', er.sector_limits()['SW1'] == 30.0, er.sector_limits())
    er.apply_event('market', 'reduce_exposure', severity=0.6)
    er.apply_event('market', 'alert')
    er.apply_event('stock', 'reduce_position', symbols=['000002.XSHE'])
    # 标签规则需要 event_type (引擎 onFinancialEvent 事件必有 event_type)
    er.apply_event('news.investigation', 'x', symbols=['000003.XSHE'],
                   tags={'立案调查': 'true'}, event_type='news.investigation')
    check('立案调查封禁', '000003.XSHE' in er.blocked())
    check('止损收紧<=5', mod.STRATEGY['risk']['stop_loss_pct'] <= 5.0)
    er.apply_event('news.warn', 'x', symbols=['000004.XSHE'], tags={'ST警示': 'true'},
                   event_type='news.warn')
    check('ST警示封禁', '000004.XSHE' in er.blocked())
    check('限仓收紧<=2', mod.STRATEGY['risk']['max_position_pct'] <= 2.0)
    er.clear_blocked()
    check('T+1 解禁清空', len(er.blocked()) == 0 and len(er.liquidated()) == 0 and len(er.sector_limits()) == 0)
    mod.STRATEGY['risk'].clear()
    mod.STRATEGY['risk'].update(saved)


# 16. 股票池
def test_universe():
    ctx = SimpleNamespace(current_dt=pd.Timestamp('2026-06-01'))
    codes = mod.build_universe(ctx)
    check('剔除 ST', '000060.XSHE' not in codes)
    check('剔除 退', '000061.XSHE' not in codes)
    check('剔除北交所', '830001.BJ' not in codes)
    check('剔除次新', '000062.XSHE' not in codes)
    check('正常股票保留', '%06d.XSHE' % 1000 in codes)
    mod.STRATEGY['universe_mode'] = 'hs300'
    codes2 = mod.build_universe(ctx)
    check('hs300 模式只取成分', codes2 and all(c in set(_stub_get_index_stocks('x')) for c in codes2), codes2)
    mod.STRATEGY['universe_mode'] = 'all_a'


# 17. 全流程 handle_data (桩平台订单记录)
class FakePos:
    def __init__(self, qty, avg):
        self.closeable_amount = qty
        self.avg_cost = avg


class FakePortfolio:
    def __init__(self, cash=1000000.0):
        self.cash = cash
        self.positions = {}

    @property
    def total_value(self):
        mv = 0.0
        for code, p in self.positions.items():
            px = mod.last_close_cache.get(code, p.avg_cost)
            mv += px * p.closeable_amount
        return self.cash + mv


class FakeContext:
    def __init__(self):
        self.current_dt = pd.Timestamp('2025-03-14')      # 决策成交日 (周五)
        self.previous_date = pd.Timestamp('2025-03-13')   # 决策数据锚点 (方案A: T-1)
        self.portfolio = FakePortfolio()


def test_full_flow():
    """全流程确定性几何 (多因子六阶段):
      首日 (预热 160 行, 锚点 2025-03-13): 目标股换手率恒定 (CV=0 → score=1.0),
        其余 7 只换手率波动 (CV≈0.54 → score≈0.73) → 截面 z: 目标股 +2.64 入选,
        其余为负不入选 → 目标股按等权 0.1 买入 10 万元; 指数恒上行 → 择时放行;
        收盘面板全部站上 MA60 → breadth=1.0 → bull 不冻结。
      次日 (增量追加 1 行 10.05 于 10.0 之后, 锚点 2025-03-14): 全市场换手率同值
        → 截面 std=0 → z=0 → composite=0 → 持仓评分跌破阈值 (0<0.2) → 全清卖出。
      其他 7 只: 线性上行 (breadth 全绿); 目标股末两行 9.9 → 10.0 (不触发涨跌停过滤)。
    """
    target = '000001.XSHE'
    codes = [target] + ['%06d.XSHE' % (1000 + i) for i in range(1, 8)]

    g.universe = codes
    g.panel_cache = None       # 重置增量缓存 → 首日走预热路径
    # 因子层仅换手率稳定性 (用户裁定 2026-08-15): 预填 59 个有效日, 当日批量查询追加
    # 第 60 个 (目标股恒定 1.5 → CV=0 → score=1.0; 其余 linspace(0.1,3.0)+3.0 → CV≈0.54)
    g.turnover_cache = {target: deque([1.5] * 59, maxlen=60)}
    for c in codes[1:]:
        g.turnover_cache[c] = deque(np.linspace(0.1, 3.0, 59), maxlen=60)
    g.next_turnover_start = None
    g.holding_highs = {}
    g.day_count = 0
    g.journal = []
    mod.g.breaker = mod.TimedCircuitBreaker(mod.STRATEGY['circuit_breaker'])
    mod.g.breaker.update_eod(1000000.0)
    mod.g.event_risk = mod.EventRiskController(mod.STRATEGY['risk'])
    mod.g.peak_equity = 1000000.0

    day_counter = [0]

    def fake_get_price(universe, end_date=None, count=LOOKBACK, frequency='daily',
                       fields=None, skip_paused=True, fq='pre', panel=False):
        # 按 count/end_date 感知: n 行历史, 末日 == end_date (增量层锚点校验依赖)
        n = count if count and count > 0 else LOOKBACK
        idxn = pd.date_range(end=end_date, periods=n, freq='B')
        if isinstance(universe, str):   # 指数: 恒上行 (增量单行也保持上行)
            return pd.DataFrame({'close': [13.01] if n == 1 else np.linspace(10.0, 13.0, n)},
                                index=idxn)
        dn = day_counter[0]
        fld = fields[0]
        if dn == 0:   # 预热日: 160 行 close / 61 行 high
            data = {}
            for c in codes:
                if c == target:
                    data[c] = [10.0] * max(n - 2, 0) + [9.9, 10.0]   # 末两行 9.9 → 10.0
                else:
                    data[c] = np.linspace(10.0, 14.0, n)             # 线性上行 → breadth 全绿
            if fld == 'high':
                data = {c: np.asarray(v, dtype=float) * 1.01 for c, v in data.items()}
        else:   # 增量日: 单行 (n==1)
            data = {c: ([10.05] if c == target else [14.01]) for c in codes}
            if fld == 'high':
                data = {c: [10.1505] if c == target else [14.1501] for c in codes}
        return pd.DataFrame(data, index=idxn)

    mod.get_price = fake_get_price

    _turnover_override.clear()
    _turnover_override.update({c: 3.0 for c in codes})
    _turnover_override[target] = 1.5

    ctx = FakeContext()

    def fake_order_target_value(code, value):
        ORDER_LOG.append(('target_value', code, value))
        px = mod.last_close_cache.get(code, 0.0)
        if value <= 0 or px <= 0:
            p = ctx.portfolio.positions.pop(code, None)
            if p is not None:
                ctx.portfolio.cash += p.closeable_amount * px
        else:
            qty = int(value / px / 100.0) * 100
            old = ctx.portfolio.positions.get(code)
            if old is not None:
                ctx.portfolio.cash += (old.closeable_amount - qty) * px
            else:
                ctx.portfolio.cash -= qty * px
            if qty > 0:
                avg = old.avg_cost if old is not None else px
                ctx.portfolio.positions[code] = FakePos(qty, avg)

    mod.order_target_value = fake_order_target_value
    mod.order_target = lambda code, qty: ORDER_LOG.append(('target', code, qty))

    # ── 首日 (预热 160 行): 截面分化 → 目标股 z>0 买入 ──
    ORDER_LOG.clear()
    LOG_LINES.clear()
    mod.handle_data(ctx, {})   # 新版平台签名 (context, data); data 当日快照本策略不使用
    check('多因子日产生买入 (目标股 z 分最高)', any(o[0] == 'target_value' and o[1] == target and o[2] > 0 for o in ORDER_LOG), ORDER_LOG)
    check('买入后持仓建立', target in ctx.portfolio.positions, ctx.portfolio.positions)
    qty_bought = ctx.portfolio.positions[target].closeable_amount if target in ctx.portfolio.positions else 0
    check('买入数量>0', qty_bought > 0, qty_bought)
    check('买入日志', any('[买入]' in l for l in LOG_LINES), LOG_LINES)
    check('组合日志输出', any('[组合]' in l for l in LOG_LINES), LOG_LINES)
    check('熔断器未熔断', not g.breaker.is_halted())

    # ── 次日 (增量追加 1 行): 全市场换手率同值 → z=0 → 评分跌破阈值全清 ──
    day_counter[0] = 1
    ctx.current_dt = pd.Timestamp('2025-03-17')   # 周一 (增量行日期=锚点=2025-03-14 周五)
    ctx.previous_date = pd.Timestamp('2025-03-14')
    for c in codes:
        g.turnover_cache[c] = deque([1.5] * 60, maxlen=60)
    ORDER_LOG.clear()
    mod.handle_data(ctx, {})
    check('评分跌破阈值产生全清卖单', any(o[0] == 'target_value' and o[1] == target and o[2] == 0.0 for o in ORDER_LOG), ORDER_LOG)
    check('卖出后持仓清除', target not in ctx.portfolio.positions, ctx.portfolio.positions)
    check('卖出记入成交日志', any(t['code'] == target and not t['is_buy'] for t in g.journal), g.journal)
    check('熔断器未熔断', not g.breaker.is_halted())


# 18. 停牌 NaN 行跳过链 (差异 #26)
def test_paused_nan():
    """停牌 NaN 行跳过链 (差异 #26: 新平台多股票面板停牌日个股为 NaN):
    快照/上涨家数分母排除停牌股; 低波因子 ok 掩码剔除; NaN composite 不产生
    买卖信号。全部不得异常/误判"""
    codes = ['000001.XSHE'] + ['%06d.XSHE' % (1000 + i) for i in range(1, 4)]
    rows = 70
    idx = pd.date_range('2024-11-01', periods=rows, freq='B')
    rng = np.random.default_rng(555)
    data = {}
    for c in codes[1:]:
        data[c] = 10.0 * np.cumprod(1.0 + rng.normal(0.002, 0.009, rows))
    data[codes[0]] = [10.0] * (rows - 1) + [np.nan]   # 末日停牌
    close = pd.DataFrame(data, index=idx)

    snap = mod.compute_market_snapshot(close)
    snap_wo = mod.compute_market_snapshot(close[codes[1:]])
    check('快照停牌股被排除分母 (breadth 与剔除后一致)',
          snap is not None and snap_wo is not None
          and abs(snap['breadth60'] - snap_wo['breadth60']) < 1e-12)
    check('上涨家数停牌股被排除',
          abs(mod.compute_advance_ratio(close)
              - mod.compute_advance_ratio(close[codes[1:]])) < 1e-12)

    idx_close = pd.Series(np.linspace(10.0, 13.0, rows), index=idx)
    lv = mod.compute_lowvol_scores(close, idx_close)
    check('低波因子剔除停牌股', codes[0] not in lv, lv)

    # NaN composite: 卖出条件 (NaN<0.2 为 False) 与入选条件 (NaN>0 为 False) 均不命中
    comp = {'000001.XSHE': np.nan, codes[1]: 1.0}
    sells, buys = mod.evaluate_multi_factor_signals(dict(comp), {'000001.XSHE': None}, True, set())
    check('停牌股 NaN composite 无卖出信号', all(s[0] != codes[0] for s in sells), sells)
    check('停牌股 NaN composite 不入选', all(b[0] != codes[0] for b in buys), buys)


# 19. 面板布局归一化 (差异 #27: 新版平台弃用 panel=True → panel=False, 返回布局随引擎版本变化)
def test_normalize_panel():
    """面板布局归一化 (差异 #27): 新版平台 panel=False 返回布局随引擎版本变化, 支持三种口径:
    A 宽表 MultiIndex (field, code) 列 → 取代码层; B 长表 (date, code)/(code, date) 行索引
    → unstack/转置成宽表; C 标准宽表 code 列 → 原样。未知布局 → None (fetch_panels 诊断日志当日跳过)"""
    codes = ['000001.XSHE'] + ['%06d.XSHE' % (1000 + i) for i in range(1, 4)]
    idx = pd.date_range('2024-11-01', periods=70, freq='B')
    raw = pd.DataFrame({c: np.linspace(10.0, 14.0, 70) for c in codes}, index=idx)

    multi = raw.copy()
    multi.columns = pd.MultiIndex.from_product([['close'], list(raw.columns)])
    norm = mod._normalize_panel(multi)
    check('A 宽表 MultiIndex 列归一到代码层', list(norm.columns) == list(raw.columns), norm.columns)

    plain = mod._normalize_panel(raw)
    check('C 标准宽表布局不变 (内容等价, 防御性副本)', plain is not None and plain.equals(raw),
          type(plain.columns).__name__)

    long_dc = raw.stack().to_frame('close')   # B 长表: 行=(日期, 代码)
    norm_dc = mod._normalize_panel(long_dc)
    check('B 长表(日期,代码) unstack 成宽表',
          norm_dc is not None and list(norm_dc.columns) == list(raw.columns)
          and np.allclose(norm_dc.values, raw.values, equal_nan=True),
          norm_dc.shape if norm_dc is not None else None)

    long_cd = raw.T.stack().to_frame('close')   # B 长表: 行=(代码, 日期)
    norm_cd = mod._normalize_panel(long_cd)
    check('B 长表(代码,日期) 转置成宽表',
          norm_cd is not None and list(norm_cd.columns) == list(raw.columns)
          and np.allclose(norm_cd.values, raw.values, equal_nan=True),
          norm_cd.shape if norm_cd is not None else None)

    two_col = raw.stack().to_frame('close').copy()
    two_col['extra'] = 1.0
    check('未知布局 (长表多列) → None', mod._normalize_panel(two_col) is None,
          mod._normalize_panel(two_col))

    check('None 透传', mod._normalize_panel(None) is None, mod._normalize_panel(None))

    # D 维度列变体: 平台把 date/code 放数据列而非索引 (jqboson 实测第三口径)
    long_cols = raw.reset_index().melt(id_vars='index', var_name='code', value_name='close')
    long_cols = long_cols.rename(columns={'index': 'date'}).reset_index(drop=True)
    norm_d = mod._normalize_panel(long_cols)
    check('D 维度列长表 (date/code 数据列) 归一化成宽表',
          norm_d is not None and list(norm_d.columns) == list(raw.columns)
          and np.allclose(norm_d.values, raw.values, equal_nan=True),
          norm_d.shape if norm_d is not None else None)

    # E 变体: 索引已是 (date, code) 且带重复 date 数据列 → 丢弃重复列后正常
    long_dup = raw.stack().to_frame('close').reset_index()
    long_dup = long_dup.rename(columns={'level_0': 'date'}).set_index(['date', 'level_1'])
    long_dup['date'] = long_dup.index.get_level_values(0)
    norm_e = mod._normalize_panel(long_dup)
    check('E 重复 date 数据列 (索引已含时间) 丢弃后归一化',
          norm_e is not None and list(norm_e.columns) == list(raw.columns)
          and np.allclose(norm_e.values, raw.values, equal_nan=True),
          norm_e.shape if norm_e is not None else None)

    # 数值清洗: 宽表混入 datetime64 列 (非维度命名) → 丢弃 + warn, 其余列保留
    LOG_LINES.clear()
    tainted = raw.copy()
    tainted['trade_date'] = raw.index.to_series().values   # datetime64 值, 滚动计算会崩
    norm_t = mod._normalize_panel(tainted)
    check('混入 datetime64 列 → 丢弃且其余列保留',
          norm_t is not None and 'trade_date' not in norm_t.columns
          and list(norm_t.columns) == list(raw.columns),
          norm_t.columns.tolist() if norm_t is not None else None)
    check('datetime64 列丢弃 → [数据] warn 显式输出',
          any('[数据] 面板混入日期列' in l for l in LOG_LINES), LOG_LINES)

    # 无法数值化的列 (字符串) → warn + 丢弃, 不崩
    LOG_LINES.clear()
    junk = raw.copy()
    junk['junk'] = 'x'
    norm_j = mod._normalize_panel(junk)
    check('无法数值化列 → 丢弃且不崩',
          norm_j is not None and 'junk' not in norm_j.columns
          and list(norm_j.columns) == list(raw.columns),
          norm_j.columns.tolist() if norm_j is not None else None)
    check('无法数值化列丢弃 → [数据] warn 显式输出',
          any('[数据] 面板含无法数值化的列' in l for l in LOG_LINES), LOG_LINES)

    # 清洗后无数据列 → None
    all_junk = pd.DataFrame({'000001.XSHE': ['a', 'b']},
                            index=pd.date_range('2024-11-01', periods=2, freq='B'))
    check('清洗后列全空 → None', mod._normalize_panel(all_junk) is None,
          mod._normalize_panel(all_junk))
    LOG_LINES.clear()


# 20. 数据失败路径无静默 + 增量查询次数契约 (每日 3 次: close/high/指数)
def test_data_diagnostics():
    """数据失败路径无静默: 预热历史不足/空面板/增量锚点不符/增量查询异常/换手批量区间 —
    全部返回 None 或跳过且 LOG_LINES 有对应 [数据] warn。若回测全程无 [评估] 行,
    日志必然指出卡在哪个数据环节。
    查询次数契约 (用户裁定 2026-08-15 速度优化): 预热 3 次 (close160/high61/指数160),
    之后每日 3 次 count=1 单行查询 (原 6 次; low/open/volume 查询已随 10 条旧规则删除)"""

    def fake_short(universe, end_date=None, count=LOOKBACK, frequency='daily',
                   fields=None, skip_paused=True, fq='pre', panel=False):
        rows = 30
        idx = pd.date_range(end=end_date, periods=rows, freq='B')
        return pd.DataFrame({'close': np.full(rows, 10.0)}, index=idx)

    def fake_empty(universe, end_date=None, count=LOOKBACK, frequency='daily',
                   fields=None, skip_paused=True, fq='pre', panel=False):
        idx = pd.date_range(end=end_date, periods=0, freq='B')
        return pd.DataFrame({'close': []}, index=idx)

    LOG_LINES.clear()
    g.panel_cache = None   # 重置增量缓存 → 走预热路径
    mod.get_price = fake_short
    check('预热历史不足 → fetch_panels None', mod.fetch_panels(['000001.XSHE'], '2025-03-15') is None, None)
    check('历史不足 → [数据] warn 显式输出', any('[数据] 收盘面板历史不足' in l for l in LOG_LINES), LOG_LINES)

    LOG_LINES.clear()
    g.panel_cache = None
    mod.get_price = fake_empty
    check('空面板 → fetch_panels None', mod.fetch_panels(['000001.XSHE'], '2025-03-15') is None, None)
    check('空面板 → [数据] warn 显式输出', any('[数据] 收盘面板为空' in l for l in LOG_LINES), LOG_LINES)

    # 增量锚点不符: 预热就绪后, count=1 返回行日期 != 锚点 → warn + 当日跳过 (缓存不动)
    idx_ok = pd.date_range(end='2025-03-14', periods=160, freq='B')
    close_ok = pd.DataFrame({'000001.XSHE': np.full(160, 10.0)}, index=idx_ok)
    g.panel_cache = dict(close=close_ok, high=close_ok * 1.01,
                         idx_close=pd.Series(np.full(160, 10.0), index=idx_ok))

    def fake_wrong_date(universe, end_date=None, count=LOOKBACK, frequency='daily',
                        fields=None, skip_paused=True, fq='pre', panel=False):
        idx = pd.date_range(end='2025-03-10', periods=1, freq='B')
        return pd.DataFrame({'close': [10.0]}, index=idx)

    LOG_LINES.clear()
    mod.get_price = fake_wrong_date
    check('增量面板日期与锚点不符 → fetch_panels None',
          mod.fetch_panels(['000001.XSHE'], '2025-03-15') is None, None)
    check('锚点不符 → [数据] warn 显式输出', any('日期与锚点不符' in l for l in LOG_LINES), LOG_LINES)

    # 增量查询异常 → warn + 当日跳过
    def fake_raise(universe, end_date=None, count=LOOKBACK, frequency='daily',
                   fields=None, skip_paused=True, fq='pre', panel=False):
        raise RuntimeError('stub 查询异常')

    LOG_LINES.clear()
    mod.get_price = fake_raise
    check('增量查询异常 → fetch_panels None',
          mod.fetch_panels(['000001.XSHE'], '2025-03-15') is None, None)
    check('增量查询异常 → [数据] warn 显式输出', any('[数据] close 面板获取失败' in l for l in LOG_LINES), LOG_LINES)

    # 换手批量: (上次锚点, 本次锚点] 区间内交易日逐日查询 (get_fundamentals 单日);
    # 某日失败 → 区间起点停在该日 (已成功日不重复, 下批从失败日重试)
    g.day_count = 6
    g.next_turnover_start = '2025-03-10'
    g.turnover_cache = {}
    _last_query_codes[:] = ['000001.XSHE']
    idx_t = pd.date_range(end='2025-03-14', periods=160, freq='B')
    g.panel_cache = dict(close=pd.DataFrame(index=idx_t))
    fund_dates = []
    orig_fundamentals = mod.get_fundamentals

    def fake_fundamentals_ok(q, date=None):
        fund_dates.append(date)
        return pd.DataFrame([dict(code='000001.XSHE', turnover_ratio=1.5)])

    mod.get_fundamentals = fake_fundamentals_ok
    LOG_LINES.clear()
    mod.update_turnover_cache('2025-03-14')
    check('换手逐日查询日期序列', fund_dates == ['2025-03-10', '2025-03-11', '2025-03-12',
                                              '2025-03-13', '2025-03-14'], fund_dates)
    check('批量后区间起点推进', g.next_turnover_start == '2025-03-15', g.next_turnover_start)
    check('换手值入缓存 (按日期升序)', list(g.turnover_cache.get('000001.XSHE', [])) == [1.5] * 5,
          g.turnover_cache)

    # 中断续传: 03-12 返回空 → 起点停在 03-12, 03-10/03-11 已入缓存不重复
    g.next_turnover_start = '2025-03-10'
    g.turnover_cache = {}
    fund_dates[:] = []

    def fake_fundamentals_hole(q, date=None):
        fund_dates.append(date)
        if date == '2025-03-12':
            return pd.DataFrame(columns=['code', 'turnover_ratio'])   # 空 → 中断
        return pd.DataFrame([dict(code='000001.XSHE', turnover_ratio=1.5)])

    mod.get_fundamentals = fake_fundamentals_hole
    LOG_LINES.clear()
    mod.update_turnover_cache('2025-03-14')
    check('空面板 → 起点停在失败日', g.next_turnover_start == '2025-03-12', g.next_turnover_start)
    check('中断前成功日已入缓存', list(g.turnover_cache.get('000001.XSHE', [])) == [1.5, 1.5],
          g.turnover_cache)
    check('空面板 → [数据] warn 显式输出', any('换手率返回为空' in l for l in LOG_LINES), LOG_LINES)
    check('中断 warn 显式输出', any('批量中断于 2025-03-12' in l for l in LOG_LINES), LOG_LINES)

    # 下批重试: 从失败日 03-12 继续, 已成功日不重复
    mod.get_fundamentals = fake_fundamentals_ok
    fund_dates[:] = []
    mod.update_turnover_cache('2025-03-14')
    check('下批从失败日重试 (日期序列)', fund_dates == ['2025-03-12', '2025-03-13', '2025-03-14'],
          fund_dates)
    check('下批重试后缓存不重复', list(g.turnover_cache.get('000001.XSHE', [])) == [1.5] * 5,
          g.turnover_cache)

    # 查询异常 → 起点停在失败日 + warn
    g.next_turnover_start = '2025-03-10'
    g.turnover_cache = {}

    def fake_fundamentals_raise(q, date=None):
        raise RuntimeError('stub 换手查询异常')

    saved_start = g.next_turnover_start
    mod.get_fundamentals = fake_fundamentals_raise
    LOG_LINES.clear()
    mod.update_turnover_cache('2025-03-14')
    check('换手查询异常 → 区间起点不推进', g.next_turnover_start == saved_start, g.next_turnover_start)
    check('换手查询异常 → [数据] warn', any('[数据] 换手率查询失败' in l for l in LOG_LINES), LOG_LINES)
    mod.get_fundamentals = orig_fundamentals

    # 查询次数契约: 预热 3 次 (close160/high61/指数160), 增量日 3 次 count=1
    calls = []

    def counting_price(universe, end_date=None, count=LOOKBACK, frequency='daily',
                       fields=None, skip_paused=True, fq='pre', panel=False):
        calls.append((count, tuple(fields or [])))
        n = count if count and count > 0 else LOOKBACK
        idxn = pd.date_range(end=end_date, periods=n, freq='B')
        if isinstance(universe, str):
            return pd.DataFrame({'close': np.full(n, 10.0)}, index=idxn)
        return pd.DataFrame({c: np.full(n, 10.0) for c in universe}, index=idxn)

    mod.get_price = counting_price
    g.panel_cache = None
    ok = mod.fetch_panels(['000001.XSHE', '000002.XSHE'], '2025-03-14')
    check('预热只发 3 次查询 (close/high/指数)',
          ok is not None and len(calls) == 3 and [c[0] for c in calls] == [160, 61, 160], calls)
    ok2 = mod.fetch_panels(['000001.XSHE', '000002.XSHE'], '2025-03-17')
    check('增量日只发 3 次 count=1 单行查询',
          ok2 is not None and len(calls) == 6 and [c[0] for c in calls[3:]] == [1, 1, 1], calls[3:])

    idx = pd.date_range('2024-11-01', periods=2, freq='B')
    all_nan = pd.DataFrame({'000001.XSHE': [np.nan, np.nan]}, index=idx)
    check('全 NaN 面板 → 市场快照 None (handle_data 层打 warn)', mod.compute_market_snapshot(all_nan) is None, None)


# ═════════════════════════════════════════════════════════════════════
RUNNERS = [
    test_snapshot, test_advance_ratio, test_market_freeze, test_market_allow_mode,
    test_timing_gate, test_lowvol, test_turnover_stability, test_build_composite,
    test_multi_factor_signals, test_trend_health, test_exit_rules, test_circuit_breaker,
    test_risk_evaluator, test_kelly, test_event_risk, test_universe, test_full_flow,
    test_paused_nan, test_normalize_panel, test_data_diagnostics,
]

if __name__ == '__main__':
    total = 0
    fails = 0
    for fn in RUNNERS:
        RESULTS.clear()
        try:
            fn()
        except Exception as exc:
            import traceback
            traceback.print_exc()
            RESULTS.append((fn.__name__, False, '异常: %r' % exc))
        n_pass = sum(1 for _, ok, _ in RESULTS if ok)
        n_fail = len(RESULTS) - n_pass
        total += len(RESULTS)
        fails += n_fail
        print('%s: %d/%d 通过' % (fn.__name__, n_pass, len(RESULTS)))
    print('\nTOTAL: %d 项断言, %d 失败' % (total, fails))
    sys.exit(1 if fails else 0)
