#!/usr/bin/env python3
"""
日更流水线 ── 四阶段:
  Phase 1: 多线程补齐非Baostock字段 (市值/流通市值 ── AKShare)  (复权因子 ── Juejin)
  Phase 2: 本地SQL计算补全 (change_amt / amplitude / turnover_rate)
  Phase 3: Baostock 增量日线更新 (OHLCV/PE/PB ── 仅补缺口)
  Phase 4: 日线衍生字段回填 (周线/月线聚合)

每个阶段输出明确的:
  [开始] 阶段名 ── 补什么数据
  [检查] 需补标的数 / 需补行数
  [完成] 实际写入行数 / 耗时
"""
from __future__ import annotations
import argparse, datetime as dt, os, sys, time, traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import import_from_baostock as baostock
from db_config import pg_connect
import psycopg2, pandas as pd
from concurrent.futures import ThreadPoolExecutor, as_completed

BATCH = 50
SLEEP = 0.2

def conn(): return pg_connect()

def now(): return dt.datetime.now().strftime('%H:%M:%S')

def elapsed(start_time): return f"{time.time()-start_time:.1f}s"

def phase_header(phase: str, desc: str, target_cols: str):
    print(f"\n{'='*60}")
    print(f"[{now()}] Phase {phase} ── {desc}")
    print(f"        目标字段: {target_cols}")
    print(f"{'='*60}", flush=True)

def phase_done(phase: str, rows: int, elapsed_s: float):
    print(f"[{now()}] Phase {phase} 完成 ── 写入 {rows} 行 / 耗时 {elapsed_s:.1f}s", flush=True)

# ══════════════════════════════════════════════════
# 预加载 symbol → symbol_id 映射
# ══════════════════════════════════════════════════

def load_sym_to_id(c):
    with c.cursor() as cur:
        cur.execute("SELECT symbol, id FROM ref.symbol_info")
        return dict(cur.fetchall())

def sym_list(c, symbols):
    if not symbols: return {}
    with c.cursor() as cur:
        cur.execute("SELECT symbol, id FROM ref.symbol_info WHERE symbol = ANY(%s)", (list(symbols),))
        return {row[0]: row[1] for row in cur.fetchall()}


# ══════════════════════════════════════════════════
# Phase 1a: 市值 ── AKShare
# ══════════════════════════════════════════════════

def phase1a_market_cap(c, sym_to_id: dict[str, int]):
    """补 market_cap / circulating_market_cap ── AKShare"""
    phase_header("1a", "市值/流通市值 回填", "market_cap, circulating_market_cap")

    with c.cursor() as cur:
        cur.execute("SELECT DISTINCT symbol FROM mkt.daily_bar WHERE (market_cap IS NULL OR market_cap=0) AND symbol LIKE '%.%'")
        need = [r[0] for r in cur.fetchall()]
    print(f"[{now()}] [检查] 市值缺失标的: {len(need)}", flush=True)
    if not need:
        print(f"[{now()}] [跳过] 无缺失", flush=True)
        return

    import akshare as ak
    t0 = time.time()
    ok, fail, updated = 0, 0, 0

    def fetch_one(sym):
        try:
            code = sym.split('.')[0]
            df = ak.stock_individual_info_em(symbol=code)
            if df is None or df.empty: return None
            row = {r['item']: r['value'] for _, r in df.iterrows()}
            sid = sym_to_id.get(sym)
            if not sid: return None
            mc = row.get('总市值')
            cmc = row.get('流通市值')
            return {
                'symbol_id': sid,
                'market_cap': float(mc) if mc else None,
                'circulating_market_cap': float(cmc) if cmc else None,
            }
        except: return None

    print(f"[{now()}] [启动] AKShare 多线程, 8 workers", flush=True)
    with ThreadPoolExecutor(max_workers=8) as ex:
        futures = {ex.submit(fetch_one, s): s for s in need}
        for f in as_completed(futures):
            r = f.result()
            if r and r['market_cap']:
                try:
                    c2 = conn()
                    with c2.cursor() as cur:
                        cur.execute(
                            "UPDATE mkt.daily_bar SET market_cap=%s, circulating_market_cap=%s"
                            " WHERE symbol_id=%s AND trade_date="
                            "(SELECT MAX(trade_date) FROM mkt.daily_bar WHERE symbol_id=%s)",
                            (r['market_cap'], r['circulating_market_cap'], r['symbol_id'], r['symbol_id']))
                    c2.commit(); c2.close()
                    ok += 1; updated += 1
                except: fail += 1
            else:
                fail += 1

    phase_done("1a", updated, time.time()-t0)
    print(f"        成功={ok} 失败={fail}", flush=True)


