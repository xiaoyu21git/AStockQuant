"""
check_data_latest.py
检测数据库日线数据是否已更新到最近已收盘交易日。
"""

import datetime as dt
import os
import sys
from pathlib import Path

import pymysql

PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools.trading_day_utils import DEFAULT_MARKET_CLOSE_TIME, parse_time_text, resolve_latest_closed_trade_date

MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
}

def is_data_latest(close_time_text: str = DEFAULT_MARKET_CLOSE_TIME):
    target_date = resolve_latest_closed_trade_date(
        dt.datetime.now(),
        parse_time_text(close_time_text),
    )
    conn = pymysql.connect(**MYSQL_CONFIG)
    cur = conn.cursor()
    cur.execute("SELECT MAX(trade_date) FROM daily_bar")
    row = cur.fetchone()
    cur.close()
    conn.close()
    if row and row[0]:
        latest = row[0]
        return str(latest) == str(target_date)
    return False

if __name__ == "__main__":
    print(is_data_latest())
