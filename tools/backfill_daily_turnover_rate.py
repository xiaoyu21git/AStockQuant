from __future__ import annotations

import argparse
import sys
from typing import Optional, Tuple

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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="按成交额/流通市值回填 daily_bar.turnover_rate(%)")
    parser.add_argument("--start-date", help="开始日期，格式 yyyy-mm-dd；默认取 daily_bar 最早日期")
    parser.add_argument("--end-date", help="结束日期，格式 yyyy-mm-dd；默认取 daily_bar 最晚日期")
    parser.add_argument("--dry-run", action="store_true", help="仅输出统计与示例，不执行更新")
    parser.add_argument("--limit-sample", type=int, default=10, help="输出回填前示例记录数")
    return parser.parse_args()


def get_connection():
    return pymysql.connect(**MYSQL_CONFIG)


def resolve_date_range(cursor, start_date: Optional[str], end_date: Optional[str]) -> Tuple[str, str]:
    cursor.execute("SELECT MIN(trade_date), MAX(trade_date) FROM daily_bar")
    row = cursor.fetchone()
    if not row or row[0] is None or row[1] is None:
        raise RuntimeError("daily_bar 无数据，无法回填 turnover_rate")

    resolved_start = start_date or row[0].isoformat()
    resolved_end = end_date or row[1].isoformat()
    return resolved_start, resolved_end


def print_summary(cursor, start_date: str, end_date: str) -> None:
    cursor.execute(
        """
        SELECT COUNT(*) AS total_rows,
               SUM(turnover_rate IS NULL) AS null_rows,
               SUM(turnover_rate = 0) AS zero_rows,
               SUM(turnover_rate > 0) AS positive_rows,
               SUM(turnover > 0 AND circulating_market_cap > 0) AS repairable_rows,
               SUM((turnover_rate IS NULL OR turnover_rate = 0) AND turnover > 0 AND circulating_market_cap > 0) AS target_rows
        FROM daily_bar
        WHERE trade_date BETWEEN %s AND %s
        """,
        (start_date, end_date),
    )
    summary = cursor.fetchone()
    print("SUMMARY", summary, flush=True)


def print_samples(cursor, start_date: str, end_date: str, limit_sample: int) -> None:
    if limit_sample <= 0:
        return

    cursor.execute(
        """
        SELECT symbol,
               trade_date,
               turnover,
               circulating_market_cap,
               turnover_rate,
               ROUND((turnover / circulating_market_cap) * 100, 4) AS implied_turnover_rate
        FROM daily_bar
        WHERE trade_date BETWEEN %s AND %s
          AND (turnover_rate IS NULL OR turnover_rate = 0)
          AND turnover > 0
          AND circulating_market_cap > 0
        ORDER BY trade_date ASC, symbol ASC
        LIMIT %s
        """,
        (start_date, end_date, limit_sample),
    )
    rows = cursor.fetchall()
    print("SAMPLE_ROWS", flush=True)
    for row in rows:
        print(row, flush=True)


def apply_backfill(cursor, start_date: str, end_date: str) -> int:
    cursor.execute(
        """
        UPDATE daily_bar
        SET turnover_rate = ROUND((turnover / circulating_market_cap) * 100, 4),
            updated_at = CURRENT_TIMESTAMP
        WHERE trade_date BETWEEN %s AND %s
          AND (turnover_rate IS NULL OR turnover_rate = 0)
          AND turnover > 0
          AND circulating_market_cap > 0
        """,
        (start_date, end_date),
    )
    return cursor.rowcount


def main() -> None:
    args = parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)

    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            start_date, end_date = resolve_date_range(cursor, args.start_date, args.end_date)
            print(f"backfill_daily_turnover_rate: range={start_date}..{end_date} dry_run={args.dry_run}", flush=True)
            print_summary(cursor, start_date, end_date)
            print_samples(cursor, start_date, end_date, args.limit_sample)

            if args.dry_run:
                conn.rollback()
                print("dry-run complete", flush=True)
                return

            updated_rows = apply_backfill(cursor, start_date, end_date)
            conn.commit()
            print(f"updated_rows={updated_rows}", flush=True)

        with conn.cursor() as cursor:
            print_summary(cursor, start_date, end_date)
            cursor.execute(
                """
                SELECT MIN(turnover_rate), AVG(turnover_rate), MAX(turnover_rate)
                FROM daily_bar
                WHERE trade_date BETWEEN %s AND %s
                  AND turnover_rate > 0
                """,
                (start_date, end_date),
            )
            print("POST_UPDATE_STATS", cursor.fetchone(), flush=True)
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


if __name__ == "__main__":
    main()