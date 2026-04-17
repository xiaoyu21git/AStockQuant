from __future__ import annotations

import argparse
import datetime as dt
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

import pymysql


PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools.a_share_symbol_utils import classify_mainland_stock_symbol
from tools.import_from_juejin import fetch_all_mainland_stock_symbols_from_juejin, fetch_daily_bars_from_juejin
from tools.symbol_status_utils import normalize_symbol_status
from tools.trading_day_utils import DEFAULT_MARKET_CLOSE_TIME, parse_time_text, resolve_latest_closed_trade_date


MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
    "autocommit": False,
}

ACTIVE_DELIST_SENTINEL_YEAR = 2030


@dataclass
class SymbolRow:
    symbol: str
    name: str
    exchange: str
    asset_class: str
    list_date: Optional[dt.date]
    delist_date: Optional[dt.date]
    status: str
    latest_trade_date: Optional[dt.date]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="修复 symbol_info 的 status / delist_date / name 元数据")
    parser.add_argument("--dry-run", action="store_true", help="仅输出变更摘要，不写入数据库")
    parser.add_argument(
        "--close-time",
        default=DEFAULT_MARKET_CLOSE_TIME,
        help="停牌推断使用的目标收盘时间，格式 HH:MM，默认 15:30",
    )
    parser.add_argument(
        "--suspension-gap-trading-days",
        type=int,
        default=3,
        help="连续缺失至少多少个交易日才会被推断为停牌，默认 3",
    )
    parser.add_argument(
        "--sample-limit",
        type=int,
        default=20,
        help="最多打印多少条变更样本，默认 20",
    )
    return parser.parse_args()


def get_connection():
    return pymysql.connect(**MYSQL_CONFIG)


def print_status_summary(cursor, title: str) -> None:
    print(title)
    cursor.execute(
        "SELECT status, COUNT(*), SUM(delist_date IS NULL) FROM symbol_info GROUP BY status ORDER BY COUNT(*) DESC"
    )
    for row in cursor.fetchall():
        print(row)


def normalize_remote_delist_date(value: Optional[dt.date]) -> Optional[dt.date]:
    if value is None:
        return None
    if value.year >= ACTIVE_DELIST_SENTINEL_YEAR:
        return None
    return value


def classify_name_status(name: str) -> str:
    text = str(name or "").strip().upper()
    if not text:
        return "ACTIVE"
    if text.startswith("*ST"):
        return "*ST"
    if text.startswith("ST"):
        return "ST"
    return "ACTIVE"


def load_latest_trade_date_by_symbol(cursor) -> tuple[dict[str, Optional[dt.date]], dt.date]:
    cursor.execute("SELECT MAX(trade_date) FROM daily_bar")
    target_date = cursor.fetchone()[0]
    cursor.execute(
        """
        SELECT symbol, MAX(trade_date) AS latest_trade_date
        FROM daily_bar
        GROUP BY symbol
        """
    )
    latest_by_symbol: dict[str, Optional[dt.date]] = {}
    for symbol, latest_trade_date in cursor.fetchall():
        latest_by_symbol[str(symbol).strip()] = latest_trade_date
    if target_date is None:
        raise RuntimeError("daily_bar 为空，无法修复 symbol_info 状态")
    return latest_by_symbol, target_date


def load_symbol_rows(cursor, latest_by_symbol: dict[str, Optional[dt.date]]) -> list[SymbolRow]:
    cursor.execute(
        """
        SELECT symbol, name, exchange, asset_class, list_date, delist_date, status
        FROM symbol_info
        WHERE asset_class = 'STOCK'
        ORDER BY symbol
        """
    )
    rows: list[SymbolRow] = []
    for symbol, name, exchange, asset_class, list_date, delist_date, status in cursor.fetchall():
        symbol_text = str(symbol).strip()
        rows.append(
            SymbolRow(
                symbol=symbol_text,
                name=str(name or symbol_text).strip(),
                exchange=str(exchange or "").strip(),
                asset_class=str(asset_class or "STOCK").strip(),
                list_date=list_date,
                delist_date=delist_date,
                status=normalize_symbol_status(status),
                latest_trade_date=latest_by_symbol.get(symbol_text),
            )
        )
    return rows


def trading_day_gap_count(latest_trade_date: dt.date, target_date: dt.date) -> int:
    if latest_trade_date >= target_date:
        return 0
    current = latest_trade_date
    gap_count = 0
    while current < target_date:
        current += dt.timedelta(days=1)
        if current.weekday() < 5:
            gap_count += 1
    return gap_count


