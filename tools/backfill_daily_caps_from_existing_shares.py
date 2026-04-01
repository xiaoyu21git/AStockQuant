from __future__ import annotations

import argparse
import bisect
import datetime as dt
import sys
from collections import defaultdict
from typing import DefaultDict, Dict, List, Optional, Sequence, Tuple

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


KnownSharePoint = Tuple[dt.date, Optional[float], Optional[float]]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="用 daily_bar 已有股本快照回填缺失的总市值/流通市值")
    parser.add_argument("--start-date", required=True, help="开始日期，格式 yyyy-mm-dd")
    parser.add_argument("--end-date", required=True, help="结束日期，格式 yyyy-mm-dd")
    parser.add_argument("--dry-run", action="store_true", help="仅统计和示例，不执行更新")
    parser.add_argument("--limit-sample", type=int, default=10, help="输出示例条数")
    return parser.parse_args()


def get_connection():
    return pymysql.connect(**MYSQL_CONFIG)


def load_target_symbols(cursor, start_date: str, end_date: str) -> List[str]:
    cursor.execute(
        """
        SELECT DISTINCT symbol
        FROM daily_bar
        WHERE trade_date BETWEEN %s AND %s
          AND close > 0
          AND (
                market_cap IS NULL OR market_cap <= 0 OR
                circulating_market_cap IS NULL OR circulating_market_cap <= 0
          )
        ORDER BY symbol
        """,
        (start_date, end_date),
    )
    return [str(row[0]) for row in cursor.fetchall()]


def load_missing_rows(cursor, start_date: str, end_date: str, symbols: Sequence[str]) -> List[Tuple[str, dt.date, float, Optional[float], Optional[float]]]:
    if not symbols:
                return []

    placeholders = ", ".join(["%s"] * len(symbols))
    cursor.execute(
                f"""
        SELECT symbol, trade_date, close, market_cap, circulating_market_cap
        FROM daily_bar
        WHERE trade_date BETWEEN %s AND %s
                    AND symbol IN ({placeholders})
          AND close > 0
          AND (
                market_cap IS NULL OR market_cap <= 0 OR
                circulating_market_cap IS NULL OR circulating_market_cap <= 0
          )
        ORDER BY symbol, trade_date
                """,
                (start_date, end_date, *symbols),
    )
    return list(cursor.fetchall())


def load_known_shares(cursor, symbols: Sequence[str]) -> DefaultDict[str, List[KnownSharePoint]]:
    result: DefaultDict[str, List[KnownSharePoint]] = defaultdict(list)
    if not symbols:
        return result

    placeholders = ", ".join(["%s"] * len(symbols))
    cursor.execute(
        f"""
        SELECT symbol,
               trade_date,
               close,
               market_cap,
               circulating_market_cap
        FROM daily_bar
        WHERE symbol IN ({placeholders})
          AND close > 0
          AND (
                (market_cap IS NOT NULL AND market_cap > 0) OR
                (circulating_market_cap IS NOT NULL AND circulating_market_cap > 0)
          )
        ORDER BY symbol, trade_date
        """,
        tuple(symbols),
    )

    for symbol, trade_date, close, market_cap, circulating_market_cap in cursor.fetchall():
        total_share = None
        circ_share = None
        if market_cap is not None and market_cap > 0 and close > 0:
            total_share = float(market_cap) / float(close)
        if circulating_market_cap is not None and circulating_market_cap > 0 and close > 0:
            circ_share = float(circulating_market_cap) / float(close)
        if total_share is None and circ_share is None:
            continue
        result[symbol].append((trade_date, total_share, circ_share))
    return result


def resolve_share(point_list: Sequence[KnownSharePoint], trade_date: dt.date) -> Tuple[Optional[float], Optional[float]]:
    if not point_list:
        return None, None

    dates = [point[0] for point in point_list]
    index = bisect.bisect_right(dates, trade_date) - 1
    if index < 0:
        index = 0

    total_share = None
    circ_share = None

    for scan in range(index, -1, -1):
        _, total_value, circ_value = point_list[scan]
        if total_share is None and total_value is not None and total_value > 0:
            total_share = total_value
        if circ_share is None and circ_value is not None and circ_value > 0:
            circ_share = circ_value
        if total_share is not None and circ_share is not None:
            break

    if total_share is None or circ_share is None:
        for scan in range(index + 1, len(point_list)):
            _, total_value, circ_value = point_list[scan]
            if total_share is None and total_value is not None and total_value > 0:
                total_share = total_value
            if circ_share is None and circ_value is not None and circ_value > 0:
                circ_share = circ_value
            if total_share is not None and circ_share is not None:
                break

    return total_share, circ_share


