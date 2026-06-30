"""
update_minute_data.py — 分析 minute_bar 缺口并回填。
最新模式: 只拉当日。历史模式: 自动找缺口逐日补。
用法: python tools/update_minute_data.py              # 当日
      python tools/update_minute_data.py --backfill    # 自动找缺口
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
            o = float(r.get("开盘", 0) or 0)
            c = float(r.get("收盘", 0) or 0)
            if o == 0: o = c
            bars.append({"trade_ts": ts, "open": o, "high": float(r.get("最高", 0) or 0),
                         "low": float(r.get("最低", 0) or 0), "close": c,
                         "volume": int(r.get("成交量", 0) or 0), "amount": float(r.get("成交额", 0) or 0)})
        except Exception:
            continue
    return bars

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--target-date", default="")
    p.add_argument("--backfill", action="store_true")
    p.add_argument("--sleep", type=float, default=0.05)
    a = p.parse_args()
    target = dt.date.fromisoformat(a.target_date) if a.target_date else dt.date.today()

    conn = psycopg2.connect(**PG_CONFIG)
    cur = conn.cursor()
    cur.execute("SELECT id, symbol FROM ref.symbol_info WHERE status='ACTIVE' ORDER BY id")
    sid_map = {r[0]: r[1] for r in cur.fetchall()}

    if a.backfill:
        # 分析缺口: 有日线但没分钟线的交易日
        cur.execute("""
            SELECT DISTINCT d.trade_date
            FROM mkt.daily_bar d
            WHERE NOT EXISTS (
                SELECT 1 FROM mkt.minute_bar m
                WHERE m.symbol_id=d.symbol_id AND m.trade_ts::date=d.trade_date
            ) AND d.trade_date <= %s
            ORDER BY d.trade_date
        """, (target,))
        dates = [r[0] for r in cur.fetchall()]
        if not dates:
            print("[minute] 无缺口, 数据完整")
            conn.close(); return
        print(f"[minute] 发现 {len(dates)} 天缺口 ({dates[0]}→{dates[-1]})")
    else:
        dates = [target]
        print(f"[minute] 当日 {target}")

    total = skipped = 0
    for di, d in enumerate(dates):
        day_total = 0
        for sid, sym in sid_map.items():
            cur.execute("SELECT 1 FROM mkt.minute_bar WHERE symbol_id=%s AND trade_ts::date=%s LIMIT 1", (sid, d))
            if cur.fetchone():
                skipped += 1; continue
            bars = fetch(sym.zfill(6), d)
            if bars:
                for b in bars: b["symbol_id"] = sid
                psycopg2.extras.execute_values(cur, UPSERT, bars, page_size=500)
                day_total += len(bars)
            time.sleep(a.sleep)
        total += day_total
        conn.commit()
        print(f"  [{di+1}/{len(dates)}] {d}: +{day_total}条 跳过{skipped}只")
    print(f"[minute] 完成 +{total}条")
    conn.close()

if __name__ == "__main__":
    main()
