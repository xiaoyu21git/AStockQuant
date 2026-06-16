#!/usr/bin/env python3
"""
统一日更脚本 — Baostock 日线 + 补全缺失字段
- 完全单线程，Baostock 安全
- 批量拉取 (1批1000只)
- 写入后自动补全 换手率/涨跌额/振幅
"""
from __future__ import annotations
import argparse, datetime as dt, os, sys, time, traceback
import pymysql
import pandas as pd

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import import_from_baostock as baostock

DB_CONFIG = {
    "host": "127.0.0.1", "port": 3306, "user": "root", "password": "123456a",
    "database": "astock_quant", "charset": "utf8mb4",
}
BATCH_SIZE = 500
SLEEP_BATCH = 0.2

def get_conn():
    return pymysql.connect(**DB_CONFIG)

def load_active_symbols(conn) -> list[str]:
    with conn.cursor() as cur:
        cur.execute("SELECT symbol FROM symbol_info WHERE status NOT IN ('DELISTED','退市') AND symbol LIKE '%.%' ORDER BY symbol")
        return [r[0] for r in cur.fetchall()]

def load_latest_dates(conn, symbols: list[str]) -> dict[str, str | None]:
    result = {}
    for i in range(0, len(symbols), 500):
        batch = symbols[i:i+500]
        placeholders = ','.join(['%s'] * len(batch))
        with conn.cursor() as cur:
            cur.execute(f"SELECT symbol, MAX(trade_date) FROM daily_bar WHERE symbol IN ({placeholders}) GROUP BY symbol", batch)
            for row in cur.fetchall():
                result[row[0]] = str(row[1]) if row[1] else None
    return result

def upsert_daily_bars(conn, df: pd.DataFrame) -> int:
    if df.empty: return 0
    sql = """INSERT INTO daily_bar (symbol, trade_date, open, high, low, close, pre_close, volume, turnover,
        change_pct, pe_ratio, pb_ratio, data_source)
        VALUES (%(symbol)s, %(trade_date)s, %(open)s, %(high)s, %(low)s, %(close)s, %(pre_close)s,
        %(volume)s, %(turnover)s, %(change_pct)s, %(pe_ratio)s, %(pb_ratio)s, %(data_source)s)
        ON DUPLICATE KEY UPDATE
        open=VALUES(open), high=VALUES(high), low=VALUES(low), close=VALUES(close),
        pre_close=VALUES(pre_close), volume=VALUES(volume), turnover=VALUES(turnover),
        change_pct=VALUES(change_pct), pe_ratio=VALUES(pe_ratio), pb_ratio=VALUES(pb_ratio),
        data_source=VALUES(data_source), updated_at=CURRENT_TIMESTAMP"""
    rows = df.to_dict('records')
    written = 0
    with conn.cursor() as cur:
        for row in rows:
            try:
                cur.execute(sql, {k: row.get(k) for k in ['symbol','trade_date','open','high','low','close','pre_close','volume','turnover','change_pct','pe_ratio','pb_ratio','data_source']})
                written += 1
            except Exception as e:
                if written < 5: print(f"  [warn] {row.get('symbol','?')} {row.get('trade_date','?')}: {e}", flush=True)
    conn.commit()
    return written

def backfill_turnover_rate(conn):
    with conn.cursor() as cur:
        cur.execute("""UPDATE daily_bar SET turnover_rate = ROUND(turnover / circulating_market_cap * 100, 4), updated_at=CURRENT_TIMESTAMP
            WHERE turnover_rate IS NULL AND turnover > 0 AND circulating_market_cap > 0""")
        n = cur.rowcount
    conn.commit()
    print(f"  turnover_rate: {n} rows backfilled", flush=True)

def backfill_change_amt_amplitude(conn):
    with conn.cursor() as cur:
        cur.execute("""UPDATE daily_bar SET change_amt = ROUND(close - pre_close, 4), updated_at=CURRENT_TIMESTAMP
            WHERE change_amt IS NULL AND close > 0 AND pre_close > 0""")
        ca = cur.rowcount
        cur.execute("""UPDATE daily_bar SET amplitude = ROUND((high - low) / pre_close * 100, 4), updated_at=CURRENT_TIMESTAMP
            WHERE amplitude IS NULL AND high > 0 AND low > 0 AND pre_close > 0""")
        am = cur.rowcount
    conn.commit()
    print(f"  change_amt: {ca}, amplitude: {am}", flush=True)