def build_updates(missing_rows: Sequence[Tuple[str, dt.date, float, Optional[float], Optional[float]]],
                  known_shares: Dict[str, List[KnownSharePoint]]) -> List[Tuple[Optional[float], Optional[float], str, dt.date]]:
    updates: List[Tuple[Optional[float], Optional[float], str, dt.date]] = []
    for symbol, trade_date, close, market_cap, circulating_market_cap in missing_rows:
        share_points = known_shares.get(symbol)
        if not share_points:
            continue

        total_share, circ_share = resolve_share(share_points, trade_date)
        new_market_cap = None
        new_circulating_market_cap = None

        if (market_cap is None or market_cap <= 0) and total_share is not None and total_share > 0:
            new_market_cap = round(float(close) * total_share, 4)
        elif market_cap is not None and market_cap > 0:
            new_market_cap = float(market_cap)

        if (circulating_market_cap is None or circulating_market_cap <= 0) and circ_share is not None and circ_share > 0:
            new_circulating_market_cap = round(float(close) * circ_share, 4)
        elif circulating_market_cap is not None and circulating_market_cap > 0:
            new_circulating_market_cap = float(circulating_market_cap)

        if ((market_cap is None or market_cap <= 0) and new_market_cap is not None) or \
           ((circulating_market_cap is None or circulating_market_cap <= 0) and new_circulating_market_cap is not None):
            updates.append((new_market_cap, new_circulating_market_cap, symbol, trade_date))
    return updates


def print_summary(missing_row_count: int,
                  candidate_update_count: int,
                  sample_updates: Sequence[Tuple[Optional[float], Optional[float], str, dt.date]],
                  limit_sample: int) -> None:
    print(f"missing_rows={missing_row_count} candidate_updates={candidate_update_count}", flush=True)
    if limit_sample <= 0:
        return
    print("SAMPLE_UPDATES", flush=True)
    for row in list(sample_updates)[:limit_sample]:
        print(row, flush=True)


def apply_updates(cursor, updates: Sequence[Tuple[Optional[float], Optional[float], str, dt.date]]) -> int:
    if not updates:
        return 0
    cursor.executemany(
        """
        UPDATE daily_bar
        SET market_cap = CASE
                WHEN (market_cap IS NULL OR market_cap <= 0) AND %s IS NOT NULL THEN %s
                ELSE market_cap
            END,
            circulating_market_cap = CASE
                WHEN (circulating_market_cap IS NULL OR circulating_market_cap <= 0) AND %s IS NOT NULL THEN %s
                ELSE circulating_market_cap
            END,
            updated_at = CURRENT_TIMESTAMP
        WHERE symbol = %s AND trade_date = %s
        """,
        [
            (market_cap, market_cap, circulating_market_cap, circulating_market_cap, symbol, trade_date)
            for market_cap, circulating_market_cap, symbol, trade_date in updates
        ],
    )
    return cursor.rowcount


def chunked(items: Sequence[str], chunk_size: int) -> List[Sequence[str]]:
    return [items[index:index + chunk_size] for index in range(0, len(items), chunk_size)]


def main() -> None:
    args = parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)

    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            target_symbols = load_target_symbols(cursor, args.start_date, args.end_date)

        print(f"target_symbols={len(target_symbols)}", flush=True)
        total_missing_rows = 0
        total_candidate_updates = 0
        total_updated_rows = 0
        sample_updates: List[Tuple[Optional[float], Optional[float], str, dt.date]] = []

        for batch_index, symbol_batch in enumerate(chunked(target_symbols, 100), start=1):
            with conn.cursor() as cursor:
                missing_rows = load_missing_rows(cursor, args.start_date, args.end_date, symbol_batch)
                known_shares = load_known_shares(cursor, symbol_batch)

            updates = build_updates(missing_rows, known_shares)
            total_missing_rows += len(missing_rows)
            total_candidate_updates += len(updates)
            if len(sample_updates) < args.limit_sample:
                sample_updates.extend(updates[: max(0, args.limit_sample - len(sample_updates))])

            if not args.dry_run and updates:
                with conn.cursor() as cursor:
                    updated_rows = apply_updates(cursor, updates)
                conn.commit()
                total_updated_rows += updated_rows

            if batch_index % 10 == 0 or batch_index == 1:
                print(
                    f"progress batch={batch_index} processed_symbols={min(batch_index * 100, len(target_symbols))}/{len(target_symbols)} "
                    f"missing_rows={total_missing_rows} candidate_updates={total_candidate_updates} updated_rows={total_updated_rows}",
                    flush=True,
                )

        print_summary(total_missing_rows, total_candidate_updates, sample_updates, args.limit_sample)

        if args.dry_run:
            conn.rollback()
            print(f"dry-run complete candidate_updates={total_candidate_updates}", flush=True)
            return

        print(f"updated_rows={total_updated_rows}", flush=True)
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


if __name__ == "__main__":
    main()