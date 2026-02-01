import datetime as dt

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
    conn = pymysql.connect(
        host=MYSQL_CONFIG["host"],
        port=MYSQL_CONFIG["port"],
        user=MYSQL_CONFIG["user"],
        password=MYSQL_CONFIG["password"],
        database=MYSQL_CONFIG["database"],
        charset=MYSQL_CONFIG["charset"],
    )
    cur = conn.cursor()

    queries = [
        ("symbol_count", "SELECT COUNT(*) FROM symbol_info"),
        ("daily_bar_count", "SELECT COUNT(*) FROM daily_bar"),
        (
            "daily_bar_latest",
            "SELECT symbol, trade_date, open, close, volume "
            "FROM daily_bar ORDER BY trade_date DESC LIMIT 10",
        ),
    ]

    for name, sql in queries:
        print(f"\n== {name} ==")
        cur.execute(sql)
        rows = cur.fetchall()
        for row in rows:
            print(row)

    cur.close()
    conn.close()


if __name__ == "__main__":
    main()
