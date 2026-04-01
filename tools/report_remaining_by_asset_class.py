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


def print_section(cursor, name: str, sql: str) -> None:
    print(name)
    cursor.execute(sql)
    for row in cursor.fetchall():
        print(row)
    print()


def main() -> None:
    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            print_section(
                cursor,
                "turnover_rate_by_asset_class",
                """
                SELECT COALESCE(si.asset_class, 'UNKNOWN') AS asset_class, COUNT(1)
                FROM daily_bar db
                LEFT JOIN symbol_info si ON si.symbol = db.symbol
                WHERE db.turnover > 0 AND db.turnover_rate = 0
                GROUP BY COALESCE(si.asset_class, 'UNKNOWN')
                ORDER BY COUNT(1) DESC
                """,
            )
            print_section(
                cursor,
                "caps_by_asset_class",
                """
                SELECT COALESCE(si.asset_class, 'UNKNOWN') AS asset_class, COUNT(1)
                FROM daily_bar db
                LEFT JOIN symbol_info si ON si.symbol = db.symbol
                WHERE db.close > 0 AND (db.market_cap = 0 OR db.circulating_market_cap = 0)
                GROUP BY COALESCE(si.asset_class, 'UNKNOWN')
                ORDER BY COUNT(1) DESC
                """,
            )
            print_section(
                cursor,
                "pe_pb_by_asset_class",
                """
                SELECT COALESCE(si.asset_class, 'UNKNOWN') AS asset_class, COUNT(1)
                FROM daily_bar db
                LEFT JOIN symbol_info si ON si.symbol = db.symbol
                WHERE db.close > 0 AND (db.pe_ratio = 0 OR db.pb_ratio = 0)
                GROUP BY COALESCE(si.asset_class, 'UNKNOWN')
                ORDER BY COUNT(1) DESC
                """,
            )
            print_section(
                cursor,
                "index_samples",
                """
                SELECT db.symbol, db.trade_date, db.turnover_rate, db.market_cap,
                       db.circulating_market_cap, db.pe_ratio, db.pb_ratio, db.data_source
                FROM daily_bar db
                LEFT JOIN symbol_info si ON si.symbol = db.symbol
                WHERE COALESCE(si.asset_class, 'UNKNOWN') = 'INDEX'
                  AND (
                        (db.turnover > 0 AND db.turnover_rate = 0) OR
                        (db.close > 0 AND (db.market_cap = 0 OR db.circulating_market_cap = 0)) OR
                        (db.close > 0 AND (db.pe_ratio = 0 OR db.pb_ratio = 0))
                      )
                ORDER BY db.trade_date DESC, db.symbol ASC
                LIMIT 20
                """,
            )
            print_section(
                cursor,
                "non_index_samples",
                """
                SELECT db.symbol, db.trade_date, db.turnover_rate, db.market_cap,
                       db.circulating_market_cap, db.pe_ratio, db.pb_ratio, db.data_source
                FROM daily_bar db
                LEFT JOIN symbol_info si ON si.symbol = db.symbol
                WHERE COALESCE(si.asset_class, 'UNKNOWN') <> 'INDEX'
                  AND (
                        (db.turnover > 0 AND db.turnover_rate = 0) OR
                        (db.close > 0 AND (db.market_cap = 0 OR db.circulating_market_cap = 0)) OR
                        (db.close > 0 AND (db.pe_ratio = 0 OR db.pb_ratio = 0))
                      )
                ORDER BY db.trade_date DESC, db.symbol ASC
                LIMIT 20
                """,
            )
    finally:
        conn.close()


if __name__ == "__main__":
    main()
