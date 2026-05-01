from __future__ import annotations

import datetime as dt
import sys
from pathlib import Path

import pymysql

PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools.update_daily_data import BENCHMARK_INDEX_SYMBOLS, fetch_benchmark_daily, persist_daily_rows_with_fallback

MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
}


def get_target_date_range(conn):
    with conn.cursor() as cursor:
        cursor.execute("SELECT MIN(trade_date), MAX(trade_date) FROM cleaned_daily_bar")
        row = cursor.fetchone()
        if row and row[0] and row[1]:
            return row[0], row[1]

        cursor.execute("SELECT MIN(trade_date), MAX(trade_date) FROM daily_bar")
        row = cursor.fetchone()
        if row and row[0] and row[1]:
            return row[0], row[1]

    return None, None


def main() -> None:
    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        start_date, target_date = get_target_date_range(conn)
        if start_date is None or target_date is None:
            print("skip benchmark backfill: unable to resolve target date range from daily tables")
            return

        total_written = 0
        with conn.cursor() as cursor:
            for symbol, _ in BENCHMARK_INDEX_SYMBOLS:
                df = fetch_benchmark_daily(symbol, start_date, target_date)
                if df.empty:
                    print(f"skip {symbol}: no rows from {start_date} to {target_date}")
                    continue
                written_rows, _ = persist_daily_rows_with_fallback(symbol, df)
                total_written += written_rows
                conn.commit()
                print(f"updated {symbol}: {start_date}..{target_date} rows={written_rows}")
        print(f"done total_written={total_written}")
    finally:
        conn.close()


if __name__ == "__main__":
    main()
