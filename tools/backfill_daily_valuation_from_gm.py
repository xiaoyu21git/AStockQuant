from __future__ import annotations

import argparse
import datetime as dt
import sys
import time
from pathlib import Path
from typing import Iterable, List, Optional, Set, Tuple

PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools.import_from_juejin import fetch_daily_bars_from_juejin, get_connection
from tools.daily_bar_quality import format_invalid_samples, sanitize_valuation_record


GM_ENRICHED_SOURCE = "JUEJIN_GM_ENRICHED"
MAX_FETCH_RETRIES = 3
INVALID_SAMPLE_LIMIT = 3


def _normalize_numeric(value) -> Optional[float]:
    if value is None:
        return None
    try:
        result = float(value)
    except Exception:
        return None
    if result != result:
        return None
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="使用 GM 回填 daily_bar 的标准估值/市值字段，支持沪深 A/B 股")
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
        pe_ratio = _normalize_numeric(row.get("pe_ratio"))
        pb_ratio = _normalize_numeric(row.get("pb_ratio"))
        market_cap = _normalize_numeric(row.get("market_cap"))
        circulating_market_cap = _normalize_numeric(row.get("circulating_market_cap"))
        if pe_ratio is None and pb_ratio is None and market_cap is None and circulating_market_cap is None:
            continue
        updates.append(
            (
                pe_ratio,
                pb_ratio,
                market_cap,
                circulating_market_cap,
                GM_ENRICHED_SOURCE,
                symbol,
                row["trade_date"],
            )
        )
    return updates


def fetch_symbol_rows_with_retry(
    symbol: str,
    start_date: dt.date,
    end_date: dt.date,
    target_dates: Optional[Set[dt.date]],
) -> tuple[List[dict], int, str]:
    last_error: Exception | None = None
    for attempt in range(1, MAX_FETCH_RETRIES + 1):
        try:
            rows = fetch_daily_bars_from_juejin(symbol, start_date, end_date)
            rows = filter_rows(rows, start_date, end_date, target_dates)

            sanitized_rows: list[dict] = []
            invalid_samples: list[tuple[dict[str, object], list[str]]] = []
            for row in rows:
                sanitized_row, anomalies = sanitize_valuation_record(row)
                if anomalies:
                    invalid_samples.append((sanitized_row, anomalies))
                if any(
                    sanitized_row.get(field_name) is not None
                    for field_name in ("pe_ratio", "pb_ratio", "market_cap", "circulating_market_cap")
                ):
                    sanitized_rows.append(sanitized_row)

            if invalid_samples:
                summary = format_invalid_samples(invalid_samples, INVALID_SAMPLE_LIMIT)
                if attempt < MAX_FETCH_RETRIES:
                    raise ValueError(f"abnormal_rows={len(invalid_samples)} samples={summary}")
                if not sanitized_rows:
                    raise ValueError(f"abnormal_rows={len(invalid_samples)} samples={summary}")
                print(
                    f"[warn] {symbol} gm valuation abnormal_rows={len(invalid_samples)} use_valid_rows={len(sanitized_rows)} samples={summary}",
                    flush=True,
                )
                return sanitized_rows, len(invalid_samples), summary

            return sanitized_rows, 0, ""
        except Exception as exc:
            last_error = exc
            time.sleep(1.0 * attempt)

    if last_error is not None:
        raise RuntimeError(f"{symbol}: {last_error}")
    return [], 0, ""


