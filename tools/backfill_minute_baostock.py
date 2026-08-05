"""baostock 5分钟线回补 mkt.minute_bar"""
import sys, time, argparse
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import baostock as bs
import psycopg2, psycopg2.extras
from tools.db_config import PG_CONFIG

UPSERT = """
INSERT INTO mkt.minute_bar(symbol_id,trade_ts,open,high,low,close,volume,amount)
VALUES %s
ON CONFLICT(symbol_id,trade_ts) DO NOTHING
"""

def symbol_to_bs(sym: str) -> str:
    if "." not in sym: return ""
    code, ex = sym.split(".")
    if ex.upper() not in ("SH", "SZ", "BJ"): return ""
    return f"{ex.lower()}.{code}"

def fetch_stock(sid: int, sym: str, date_str: str, freq: str) -> list:
    bs_code = symbol_to_bs(sym)
    if not bs_code: return []
    rs = bs.query_history_k_data_plus(
        bs_code, "date,time,open,high,low,close,volume,amount",
        start_date=date_str, end_date=date_str, frequency=freq, adjustflag="3")
    if rs.error_code != "0": return []
    rows = []
    while rs.next():
        r = rs.get_row_data()
        o, h, l = float(r[2] or 0), float(r[3] or 0), float(r[4] or 0)
        c, v, amt = float(r[5] or 0), int(r[6] or 0), float(r[7] or 0)
        if c <= 0: continue
        raw = r[1]
        ts = f"{raw[:4]}-{raw[4:6]}-{raw[6:8]} {raw[8:10]}:{raw[10:12]}:{raw[12:14]}"
        rows.append((sid, ts, o, h, l, c, v, amt))
    return rows

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--start", default="2015-01-01")
    p.add_argument("--end", default="")
    p.add_argument("--freq", default="5", choices=["5","15","30","60"])
    p.add_argument("--batch", type=int, default=100)
    p.add_argument("--sleep", type=float, default=0.1, help="单股间隔秒")
    a = p.parse_args()

    end_date = a.end or time.strftime("%Y-%m-%d")
    bs.login()
    conn = psycopg2.connect(**PG_CONFIG)
    conn.autocommit = False
    cur = conn.cursor()

    cur.execute("SELECT id, symbol FROM ref.symbol_info WHERE status='ACTIVE' ORDER BY id")
    symbols = [(r[0], r[1]) for r in cur.fetchall()]
    print(f"活跃标的: {len(symbols)}")

    cur.execute("""
        SELECT d.trade_date::text FROM mkt.daily_bar d
        WHERE d.trade_date BETWEEN %s AND %s
          AND NOT EXISTS (SELECT 1 FROM mkt.minute_bar m
            WHERE m.symbol_id=d.symbol_id AND m.trade_ts::date=d.trade_date)
        GROUP BY d.trade_date ORDER BY d.trade_date
    """, (a.start, end_date))
    gap_dates = [r[0] for r in cur.fetchall()]
    conn.close()

    if not gap_dates:
        print("无缺口"); bs.logout(); return
    print(f"缺口: {len(gap_dates)} 天 ({gap_dates[0]} ~ {gap_dates[-1]})")

    total_bars = 0
    conn = psycopg2.connect(**PG_CONFIG)
    conn.autocommit = False

    for di, dt in enumerate(gap_dates):
        # 预检
        probe = bs.query_history_k_data_plus(
            "sh.600519", "date,time,close", start_date=dt, end_date=dt,
            frequency=a.freq, adjustflag="3")
        n = 0
        if probe.error_code == "0":
            while probe.next(): n += 1
        if n == 0:
            print(f"  [{di+1}/{len(gap_dates)}] {dt}: 无数据 (err={probe.error_code}), 跳过", flush=True)
            continue

        day_rows = []
        for sid, sym in symbols:
            rows = fetch_stock(sid, sym, dt, a.freq)
            if rows: day_rows.extend(rows)
            time.sleep(a.sleep)

        if day_rows:
            cur = conn.cursor()
            psycopg2.extras.execute_values(cur, UPSERT, day_rows, page_size=500)
            conn.commit()
            total_bars += len(day_rows)

        print(f"  [{di+1}/{len(gap_dates)}] {dt}: +{len(day_rows)}条 (累计{total_bars})", flush=True)

    conn.close()
    bs.logout()
    print(f"完成: {total_bars} 条")

if __name__ == "__main__":
    main()
