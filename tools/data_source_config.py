"""
数据源配置 — 统一切换交易日历、实时行情、历史日线数据源
用法:
  from data_source_config import get_trade_calendar, DataSource
  dates = get_trade_calendar()              # 使用当前配置的源
  DataSource.set_calendar('baostock')       # 切换到 Baostock
  DataSource.set_realtime('juejin')          # 实时行情切掘金
"""
from __future__ import annotations
import datetime as dt
import json, os, sys
from functools import lru_cache
from typing import Optional

# ═══════════════════════════════════════════
# 配置存储
# ═══════════════════════════════════════════
_CONFIG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), '.data_source_config.json')

def _load_config() -> dict:
    try:
        with open(_CONFIG_PATH) as f:
            return json.load(f)
    except:
        return {}

def _save_config(cfg: dict):
    with open(_CONFIG_PATH, 'w') as f:
        json.dump(cfg, f, indent=2)

# ═══════════════════════════════════════════
# 交易日历
# ═══════════════════════════════════════════

def _calendar_akshare() -> list[dt.date]:
    import akshare as ak
    df = ak.tool_trade_date_hist_sina()
    if df is None or df.empty:
        raise RuntimeError("AKShare 交易日历获取失败")
    dates = []
    for v in df['trade_date'].tolist():
        if isinstance(v, dt.datetime): dates.append(v.date())
        elif isinstance(v, dt.date): dates.append(v)
        else: dates.append(dt.date.fromisoformat(str(v)))
    dates.sort()
    return dates

def _calendar_baostock() -> list[dt.date]:
    import baostock as bs
    bs.login()
    try:
        rs = bs.query_trade_dates(start_date="2015-01-01", end_date=(dt.date.today()+dt.timedelta(days=365)).strftime("%Y-%m-%d"))
        dates = []
        while rs.next():
            row = rs.get_row_data()
            if row[1] == '1':
                dates.append(dt.date.fromisoformat(row[0]))
        bs.logout()
        return sorted(dates)
    except:
        bs.logout()
        raise

def _calendar_juejin() -> list[dt.date]:
    from gm.api import get_trading_dates_by_year
    dates = []
    for exch in ['SHSE', 'SZSE']:
        for y in range(2015, dt.date.today().year + 2):
            try:
                ret = get_trading_dates_by_year(exchange=exch, start_year=y, end_year=y)
                for item in ret:
                    d = item.trade_date if hasattr(item, 'trade_date') else item.get('trade_date')
                    if isinstance(d, str): d = dt.date.fromisoformat(d)
                    if isinstance(d, dt.datetime): d = d.date()
                    dates.append(d)
            except: pass
    return sorted(set(dates))

def _calendar_mysql() -> list[dt.date]:
    from db_config import pg_connect
    conn = pg_connect()
    try:
        with conn.cursor() as cur:
            cur.execute("SELECT DISTINCT trade_date FROM mkt.daily_bar ORDER BY trade_date")
            return [row[0] for row in cur.fetchall() if isinstance(row[0], dt.date)]
    finally:
        conn.close()

_CALENDAR_SOURCES = {
    'akshare': _calendar_akshare,
    'baostock': _calendar_baostock,
    'juejin': _calendar_juejin,
    'mysql': _calendar_mysql,  # 已改用 PG，保持兼容
    'pg': _calendar_mysql,
}

@lru_cache(maxsize=1)
def _get_cached_calendar(source: str) -> list[dt.date]:
    fn = _CALENDAR_SOURCES.get(source, _calendar_baostock)
    return fn()

def get_trade_calendar(force_source: Optional[str] = None) -> list[dt.date]:
    """获取交易日历，默认使用配置的源"""
    cfg = _load_config()
    source = force_source or cfg.get('calendar_source', 'mysql')
    return _get_cached_calendar(source)

def clear_calendar_cache():
    _get_cached_calendar.cache_clear()

# ═══════════════════════════════════════════
# 实时行情
# ═══════════════════════════════════════════

def get_realtime_quote_juejin(symbols: list[str]) -> dict:
    """掘金实时行情 (需要 token)"""
    from gm.api import current
    result = {}
    for sym in symbols:
        try:
            tick = current(sym)
            if tick:
                result[sym] = {
                    'price': float(tick.price),
                    'volume': float(tick.volume),
                    'high': float(tick.high),
                    'low': float(tick.low),
                    'open': float(tick.open),
                    'pre_close': float(tick.pre_close),
                    'timestamp': str(tick.created_at),
                }
        except: pass
    return result

