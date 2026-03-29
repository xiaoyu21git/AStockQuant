from __future__ import annotations

import argparse
import datetime as dt
import os
import sys
from typing import Iterable, List, Optional, Tuple

import jqdatasdk as jq
import pymysql


JQ_BACKFILL_SOURCE = "JQ_BACKFILL"


MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
    "autocommit": False,
}

DEFAULT_JQ_USER = os.getenv("JQ_USERNAME") or "13552314165"
DEFAULT_JQ_PASS = os.getenv("JQ_PASSWORD") or "xiaoyu21A"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="使用聚宽回填 daily_bar 的估值/市值字段")
    parser.add_argument("--start-date", required=True, help="开始日期，格式 yyyy-mm-dd")
    parser.add_argument("--end-date", required=True, help="结束日期，格式 yyyy-mm-dd")
    parser.add_argument("--batch-size", type=int, default=200, help="单批股票数量")
    parser.add_argument("--only-missing", action="store_true", help="只处理估值字段缺失或非正数的交易日")
    return parser.parse_args()


def local_to_jq_symbol(symbol: str) -> Optional[str]:
    if symbol.endswith(".SZ"):
        return symbol[:-3] + ".XSHE"
    if symbol.endswith(".SH"):
        return symbol[:-3] + ".XSHG"
    return None


def jq_to_local_symbol(code: str) -> Optional[str]:
    if code.endswith(".XSHE"):
        return code[:-5] + ".SZ"
    if code.endswith(".XSHG"):
        return code[:-5] + ".SH"
    return None


def auth_jq() -> None:
    jq.auth(DEFAULT_JQ_USER, DEFAULT_JQ_PASS)


def get_connection():
    return pymysql.connect(**MYSQL_CONFIG)


def chunked(items: List[str], size: int) -> Iterable[List[str]]:
    for index in range(0, len(items), size):
        yield items[index:index + size]


def resolve_trade_dates(cursor, start_date: str, end_date: str, only_missing: bool) -> List[dt.date]:
    if only_missing:
        cursor.execute(
            """
            SELECT DISTINCT trade_date
            FROM daily_bar
            WHERE trade_date BETWEEN %s AND %s
              AND (
                                        pe_ratio IS NULL OR pe_ratio = 0 OR
                                        pb_ratio IS NULL OR pb_ratio = 0 OR
                    market_cap IS NULL OR market_cap <= 0 OR
                    circulating_market_cap IS NULL OR circulating_market_cap <= 0
              )
            ORDER BY trade_date
            """,
            (start_date, end_date),
        )
    else:
        cursor.execute(
            "SELECT DISTINCT trade_date FROM daily_bar WHERE trade_date BETWEEN %s AND %s ORDER BY trade_date",
            (start_date, end_date),
        )
    return [row[0] for row in cursor.fetchall()]


def load_symbols_for_date(cursor, trade_date: dt.date) -> List[str]:
    cursor.execute("SELECT symbol FROM daily_bar WHERE trade_date = %s ORDER BY symbol", (trade_date,))
    return [row[0] for row in cursor.fetchall()]


def fetch_valuation_rows(jq_symbols: List[str], trade_date: dt.date):
    query = jq.query(
        jq.valuation.code,
        jq.valuation.pe_ratio,
        jq.valuation.pb_ratio,
        jq.valuation.market_cap,
        jq.valuation.circulating_market_cap,
    ).filter(jq.valuation.code.in_(jq_symbols))
    return jq.get_fundamentals(query, date=trade_date.isoformat())


def build_updates(df, trade_date: dt.date) -> List[Tuple[object, ...]]:
    updates: List[Tuple[object, ...]] = []
    if df is None or getattr(df, "empty", True):
        return updates

    for _, row in df.iterrows():
        symbol = jq_to_local_symbol(str(row["code"]))
        if not symbol:
            continue

        pe_ratio = None if row.get("pe_ratio") != row.get("pe_ratio") else float(row.get("pe_ratio"))
        pb_ratio = None if row.get("pb_ratio") != row.get("pb_ratio") else float(row.get("pb_ratio"))
        market_cap = None if row.get("market_cap") != row.get("market_cap") else float(row.get("market_cap")) * 100000000.0
        circulating_market_cap = None if row.get("circulating_market_cap") != row.get("circulating_market_cap") else float(row.get("circulating_market_cap")) * 100000000.0

        updates.append((pe_ratio, pb_ratio, market_cap, circulating_market_cap, JQ_BACKFILL_SOURCE, symbol, trade_date))

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
                WHEN data_source IS NULL OR data_source = '' OR data_source = 'UNKNOWN'
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
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)
    auth_jq()
    conn = get_connection()
    total_updates = 0
    try:
        with conn.cursor() as cursor:
            trade_dates = resolve_trade_dates(cursor, args.start_date, args.end_date, args.only_missing)

        print(
            f"backfill start: dates={len(trade_dates)} range={args.start_date}..{args.end_date} only_missing={args.only_missing}",
            flush=True,
        )

        for date_index, trade_date in enumerate(trade_dates, start=1):
            with conn.cursor() as cursor:
                symbols = load_symbols_for_date(cursor, trade_date)

            jq_symbols = [value for value in (local_to_jq_symbol(symbol) for symbol in symbols) if value]
            print(
                f"[{date_index}/{len(trade_dates)}] trade_date={trade_date} symbols={len(symbols)} jq_symbols={len(jq_symbols)}",
                flush=True,
            )
            date_updates: List[Tuple[object, ...]] = []
            total_batches = (len(jq_symbols) + args.batch_size - 1) // args.batch_size if jq_symbols else 0
            for batch_index, batch in enumerate(chunked(jq_symbols, args.batch_size), start=1):
                print(
                    f"  batch {batch_index}/{total_batches}: requesting {len(batch)} symbols",
                    flush=True,
                )
                df = fetch_valuation_rows(batch, trade_date)
                batch_updates = build_updates(df, trade_date)
                date_updates.extend(batch_updates)
                print(
                    f"  batch {batch_index}/{total_batches}: fetched_rows={0 if df is None else len(df)} mapped_updates={len(batch_updates)} cumulative_updates={len(date_updates)}",
                    flush=True,
                )

            with conn.cursor() as cursor:
                updated = apply_updates(cursor, date_updates)
            conn.commit()
            total_updates += updated
            print(
                f"[{date_index}/{len(trade_dates)}] trade_date={trade_date} updated={updated} total_updates={total_updates}",
                flush=True,
            )

        print(f"backfill_daily_valuation_from_jq: total_updates={total_updates}", flush=True)
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


if __name__ == "__main__":
    main()