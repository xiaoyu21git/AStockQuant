#!/usr/bin/env python3
"""同步交易日历到数据库 — C++ 侧通过 MarketDataRepository 查询"""
from __future__ import annotations
import datetime as dt, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from data_source_config import get_trade_calendar
import pymysql

DB = {"host":"127.0.0.1","port":3306,"user":"root","password":"123456a","database":"astock_quant","charset":"utf8mb4"}

def init_table(conn):
    with conn.cursor() as cur:
        cur.execute("""CREATE TABLE IF NOT EXISTS trade_calendar (
            trade_date DATE PRIMARY KEY,
            is_trading_day TINYINT(1) NOT NULL DEFAULT 1,
            pre_trade_date DATE NULL,
            next_trade_date DATE NULL,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4""")
    conn.commit()

def sync(conn, dates: list[dt.date]):
    init_table(conn)
    sorted_dates = sorted(dates)
    with conn.cursor() as cur:
        for i, d in enumerate(sorted_dates):
            pre = sorted_dates[i-1] if i > 0 else None
            nxt = sorted_dates[i+1] if i < len(sorted_dates)-1 else None
            cur.execute("""INSERT INTO trade_calendar (trade_date, is_trading_day, pre_trade_date, next_trade_date)
                VALUES (%s,1,%s,%s) ON DUPLICATE KEY UPDATE pre_trade_date=VALUES(pre_trade_date), next_trade_date=VALUES(next_trade_date)""",
                (d.strftime('%Y-%m-%d'), pre.strftime('%Y-%m-%d') if pre else None, nxt.strftime('%Y-%m-%d') if nxt else None))
    conn.commit()
    print(f"[sync] {len(dates)} trading days synced to trade_calendar")

if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description="同步交易日历到数据库")
    p.add_argument('--source', choices=['baostock','akshare','juejin'], default='baostock')
    args = p.parse_args()
    dates = get_trade_calendar(args.source)
    conn = pymysql.connect(**DB)
    sync(conn, dates)
    conn.close()