def get_realtime_quote_akshare(symbols: list[str]) -> dict:
    """AKShare 实时行情"""
    import akshare as ak
    result = {}
    try:
        df = ak.stock_zh_a_spot_em()
        if df is not None and not df.empty:
            for sym in symbols:
                code = sym.split('.')[0]
                row = df[df['代码'] == code]
                if not row.empty:
                    r = row.iloc[0]
                    result[sym] = {
                        'price': float(r['最新价']),
                        'volume': float(r['成交量']),
                        'high': float(r['最高']),
                        'low': float(r['最低']),
                        'open': float(r['今开']),
                        'pre_close': float(r['昨收']),
                        'change_pct': float(r['涨跌幅']),
                    }
    except: pass
    return result

_REALTIME_SOURCES = {
    'juejin': get_realtime_quote_juejin,
    'akshare': get_realtime_quote_akshare,
}

def get_realtime_quote(symbols: list[str], force_source: Optional[str] = None) -> dict:
    cfg = _load_config()
    source = force_source or cfg.get('realtime_source', 'juejin')
    fn = _REALTIME_SOURCES.get(source)
    if fn: return fn(symbols)
    return {}

# ═══════════════════════════════════════════
# 历史日线
# ═══════════════════════════════════════════
_DAILY_SOURCES = ['baostock', 'juejin', 'akshare']
def daily_source() -> str:
    return _load_config().get('daily_source', 'baostock')

# ═══════════════════════════════════════════
# 统一配置接口
# ═══════════════════════════════════════════

class DataSource:
    @staticmethod
    def set_calendar(source: str):
        assert source in _CALENDAR_SOURCES, f"无效日历源: {source}, 可选: {list(_CALENDAR_SOURCES)}"
        cfg = _load_config()
        cfg['calendar_source'] = source
        _save_config(cfg)
        clear_calendar_cache()
        print(f"[DataSource] 交易日历 → {source}")

    @staticmethod
    def set_realtime(source: str):
        assert source in _REALTIME_SOURCES, f"无效实时行情源: {source}, 可选: {list(_REALTIME_SOURCES)}"
        cfg = _load_config()
        cfg['realtime_source'] = source
        _save_config(cfg)
        print(f"[DataSource] 实时行情 → {source}")

    @staticmethod
    def set_daily(source: str):
        assert source in _DAILY_SOURCES, f"无效日线源: {source}, 可选: {_DAILY_SOURCES}"
        cfg = _load_config()
        cfg['daily_source'] = source
        _save_config(cfg)
        print(f"[DataSource] 历史日线 → {source}")

    @staticmethod
    def show():
        cfg = _load_config()
        print(f"交易日历: {cfg.get('calendar_source', 'baostock')}")
        print(f"实时行情: {cfg.get('realtime_source', 'juejin')}")
        print(f"历史日线: {cfg.get('daily_source', 'baostock')}")

    @staticmethod
    def set_all(source: str):
        """一键全切: baostock / juejin / akshare"""
        if source == 'baostock':
            DataSource.set_calendar('baostock')
            DataSource.set_daily('baostock')
            DataSource.set_realtime('juejin')  # Baostock 无实时行情, 保留掘金
        elif source == 'juejin':
            DataSource.set_calendar('juejin')
            DataSource.set_daily('juejin')
            DataSource.set_realtime('juejin')
        elif source == 'akshare':
            DataSource.set_calendar('akshare')
            DataSource.set_daily('akshare')
            DataSource.set_realtime('akshare')
        else:
            print(f"未知预设: {source}, 可选: baostock/juejin/akshare")

if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description="数据源配置")
    p.add_argument('--show', action='store_true', help='显示当前配置')
    p.add_argument('--calendar', choices=list(_CALENDAR_SOURCES), help='切换交易日历源')
    p.add_argument('--realtime', choices=list(_REALTIME_SOURCES), help='切换实时行情源')
    p.add_argument('--daily', choices=_DAILY_SOURCES, help='切换历史日线源')
    p.add_argument('--all', choices=['baostock','juejin','akshare'], help='一键切换全部')
    args = p.parse_args()

    if args.show or not any([args.calendar, args.realtime, args.daily, args.all]):
        DataSource.show()
    if args.calendar: DataSource.set_calendar(args.calendar)
    if args.realtime: DataSource.set_realtime(args.realtime)
    if args.daily: DataSource.set_daily(args.daily)
    if args.all: DataSource.set_all(args.all)
