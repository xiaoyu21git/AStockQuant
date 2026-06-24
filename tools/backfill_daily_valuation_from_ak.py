from __future__ import annotations

import argparse
import datetime as dt
import os
import sys
from db_config import pg_connect
import time
from pathlib import Path
from typing import Iterable, List, Optional, Tuple

import akshare as ak
import pandas as pd
import psycopg2


AK_BACKFILL_SOURCE = "AK_VALUE_EM"


MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
    "autocommit": False,
}


PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools.a_share_symbol_utils import is_mainland_a_share_symbol
from tools.daily_bar_quality import format_invalid_samples, sanitize_valuation_record


def disable_proxy_env() -> None:
    for key in [
        "http_proxy",
        "https_proxy",
        "HTTP_PROXY",
        "HTTPS_PROXY",
        "all_proxy",
        "ALL_PROXY",
    ]:
        os.environ.pop(key, None)
    os.environ["NO_PROXY"] = "*"
    os.environ["no_proxy"] = "*"


disable_proxy_env()


VALUATION_SUPPORTED_A_SHARE_PREFIXES = (
    "000",
    "001",
    "002",
    "003",
    "300",
    "301",
    "600",
    "601",
    "603",
    "605",
    "688",
    "689",
)

MAX_FETCH_RETRIES = 3
INVALID_SAMPLE_LIMIT = 3


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="使用 AKShare 回填 daily_bar 的估值/市值字段")
    parser.add_argument("--start-date", required=True, help="开始日期，格式 yyyy-mm-dd")
    parser.add_argument("--end-date", required=True, help="结束日期，格式 yyyy-mm-dd")
    parser.add_argument("--asset-class", default="STOCK", help="仅处理指定 asset_class，默认 STOCK")
    parser.add_argument("--only-missing", action="store_true", help="只处理估值字段缺失或非正数的记录")
    parser.add_argument("--limit-symbols", type=int, default=0, help="限制处理的股票数量，0 表示不限制")
    parser.add_argument("--sleep", type=float, default=0.3, help="每只股票抓取后的休眠秒数")
    return parser.parse_args()


def normalize_symbol(symbol: str) -> str:
    return str(symbol).strip().upper()


def is_value_em_supported_symbol(symbol: str) -> bool:
    normalized = normalize_symbol(symbol)
    if not is_mainland_a_share_symbol(normalized):
        return False
    code = normalized.split(".", 1)[0]
    return any(code.startswith(prefix) for prefix in VALUATION_SUPPORTED_A_SHARE_PREFIXES)


def to_plain_code(symbol: str) -> Optional[str]:
    normalized = normalize_symbol(symbol)
    if normalized.endswith(".SH") or normalized.endswith(".SZ"):
        return normalized.split(".", 1)[0]
    return None


def get_connection():
    return pg_connect()


def load_target_symbols(
    cursor,
    start_date: str,
    end_date: str,
    asset_class: str,
    only_missing: bool,
    limit_symbols: int,
) -> Tuple[List[str], List[str]]:
    missing_filter = ""
    if only_missing:
        missing_filter = """
          AND (
                db.pe_ratio IS NULL OR db.pe_ratio = 0 OR
                db.pb_ratio IS NULL OR db.pb_ratio = 0 OR
                db.market_cap IS NULL OR db.market_cap <= 0 OR
                db.circulating_market_cap IS NULL OR db.circulating_market_cap <= 0
          )
        """

    sql = f"""
        SELECT DISTINCT db.symbol
        FROM daily_bar db
        INNER JOIN symbol_info si ON si.symbol = db.symbol
        WHERE db.trade_date BETWEEN %s AND %s
          AND si.asset_class = %s
          {missing_filter}
        ORDER BY db.symbol
    """
    cursor.execute(sql, (start_date, end_date, asset_class))
    raw_symbols = [normalize_symbol(row[0]) for row in cursor.fetchall() if row and row[0]]
    symbols = [symbol for symbol in raw_symbols if is_value_em_supported_symbol(symbol)]
    skipped_symbols = [symbol for symbol in raw_symbols if not is_value_em_supported_symbol(symbol)]
    if limit_symbols > 0:
        symbols = symbols[:limit_symbols]
    return symbols, skipped_symbols


def fetch_symbol_valuation(symbol: str) -> pd.DataFrame:
    code = to_plain_code(symbol)
    if not code:
        return pd.DataFrame()

    last_error: Exception | None = None
    for attempt in range(1, MAX_FETCH_RETRIES + 1):
        try:
            df = ak.stock_value_em(symbol=code)
            if df is None or df.empty:
                return pd.DataFrame()

            normalized = df.rename(
                columns={
                    "数据日期": "trade_date",
                    "总市值": "market_cap",
                    "流通市值": "circulating_market_cap",
                    "PE(TTM)": "pe_ratio",
                    "市净率": "pb_ratio",
                }
            ).copy()
            normalized["trade_date"] = pd.to_datetime(normalized["trade_date"]).dt.date
            normalized["symbol"] = symbol
            for column in ["market_cap", "circulating_market_cap", "pe_ratio", "pb_ratio"]:
                normalized[column] = pd.to_numeric(normalized[column], errors="coerce")
            records = normalized[["symbol", "trade_date", "pe_ratio", "pb_ratio", "market_cap", "circulating_market_cap"]].to_dict("records")
            sanitized_records: list[dict] = []
            invalid_samples: list[tuple[dict[str, object], list[str]]] = []
            for record in records:
                sanitized_record, anomalies = sanitize_valuation_record(record)
                if anomalies:
                    invalid_samples.append((sanitized_record, anomalies))
                if any(
                    sanitized_record.get(field_name) is not None
                    for field_name in ("pe_ratio", "pb_ratio", "market_cap", "circulating_market_cap")
                ):
                    sanitized_records.append(sanitized_record)

            if invalid_samples:
                summary = format_invalid_samples(invalid_samples, INVALID_SAMPLE_LIMIT)
                if attempt < MAX_FETCH_RETRIES:
                    raise ValueError(f"abnormal_rows={len(invalid_samples)} samples={summary}")
                if not sanitized_records:
                    raise ValueError(f"abnormal_rows={len(invalid_samples)} samples={summary}")
                print(
                    f"[warn] {symbol} ak valuation abnormal_rows={len(invalid_samples)} use_valid_rows={len(sanitized_records)} samples={summary}",
                    flush=True,
                )

            if not sanitized_records:
                return pd.DataFrame()
            return pd.DataFrame(sanitized_records)
        except Exception as exc:
            last_error = exc
            time.sleep(1.0 * attempt)

    raise RuntimeError(f"{symbol}: {last_error}")


