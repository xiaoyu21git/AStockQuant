from __future__ import annotations

import json
from pathlib import Path

import pymysql

MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
}

OUT_PATH = Path(__file__).with_name("remaining_by_asset_class.json")


def rows_to_dicts(cursor) -> list[dict[str, object]]:
    columns = [col[0] for col in cursor.description]
    return [dict(zip(columns, row)) for row in cursor.fetchall()]


def main() -> None:
    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            result: dict[str, object] = {}

            cursor.execute(
                """
                SELECT COALESCE(si.asset_class, 'UNKNOWN') AS asset_class, COUNT(1) AS count
                FROM daily_bar db
                LEFT JOIN symbol_info si ON si.symbol = db.symbol
                WHERE db.turnover > 0 AND db.turnover_rate = 0
                GROUP BY COALESCE(si.asset_class, 'UNKNOWN')
                ORDER BY count DESC
                """
            )
            result["turnover_rate_by_asset_class"] = rows_to_dicts(cursor)

            cursor.execute(
                """
                SELECT COALESCE(si.asset_class, 'UNKNOWN') AS asset_class, COUNT(1) AS count
                FROM daily_bar db
                LEFT JOIN symbol_info si ON si.symbol = db.symbol
                WHERE db.close > 0 AND (db.market_cap = 0 OR db.circulating_market_cap = 0)
                GROUP BY COALESCE(si.asset_class, 'UNKNOWN')
                ORDER BY count DESC
                """
            )
            result["caps_by_asset_class"] = rows_to_dicts(cursor)

            cursor.execute(
                """
                SELECT COALESCE(si.asset_class, 'UNKNOWN') AS asset_class, COUNT(1) AS count
                FROM daily_bar db
                LEFT JOIN symbol_info si ON si.symbol = db.symbol
                WHERE db.close > 0 AND (db.pe_ratio = 0 OR db.pb_ratio = 0)
                GROUP BY COALESCE(si.asset_class, 'UNKNOWN')
                ORDER BY count DESC
                """
            )
            result["pe_pb_by_asset_class"] = rows_to_dicts(cursor)

            cursor.execute(
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
                """
            )
            result["index_samples"] = rows_to_dicts(cursor)

            cursor.execute(
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
                """
            )
            result["non_index_samples"] = rows_to_dicts(cursor)

        OUT_PATH.write_text(json.dumps(result, ensure_ascii=False, indent=2, default=str), encoding="utf-8")
        print(str(OUT_PATH))
    finally:
        conn.close()


if __name__ == "__main__":
    main()
