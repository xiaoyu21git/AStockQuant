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


def main() -> None:
    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            cursor.execute("SELECT MAX(trade_date) FROM daily_bar")
            latest = cursor.fetchone()[0]
            cursor.execute("SELECT COUNT(DISTINCT symbol) FROM daily_bar WHERE trade_date = %s", (latest,))
            symbol_count = cursor.fetchone()[0]
            cursor.execute("SELECT COUNT(*) FROM daily_bar WHERE trade_date = %s", (latest,))
            row_count = cursor.fetchone()[0]
            cursor.execute("SELECT COUNT(DISTINCT symbol) FROM symbol_info WHERE asset_class = 'STOCK' AND status = 'ACTIVE'")
            active_count = cursor.fetchone()[0]
            cursor.execute(
                """
                SELECT COUNT(DISTINCT d.symbol)
                FROM daily_bar d
                JOIN symbol_info s ON s.symbol = d.symbol
                WHERE s.asset_class = 'STOCK'
                  AND s.status = 'ACTIVE'
                  AND d.trade_date = (SELECT MAX(trade_date) FROM daily_bar)
                """
            )
            covered_active = cursor.fetchone()[0]
    finally:
        conn.close()

    print(
        f"latest_trade_date={latest} symbol_count={symbol_count} row_count={row_count} "
        f"active_stock_symbols={active_count} covered_active_on_latest={covered_active}"
    )


if __name__ == "__main__":
    main()