# ══════════════════════════════════════════════════
# Phase 1b: 复权因子 ── Juejin
# ══════════════════════════════════════════════════

def phase1b_adjust_factors(c, sym_to_id: dict[str, int]):
    """补 pre_adjust_factor / post_adjust_factor ── Juejin API"""
    phase_header("1b", "复权因子 回填", "pre_adjust_factor, post_adjust_factor")

    with c.cursor() as cur:
        cur.execute("SELECT DISTINCT symbol FROM mkt.daily_bar WHERE (pre_adjust_factor IS NULL OR pre_adjust_factor=0) AND symbol LIKE '%.%'")
        need = [r[0] for r in cur.fetchall()]
    print(f"[{now()}] [检查] 复权因子缺失标的: {len(need)}", flush=True)
    if not need:
        print(f"[{now()}] [跳过] 无缺失", flush=True)
        return

    try:
        from gm.api import set_token
        set_token(os.environ.get('GM_TOKEN', ''))
    except: pass

    t0 = time.time()
    ok, fail, total_rows = 0, 0, 0

    def fetch_one(sym):
        try:
            from gm.api import history
            sid = sym_to_id.get(sym)
            if not sid: return None
            df = history(sym, '1d', '2015-01-01', dt.date.today().strftime('%Y-%m-%d'), adjust=1, df=True)
            if df is None or df.empty: return None
            return [(sid, str(row['bob'])[:10], float(row['pre_adjust_factor']), float(row['post_adjust_factor']))
                    for _, row in df.iterrows()
                    if row.get('pre_adjust_factor') and row.get('post_adjust_factor')]
        except: return None

    print(f"[{now()}] [启动] Juejin 多线程, 4 workers", flush=True)
    with ThreadPoolExecutor(max_workers=4) as ex:
        futures = {ex.submit(fetch_one, s): s for s in need}
        for f in as_completed(futures):
            rows = f.result()
            if rows:
                try:
                    c2 = conn()
                    with c2.cursor() as cur:
                        cur.executemany(
                            "UPDATE mkt.daily_bar SET pre_adjust_factor=%s, post_adjust_factor=%s"
                            " WHERE symbol_id=%s AND trade_date=%s",
                            [(pre, post, sid, td) for sid, td, pre, post in rows])
                    c2.commit(); c2.close()
                    ok += 1; total_rows += len(rows)
                except: fail += 1
            else:
                fail += 1

    phase_done("1b", total_rows, time.time()-t0)
    print(f"        标的成功={ok} 失败={fail}", flush=True)


# ══════════════════════════════════════════════════
# Phase 2: 本地计算补全
# ══════════════════════════════════════════════════

def phase2_local_backfill(c):
    """change_amt / amplitude / turnover_rate ── SQL 表达式"""
    phase_header("2", "本地SQL计算补全", "change_amt, amplitude, turnover_rate")

    updates = [
        ("change_amt",     "UPDATE mkt.daily_bar SET change_amt=ROUND(close-pre_close,4), updated_at=CURRENT_TIMESTAMP WHERE change_amt IS NULL AND close>0 AND pre_close>0"),
        ("amplitude",      "UPDATE mkt.daily_bar SET amplitude=ROUND((high-low)/pre_close*100,4), updated_at=CURRENT_TIMESTAMP WHERE amplitude IS NULL AND high>0 AND low>0 AND pre_close>0"),
        ("turnover_rate",  "UPDATE mkt.daily_bar SET turnover_rate=ROUND(turnover/circulating_market_cap*100,4), updated_at=CURRENT_TIMESTAMP WHERE turnover_rate IS NULL AND turnover>0 AND circulating_market_cap>0"),
    ]
    t0 = time.time()
    total = 0
    for name, sql in updates:
        print(f"[{now()}] [计算] {name} ...", flush=True)
        with c.cursor() as cur:
            cur.execute(sql)
            n = cur.rowcount
        c.commit()
        total += n
        print(f"        → {n} 行", flush=True)

    phase_done("2", total, time.time()-t0)