def build_updates(df: pd.DataFrame, start_date: dt.date, end_date: dt.date) -> List[Tuple[object, ...]]:
    if df is None or df.empty:
        return []

    filtered = df[(df["trade_date"] >= start_date) & (df["trade_date"] <= end_date)].copy()
    updates: List[Tuple[object, ...]] = []
    for _, row in filtered.iterrows():
        pe_ratio = None if pd.isna(row["pe_ratio"]) else float(row["pe_ratio"])
        pb_ratio = None if pd.isna(row["pb_ratio"]) else float(row["pb_ratio"])
        market_cap = None if pd.isna(row["market_cap"]) else float(row["market_cap"])
        circulating_market_cap = None if pd.isna(row["circulating_market_cap"]) else float(row["circulating_market_cap"])
        updates.append(
            (
                pe_ratio,
                pb_ratio,
                market_cap,
                circulating_market_cap,
                AK_BACKFILL_SOURCE,
                row["symbol"],
                row["trade_date"],
            )
        )
    return updates


def apply_updates(cursor, updates: List[Tuple[object, ...]], only_missing: bool) -> int:
    if not updates:
        return 0

    if only_missing:
        sql = """
            UPDATE daily_bar
            SET pe_ratio = CASE
                    WHEN pe_ratio IS NULL OR pe_ratio = 0 THEN %s
                    ELSE pe_ratio
                END,
                pb_ratio = CASE
                    WHEN pb_ratio IS NULL OR pb_ratio = 0 THEN %s
                    ELSE pb_ratio
                END,
                market_cap = CASE
                    WHEN market_cap IS NULL OR market_cap <= 0 THEN %s
                    ELSE market_cap
                END,
                circulating_market_cap = CASE
                    WHEN circulating_market_cap IS NULL OR circulating_market_cap <= 0 THEN %s
                    ELSE circulating_market_cap
                END,
                updated_at = CURRENT_TIMESTAMP,
                data_source = CASE
                    WHEN data_source IS NULL OR data_source = '' OR data_source = 'UNKNOWN'
                        THEN %s
                    ELSE data_source
                END
            WHERE symbol = %s AND trade_date = %s
        """
    else:
        sql = """
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
        """

    cursor.executemany(sql, updates)
    return cursor.rowcount


def main() -> None:
    args = parse_args()
    start_date = dt.date.fromisoformat(args.start_date)
    end_date = dt.date.fromisoformat(args.end_date)

    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)

    conn = get_connection()
    total_updated = 0
    success_symbols = 0
    failed_symbols: List[str] = []
    try:
        with conn.cursor() as cursor:
            symbols, skipped_symbols = load_target_symbols(
                cursor,
                args.start_date,
                args.end_date,
                args.asset_class,
                args.only_missing,
                args.limit_symbols,
            )

        print(
            f"ak backfill start: symbols={len(symbols)} range={args.start_date}..{args.end_date} only_missing={args.only_missing} asset_class={args.asset_class} skipped_non_a_share_symbols={len(skipped_symbols)}",
            flush=True,
        )
        if skipped_symbols:
            print("skipped_non_a_share_samples:", flush=True)
            for item in skipped_symbols[:10]:
                print(f"  {item}", flush=True)

        for index, symbol in enumerate(symbols, start=1):
            try:
                df = fetch_symbol_valuation(symbol)
                updates = build_updates(df, start_date, end_date)
                with conn.cursor() as cursor:
                    updated = apply_updates(cursor, updates, args.only_missing)
                conn.commit()
                total_updated += updated
                success_symbols += 1
                print(
                    f"[{index}/{len(symbols)}] {symbol}: fetched_rows={0 if df is None else len(df)} update_candidates={len(updates)} updated_rows={updated} total_updated={total_updated}",
                    flush=True,
                )
            except Exception as exc:
                conn.rollback()
                failed_symbols.append(f"{symbol}: {exc}")
                print(f"[{index}/{len(symbols)}] {symbol}: failed={exc}", flush=True)

            if args.sleep > 0:
                time.sleep(args.sleep)

        print(
            f"backfill_daily_valuation_from_ak: success_symbols={success_symbols} failed_symbols={len(failed_symbols)} total_updated={total_updated}",
            flush=True,
        )
        if failed_symbols:
            print("failed_samples:", flush=True)
            for item in failed_symbols[:10]:
                print(f"  {item}", flush=True)
    finally:
        conn.close()


if __name__ == "__main__":
    main()