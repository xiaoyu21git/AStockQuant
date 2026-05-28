from __future__ import annotations

import argparse
import sys
from pathlib import Path

import pymysql


PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools.history_start_policy import UNIFIED_HISTORY_START_DATE


MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
    "autocommit": False,
}

TARGET_TABLE_SPECS = (
    ("cleaned_daily_bar", "trade_date"),
    ("daily_bar", "trade_date"),
    ("weekly_bar", "trade_date"),
    ("monthly_bar", "trade_date"),
    ("financial_indicator_daily", "trade_date"),
)

TABLE_SPEC_MAP = {table: (table, date_column) for table, date_column in TARGET_TABLE_SPECS}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="分批裁剪 2015-01-01 之前的日线数据")
    parser.add_argument("--batch-size", type=int, default=50000, help="单批删除行数，默认 50000")
    parser.add_argument("--check-only", action="store_true", help="只检查边界和待删行数，不执行删除")
    parser.add_argument(
        "--tables",
        nargs="+",
        choices=tuple(TABLE_SPEC_MAP.keys()),
        help="仅处理指定表；默认处理全部受管表",
    )
    return parser.parse_args()


def resolve_target_table_specs(selected_tables: list[str] | None) -> tuple[tuple[str, str], ...]:
    if not selected_tables:
        return TARGET_TABLE_SPECS
    return tuple(TABLE_SPEC_MAP[table] for table in selected_tables)


def get_connection():
    return pymysql.connect(**MYSQL_CONFIG)


def print_table_status(cursor, cutoff_date: str) -> None:
    for table, date_column in TARGET_TABLE_SPECS:
        cursor.execute(
            f"SELECT MIN({date_column}), MAX({date_column}), COUNT(1) FROM {table}"
        )
        bounds = cursor.fetchone()
        cursor.execute(
            f"SELECT COUNT(1) FROM {table} WHERE {date_column} < %s",
            (cutoff_date,),
        )
        stale_rows = cursor.fetchone()[0]
        print(f"{table}[{date_column}]: bounds={bounds} rows_lt_cutoff={stale_rows}", flush=True)


def trim_table(cursor, connection, table: str, date_column: str, cutoff_date: str, batch_size: int) -> None:
    total_deleted = 0
    while True:
        cursor.execute(
            f"DELETE FROM {table} WHERE {date_column} < %s LIMIT {batch_size}",
            (cutoff_date,),
        )
        deleted = cursor.rowcount
        connection.commit()
        total_deleted += deleted
        print(f"{table}[{date_column}]: batch_deleted={deleted} total_deleted={total_deleted}", flush=True)
        if deleted == 0:
            break


def main() -> None:
    args = parse_args()
    cutoff_date = UNIFIED_HISTORY_START_DATE.isoformat()
    target_table_specs = resolve_target_table_specs(args.tables)

    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            print(
                f"cutoff_date={cutoff_date} check_only={args.check_only} batch_size={args.batch_size} tables={[table for table, _ in target_table_specs]}",
                flush=True,
            )
            global TARGET_TABLE_SPECS
            original_specs = TARGET_TABLE_SPECS
            TARGET_TABLE_SPECS = target_table_specs
            print_table_status(cursor, cutoff_date)
            if args.check_only:
                return
            for table, date_column in TARGET_TABLE_SPECS:
                trim_table(cursor, conn, table, date_column, cutoff_date, args.batch_size)
            print_table_status(cursor, cutoff_date)
    finally:
        TARGET_TABLE_SPECS = original_specs if 'original_specs' in locals() else TARGET_TABLE_SPECS
        conn.close()


if __name__ == "__main__":
    main()