# ══════════════════════════════════════════════════
# Phase 3: Baostock 日线增量
# ══════════════════════════════════════════════════

def phase3_baostock_update(c, target_date: str, start_date: str = None):
    """日线 OHLCV/PE/PB ── Baostock 单线程增量"""
    gap_desc = f"历史缺口 [{start_date}..{target_date}]" if start_date else f"尾部缺口 → {target_date}"
    phase_header("3", f"Baostock 日线增量 ── {gap_desc}",
                 "open,high,low,close,pre_close,volume,turnover,change_pct,turnover_rate,pe_ratio,pb_ratio")

    with c.cursor() as cur:
        cur.execute("SELECT symbol FROM ref.symbol_info WHERE status NOT IN ('DELISTED','退市') AND symbol LIKE '%.%' ORDER BY symbol")
        symbols = [r[0] for r in cur.fetchall()]
    print(f"[{now()}] [检查] 全市场标的: {len(symbols)}", flush=True)

    latest: dict[str, str | None] = {}
    coverage: dict[str, int] = {}
    expected_per_sym: dict[str, int] = {}
    t0 = time.time()

    if start_date:
        import bisect
        with c.cursor() as cur:
            cur.execute("SELECT trade_date FROM ref.trade_calendar WHERE trade_date BETWEEN %s AND %s ORDER BY trade_date",
                       (start_date, target_date))
            all_tdays = [r[0] for r in cur.fetchall()]
        print(f"[{now()}] [检查] 区间交易日: {len(all_tdays)} 天", flush=True)

        with c.cursor() as cur:
            cur.execute(
                """SELECT si.symbol, si.list_date, MAX(d.trade_date), COUNT(DISTINCT d.trade_date)
                FROM ref.symbol_info si
                LEFT JOIN mkt.daily_bar d ON d.symbol_id = si.id
                    AND d.trade_date BETWEEN %s AND %s
                WHERE si.status NOT IN ('DELISTED','退市') AND si.symbol LIKE '%%.%%'
                GROUP BY si.symbol, si.list_date""",
                (start_date, target_date))
            for row in cur.fetchall():
                sym = row[0]
                list_date = row[1]
                latest[sym] = str(row[2]) if row[2] else None
                coverage[sym] = int(row[3]) if row[3] else 0
                effective_start = max(list_date or start_date, start_date)
                expected_per_sym[sym] = len(all_tdays) - bisect.bisect_left(all_tdays, effective_start)

        need = [
            s for s in symbols
            if latest.get(s) is None
            or str(latest[s]) < target_date
            or coverage.get(s, 0) < expected_per_sym.get(s, 0)
        ]
        no_data_count = sum(1 for s in need if latest.get(s) is None)
        tail_count     = sum(1 for s in need if latest.get(s) is not None and str(latest[s]) < target_date and coverage.get(s,0) >= expected_per_sym.get(s,0))
        internal_count = len(need) - no_data_count - tail_count
    else:
        with c.cursor() as cur:
            cur.execute(
                """SELECT si.symbol, MAX(d.trade_date)
                FROM mkt.daily_bar d
                JOIN ref.symbol_info si ON d.symbol_id = si.id
                WHERE si.status NOT IN ('DELISTED','退市') AND si.symbol LIKE '%%.%%'
                GROUP BY si.symbol""")
            for row in cur.fetchall():
                latest[row[0]] = str(row[1]) if row[1] else None
        need = [s for s in symbols if latest.get(s) is None or str(latest[s]) < target_date]
        no_data_count = sum(1 for s in need if latest.get(s) is None)
        tail_count = len(need) - no_data_count
        internal_count = 0

    print(f"[{now()}] [检查] 需更新标的: {len(need)} (无数据={no_data_count} 尾部缺口={tail_count} 内部缺口={internal_count})", flush=True)
    if not need:
        print(f"[{now()}] [跳过] 数据已是最新", flush=True)
        return

    if not baostock.login():
        print(f"[{now()}] [失败] Baostock 登录失败", flush=True)
        return
    print(f"[{now()}] [启动] Baostock 单线程, 每批{BATCH}只, 间隔{SLEEP}s", flush=True)

    fetched = written = 0
    try:
        for i in range(0, len(need), BATCH):
            batch = need[i:i+BATCH]
            if start_date:
                from datetime import timedelta
                starts = []
                for s in batch:
                    lat = latest.get(s)
                    cov = coverage.get(s, 0)
                    exp = expected_per_sym.get(s, 0)
                    if lat is None:
                        starts.append(start_date)
                    elif str(lat) < target_date and cov >= exp:
                        starts.append((dt.date.fromisoformat(str(lat)) + timedelta(days=1)).isoformat())
                    else:
                        starts.append(start_date)
                start = min(starts)
            else:
                batch_latest = [str(latest.get(s, target_date)) for s in batch]
                start = min(batch_latest)

            df = baostock.fetch_daily_k_data_batch(batch, dt.date.fromisoformat(start), dt.date.fromisoformat(target_date))
            if not df.empty:
                df = baostock.normalize_baostock_frame(df)
                if 'trade_date' in df.columns:
                    df['_target'] = pd.to_datetime(target_date)
                    df = df[pd.to_datetime(df['trade_date']) <= df['_target']].drop(columns=['_target'])
                    fetched += len(df)
                    w = upsert_baostock(c, df, batch)
                    written += w
            pct = (i+len(batch))*100//len(need)
            print(f"  [{now()}] {min(pct,100)}% batch {i//BATCH+1}: {start}..{target_date} symbols={len(batch)} fetched={len(df) if not df.empty else 0} total_written={written}", flush=True)
            time.sleep(SLEEP)
    finally:
        baostock.logout()

    phase_done("3", written, time.time()-t0)


