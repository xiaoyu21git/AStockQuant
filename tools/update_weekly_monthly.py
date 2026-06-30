"""
update_weekly_monthly.py — 日线聚合周线/月线。
每个交易日都执行: 第一天INSERT, 中间UPDATE, 最后一天完成。
用法: python tools/update_weekly_monthly.py [--target-date YYYY-MM-DD]
"""

import sys, argparse, datetime as dt
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import psycopg2
from tools.db_config import PG_CONFIG

W_SQL = """
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

M_SQL = """
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

def is_trade_day(cur, d):
    cur.execute("SELECT 1 FROM ref.trade_calendar WHERE trade_date=%s", (d,))
    return cur.fetchone() is not None

def is_last_of_week(cur, d):
    cur.execute("SELECT NOT EXISTS(SELECT 1 FROM ref.trade_calendar WHERE trade_date>%s AND EXTRACT(WEEK FROM trade_date)=EXTRACT(WEEK FROM %s::date))", (d,d))
    return bool(cur.fetchone()[0])

def is_last_of_month(cur, d):
    cur.execute("SELECT NOT EXISTS(SELECT 1 FROM ref.trade_calendar WHERE trade_date>%s AND EXTRACT(MONTH FROM trade_date)=EXTRACT(MONTH FROM %s::date))", (d,d))
    return bool(cur.fetchone()[0])

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--target-date", default="")
    a = p.parse_args()
    target = dt.date.fromisoformat(a.target_date) if a.target_date else dt.date.today()

    conn = psycopg2.connect(**PG_CONFIG)
    cur = conn.cursor()
    if not is_trade_day(cur, target):
        print(f"[wl/ml] {target} 非交易日")
        conn.close(); return

    cur.execute(W_SQL, (target,)); wc = cur.rowcount
    cur.execute(M_SQL, (target,)); mc = cur.rowcount
    w_last = is_last_of_week(cur, target)
    m_last = is_last_of_month(cur, target)
    conn.commit()
    conn.close()

    w_tag = "完成" if w_last else "更新"
    m_tag = "完成" if m_last else "更新"
    print(f"[wl/ml] {target} 周线{w_tag} rows={wc}  月线{m_tag} rows={mc}")

if __name__ == "__main__":
    main()
