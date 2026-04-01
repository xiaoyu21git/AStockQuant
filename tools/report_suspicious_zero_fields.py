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

SUSPICIOUS_METRICS = [
    (
        "volume_or_turnover_zero_with_price",
        "close > 0 AND (volume = 0 OR turnover = 0)",
    ),
    (
        "change_amt_zero_but_close_changed",
        "pre_close IS NOT NULL AND pre_close <> 0 AND close <> pre_close AND change_amt = 0",
    ),
    (
        "change_pct_zero_but_close_changed",
        "pre_close IS NOT NULL AND pre_close <> 0 AND close <> pre_close AND change_pct = 0",
    ),
    (
        "amplitude_zero_but_high_low_diff",
        "pre_close IS NOT NULL AND pre_close <> 0 AND high <> low AND amplitude = 0",
    ),
    (
        "turnover_rate_zero_but_turnover_positive",
        "turnover > 0 AND turnover_rate = 0",
    ),
    (
        "market_cap_zero_but_close_positive",
        "close > 0 AND market_cap = 0",
    ),
    (
        "circulating_cap_zero_but_close_positive",
        "close > 0 AND circulating_market_cap = 0",
    ),
    (
        "pe_ratio_zero_but_close_positive",
        "close > 0 AND pe_ratio = 0",
    ),
    (
        "pb_ratio_zero_but_close_positive",
        "close > 0 AND pb_ratio = 0",
    ),
]

RECENT_FILTER = "trade_date BETWEEN '2026-03-24' AND '2026-03-30'"


def count_by_metric(cursor: pymysql.cursors.Cursor, extra_filter: str | None = None) -> list[tuple[str, int]]:
    results: list[tuple[str, int]] = []
    for metric, where_clause in SUSPICIOUS_METRICS:
        sql = f"SELECT COUNT(1) FROM daily_bar WHERE {where_clause}"
        if extra_filter:
            sql += f" AND {extra_filter}"
        cursor.execute(sql)
        results.append((metric, int(cursor.fetchone()[0])))
    return results


def fetch_samples(cursor: pymysql.cursors.Cursor) -> list[tuple]:
    where_clauses = [clause for _, clause in SUSPICIOUS_METRICS]
    sql = f"""
        SELECT symbol, trade_date, close, pre_close, volume, turnover,
               change_pct, change_amt, amplitude, turnover_rate,
               pe_ratio, pb_ratio, market_cap, circulating_market_cap,
               data_source
        FROM daily_bar
        WHERE {' OR '.join(f'({clause})' for clause in where_clauses)}
        ORDER BY trade_date DESC, symbol
        LIMIT 20
    """
    cursor.execute(sql)
    return list(cursor.fetchall())


def main() -> None:
    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            cursor.execute("SELECT COUNT(1) FROM daily_bar")
            total_rows = int(cursor.fetchone()[0])
            print(f"total_rows\t{total_rows}")
            print("\nall_time")
            for metric, count in count_by_metric(cursor):
                print(f"{metric}\t{count}")

            print("\nrecent_2026_03_24_to_2026_03_30")
            for metric, count in count_by_metric(cursor, RECENT_FILTER):
                print(f"{metric}\t{count}")

            print("\nsamples")
            for row in fetch_samples(cursor):
                print(row)
    finally:
        conn.close()


if __name__ == "__main__":
    main()