def upsert_baostock(c, df: pd.DataFrame, batch_symbols: list[str]) -> int:
    if df.empty: return 0
    id_map = sym_list(c, df['symbol'].unique().tolist())

    sql = """INSERT INTO mkt.daily_bar (symbol_id,trade_date,open,high,low,close,pre_close,volume,turnover,change_pct,
        turnover_rate,pe_ratio,pb_ratio,data_source)
        VALUES (%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s)
        ON CONFLICT (symbol_id, trade_date) DO UPDATE SET
        open=EXCLUDED.open,high=EXCLUDED.high,low=EXCLUDED.low,close=EXCLUDED.close,
        pre_close=EXCLUDED.pre_close,volume=EXCLUDED.volume,turnover=EXCLUDED.turnover,
        change_pct=EXCLUDED.change_pct,turnover_rate=EXCLUDED.turnover_rate,
        pe_ratio=EXCLUDED.pe_ratio,pb_ratio=EXCLUDED.pb_ratio,
        data_source=EXCLUDED.data_source,updated_at=CURRENT_TIMESTAMP"""
    w = 0
    with c.cursor() as cur:
        for _, row in df.iterrows():
            sid = id_map.get(row.get('symbol'))
            if not sid: continue
            try:
                cur.execute(sql, (
                    sid,
                    row.get('trade_date'), row.get('open'), row.get('high'), row.get('low'),
                    row.get('close'), row.get('pre_close'), row.get('volume'), row.get('turnover'),
                    row.get('change_pct'), row.get('turnover_rate'), row.get('pe_ratio'),
                    row.get('pb_ratio'), row.get('data_source')
                ))
                w += 1
            except: pass
    c.commit()
    return w


# ══════════════════════════════════════════════════
# Phase 4: 周线/月线 聚合 (从日线派生)
# ══════════════════════════════════════════════════

