from __future__ import annotations

import argparse
import datetime as dt
import sys
from pathlib import Path
from typing import Iterable, List, Optional, Set, Tuple

PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools.import_from_juejin import fetch_daily_bars_from_juejin, get_connection


GM_ENRICHED_SOURCE = "JUEJIN_GM_ENRICHED"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="使用 GM 回填 daily_bar 的估值/市值字段")
    parser.add_argument("--start-date", required=True, help="开始日期，格式 yyyy-mm-dd")
    parser.add_argument("--end-date", required=True, help="结束日期，格式 yyyy-mm-dd")
    parser.add_argument("--only-missing", action="store_true", help="只处理估值/市值缺失或非正数的记录")
    parser.add_argument("--limit-symbols", type=int, default=0, help="仅处理前 N 个股票，0 表示不限")
    return parser.parse_args()


def resolve_symbols(cursor, start_date: str, end_date: str, only_missing: bool, limit_symbols: int) -> List[str]:
    sql = """
    SELECT DISTINCT symbol
    FROM daily_bar
    WHERE trade_date BETWEEN %s AND %s
      AND symbol_info.asset_class = 'STOCK'
    """
    if only_missing:
        sql = """
        SELECT DISTINCT d.symbol
        FROM daily_bar d
        JOIN symbol_info ON symbol_info.symbol = d.symbol
        WHERE d.trade_date BETWEEN %s AND %s
          AND symbol_info.asset_class = 'STOCK'
          AND (
                                d.pe_ratio IS NULL OR d.pe_ratio = 0 OR
                                d.pb_ratio IS NULL OR d.pb_ratio = 0 OR
                d.market_cap IS NULL OR d.market_cap <= 0 OR
                d.circulating_market_cap IS NULL OR d.circulating_market_cap <= 0
          )
        """
    else:
        sql = """
        SELECT DISTINCT d.symbol
        FROM daily_bar d
        JOIN symbol_info ON symbol_info.symbol = d.symbol
        WHERE d.trade_date BETWEEN %s AND %s
          AND symbol_info.asset_class = 'STOCK'
        """

    sql += " ORDER BY 1"
    if limit_symbols > 0:
        sql += " LIMIT %s"
        cursor.execute(sql, (start_date, end_date, limit_symbols))
    else:
        cursor.execute(sql, (start_date, end_date))
    return [row[0] for row in cursor.fetchall()]


def load_missing_dates_for_symbol(cursor, symbol: str, start_date: str, end_date: str) -> Set[dt.date]:
    cursor.execute(
        """
        SELECT trade_date
        FROM daily_bar
        WHERE symbol = %s
          AND trade_date BETWEEN %s AND %s
          AND (
                pe_ratio IS NULL OR pe_ratio = 0 OR
                pb_ratio IS NULL OR pb_ratio = 0 OR
                market_cap IS NULL OR market_cap <= 0 OR
                circulating_market_cap IS NULL OR circulating_market_cap <= 0
          )
        ORDER BY trade_date
        """,
        (symbol, start_date, end_date),
    )
    return {row[0] for row in cursor.fetchall()}


def filter_rows(rows, start_date: dt.date, end_date: dt.date, target_dates: Optional[Set[dt.date]]) -> List[dict]:
    filtered = []
    for row in rows:
        trade_date = row.get("trade_date")
        if trade_date is None or trade_date < start_date or trade_date > end_date:
            continue
        if target_dates is not None and trade_date not in target_dates:
            continue
        filtered.append(row)
    return filtered


def build_updates(symbol: str, rows: Iterable[dict]) -> List[Tuple[object, ...]]:
    updates: List[Tuple[object, ...]] = []
    for row in rows:
        updates.append(
            (
                row.get("pe_ratio"),
                row.get("pb_ratio"),
                row.get("market_cap"),
                row.get("circulating_market_cap"),
                GM_ENRICHED_SOURCE,
                symbol,
                row["trade_date"],
            )
        )
    return updates


def apply_updates(cursor, updates: List[Tuple[object, ...]]) -> int:
    if not updates:
        return 0
    cursor.executemany(
        """
        UPDATE daily_bar
        SET pe_ratio = %s,
            pb_ratio = %s,
            market_cap = %s,
            circulating_market_cap = %s,
            updated_at = CURRENT_TIMESTAMP,
            data_source = CASE
                WHEN data_source IS NULL OR data_source = '' OR data_source = 'UNKNOWN' OR data_source = 'JUEJIN'
                    THEN %s
                ELSE data_source
            END
        WHERE symbol = %s AND trade_date = %s
        """,
        updates,
    )
    return len(updates)


def main() -> None:
    args = parse_args()
    start_date = dt.date.fromisoformat(args.start_date)
    end_date = dt.date.fromisoformat(args.end_date)

    conn = get_connection()
    total_updates = 0
    try:
        with conn.cursor() as cursor:
            symbols = resolve_symbols(cursor, args.start_date, args.end_date, args.only_missing, args.limit_symbols)

        print(
            f"gm backfill start: symbols={len(symbols)} range={args.start_date}..{args.end_date} only_missing={args.only_missing}",
            flush=True,
        )

        for index, symbol in enumerate(symbols, start=1):
            target_dates: Optional[Set[dt.date]] = None
            if args.only_missing:
                with conn.cursor() as cursor:
                    target_dates = load_missing_dates_for_symbol(cursor, symbol, args.start_date, args.end_date)
                if not target_dates:
                    print(
                        f"[{index}/{len(symbols)}] symbol={symbol} missing_dates=0 fetched=0 updated=0 total_updates={total_updates}",
                        flush=True,
                    )
                    continue

            rows = fetch_daily_bars_from_juejin(symbol, start_date, end_date)
            rows = filter_rows(rows, start_date, end_date, target_dates)
            updates = build_updates(symbol, rows)
            with conn.cursor() as cursor:
                updated = apply_updates(cursor, updates)
            conn.commit()
            total_updates += updated
            print(
                f"[{index}/{len(symbols)}] symbol={symbol} missing_dates={0 if target_dates is None else len(target_dates)} fetched={len(rows)} updated={updated} total_updates={total_updates}",
                flush=True,
            )

        print(f"gm backfill done: total_updates={total_updates}", flush=True)
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


if __name__ == "__main__":
    main()