def backfill_adjust_factors(conn, symbols: list[str]):
    """补全复权因子 — 逐只从 Baostock 拉取 (API 限制每只一次)"""
    need = []
    for i in range(0, len(symbols), 500):
        batch = symbols[i:i+500]
        placeholders = ','.join(['%s'] * len(batch))
        with conn.cursor() as cur:
            cur.execute(f"SELECT DISTINCT symbol FROM daily_bar WHERE symbol IN ({placeholders}) AND (pre_adjust_factor IS NULL OR pre_adjust_factor=0) LIMIT 500", batch)
            need.extend([r[0] for r in cur.fetchall()])
    if not need:
        print("  adjust factors: all up to date", flush=True)
        return
    print(f"  adjust factors backfill: {len(need)} symbols (single-threaded, may take a while)", flush=True)
    if not baostock.login():
        print("  [warn] Baostock login failed for adjust factors", flush=True)
        return
    total = 0
    try:
        for idx, sym in enumerate(need):
            try:
                factors = baostock.fetch_adjust_factors(sym, dt.date(2015,1,1), dt.date.today())
                with conn.cursor() as cur:
                    for d, vals in factors.items():
                        pref = vals.get('pre_adjust_factor'); postf = vals.get('post_adjust_factor')
                        if pref and postf:
                            cur.execute("UPDATE daily_bar SET pre_adjust_factor=%s, post_adjust_factor=%s WHERE symbol=%s AND trade_date=%s",
                                      (float(pref), float(postf), sym, d.strftime('%Y-%m-%d')))
                conn.commit()
                total += 1
            except Exception as e:
                if idx < 5: print(f"  [warn] {sym}: {e}", flush=True)
            if (idx+1) % 100 == 0:
                print(f"  adjust factors progress: {idx+1}/{len(need)}", flush=True)
        print(f"  adjust factors: {total} symbols updated", flush=True)
    finally:
        baostock.logout()

def main():
    parser = argparse.ArgumentParser(description="统一日更 — Baostock 日线 + 字段补全")
    parser.add_argument("--date", help="目标日期 YYYY-MM-DD, 默认昨天")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--skip-backfill", action="store_true")
    args = parser.parse_args()

    target = args.date or (dt.date.today() - dt.timedelta(days=1)).strftime('%Y-%m-%d')
    print(f"[0] target date: {target}", flush=True)

    conn = get_conn()
    print("[1] loading symbols...", flush=True)
    symbols = load_active_symbols(conn)
    print(f"  {len(symbols)} active symbols", flush=True)

    print("[2] checking latest dates...", flush=True)
    latest = load_latest_dates(conn, symbols)
    need_update = [s for s in symbols if latest.get(s) is None or str(latest[s]) < target]
    print(f"  {len(need_update)} need update", flush=True)

    if need_update:
        print("[3] Baostock batch fetch (single-threaded)...", flush=True)
        if not baostock.login():
            print("[error] Baostock login failed", flush=True)
            conn.close(); sys.exit(1)
        try:
            fetched = written = 0
            for i in range(0, len(need_update), BATCH_SIZE):
                batch = need_update[i:i+BATCH_SIZE]
                sym_start = str(latest.get(batch[0], '2015-01-01'))
                start = min(sym_start, target)
                df = baostock.fetch_daily_k_data_batch(batch, dt.date.fromisoformat(start), dt.date.fromisoformat(target))
                if not df.empty:
                    df = baostock.normalize_baostock_frame(df)
                    df = df[df['trade_date'] <= target]
                    fetched += len(df)
                    if not args.dry_run:
                        w = upsert_daily_bars(conn, df)
                        written += w
                print(f"  batch {i//BATCH_SIZE+1}/{(len(need_update)+BATCH_SIZE-1)//BATCH_SIZE}: symbols={len(batch)} fetched={len(df) if not df.empty else 0} written={written}", flush=True)
                time.sleep(SLEEP_BATCH)
            print(f"[3 done] fetched={fetched} written={written}", flush=True)
        finally:
            baostock.logout()

    if not args.skip_backfill and not args.dry_run:
        print("[4] backfilling derived fields...", flush=True)
        backfill_change_amt_amplitude(conn)
        backfill_turnover_rate(conn)
    conn.close()
    print("[done]", flush=True)

if __name__ == '__main__':
    main()
