#!/usr/bin/env python3
"""
日更流水线 — 三阶段：
  Phase 1: 多线程补齐非Baostock字段 (市值/复权因子) — AKShare/Juejin
  Phase 2: 本地计算补全 (change_amt/amplitude/turnover_rate) — SQL
  Phase 3: 单线程 Baostock 更新全部能获取的字段
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

# symbol → symbol_id 映射缓存
_sym_cache = {}
def sym_id(symbol):
    if symbol not in _sym_cache:
        c = conn()
        with c.cursor() as cur:
            cur.execute("SELECT id FROM ref.symbol_info WHERE symbol=%s", (symbol,))
            row = cur.fetchone()
            _sym_cache[symbol] = row[0] if row else None
        c.close()
    return _sym_cache[symbol]

# 批量 symbol → symbol_id（用于 IN 子句）
def sym_list(symbols):
    if not symbols: return {}
    c = conn()
    try:
        with c.cursor() as cur:
            cur.execute("SELECT symbol, id FROM ref.symbol_info WHERE symbol = ANY(%s)", (list(symbols),))
            return {row[0]: row[1] for row in cur.fetchall()}
    finally:
        c.close()

# ══════════════════════════════════════════════════
# Phase 1: 多线程补齐非 Baostock 字段
# ══════════════════════════════════════════════════

def backfill_market_cap_akshare(symbols: list[str]):
    """多线程从 AKShare 拉取市值/PE/PB"""
    import akshare as ak
    results = {"ok":0, "fail":0}
    def fetch_one(sym):
        try:
            code = sym.split('.')[0]
            df = ak.stock_individual_info_em(symbol=code)
            if df is None or df.empty: return None
            row = {r['item']: r['value'] for _, r in df.iterrows()}
            return {
                'symbol': sym,
                'market_cap': float(row.get('总市值', 0)) if row.get('总市值') else None,
                'circulating_market_cap': float(row.get('流通市值', 0)) if row.get('流通市值') else None,
            }
        except: return None

    print(f"  [Phase1/mcap] AKShare multi-threaded: {len(symbols)} symbols, workers=8", flush=True)
    with ThreadPoolExecutor(max_workers=8) as ex:
        futures = {ex.submit(fetch_one, s): s for s in symbols}
        for f in as_completed(futures):
            r = f.result()
            if r and r['market_cap']:
                try:
                    c = conn()
                    with c.cursor() as cur:
                        cur.execute("UPDATE mkt.daily_bar SET market_cap=%s, circulating_market_cap=%s WHERE symbol_id=(SELECT id FROM ref.symbol_info WHERE symbol=%s) AND trade_date=(SELECT MAX(trade_date) FROM mkt.daily_bar WHERE symbol_id=(SELECT id FROM ref.symbol_info WHERE symbol=%s)",
                                  (r['market_cap'], r['circulating_market_cap'], r['symbol'], r['symbol']))
                    c.commit(); c.close()
                    results['ok'] += 1
                except: results['fail'] += 1
    print(f"  [Phase1/mcap] done: ok={results['ok']} fail={results['fail']}", flush=True)

def backfill_adjust_factors_juejin(symbols: list[str]):
    """多线程从 Juejin 拉取复权因子"""
    try:
        from gm.api import set_token
        set_token(os.environ.get('GM_TOKEN', ''))
    except: pass

    results = {"ok":0, "fail":0}
    def fetch_one(sym):
        try:
            from gm.api import history
            df = history(sym, '1d', '2023-01-01', dt.date.today().strftime('%Y-%m-%d'), adjust=1, df=True)
            if df is None or df.empty: return None
            df['symbol'] = sym
            return df[['symbol','bob','eob','pre_adjust_factor','post_adjust_factor']].dropna()
        except: return None

    print(f"  [Phase1/adj] Juejin multi-threaded: {len(symbols)} symbols, workers=4", flush=True)
    if not symbols: return
    with ThreadPoolExecutor(max_workers=4) as ex:
        futures = {ex.submit(fetch_one, s): s for s in symbols}
        for f in as_completed(futures):
            r = f.result()
            if r is not None and not r.empty:
                try:
                    c = conn()
                    with c.cursor() as cur:
                        for _, row in r.iterrows():
                            cur.execute("UPDATE mkt.daily_bar SET pre_adjust_factor=%s, post_adjust_factor=%s WHERE symbol_id=(SELECT id FROM ref.symbol_info WHERE symbol=%s) AND trade_date=%s",
                                      (float(row['pre_adjust_factor']), float(row['post_adjust_factor']), row['symbol'], str(row['bob'])[:10]))
                    c.commit(); c.close()
                    results['ok'] += 1
                except: results['fail'] += 1
    print(f"  [Phase1/adj] done: ok={results['ok']} fail={results['fail']}", flush=True)

# ══════════════════════════════════════════════════
# Phase 2: 本地 SQL 计算补全
# ══════════════════════════════════════════════════

def phase2_local_backfill(c):
    print("[Phase2] local SQL backfill...", flush=True)
    updates = [
        ("change_amt", "UPDATE mkt.daily_bar SET change_amt=ROUND(close-pre_close,4), updated_at=CURRENT_TIMESTAMP WHERE change_amt IS NULL AND close>0 AND pre_close>0"),
        ("amplitude", "UPDATE mkt.daily_bar SET amplitude=ROUND((high-low)/pre_close*100,4), updated_at=CURRENT_TIMESTAMP WHERE amplitude IS NULL AND high>0 AND low>0 AND pre_close>0"),
        ("turnover_rate", "UPDATE mkt.daily_bar SET turnover_rate=ROUND(turnover/circulating_market_cap*100,4), updated_at=CURRENT_TIMESTAMP WHERE turnover_rate IS NULL AND turnover>0 AND circulating_market_cap>0"),
    ]
    for name, sql in updates:
        with c.cursor() as cur:
            cur.execute(sql)
            n = cur.rowcount
        c.commit()
        print(f"  {name}: {n} rows", flush=True)

# ══════════════════════════════════════════════════
# Phase 3: 单线程 Baostock 全量更新
# ══════════════════════════════════════════════════

def phase3_baostock_update(c, target_date: str):
    print("[Phase3] Baostock single-threaded update...", flush=True)
    with c.cursor() as cur:
        cur.execute("SELECT symbol FROM ref.symbol_info WHERE status NOT IN ('DELISTED','退市') AND symbol LIKE '%.%' ORDER BY symbol")
        symbols = [r[0] for r in cur.fetchall()]
    print(f"  {len(symbols)} symbols", flush=True)

    # 检查最新日期
    latest = {}
    print(f"  Checking latest dates (one pass)...", flush=True)
    with c.cursor() as cur:
        cur.execute("SELECT si.symbol, MAX(d.trade_date) FROM mkt.daily_bar d JOIN ref.symbol_info si ON d.symbol_id=si.id GROUP BY si.symbol")
        for row in cur.fetchall(): latest[row[0]] = str(row[1]) if row[1] else None
    need = [s for s in symbols if latest.get(s) is None or str(latest[s]) < target_date]
    print(f"  {len(need)} need update", flush=True)
    if not need:
        print("[Phase3] all up to date", flush=True)
        return

    if not baostock.login():
        print("[Phase3] Baostock login FAILED", flush=True)
        return

    fetched = written = 0
    try:
        for i in range(0, len(need), BATCH):
            batch = need[i:i+BATCH]
            sd = str(latest.get(batch[0], '2015-01-01'))
            print(f"  DEBUG batch0={batch[0]} sd={sd}", flush=True)
            start = min(sd, target_date)
            df = baostock.fetch_daily_k_data_batch(batch, dt.date.fromisoformat(start), dt.date.fromisoformat(target_date))
            if not df.empty:
                df = baostock.normalize_baostock_frame(df)
                if 'trade_date' not in df.columns:
                    print(f"  WARN: trade_date missing, cols={list(df.columns)[:10]}", flush=True)
                    continue
                df['_target'] = pd.to_datetime(target_date)
                df = df[pd.to_datetime(df['trade_date']) <= df['_target']].drop(columns=['_target'])
                fetched += len(df)
                w = upsert_baostock(c, df)
                written += w
            pct = (i+len(batch))*100//len(need) if need else 100
            print(f"  [{min(pct,100)}%] batch {i//BATCH+1}: fetched={len(df) if not df.empty else 0} written={written}", flush=True)
            time.sleep(SLEEP)
    finally:
        baostock.logout()
    print(f"[Phase3 done] fetched={fetched} written={written}", flush=True)

def upsert_baostock(c, df: pd.DataFrame) -> int:
    """只更新 Baostock 能提供的字段"""
    if df.empty: return 0
    # 先批量查 symbol → symbol_id 映射
    symbols = df['symbol'].unique().tolist()
    id_map = sym_list(symbols)

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
# main
# ══════════════════════════════════════════════════

def main():
    p = argparse.ArgumentParser(description="日更流水线: Phase1(多线程补非Baostock) Phase2(SQL补) Phase3(单线程Baostock全量更新)")
    p.add_argument("--date", help="目标日期 YYYY-MM-DD")
    p.add_argument("--phase1-only", action="store_true", help="仅运行Phase1")
    p.add_argument("--phase3-only", action="store_true", help="仅运行Phase3")
    p.add_argument("--skip-phase1", action="store_true")
    args = p.parse_args()

    target = args.date or (dt.date.today()-dt.timedelta(days=1)).strftime('%Y-%m-%d')
    print(f"=== daily_pipeline target={target} ===", flush=True)

    c = conn()

    if not args.phase3_only:
        # Phase 1: 多线程补齐非 Baostock 字段
        if not args.skip_phase1:
            print("--- Phase 1: Multi-threaded non-Baostock backfill ---", flush=True)
            with c.cursor() as cur:
                cur.execute("SELECT DISTINCT symbol FROM daily_bar WHERE (market_cap IS NULL OR market_cap=0) AND symbol LIKE '%.%'")
                mcap_need = [r[0] for r in cur.fetchall()]
            if mcap_need:
                backfill_market_cap_akshare(mcap_need)

            with c.cursor() as cur:
                cur.execute("SELECT DISTINCT symbol FROM daily_bar WHERE (pre_adjust_factor IS NULL OR pre_adjust_factor=0) AND symbol LIKE '%.%'")
                adj_need = [r[0] for r in cur.fetchall()]
            if adj_need:
                backfill_adjust_factors_juejin(adj_need)

        # Phase 2: 本地计算
        print("--- Phase 2: Local SQL backfill ---", flush=True)
        phase2_local_backfill(c)

    if not args.phase1_only:
        # Phase 3: 单线程 Baostock 全量更新
        print("--- Phase 3: Baostock single-threaded update ---", flush=True)
        phase3_baostock_update(c, target)

    c.close()
    print("=== done ===", flush=True)

if __name__ == '__main__':
    main()
