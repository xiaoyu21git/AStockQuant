from __future__ import annotations

import pymysql


MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
    "autocommit": False,
}


def get_connection():
    return pymysql.connect(**MYSQL_CONFIG)


def print_status_summary(cursor, title: str) -> None:
    print(title)
    cursor.execute(
        "SELECT status, COUNT(*), SUM(delist_date IS NULL) FROM symbol_info GROUP BY status ORDER BY COUNT(*) DESC"
    )
    for row in cursor.fetchall():
        print(row)


def main() -> None:
    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            print_status_summary(cursor, "before:")

            cursor.execute(
                """
                UPDATE symbol_info si
                JOIN (
                    SELECT symbol, MAX(trade_date) AS latest_trade_date
                    FROM daily_bar
                    GROUP BY symbol
                ) db ON db.symbol = si.symbol
                SET si.status = 'ACTIVE'
                WHERE si.asset_class = 'STOCK'
                  AND si.status = 'DELISTED'
                  AND si.delist_date IS NULL
                  AND db.latest_trade_date >= DATE_SUB((SELECT MAX(trade_date) FROM daily_bar), INTERVAL 60 DAY)
                """
            )
            restored_recent = cursor.rowcount

            cursor.execute(
                """
                UPDATE symbol_info
                SET status = 'ACTIVE'
                WHERE asset_class = 'STOCK'
                  AND status = 'DELISTED'
                  AND delist_date IS NULL
                  AND symbol IN ('000001.SZ', '000002.SZ', '000006.SZ')
                """
            )
            restored_known = cursor.rowcount

            print(f"restored_recent={restored_recent}")
            print(f"restored_known={restored_known}")
            print_status_summary(cursor, "after:")

        conn.commit()
        print("repair_symbol_info_status: OK")
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


if __name__ == "__main__":
    main()