def should_mark_suspended(
    symbol: str,
    latest_trade_date: Optional[dt.date],
    target_date: dt.date,
    gap_threshold: int,
) -> bool:
    if latest_trade_date is None or latest_trade_date >= target_date:
        return False
    if trading_day_gap_count(latest_trade_date, target_date) < gap_threshold:
        return False
    try:
        rows = fetch_daily_bars_from_juejin(symbol, latest_trade_date + dt.timedelta(days=1), target_date)
    except Exception:
        return False
    return len(rows or []) == 0


def determine_target_state(
    row: SymbolRow,
    remote_row: Optional[dict],
    target_date: dt.date,
    gap_threshold: int,
) -> tuple[str, str, Optional[dt.date], str]:
    remote_name = str((remote_row or {}).get("name") or row.name).strip()
    remote_delist_date = normalize_remote_delist_date((remote_row or {}).get("delist_date"))
    current_delist_date = row.delist_date if row.delist_date and row.delist_date <= target_date else None
    next_delist_date = remote_delist_date or current_delist_date

    if next_delist_date is not None and next_delist_date <= target_date:
        return remote_name, "DELISTED", next_delist_date, "delist_date"

    if remote_name.endswith("退") or row.name.endswith("退"):
        return remote_name, "DELISTED", next_delist_date, "delist_name"

    name_status = classify_name_status(remote_name)
    if name_status in {"ST", "*ST"}:
        return remote_name, name_status, next_delist_date, "name_prefix"

    share_type = classify_mainland_stock_symbol(row.symbol)
    if share_type in {"A", "BJ"} and should_mark_suspended(row.symbol, row.latest_trade_date, target_date, gap_threshold):
        return remote_name, "SUSPENDED", next_delist_date, "no_trade_after_latest"

    return remote_name, "ACTIVE", next_delist_date, "remote_active"


def main() -> None:
    args = parse_args()
    today_target = resolve_latest_closed_trade_date(dt.datetime.now(), parse_time_text(args.close_time))
    remote_symbols = fetch_all_mainland_stock_symbols_from_juejin(include_b_shares=True)
    remote_by_symbol = {
        str(item.get("symbol") or "").strip(): item
        for item in remote_symbols
        if item.get("symbol")
    }

    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            latest_by_symbol, latest_db_trade_date = load_latest_trade_date_by_symbol(cursor)
            target_date = min(today_target, latest_db_trade_date)
            rows = load_symbol_rows(cursor, latest_by_symbol)

            print_status_summary(cursor, "before:")

            changes: list[tuple[str, str, str, Optional[dt.date], str, str, Optional[dt.date], str]] = []
            for row in rows:
                remote_row = remote_by_symbol.get(row.symbol)
                next_name, next_status, next_delist_date, reason = determine_target_state(
                    row,
                    remote_row,
                    target_date,
                    args.suspension_gap_trading_days,
                )
                if (
                    next_name != row.name
                    or next_status != row.status
                    or next_delist_date != row.delist_date
                ):
                    changes.append(
                        (
                            row.symbol,
                            row.name,
                            next_name,
                            row.delist_date,
                            next_delist_date,
                            row.status,
                            next_status,
                            reason,
                        )
                    )

            print(f"target_trade_date={target_date} remote_symbol_count={len(remote_by_symbol)} pending_changes={len(changes)}")
            for sample in changes[:args.sample_limit]:
                symbol, old_name, new_name, old_delist, new_delist, old_status, new_status, reason = sample
                print(
                    f"- {symbol}: status {old_status}->{new_status}, delist_date {old_delist}->{new_delist}, "
                    f"name {old_name}->{new_name}, reason={reason}"
                )

            if not args.dry_run and changes:
                cursor.executemany(
                    """
                    UPDATE symbol_info
                    SET name = %s,
                        delist_date = %s,
                        status = %s,
                        updated_at = CURRENT_TIMESTAMP
                    WHERE symbol = %s
                    """,
                    [
                        (new_name, new_delist, new_status, symbol)
                        for symbol, _old_name, new_name, _old_delist, new_delist, _old_status, new_status, _reason in changes
                    ],
                )

            if not args.dry_run:
                print_status_summary(cursor, "after:")

        if args.dry_run:
            conn.rollback()
            print("repair_symbol_info_status: DRY RUN")
        else:
            conn.commit()
            print("repair_symbol_info_status: OK")
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


if __name__ == "__main__":
    main()