def apply_updates(cursor, updates: List[Tuple[object, ...]], only_missing: bool) -> int:
    if not updates:
        return 0
    if only_missing:
        sql = """
        UPDATE daily_bar
        SET pe_ratio = CASE
                WHEN %s IS NOT NULL AND (pe_ratio IS NULL OR pe_ratio = 0) THEN %s
                ELSE pe_ratio
            END,
            pb_ratio = CASE
                WHEN %s IS NOT NULL AND (pb_ratio IS NULL OR pb_ratio = 0) THEN %s
                ELSE pb_ratio
            END,
            market_cap = CASE
                WHEN %s IS NOT NULL AND (market_cap IS NULL OR market_cap <= 0) THEN %s
                ELSE market_cap
            END,
            circulating_market_cap = CASE
                WHEN %s IS NOT NULL AND (circulating_market_cap IS NULL OR circulating_market_cap <= 0) THEN %s
                ELSE circulating_market_cap
            END,
            updated_at = CURRENT_TIMESTAMP,
            data_source = CASE
                WHEN (
                    (%s IS NOT NULL AND (pe_ratio IS NULL OR pe_ratio = 0)) OR
                    (%s IS NOT NULL AND (pb_ratio IS NULL OR pb_ratio = 0)) OR
                    (%s IS NOT NULL AND (market_cap IS NULL OR market_cap <= 0)) OR
                    (%s IS NOT NULL AND (circulating_market_cap IS NULL OR circulating_market_cap <= 0))
                ) AND (data_source IS NULL OR data_source = '' OR data_source = 'UNKNOWN' OR data_source = 'JUEJIN' OR data_source LIKE 'AK%%')
                    THEN %s
                ELSE data_source
            END
        WHERE symbol = %s AND trade_date = %s
        """
        parameters = [
            (
                pe_ratio,
                pe_ratio,
                pb_ratio,
                pb_ratio,
                market_cap,
                market_cap,
                circulating_market_cap,
                circulating_market_cap,
                pe_ratio,
                pb_ratio,
                market_cap,
                circulating_market_cap,
                source,
                symbol,
                trade_date,
            )
            for pe_ratio, pb_ratio, market_cap, circulating_market_cap, source, symbol, trade_date in updates
        ]
    else:
        sql = """
        UPDATE daily_bar
        SET pe_ratio = CASE WHEN %s IS NOT NULL THEN %s ELSE pe_ratio END,
            pb_ratio = CASE WHEN %s IS NOT NULL THEN %s ELSE pb_ratio END,
            market_cap = CASE WHEN %s IS NOT NULL THEN %s ELSE market_cap END,
            circulating_market_cap = CASE WHEN %s IS NOT NULL THEN %s ELSE circulating_market_cap END,
            updated_at = CURRENT_TIMESTAMP,
            data_source = CASE
                WHEN (%s IS NOT NULL OR %s IS NOT NULL OR %s IS NOT NULL OR %s IS NOT NULL)
                    THEN %s
                ELSE data_source
            END
        WHERE symbol = %s AND trade_date = %s
        """
        parameters = [
            (
                pe_ratio,
                pe_ratio,
                pb_ratio,
                pb_ratio,
                market_cap,
                market_cap,
                circulating_market_cap,
                circulating_market_cap,
                pe_ratio,
                pb_ratio,
                market_cap,
                circulating_market_cap,
                source,
                symbol,
                trade_date,
            )
            for pe_ratio, pb_ratio, market_cap, circulating_market_cap, source, symbol, trade_date in updates
        ]

    cursor.executemany(sql, parameters)
    return cursor.rowcount


def main() -> None:
    args = parse_args()
    start_date = dt.date.fromisoformat(args.start_date)
    end_date = dt.date.fromisoformat(args.end_date)

    conn = get_connection()
    total_updates = 0
    success_symbols = 0
    skipped_symbols: List[str] = []
    failed_symbols: List[str] = []
    try:
        with conn.cursor() as cursor:
            symbols = resolve_symbols(cursor, args.start_date, args.end_date, args.only_missing, args.limit_symbols)

        print(
            f"gm backfill start: symbols={len(symbols)} range={args.start_date}..{args.end_date} only_missing={args.only_missing}",
            flush=True,
        )

        for index, symbol in enumerate(symbols, start=1):
            try:
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

                rows, invalid_row_count, invalid_summary = fetch_symbol_rows_with_retry(
                    symbol,
                    start_date,
                    end_date,
                    target_dates,
                )
                updates = build_updates(symbol, rows)
                if not updates:
                    skipped_symbols.append(f"{symbol}:no-usable-values")
                    print(
                        f"[{index}/{len(symbols)}] symbol={symbol} missing_dates={0 if target_dates is None else len(target_dates)} fetched={len(rows)} updated=0 skipped=no_usable_values total_updates={total_updates}",
                        flush=True,
                    )
                    continue

                with conn.cursor() as cursor:
                    updated = apply_updates(cursor, updates, args.only_missing)
                conn.commit()
                total_updates += updated
                success_symbols += 1
                suffix = ""
                if invalid_row_count > 0:
                    suffix = f" invalid_rows={invalid_row_count} samples={invalid_summary}"
                print(
                    f"[{index}/{len(symbols)}] symbol={symbol} missing_dates={0 if target_dates is None else len(target_dates)} fetched={len(rows)} updated={updated} total_updates={total_updates}{suffix}",
                    flush=True,
                )
            except Exception as exc:
                conn.rollback()
                failed_symbols.append(f"{symbol}: {exc}")
                print(f"[{index}/{len(symbols)}] symbol={symbol} skipped_after_retries={exc}", flush=True)

        print(
            f"gm backfill done: success_symbols={success_symbols} skipped_symbols={len(skipped_symbols)} failed_symbols={len(failed_symbols)} total_updates={total_updates}",
            flush=True,
        )
        if skipped_symbols:
            print("skipped_samples:", flush=True)
            for item in skipped_symbols[:10]:
                print(f"  {item}", flush=True)
        if failed_symbols:
            print("failed_samples:", flush=True)
            for item in failed_symbols[:10]:
                print(f"  {item}", flush=True)
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


if __name__ == "__main__":
    main()