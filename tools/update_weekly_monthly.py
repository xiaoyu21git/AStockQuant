"""
update_weekly_monthly.py — 日线聚合周线/月线。
最新: 只更新当前周期。历史: 分析缺口并回填。
用法: python tools/update_weekly_monthly.py
      python tools/update_weekly_monthly.py --backfill
"""

import sys, argparse, datetime as dt
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import psycopg2
from tools.db_config import PG_CONFIG

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
    p.add_argument("--backfill", action="store_true")
    a = p.parse_args()
    target = dt.date.fromisoformat(a.target_date) if a.target_date else dt.date.today()

    conn = psycopg2.connect(**PG_CONFIG)
    cur = conn.cursor()

    if a.backfill:
        # 分析缺口
        cur.execute("SELECT COUNT(*) FROM mkt.daily_bar")
        daily_rows = cur.fetchone()[0]
        cur.execute("SELECT COUNT(*) FROM mkt.weekly_bar")
        w_rows = cur.fetchone()[0]
        cur.execute("SELECT COUNT(*) FROM mkt.monthly_bar")
        m_rows = cur.fetchone()[0]
        print(f"[wl/ml] 日线{daily_rows}行 周线{w_rows}行 月线{m_rows}行")
        cur.execute(W_FULL, (target,)); wc = cur.rowcount
        cur.execute(M_FULL, (target,)); mc = cur.rowcount
        print(f"[wl/ml] 周线+{wc-w_rows} 月线+{mc-m_rows}")
    else:
        cur.execute(W_LATEST, (target, target)); wc = cur.rowcount
        cur.execute(M_LATEST, (target, target)); mc = cur.rowcount
        print(f"[wl/ml] {target} 周线{wc} 月线{mc}")

    conn.commit()
    conn.close()

if __name__ == "__main__":
    main()