def phase4_aggregate_bars(c, target_date: str):
    """从 daily_bar 聚合 weekly_bar / monthly_bar"""
    phase_header("4", "周线/月线 聚合", "weekly_bar, monthly_bar (OHLCV)")

    t0 = time.time()
    total = 0

    # 周线 (聚合上一完整周, trade_date=周日) + 历史缺口补齐
    with c.cursor() as cur:
        # 查询缺口
        cur.execute("SELECT COALESCE(MAX(trade_date),'1900-01-01')::date FROM mkt.weekly_bar")
        last_weekly = cur.fetchone()[0]
        need_weeks = []
        d = last_weekly + dt.timedelta(days=7)
        # 取目标日期的上一完整周(上周日)
        last_wanted = target_date - dt.timedelta(days=target_date.isoweekday() % 7)
        while d <= last_wanted:
            need_weeks.append(d)
            d += dt.timedelta(days=7)
        if need_weeks:
            print(f"[{now()}] 周线缺口: {last_weekly} → {need_weeks[-1]}, 补{len(need_weeks)}周")
        for sun in need_weeks:
            cur.execute(sql_weekly, (sun,))
            total += cur.rowcount
        c.commit()
    print(f"[{now()}] 周线 → {total} 行", flush=True)

    # 月线 (聚合上一完整月, trade_date=月末) + 历史缺口补齐
    with c.cursor() as cur:
        cur.execute("SELECT COALESCE(MAX(trade_date),'1900-01-01')::date FROM mkt.monthly_bar")
        last_monthly = cur.fetchone()[0]
        need_months = []
        d = last_monthly + dt.timedelta(days=1)
        d = d.replace(day=1) + dt.timedelta(days=32)
        d = d.replace(day=1) - dt.timedelta(days=1)  # 下一个月末
        # 取目标日期的上一完整月(上月月末)
        last_wanted = target_date.replace(day=1) - dt.timedelta(days=1)
        while d <= last_wanted:
            need_months.append(d)
            d += dt.timedelta(days=32)
            d = d.replace(day=1) + dt.timedelta(days=32)
            d = d.replace(day=1) - dt.timedelta(days=1)
        if need_months:
            print(f"[{now()}] 月线缺口: {last_monthly} → {need_months[-1]}, 补{len(need_months)}月")
        for last_day in need_months:
            cur.execute(sql_monthly, (last_day,))
            total += cur.rowcount
        c.commit()
    print(f"[{now()}] 月线 → {total} 行", flush=True)

    phase_done("4", total, time.time()-t0)


# ══════════════════════════════════════════════════
# main
# ══════════════════════════════════════════════════

def main():
    p = argparse.ArgumentParser(description="日更流水线")
    p.add_argument("--date", help="目标日期 YYYY-MM-DD")
    p.add_argument("--start-date", help="历史缺口起点 YYYY-MM-DD")
    p.add_argument("--phase1-only", action="store_true")
    p.add_argument("--phase3-only", action="store_true")
    p.add_argument("--skip-phase1", action="store_true")
    p.add_argument("--skip-phase4", action="store_true")
    args = p.parse_args()

    target = args.date or (dt.date.today()-dt.timedelta(days=1)).strftime('%Y-%m-%d')

    print(f"\n{'#'*60}")
    print(f"# 日更流水线  target={target}  start={args.start_date or '(尾部模式)'}")
    print(f"# 启动: {now()}")
    print(f"{'#'*60}", flush=True)

    c = conn()
    sym_to_id = load_sym_to_id(c)
    print(f"  symbol→id 映射: {len(sym_to_id)} 条", flush=True)

    if not args.phase3_only:
        if not args.skip_phase1:
            phase1a_market_cap(c, sym_to_id)
            phase1b_adjust_factors(c, sym_to_id)
        phase2_local_backfill(c)

    if not args.phase1_only:
        phase3_baostock_update(c, target, start_date=args.start_date)

    if not args.skip_phase4:
        phase4_aggregate_bars(c, target)

    c.close()
    print(f"\n[#] 流水线结束: {now()}", flush=True)


if __name__ == '__main__':
    main()
