from __future__ import annotations

import pymysql

MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
}

INDEX_SYMBOLS = ["000300.SH", "000001.SH", "399001.SZ", "399006.SZ", "000905.SH", "000852.SH", "000016.SH"]


def main() -> None:
    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            cursor.execute("SELECT COUNT(*) FROM symbol_info WHERE asset_class='INDEX'")
            index_count = cursor.fetchone()[0]
            cursor.execute("SELECT COUNT(DISTINCT symbol) FROM daily_bar WHERE symbol IN %s", (tuple(INDEX_SYMBOLS),))
            covered_count = cursor.fetchone()[0]
            cursor.execute("SELECT MAX(trade_date) FROM daily_bar WHERE symbol IN %s", (tuple(INDEX_SYMBOLS),))
            latest_index_date = cursor.fetchone()[0]
            cursor.execute(
                """
                SELECT symbol, MAX(trade_date) AS latest_trade_date, COUNT(*) AS row_count
                FROM daily_bar
                WHERE symbol IN %s
                GROUP BY symbol
                ORDER BY symbol
                """,
                (tuple(INDEX_SYMBOLS),),
            )
            per_symbol = cursor.fetchall()
    finally:
        conn.close()

    print(f"index_symbol_count={index_count} benchmark_covered_count={covered_count} latest_index_trade_date={latest_index_date}")
    for symbol, latest_trade_date, row_count in per_symbol:
        print(f"{symbol} latest={latest_trade_date} rows={row_count}")


if __name__ == "__main__":
    main()
