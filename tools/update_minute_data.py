"""
update_minute_data.py — AKShare 1分钟线 → mkt.minute_bar。
只拉1分钟，5/15/30/60通过SQL GROUP BY聚合。
用法: python tools/update_minute_data.py [--target-date YYYY-MM-DD] [--backfill --start YYYY-MM-DD]
"""

import sys, argparse, datetime as dt, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import akshare as ak, pandas as pd, psycopg2, psycopg2.extras
from tools.db_config import PG_CONFIG

UPSERT = """
INSERT INTO mkt.minute_bar(symbol_id,trade_ts,open,high,low,close,volume,amount)
VALUES(%(symbol_id)s,%(trade_ts)s,%(open)s,%(high)s,%(low)s,%(close)s,%(volume)s,%(amount)s)
ON CONFLICT(symbol_id,trade_ts) DO UPDATE SET
 open=EXCLUDED.open,high=EXCLUDED.high,low=EXCLUDED.low,close=EXCLUDED.close,volume=EXCLUDED.volume,amount=EXCLUDED.amount
"""

def fetch(symbol, date):
    """symbol: 纯数字如'000001', date: dt.date"""
    try:
        df = ak.stock_zh_a_hist_min_em(symbol=symbol, period="1",
            start_date=date.strftime("%Y-%m-%d"), end_date=date.strftime("%Y-%m-%d"), adjust="")
    except Exception:
        return []
    if df is None or df.empty:
        return []
    bars = []
    for _, r in df.iterrows():
        try:
            ts = pd.Timestamp(r["时间"]).to_pydatetime()
            bars.append({"trade_ts": ts, "open": float(r["开盘"] or 0), "high": float(r["最高"] or 0),
                         "low": float(r["最低"] or 0), "close": float(r["收盘"] or 0),
                         "volume": int(r["成交量"] or 0), "amount": float(r["成交额"] or 0)})
        except Exception:
            continue
    return bars

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--target-date", default="")
    p.add_argument("--backfill", action="store_true")
    p.add_argument("--start", default="")
    p.add_argument("--sleep", type=float, default=0.05)
    a = p.parse_args()
    target = dt.date.fromisoformat(a.target_date) if a.target_date else dt.date.today()

    conn = psycopg2.connect(**PG_CONFIG)
    cur = conn.cursor()
    cur.execute("SELECT id, symbol FROM ref.symbol_info WHERE status='ACTIVE' ORDER BY id")
    sid_map = {r[0]: r[1] for r in cur.fetchall()}

    if a.backfill and a.start:
        cur.execute("SELECT trade_date FROM ref.trade_calendar WHERE trade_date BETWEEN %s AND %s ORDER BY trade_date",
                    (dt.date.fromisoformat(a.start), target))
        dates = [r[0] for r in cur.fetchall()]
        print(f"[minute] 回填 {a.start}→{target} {len(dates)}天")
    else:
        dates = [target]
        print(f"[minute] {target}")

    total = 0
    for di, d in enumerate(dates):
        day_total = 0
        for sid, sym in sid_map.items():
            bars = fetch(sym.zfill(6), d)
            if bars:
                for b in bars: b["symbol_id"] = sid
                psycopg2.extras.execute_values(cur, UPSERT, bars, page_size=500)
                day_total += len(bars)
            time.sleep(a.sleep)
        total += day_total
        conn.commit()
        if len(dates) > 1:
            print(f"  [{di+1}/{len(dates)}] {d}: {day_total}")
    print(f"[minute] 完成 {total} 条")
    conn.close()

if __name__ == "__main__":
    main()
