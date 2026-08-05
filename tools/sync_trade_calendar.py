#!/usr/bin/env python3
"""同步交易日历到 PostgreSQL — C++ 侧通过 MarketDataRepository 查询"""
from __future__ import annotations
import datetime as dt, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from data_source_config import get_trade_calendar
import psycopg2

DB = {"host":"127.0.0.1","port":5432,"user":"astock","password":"astock123","dbname":"astock_quant"}

def init_table(conn):
    with conn.cursor() as cur:
        cur.execute("""CREATE TABLE IF NOT EXISTS data.trade_calendar (
            trade_date DATE PRIMARY KEY,
            is_trading_day SMALLINT NOT NULL DEFAULT 1,
            pre_trade_date DATE,
            next_trade_date DATE,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )""")
    conn.commit()

def sync(conn, dates: list[dt.date]):
    init_table(conn)
    sorted_dates = sorted(dates)
    with conn.cursor() as cur:
        for i, d in enumerate(sorted_dates):
            pre = sorted_dates[i-1] if i > 0 else None
            nxt = sorted_dates[i+1] if i < len(sorted_dates)-1 else None
            cur.execute("""INSERT INTO data.trade_calendar (trade_date, is_trading_day, pre_trade_date, next_trade_date)
                VALUES (%s,1,%s,%s)
                ON CONFLICT (trade_date) DO UPDATE SET
                pre_trade_date=EXCLUDED.pre_trade_date,
                next_trade_date=EXCLUDED.next_trade_date""",
                (d.strftime('%Y-%m-%d'), pre.strftime('%Y-%m-%d') if pre else None, nxt.strftime('%Y-%m-%d') if nxt else None))
    conn.commit()
    print(f"[sync] {len(dates)} trading days synced to trade_calendar")

if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description="同步交易日历到 PostgreSQL")
    p.add_argument('--source', choices=['baostock','akshare','juejin'], default='baostock')
    args = p.parse_args()
    dates = get_trade_calendar(args.source)
    conn = psycopg2.connect(**DB)
    sync(conn, dates)
    conn.close()
