"""
update_weekly_monthly.py — 日线聚合周线/月线。
--latest: 只更新当前周/月 (日常跑)
--backfill: 全量重建 (首次/补历史)
用法: python tools/update_weekly_monthly.py [--target-date YYYY-MM-DD] [--latest|--backfill]
"""

import sys, argparse, datetime as dt
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import psycopg2
from tools.db_config import PG_CONFIG

# ── SQL: 当前周期(只查本周/本月日线) ──
W_LATEST = """
INSERT INTO mkt.weekly_bar (symbol_id, trade_date, open, high, low, close, volume, turnover)
SELECT d.symbol_id, MAX(d.trade_date),
       (ARRAY_AGG(d.open  ORDER BY d.trade_date ASC ))[1],
       MAX(d.high), MIN(d.low),
       (ARRAY_AGG(d.close ORDER BY d.trade_date DESC))[1],
       SUM(d.volume), SUM(d.turnover)
FROM mkt.daily_bar d
WHERE d.trade_date >= date_trunc('week', %s::date)
  AND d.trade_date <= %s
GROUP BY d.symbol_id
ON CONFLICT (symbol_id, trade_date) DO UPDATE SET
    open=EXCLUDED.open, high=EXCLUDED.high, low=EXCLUDED.low,
    close=EXCLUDED.close, volume=EXCLUDED.volume, turnover=EXCLUDED.turnover
"""

M_LATEST = """
INSERT INTO mkt.monthly_bar (symbol_id, trade_date, open, high, low, close, volume, turnover)
SELECT d.symbol_id, MAX(d.trade_date),
       (ARRAY_AGG(d.open  ORDER BY d.trade_date ASC ))[1],
       MAX(d.high), MIN(d.low),
       (ARRAY_AGG(d.close ORDER BY d.trade_date DESC))[1],
       SUM(d.volume), SUM(d.turnover)
FROM mkt.daily_bar d
WHERE d.trade_date >= date_trunc('month', %s::date)
  AND d.trade_date <= %s
GROUP BY d.symbol_id
ON CONFLICT (symbol_id, trade_date) DO UPDATE SET
    open=EXCLUDED.open, high=EXCLUDED.high, low=EXCLUDED.low,
    close=EXCLUDED.close, volume=EXCLUDED.volume, turnover=EXCLUDED.turnover
"""

# ── SQL: 全量重建 ──
W_FULL = """
INSERT INTO mkt.weekly_bar (symbol_id, trade_date, open, high, low, close, volume, turnover)
SELECT d.symbol_id, MAX(d.trade_date),
       (ARRAY_AGG(d.open  ORDER BY d.trade_date ASC ))[1],
       MAX(d.high), MIN(d.low),
       (ARRAY_AGG(d.close ORDER BY d.trade_date DESC))[1],
       SUM(d.volume), SUM(d.turnover)
FROM mkt.daily_bar d
WHERE d.trade_date <= %s
GROUP BY d.symbol_id, EXTRACT(YEAR FROM d.trade_date)*100+EXTRACT(WEEK FROM d.trade_date)
ON CONFLICT (symbol_id, trade_date) DO UPDATE SET
    open=EXCLUDED.open, high=EXCLUDED.high, low=EXCLUDED.low,
    close=EXCLUDED.close, volume=EXCLUDED.volume, turnover=EXCLUDED.turnover
"""

M_FULL = """
INSERT INTO mkt.monthly_bar (symbol_id, trade_date, open, high, low, close, volume, turnover)
SELECT d.symbol_id, MAX(d.trade_date),
       (ARRAY_AGG(d.open  ORDER BY d.trade_date ASC ))[1],
       MAX(d.high), MIN(d.low),
       (ARRAY_AGG(d.close ORDER BY d.trade_date DESC))[1],
       SUM(d.volume), SUM(d.turnover)
FROM mkt.daily_bar d
WHERE d.trade_date <= %s
GROUP BY d.symbol_id, EXTRACT(YEAR FROM d.trade_date)*100+EXTRACT(MONTH FROM d.trade_date)
ON CONFLICT (symbol_id, trade_date) DO UPDATE SET
    open=EXCLUDED.open, high=EXCLUDED.high, low=EXCLUDED.low,
    close=EXCLUDED.close, volume=EXCLUDED.volume, turnover=EXCLUDED.turnover
"""

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--target-date", default="")
    p.add_argument("--latest", action="store_true", default=True, help="只更新当前周/月 (默认)")
    p.add_argument("--backfill", action="store_true", help="全量重建历史")
    a = p.parse_args()
    target = dt.date.fromisoformat(a.target_date) if a.target_date else dt.date.today()

    conn = psycopg2.connect(**PG_CONFIG)
    cur = conn.cursor()

    if a.backfill:
        cur.execute(W_FULL, (target,)); wc = cur.rowcount
        cur.execute(M_FULL, (target,)); mc = cur.rowcount
        conn.commit(); conn.close()
        print(f"[wl/ml] 全量回填 周线{ wc} 月线{ mc}")
    else:
        cur.execute(W_LATEST, (target, target)); wc = cur.rowcount
        cur.execute(M_LATEST, (target, target)); mc = cur.rowcount
        conn.commit(); conn.close()
        print(f"[wl/ml] {target} 周线{ wc} 月线{ mc}")

if __name__ == "__main__":
    main()
