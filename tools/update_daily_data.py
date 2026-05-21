"""
update_daily_data.py
按最近已收盘交易日增量拉取并更新沪深 A/B 股日线行情数据，写入数据库。
"""

from __future__ import annotations

import argparse
import bisect
from concurrent.futures import ThreadPoolExecutor, as_completed
import datetime as dt
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Iterable, Optional

import akshare as ak
import pandas as pd
import pymysql
from sqlalchemy.exc import DBAPIError, OperationalError as SQLAlchemyOperationalError

PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))


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

MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
}

os.environ["DB_HOST"] = MYSQL_CONFIG["host"]
os.environ["DB_PORT"] = str(MYSQL_CONFIG["port"])
os.environ["DB_NAME"] = MYSQL_CONFIG["database"]
os.environ["DB_USER"] = MYSQL_CONFIG["user"]
os.environ["DB_PASSWORD"] = MYSQL_CONFIG["password"]

from astock_engine.data.database.repository import DatabaseRepository
from tools.a_share_symbol_utils import (
    classify_mainland_stock_symbol,
    is_supported_akshare_stock_symbol,
    normalize_symbol,
    to_akshare_symbol,
)
from tools.import_from_juejin import (
    _fetch_daily_adjust_factor_map,
    expand_adjust_factors_for_trade_dates,
    fetch_benchmark_index_symbols_from_juejin,
    fetch_daily_bars_from_juejin,
    fetch_industry_index_symbols_from_juejin,
)
from tools import import_financial_from_jq as financial_import
from tools.daily_bar_quality import detect_daily_price_anomalies, filter_valid_records, format_invalid_samples, sanitize_valuation_record
from tools.symbol_status_utils import TRACKED_SYMBOL_STATUSES, infer_special_symbol_state, resolve_effective_target_date
from tools.trading_day_utils import (
    DEFAULT_MARKET_CLOSE_TIME,
    get_trade_calendar,
    parse_time_text,
    resolve_latest_closed_trade_date,
    wait_until_market_close,
)


MAX_FETCH_RETRIES = 3
INVALID_SAMPLE_LIMIT = 3
DEFAULT_MARKET_WORKERS = 8
DATA_SOURCE_STOCK_DAILY_WITH_GM_ADJ = "AKSHARE_STOCK_DAILY_GM_ADJ"
DATA_SOURCE_JUEJIN_STOCK_DAILY = "JUEJIN_GM_STOCK_DAILY"
DATA_SOURCE_JUEJIN_BENCHMARK_DAILY = "JUEJIN_GM_BENCHMARK_INDEX_DAILY"
DATA_SOURCE_JUEJIN_INDUSTRY_DAILY = "JUEJIN_GM_INDUSTRY_INDEX_DAILY"
PE_PB_DB_LIMIT = 999999.9999
MARKET_CAP_DB_LIMIT = 9999999999999999.9999
MAX_DB_WRITE_RETRIES = 3


class MarketDataUnavailableError(RuntimeError):
    pass

BENCHMARK_INDEX_SYMBOLS = [
    ("000300.SH", "沪深300"),
    ("000001.SH", "上证指数"),
    ("399001.SZ", "深证成指"),
    ("399006.SZ", "创业板指"),
    ("000905.SH", "中证500"),
    ("000852.SH", "中证1000"),
    ("000016.SH", "上证50"),
]


def benchmark_symbol_to_akshare_symbol(symbol: str) -> str:
    normalized = normalize_symbol(symbol)
    code, _, exchange = normalized.partition(".")
    if exchange == "SH":
        return f"sh{code}"
    if exchange == "SZ":
        return f"sz{code}"
    raise RuntimeError(f"{symbol}: unsupported benchmark symbol")


def latest_trade_date() -> Optional[dt.date]:
    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            cursor.execute("SELECT MAX(trade_date) FROM daily_bar")
            row = cursor.fetchone()
            return row[0] if row and row[0] else None
    finally:
        conn.close()


def latest_trade_date_for_symbol(symbol: str) -> Optional[dt.date]:
    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            cursor.execute(
                "SELECT MAX(trade_date) FROM daily_bar WHERE symbol = %s",
                (symbol,),
            )
            row = cursor.fetchone()
            return row[0] if row and row[0] else None
    finally:
        conn.close()


def load_daily_trade_date_bounds() -> tuple[dt.date, dt.date]:
    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            cursor.execute("SELECT MIN(trade_date), MAX(trade_date) FROM daily_bar")
            row = cursor.fetchone()
            if not row or row[0] is None or row[1] is None:
                raise RuntimeError("daily_bar 为空，无法对齐财报起止日期")
            return row[0], row[1]
    finally:
        conn.close()


def upsert_symbol_info_rows(rows: Iterable[dict[str, Any]]) -> int:
    payload = []
    for row in rows:
        payload.append({
            "symbol": str(row.get("symbol") or "").strip(),
            "name": row.get("name") or "",
            "exchange": row.get("exchange") or "",
            "asset_class": row.get("asset_class") or "INDEX",
            "list_date": row.get("list_date") or dt.date(2000, 1, 1),
            "delist_date": row.get("delist_date"),
            "status": row.get("status") or "ACTIVE",
        })
    payload = [row for row in payload if row["symbol"]]
    if not payload:
        return 0

    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            cursor.executemany(
                """
                INSERT INTO symbol_info (
                    symbol, name, exchange, asset_class, list_date, delist_date, status
                ) VALUES (
                    %(symbol)s, %(name)s, %(exchange)s, %(asset_class)s,
                    %(list_date)s, %(delist_date)s, %(status)s
                )
                ON DUPLICATE KEY UPDATE
                    name = VALUES(name),
                    exchange = VALUES(exchange),
                    asset_class = VALUES(asset_class),
                    list_date = VALUES(list_date),
                    delist_date = VALUES(delist_date),
                    status = VALUES(status)
                """,
                payload,
            )
        conn.commit()
        return len(payload)
    finally:
        conn.close()


def resolve_backfill_date_range(start_date_text: Optional[str], end_date_text: Optional[str]) -> tuple[dt.date, dt.date]:
    start_date = dt.date.fromisoformat(start_date_text) if start_date_text else None
    end_date = dt.date.fromisoformat(end_date_text) if end_date_text else None
    if start_date is not None and end_date is not None:
        return start_date, end_date

    daily_start_date, daily_end_date = load_daily_trade_date_bounds()
    return start_date or daily_start_date, end_date or daily_end_date


def run_command(step_name: str, command: list[str]) -> None:
    print(f"[stage] {step_name}", flush=True)
    print("[command] " + " ".join(command), flush=True)
    completed = subprocess.run(command, cwd=PROJECT_ROOT, check=False)
    if completed.returncode != 0:
        raise RuntimeError(f"{step_name} failed with exit_code={completed.returncode}")


def normalize_backfill_jobs(backfill_jobs: list[str], include_financial: bool) -> list[str]:
    jobs: list[str] = []
    seen: set[str] = set()
    for job in backfill_jobs:
        normalized = job.strip().lower()
        if not normalized:
            continue
        if normalized == "all":
            normalized_jobs = ["derived", "valuation", "caps", "turnover", "financial"]
            for item in normalized_jobs:
                if item not in seen:
                    jobs.append(item)
                    seen.add(item)
            continue
        if normalized not in {"derived", "valuation", "caps", "turnover", "financial"}:
            raise ValueError(f"unsupported backfill job: {job}")
        if normalized not in seen:
            jobs.append(normalized)
            seen.add(normalized)
    if include_financial and "financial" not in seen:
        jobs.append("financial")
    return jobs


def load_symbols_from_db() -> list[str]:
    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            cursor.execute(
                """
                SELECT symbol
                FROM symbol_info
                WHERE asset_class = 'STOCK' AND status IN %s
                ORDER BY symbol
                """,
                (TRACKED_SYMBOL_STATUSES,),
            )
            return [str(row[0]).strip() for row in cursor.fetchall() if row and row[0]]
    finally:
        conn.close()


def parse_requested_symbols(symbols_text: Optional[str]) -> list[str]:
    if not symbols_text:
        return []

    requested_symbols: list[str] = []
    seen: set[str] = set()
    for raw_symbol in str(symbols_text).split(","):
        normalized_symbol = normalize_symbol(raw_symbol)
        if not normalized_symbol or normalized_symbol in seen:
            continue
        seen.add(normalized_symbol)
        requested_symbols.append(normalized_symbol)
    return requested_symbols


def load_symbol_update_targets(target_date: dt.date, mode: str = "latest") -> tuple[dict[str, dt.date], list[str]]:
    trade_calendar = get_trade_calendar()

    def calendar_dates_between(start_date: dt.date, end_date: dt.date) -> list[dt.date]:
        left = bisect.bisect_left(trade_calendar, start_date)
        right = bisect.bisect_right(trade_calendar, end_date)
        return trade_calendar[left:right]

    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            cursor.execute(
                """
                SELECT s.symbol,
                       s.name,
                       s.status,
                      s.list_date,
                       s.delist_date,
                       MIN(d.trade_date) AS earliest_trade_date,
                       MAX(d.trade_date) AS latest_trade_date,
                       COUNT(DISTINCT d.trade_date) AS trade_date_count
                FROM symbol_info s
                LEFT JOIN daily_bar d ON d.symbol = s.symbol
                WHERE s.asset_class = 'STOCK' AND s.status IN %s
                GROUP BY s.symbol
                ORDER BY s.symbol
                """,
                (TRACKED_SYMBOL_STATUSES,),
            )
            targets: dict[str, dt.date] = {}
            skipped_symbols: list[str] = []
            for symbol, name, status, list_date, delist_date, earliest_trade_date, latest_trade_date, trade_date_count in cursor.fetchall():
                symbol = str(symbol).strip()
                if not is_supported_akshare_stock_symbol(symbol):
                    skipped_symbols.append(symbol)
                    continue

                special_state = infer_special_symbol_state(name, status, delist_date, target_date)
                if special_state == "DELISTED" and delist_date is None and latest_trade_date is None:
                    continue
                if mode == "latest" and special_state == "DELISTED":
                    skipped_symbols.append(symbol)
                    continue

                effective_target_date = resolve_effective_target_date(
                    target_date,
                    name,
                    status,
                    delist_date,
                    latest_trade_date,
                )
                if mode == "latest" and latest_trade_date is not None and latest_trade_date >= effective_target_date:
                    continue

                if latest_trade_date is None:
                    if mode in {"latest", "all"}:
                        targets[symbol] = effective_target_date
                    continue

                if earliest_trade_date is None:
                    targets[symbol] = effective_target_date
                    continue

                expected_start_date = list_date or earliest_trade_date
                if expected_start_date > effective_target_date:
                    continue

                expected_dates = calendar_dates_between(expected_start_date, effective_target_date)
                if not expected_dates:
                    continue

                latest_covered_dates = calendar_dates_between(expected_start_date, latest_trade_date)
                has_tail_gap_only = latest_trade_date < effective_target_date and int(trade_date_count or 0) == len(latest_covered_dates)
                if has_tail_gap_only:
                    if mode in {"latest", "all"}:
                        targets[symbol] = expected_dates[len(latest_covered_dates)]
                    continue

                expected_count = len(expected_dates)
                if latest_trade_date >= effective_target_date and int(trade_date_count or 0) >= expected_count:
                    continue

                cursor.execute(
                    """
                    SELECT trade_date
                    FROM daily_bar
                    WHERE symbol = %s AND trade_date BETWEEN %s AND %s
                    ORDER BY trade_date
                    """,
                    (symbol, expected_start_date, effective_target_date),
                )
                existing_dates = {row[0] for row in cursor.fetchall() if row and row[0]}
                first_missing_trade_date = next(
                    (trade_date for trade_date in expected_dates if trade_date not in existing_dates),
                    None,
                )
                if first_missing_trade_date is not None and mode in {"history", "all"}:
                    targets[symbol] = first_missing_trade_date
            return targets, skipped_symbols
    finally:
        conn.close()


def summarize_share_type_counts(symbols: list[str]) -> dict[str, int]:
    counts = {"A": 0, "BJ": 0}
    for symbol in symbols:
        share_type = classify_mainland_stock_symbol(symbol)
        if share_type in counts:
            counts[share_type] += 1
    return counts


def run_financial_import(
    target_date: dt.date,
    limit: int,
    workers: int,
    start_date: Optional[dt.date] = None,
    end_date: Optional[dt.date] = None,
    repair_roe_abnormal: bool = False,
    roe_abnormal_threshold: float = 100.0,
) -> tuple[int, int, int]:
    daily_start_date, daily_end_date = load_daily_trade_date_bounds()
    resolved_start_date = start_date or daily_start_date
    resolved_end_date = end_date or daily_end_date
    start_year = resolved_start_date.year
    end_year = max(target_date.year, resolved_end_date.year)
    print(
        f"[stage] align financial history to daily_bar range: {resolved_start_date}..{resolved_end_date} -> {start_year}..{end_year}, workers={workers}",
        flush=True,
    )
    period_results = financial_import.fetch_financial_history(start_year, end_year, limit, workers)
    total_rows = sum(fetched_rows for _, fetched_rows, _ in period_results)
    total_upserts = sum(written_rows for _, _, written_rows in period_results)
    if repair_roe_abnormal:
        _, _, _, _, _, aligned_rows = financial_import.repair_abnormal_roe_fields(
            roe_abnormal_threshold,
            workers,
        )
    else:
        aligned_rows = financial_import.backfill_financial_daily_alignment(target_date)

    print(
        f"财报导入完成: period_count={len(period_results)} fetched_rows={total_rows} written_rows={total_upserts} aligned_rows={aligned_rows}",
        flush=True,
    )
    return total_rows, total_upserts, aligned_rows


def run_financial_roe_repair(workers: int, roe_abnormal_threshold: float) -> tuple[int, int, int, int, int, int]:
    print(
        f"[stage] 开始修复财报 roe 异常值 threshold={roe_abnormal_threshold} workers={workers}",
        flush=True,
    )
    return financial_import.repair_abnormal_roe_fields(roe_abnormal_threshold, workers)


def normalize_daily_frame(df: pd.DataFrame) -> pd.DataFrame:
    normalized = df.copy()

    if "trade_date" not in normalized.columns and "date" in normalized.columns:
        normalized["trade_date"] = normalized["date"]
    if "turnover" not in normalized.columns and "amount" in normalized.columns:
        normalized["turnover"] = normalized["amount"]
    if "change_pct" not in normalized.columns and "pct_change" in normalized.columns:
        normalized["change_pct"] = normalized["pct_change"]
    if "turnover_rate" not in normalized.columns and "ak_turnover_rate" in normalized.columns:
        normalized["turnover_rate"] = normalized["ak_turnover_rate"]

    normalized["symbol"] = normalized["symbol"].map(normalize_symbol)
    normalized["trade_date"] = pd.to_datetime(normalized["trade_date"]).dt.date

    optional_columns = {
        "turnover": None,
        "pre_close": None,
        "change_amt": None,
        "amplitude": None,
        "turnover_rate": None,
        "pe_ratio": None,
        "pb_ratio": None,
        "market_cap": None,
        "circulating_market_cap": None,
        "pre_adjust_factor": None,
        "post_adjust_factor": None,
        "data_source": "UNKNOWN",
    }
    for column, default_value in optional_columns.items():
        if column not in normalized.columns:
            normalized[column] = default_value

    return normalized[
        [
            "symbol",
            "trade_date",
            "open",
            "high",
            "low",
            "close",
            "pre_close",
            "volume",
            "turnover",
            "change_pct",
            "change_amt",
            "amplitude",
            "turnover_rate",
            "pe_ratio",
            "pb_ratio",
            "market_cap",
            "circulating_market_cap",
            "pre_adjust_factor",
            "post_adjust_factor",
            "data_source",
        ]
    ]


def enrich_stock_frame_with_adjust_factors(symbol: str,
                                           df: pd.DataFrame,
                                           start_date: dt.date,
                                           end_date: dt.date) -> pd.DataFrame:
    enriched = df.copy()
    if enriched.empty:
        return enriched

    try:
        adjust_factor_by_date = _fetch_daily_adjust_factor_map(symbol, start_date, end_date)
    except Exception as exc:
        print(f"[warn] adjust factor 拉取失败 {symbol}: {exc}", flush=True)
        adjust_factor_by_date = {}

    if "date" in enriched.columns:
        enriched["trade_date"] = pd.to_datetime(enriched["date"]).dt.date
    else:
        enriched["trade_date"] = pd.to_datetime(enriched["trade_date"]).dt.date

    adjust_df = build_effective_adjust_factor_frame(
        symbol,
        enriched["trade_date"].tolist(),
        start_date,
        end_date,
        adjust_factor_by_date,
    )
    if adjust_df.empty:
        return merge_existing_adjust_factors(symbol, enriched, start_date, end_date)

    enriched = enriched.drop(columns=[column for column in ["pre_adjust_factor", "post_adjust_factor"] if column in enriched.columns])
    enriched = enriched.merge(adjust_df, on="trade_date", how="left")
    return enriched


def validate_market_records(symbol: str, fetcher_name: str, result_df: pd.DataFrame) -> pd.DataFrame:
    valid_records, invalid_samples = filter_valid_records(
        result_df.to_dict("records"),
        detect_daily_price_anomalies,
    )
    if invalid_samples:
        summary = format_invalid_samples(invalid_samples, INVALID_SAMPLE_LIMIT)
        raise ValueError(f"abnormal_rows={len(invalid_samples)} samples={summary}")
    if not valid_records:
        return pd.DataFrame()
    return pd.DataFrame(valid_records)


def normalize_juejin_stock_daily_frame(symbol: str,
                                       result_df: pd.DataFrame,
                                       start_date: dt.date) -> pd.DataFrame:
    normalized = result_df.copy()
    if normalized.empty:
        return normalized

    normalized["trade_date"] = pd.to_datetime(normalized["trade_date"])
    normalized = normalized.sort_values("trade_date").reset_index(drop=True)

    numeric_columns = [
        "open",
        "high",
        "low",
        "close",
        "pre_close",
        "volume",
        "turnover",
        "turnover_rate",
        "market_cap",
        "circulating_market_cap",
        "pre_adjust_factor",
        "post_adjust_factor",
    ]
    for column in numeric_columns:
        if column in normalized.columns:
            normalized[column] = pd.to_numeric(normalized[column], errors="coerce")

    if "pre_close" not in normalized.columns:
        normalized["pre_close"] = normalized["close"].shift(1)
    else:
        invalid_pre_close = normalized["pre_close"].isna() | (normalized["pre_close"] <= 0)
        normalized.loc[invalid_pre_close, "pre_close"] = normalized["close"].shift(1).loc[invalid_pre_close]

    previous_close = fetch_previous_close_from_db(symbol, start_date)
    if previous_close is not None and not normalized.empty:
        first_pre_close = normalized.loc[0, "pre_close"]
        if pd.isna(first_pre_close) or float(first_pre_close) <= 0:
            normalized.loc[0, "pre_close"] = previous_close

    valid_pre_close = normalized["pre_close"].notna() & (normalized["pre_close"] > 0)
    normalized["change_amt"] = (normalized["close"] - normalized["pre_close"]).round(4)
    normalized["change_pct"] = None
    normalized.loc[valid_pre_close, "change_pct"] = (
        (normalized.loc[valid_pre_close, "close"] - normalized.loc[valid_pre_close, "pre_close"])
        / normalized.loc[valid_pre_close, "pre_close"]
        * 100
    ).round(4)
    normalized["amplitude"] = None
    normalized.loc[valid_pre_close, "amplitude"] = (
        (normalized.loc[valid_pre_close, "high"] - normalized.loc[valid_pre_close, "low"])
        / normalized.loc[valid_pre_close, "pre_close"]
        * 100
    ).round(4)
    normalized["date"] = normalized["trade_date"]
    return normalized


def fetch_symbol_daily_from_juejin(symbol: str, start_date: dt.date, end_date: dt.date) -> pd.DataFrame:
    rows = fetch_daily_bars_from_juejin(symbol, start_date, end_date)
    if not rows:
        return pd.DataFrame()

    result_df = pd.DataFrame(rows)
    if result_df.empty:
        return result_df

    result_df = normalize_juejin_stock_daily_frame(symbol, result_df, start_date)
    result_df["symbol"] = symbol
    result_df["data_source"] = DATA_SOURCE_JUEJIN_STOCK_DAILY
    return validate_market_records(symbol, "juejin_daily", result_df)


def symbol_has_no_new_juejin_rows(symbol: str, start_date: dt.date, end_date: dt.date) -> bool:
    probe_start = max(start_date - dt.timedelta(days=30), dt.date(1990, 1, 1))
    probe_rows = fetch_daily_bars_from_juejin(symbol, probe_start, end_date)
    if not probe_rows:
        return False

    latest_trade_date = max(
        row.get("trade_date")
        for row in probe_rows
        if row.get("trade_date") is not None
    )
    if latest_trade_date is None:
        return False
    return latest_trade_date < start_date


def fetch_previous_close_from_db(symbol: str, before_date: dt.date) -> Optional[float]:
    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            cursor.execute(
                """
                SELECT close
                FROM daily_bar
                WHERE symbol = %s AND trade_date < %s
                ORDER BY trade_date DESC
                LIMIT 1
                """,
                (symbol, before_date),
            )
            row = cursor.fetchone()
            if not row or row[0] is None:
                return None
            try:
                return float(row[0])
            except Exception:
                return None
    finally:
        conn.close()


def fetch_previous_adjust_factors_from_db(symbol: str,
                                         before_date: dt.date) -> tuple[Optional[float], Optional[float]]:
    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            cursor.execute(
                """
                SELECT pre_adjust_factor, post_adjust_factor
                FROM daily_bar
                WHERE symbol = %s
                  AND trade_date < %s
                  AND (
                      (pre_adjust_factor IS NOT NULL AND pre_adjust_factor > 0)
                      OR (post_adjust_factor IS NOT NULL AND post_adjust_factor > 0)
                  )
                ORDER BY trade_date DESC
                LIMIT 1
                """,
                (symbol, before_date),
            )
            row = cursor.fetchone()
    finally:
        conn.close()

    if not row:
        return None, None

    def _normalize_factor(value: Any) -> Optional[float]:
        if value is None:
            return None
        try:
            numeric = float(value)
        except Exception:
            return None
        if not pd.notna(numeric) or numeric <= 0:
            return None
        return numeric

    return _normalize_factor(row[0]), _normalize_factor(row[1])


def fetch_existing_adjust_factors_from_db(symbol: str,
                                         start_date: dt.date,
                                         end_date: dt.date) -> pd.DataFrame:
    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            cursor.execute(
                """
                SELECT trade_date, pre_adjust_factor, post_adjust_factor
                FROM daily_bar
                WHERE symbol = %s
                  AND trade_date BETWEEN %s AND %s
                                    AND (
                                            (pre_adjust_factor IS NOT NULL AND pre_adjust_factor > 0)
                                            OR (post_adjust_factor IS NOT NULL AND post_adjust_factor > 0)
                                    )
                ORDER BY trade_date
                """,
                (symbol, start_date, end_date),
            )
            rows = cursor.fetchall()
    finally:
        conn.close()

    if not rows:
        return pd.DataFrame(columns=["trade_date", "pre_adjust_factor", "post_adjust_factor"])

    return pd.DataFrame(
        [
            {
                "trade_date": row[0],
                "pre_adjust_factor": row[1],
                "post_adjust_factor": row[2],
            }
            for row in rows
        ]
    )


def merge_existing_adjust_factors(symbol: str,
                                  df: pd.DataFrame,
                                  start_date: dt.date,
                                  end_date: dt.date) -> pd.DataFrame:
    if df.empty:
        return df

    existing_adjust_df = fetch_existing_adjust_factors_from_db(symbol, start_date, end_date)
    if existing_adjust_df.empty:
        return df

    merged = df.copy()
    if "date" in merged.columns:
        merged["trade_date"] = pd.to_datetime(merged["date"]).dt.date
    else:
        merged["trade_date"] = pd.to_datetime(merged["trade_date"]).dt.date

    merged = merged.drop(columns=[column for column in ["pre_adjust_factor", "post_adjust_factor"] if column in merged.columns])
    return merged.merge(existing_adjust_df, on="trade_date", how="left")


def build_effective_adjust_factor_frame(symbol: str,
                                        trade_dates: Iterable[dt.date],
                                        start_date: dt.date,
                                        end_date: dt.date,
                                        adjust_factor_by_date: dict[dt.date, dict[str, Optional[float]]]) -> pd.DataFrame:
    ordered_dates = sorted({trade_date for trade_date in trade_dates if trade_date is not None})
    if not ordered_dates:
        return pd.DataFrame(columns=["trade_date", "pre_adjust_factor", "post_adjust_factor"])

    seed_pre_adjust_factor, seed_post_adjust_factor = fetch_previous_adjust_factors_from_db(symbol, ordered_dates[0])
    expanded_adjust_factor_by_date = expand_adjust_factors_for_trade_dates(
        ordered_dates,
        adjust_factor_by_date,
        seed_pre_adjust_factor=seed_pre_adjust_factor,
        seed_post_adjust_factor=seed_post_adjust_factor,
    )
    effective_adjust_df = pd.DataFrame(
        [
            {
                "trade_date": trade_date,
                "pre_adjust_factor": values.get("pre_adjust_factor"),
                "post_adjust_factor": values.get("post_adjust_factor"),
            }
            for trade_date, values in expanded_adjust_factor_by_date.items()
        ]
    )
    existing_adjust_df = fetch_existing_adjust_factors_from_db(symbol, start_date, end_date)
    if existing_adjust_df.empty:
        return effective_adjust_df

    merged = effective_adjust_df.merge(
        existing_adjust_df.rename(
            columns={
                "pre_adjust_factor": "existing_pre_adjust_factor",
                "post_adjust_factor": "existing_post_adjust_factor",
            }
        ),
        on="trade_date",
        how="left",
    )
    merged["pre_adjust_factor"] = merged["pre_adjust_factor"].combine_first(merged["existing_pre_adjust_factor"])
    merged["post_adjust_factor"] = merged["post_adjust_factor"].combine_first(merged["existing_post_adjust_factor"])
    return merged[["trade_date", "pre_adjust_factor", "post_adjust_factor"]]


def normalize_ak_hist_frame(df: pd.DataFrame) -> pd.DataFrame:
    normalized = df.copy()
    normalized = normalized.rename(
        columns={
            "日期": "date",
            "开盘": "open",
            "最高": "high",
            "最低": "low",
            "收盘": "close",
            "成交量": "volume",
            "成交额": "amount",
            "涨跌幅": "change_pct",
            "涨跌额": "change_amt",
            "振幅": "amplitude",
            "换手率": "turnover_rate",
        }
    )
    return normalized


def fetch_symbol_daily(symbol: str, start_date: dt.date, end_date: dt.date) -> pd.DataFrame:
    ak_symbol = to_akshare_symbol(symbol)
    share_type = classify_mainland_stock_symbol(symbol)
    if not ak_symbol or share_type not in {"A", "BJ"}:
        raise RuntimeError(f"{symbol}: unsupported market")

    try:
        juejin_df = fetch_symbol_daily_from_juejin(symbol, start_date, end_date)
        if juejin_df is not None and not juejin_df.empty:
            return juejin_df
        if symbol_has_no_new_juejin_rows(symbol, start_date, end_date):
            return pd.DataFrame()
    except Exception as exc:
        print(f"[warn] {symbol} juejin_daily failed, fallback to akshare: {exc}", flush=True)

    code = ak_symbol[2:] if len(ak_symbol) > 2 else ak_symbol
    fetchers = [
        (
            "stock_zh_a_daily",
            lambda query_start: ak.stock_zh_a_daily(
                symbol=ak_symbol,
                start_date=query_start.strftime("%Y%m%d"),
                end_date=end_date.strftime("%Y%m%d"),
                adjust="",
            ),
        ),
        (
            "stock_zh_a_hist",
            lambda query_start: normalize_ak_hist_frame(
                ak.stock_zh_a_hist(
                    symbol=code,
                    period="daily",
                    start_date=query_start.strftime("%Y%m%d"),
                    end_date=end_date.strftime("%Y%m%d"),
                    adjust="",
                )
            ),
        ),
    ]

    error_messages: list[str] = []
    query_start = start_date - dt.timedelta(days=10)
    previous_close = fetch_previous_close_from_db(symbol, start_date)

    for fetcher_name, fetcher in fetchers:
        last_error: Exception | None = None
        for attempt in range(1, MAX_FETCH_RETRIES + 1):
            try:
                df = fetcher(query_start)
                if df is None or df.empty:
                    last_error = RuntimeError(f"{fetcher_name}: empty result")
                    break

                df["date"] = pd.to_datetime(df["date"])
                df = df.sort_values("date").reset_index(drop=True)
                df["pre_close"] = df["close"].shift(1)
                if previous_close is not None and not df.empty and pd.isna(df.loc[0, "pre_close"]):
                    df.loc[0, "pre_close"] = previous_close
                df["change_amt"] = (df["close"] - df["pre_close"]).round(4)
                df["change_pct"] = ((df["close"] - df["pre_close"]) / df["pre_close"] * 100).round(4)
                df["amplitude"] = ((df["high"] - df["low"]) / df["pre_close"] * 100).round(4)
                if "turnover" in df.columns:
                    df["turnover_rate"] = (pd.to_numeric(df["turnover"], errors="coerce") * 100).round(4)
                else:
                    df["turnover_rate"] = None
                if "amount" not in df.columns:
                    df["amount"] = None
                if "outstanding_share" in df.columns:
                    df["circulating_market_cap"] = (
                        pd.to_numeric(df["close"], errors="coerce")
                        * pd.to_numeric(df["outstanding_share"], errors="coerce")
                    ).round(4)
                else:
                    df["circulating_market_cap"] = None
                df = df[df["date"] >= pd.Timestamp(start_date)].copy()
                if df.empty:
                    break

                df = enrich_stock_frame_with_adjust_factors(symbol, df, start_date, end_date)

                df["symbol"] = symbol
                if share_type == "BJ":
                    df["data_source"] = DATA_SOURCE_STOCK_DAILY_WITH_GM_ADJ
                else:
                    df["data_source"] = DATA_SOURCE_STOCK_DAILY_WITH_GM_ADJ
                required_adjust_fields = ["pre_adjust_factor", "post_adjust_factor"]
                missing_adjust_fields = [field for field in required_adjust_fields if field not in df.columns]
                if missing_adjust_fields:
                    raise RuntimeError(f"{symbol}: missing_adjust_fields={missing_adjust_fields}")
                if df[required_adjust_fields].isna().any().any():
                    raise RuntimeError(f"{symbol}: adjust_factor_values_incomplete")
                result_df = df[[
                    "symbol",
                    "date",
                    "open",
                    "high",
                    "low",
                    "close",
                    "pre_close",
                    "volume",
                    "amount",
                    "change_pct",
                    "change_amt",
                    "amplitude",
                    "turnover_rate",
                    "circulating_market_cap",
                    "pre_adjust_factor",
                    "post_adjust_factor",
                    "data_source",
                ]]
                validated_df = validate_market_records(symbol, fetcher_name, result_df)
                if validated_df.empty:
                    return pd.DataFrame()
                return validated_df
            except Exception as exc:
                last_error = exc
                if attempt < MAX_FETCH_RETRIES:
                    print(
                        f"[retry] {symbol} {fetcher_name} attempt={attempt}/{MAX_FETCH_RETRIES} error={exc}",
                        flush=True,
                    )
                time.sleep(1.5 * attempt)

        if last_error is not None:
            error_messages.append(f"{fetcher_name}: {last_error}")

    if error_messages:
        raise MarketDataUnavailableError(f"{symbol}: {'; '.join(error_messages)}")

    raise MarketDataUnavailableError(f"{symbol}: no akshare data source returned rows")


def fetch_benchmark_daily(symbol: str, start_date: dt.date, end_date: dt.date) -> pd.DataFrame:
    ak_symbol = benchmark_symbol_to_akshare_symbol(symbol)
    try:
        df = ak.stock_zh_index_daily(symbol=ak_symbol)
    except Exception as exc:
        print(f"[skip] benchmark unavailable {symbol}: stock_zh_index_daily: {exc}", flush=True)
        return pd.DataFrame()

    if df is None or df.empty:
        return pd.DataFrame()

    normalized = df.copy()
    if "date" not in normalized.columns and "trade_date" in normalized.columns:
        normalized["date"] = normalized["trade_date"]
    normalized["trade_date"] = pd.to_datetime(normalized["date"]).dt.date
    normalized = normalized[(normalized["trade_date"] >= start_date) & (normalized["trade_date"] <= end_date)].copy()
    if normalized.empty:
        return normalized

    normalized["symbol"] = symbol
    normalized["data_source"] = "AKSHARE_INDEX_DAILY"
    return normalized


def fetch_reference_daily_from_juejin(symbol: str,
                                      start_date: dt.date,
                                      end_date: dt.date,
                                      data_source: str) -> pd.DataFrame:
    rows = fetch_daily_bars_from_juejin(symbol, start_date, end_date)
    if not rows:
        return pd.DataFrame()

    normalized = pd.DataFrame(rows)
    normalized["symbol"] = symbol
    normalized["data_source"] = data_source
    return normalized


def build_reference_update_targets(rows: Iterable[dict[str, Any]],
                                   target_date: dt.date,
                                   mode: str) -> dict[str, dt.date]:
    targets: dict[str, dt.date] = {}
    for row in rows:
        symbol = str(row.get("symbol") or "").strip()
        if not symbol:
            continue
        latest_symbol_date = latest_trade_date_for_symbol(symbol)
        if latest_symbol_date is None:
            if mode in {"latest", "all"}:
                targets[symbol] = target_date
            continue
        next_trade_date = latest_symbol_date + dt.timedelta(days=1)
        if next_trade_date <= target_date:
            targets[symbol] = next_trade_date
    return targets


def resolve_market_worker_count(requested_workers: int, task_count: int) -> int:
    if task_count <= 0:
        return 1
    return max(1, min(requested_workers, task_count))


def sanitize_market_valuation_fields(symbol: str, df: pd.DataFrame) -> pd.DataFrame:
    sanitized_rows: list[dict[str, Any]] = []
    invalid_samples: list[tuple[dict[str, Any], list[str]]] = []

    for row in df.to_dict("records"):
        sanitized_row, anomalies = sanitize_valuation_record(row)
        for field_name, limit in {
            "pe_ratio": PE_PB_DB_LIMIT,
            "pb_ratio": PE_PB_DB_LIMIT,
            "market_cap": MARKET_CAP_DB_LIMIT,
            "circulating_market_cap": MARKET_CAP_DB_LIMIT,
        }.items():
            field_value = sanitized_row.get(field_name)
            if field_value is None:
                continue
            try:
                numeric_value = float(field_value)
            except Exception:
                sanitized_row[field_name] = None
                anomalies.append(f"invalid {field_name}")
                continue
            if abs(numeric_value) > limit:
                sanitized_row[field_name] = None
                anomalies.append(f"{field_name} out_of_range")
        if anomalies:
            invalid_samples.append((sanitized_row, anomalies))
        sanitized_rows.append(sanitized_row)

    if invalid_samples:
        summary = format_invalid_samples(invalid_samples, INVALID_SAMPLE_LIMIT)
        print(
            f"[warn] {symbol} valuation abnormal_rows={len(invalid_samples)} sanitized_before_write samples={summary}",
            flush=True,
        )

    return pd.DataFrame(sanitized_rows)


def process_market_update_task(task: dict[str, Any], target_date: dt.date) -> dict[str, Any]:
    symbol = task["symbol"]
    category = task["category"]
    start_date = task["start_date"]
    data_source = task["data_source"]

    if category == "stock":
        raw_df = fetch_symbol_daily(symbol, start_date, target_date)
    else:
        raw_df = fetch_reference_daily_from_juejin(symbol, start_date, target_date, data_source)

    if raw_df.empty:
        return {
            "symbol": symbol,
            "category": category,
            "status": "empty",
            "fetched_rows": 0,
            "written_rows": 0,
            "latest_symbol_date": None,
            "failed_write_rows": [],
        }

    normalized = normalize_daily_frame(raw_df)
    normalized = normalized[
        (normalized["trade_date"] >= start_date) & (normalized["trade_date"] <= target_date)
    ].copy()
    normalized = normalized.drop_duplicates(subset=["symbol", "trade_date"]).sort_values("trade_date").reset_index(drop=True)
    normalized = sanitize_market_valuation_fields(symbol, normalized)
    if normalized.empty:
        return {
            "symbol": symbol,
            "category": category,
            "status": "empty",
            "fetched_rows": 0,
            "written_rows": 0,
            "latest_symbol_date": None,
            "failed_write_rows": [],
        }

    latest_symbol_date = normalized["trade_date"].max()
    written_rows, failed_write_rows = persist_daily_rows_with_fallback(symbol, normalized)
    return {
        "symbol": symbol,
        "category": category,
        "status": "success",
        "fetched_rows": len(normalized),
        "written_rows": written_rows,
        "latest_symbol_date": latest_symbol_date,
        "failed_write_rows": failed_write_rows,
    }


def is_retryable_daily_write_error(exc: Exception) -> bool:
    if isinstance(exc, pymysql.err.OperationalError):
        errno = exc.args[0] if exc.args else None
        return errno in {1205, 1213, 2006, 2013}
    if isinstance(exc, (SQLAlchemyOperationalError, DBAPIError)):
        origin = getattr(exc, "orig", None)
        if isinstance(origin, pymysql.err.OperationalError):
            errno = origin.args[0] if origin.args else None
            return errno in {1205, 1213, 2006, 2013}
        message = str(exc).lower()
        return "deadlock" in message or "lost connection" in message or "server has gone away" in message
    return False


def persist_daily_rows_with_fallback(symbol: str, df: pd.DataFrame) -> tuple[int, list[str]]:
    last_batch_error: Exception | None = None
    for attempt in range(1, MAX_DB_WRITE_RETRIES + 1):
        try:
            return DatabaseRepository.save_daily_bars(df), []
        except Exception as exc:
            last_batch_error = exc
            if not is_retryable_daily_write_error(exc) or attempt >= MAX_DB_WRITE_RETRIES:
                break
            print(
                f"[warn] {symbol} batch write retry attempt={attempt}/{MAX_DB_WRITE_RETRIES} error={exc}",
                flush=True,
            )
            time.sleep(0.5 * attempt)

    exc = last_batch_error if last_batch_error is not None else RuntimeError("unknown batch write failure")
    if len(df) <= 1:
        raise RuntimeError(f"{symbol}: batch_write_failed={exc}") from exc

    print(
        f"[warn] {symbol} batch write failed, fallback to row-by-row: {exc}",
        flush=True,
    )
    written_rows = 0
    failed_rows: list[str] = []
    for row in df.to_dict("records"):
        row_df = pd.DataFrame([row])
        trade_date = row.get("trade_date")
        row_written = False
        for attempt in range(1, MAX_DB_WRITE_RETRIES + 1):
            try:
                written_rows += DatabaseRepository.save_daily_bars(row_df)
                row_written = True
                break
            except Exception as row_exc:
                if not is_retryable_daily_write_error(row_exc) or attempt >= MAX_DB_WRITE_RETRIES:
                    failed_rows.append(f"{trade_date}:{row_exc}")
                    break
                print(
                    f"[warn] {symbol} row write retry trade_date={trade_date} attempt={attempt}/{MAX_DB_WRITE_RETRIES} error={row_exc}",
                    flush=True,
                )
                time.sleep(0.5 * attempt)
        if not row_written and failed_rows and failed_rows[-1].startswith(f"{trade_date}:"):
            continue

    if written_rows <= 0 and failed_rows:
        preview = "; ".join(failed_rows[:3])
        raise RuntimeError(
            f"{symbol}: batch_write_failed={exc}; row_fallback_failed={len(failed_rows)} samples={preview}"
        ) from exc
    return written_rows, failed_rows


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="按最近已收盘交易日更新沪深 A/B 股日线数据，并可选组合回填派生字段、估值、市值、换手率与财报"
    )
    parser.add_argument(
        "--target-date",
        help="指定目标交易日，格式 YYYY-MM-DD；未提供时自动解析最近已收盘交易日",
    )
    parser.add_argument(
        "--close-time",
        default=DEFAULT_MARKET_CLOSE_TIME,
        help="收盘后开始更新的时间阈值，格式 HH:MM，默认 15:30",
    )
    parser.add_argument(
        "--wait-until-close",
        action="store_true",
        help="若尚未到达下一次收盘时间，则阻塞等待到收盘后再执行一次更新",
    )
    parser.add_argument(
        "--mode",
        choices=["latest", "history", "all"],
        default="latest",
        help="latest=只补最新尾部缺口；history=只补历史内部缺口；all=两者都补，默认 latest",
    )
    parser.add_argument(
        "--with-financial",
        action="store_true",
        help="在日线更新完成后同步拉取 AkShare 历史财报并写入 financial_indicator",
    )
    parser.add_argument(
        "--backfill",
        nargs="+",
        default=[],
        help="组合回填任务，支持 derived valuation caps turnover financial，传 all 表示全部执行",
    )
    parser.add_argument(
        "--start-date",
        help="字段回填的开始日期，格式 yyyy-mm-dd；默认取 daily_bar 最早日期",
    )
    parser.add_argument(
        "--end-date",
        help="字段回填的结束日期，格式 yyyy-mm-dd；默认取 daily_bar 最晚日期",
    )
    parser.add_argument(
        "--financial-limit",
        type=int,
        default=10000,
        help="财报历史回填单次查询返回上限，默认 10000",
    )
    parser.add_argument(
        "--financial-workers",
        type=int,
        default=8,
        help="财报抓取并发线程数，默认 8",
    )
    parser.add_argument(
        "--financial-roe-abnormal-threshold",
        type=float,
        default=100.0,
        help="财报 roe 异常阈值，绝对值超过该值的记录会被重新拉取，默认 100.0",
    )
    parser.add_argument(
        "--repair-financial-roe-abnormal",
        action="store_true",
        help="在财报同步后重新拉取 roe 为空或超阈值的记录，并重建日频对齐",
    )
    parser.add_argument(
        "--market-workers",
        type=int,
        default=DEFAULT_MARKET_WORKERS,
        help="市场行情抓取并发线程数，默认 8",
    )
    parser.add_argument(
        "--symbols",
        help="仅处理指定股票代码，逗号分隔，例如 600519.SH,000001.SZ",
    )
    return parser.parse_args()


def resolve_target_date(args: argparse.Namespace) -> dt.date:
    if args.target_date:
        return dt.date.fromisoformat(args.target_date)

    close_time = parse_time_text(args.close_time)
    if args.wait_until_close:
        wait_until_market_close(
            close_time,
            status_callback=lambda message: print(message, flush=True),
        )
    return resolve_latest_closed_trade_date(dt.datetime.now(), close_time)


def main() -> None:
    args = parse_args()
    target_date = resolve_target_date(args)
    print(f"[stage] resolve target_date={target_date} mode={args.mode}", flush=True)
    print("[stage] loading update targets...", flush=True)
    update_targets, skipped_symbols = load_symbol_update_targets(target_date, args.mode)
    requested_symbols = parse_requested_symbols(args.symbols)
    if requested_symbols:
        requested_symbol_set = set(requested_symbols)
        update_targets = {
            symbol: start_date
            for symbol, start_date in update_targets.items()
            if symbol in requested_symbol_set
        }
        skipped_symbols = [symbol for symbol in skipped_symbols if symbol in requested_symbol_set]
    print(
        f"[stage] update targets ready: symbol_count={len(update_targets)} skipped_non_target={len(skipped_symbols)}",
        flush=True,
    )

    symbols = list(update_targets.keys())
    share_type_counts = summarize_share_type_counts(symbols)
    total_fetched_rows = 0
    total_written_rows = 0
    success_symbols = 0
    failed_symbols: list[str] = []
    skipped_unavailable_symbols: list[str] = []
    incomplete_symbols: list[str] = []
    partial_write_symbols: list[str] = []

    earliest_start = min(update_targets.values()) if update_targets else target_date
    if symbols:
        print(
            f"开始更新: mode={args.mode} range={earliest_start}..{target_date}, symbol_count={len(symbols)}, "
            f"a_share_symbols={share_type_counts['A']} bj_share_symbols={share_type_counts['BJ']} "
            f"mode=close-of-day, skipped_non_target_symbols={len(skipped_symbols)}"
        )
    else:
        latest = latest_trade_date()
        print(
            f"当前模式无需更新沪深A/B股，mode={args.mode} latest_trade_date={latest}, target_trade_date={target_date}, "
            f"skipped_non_a_share_symbols={len(skipped_symbols)}"
        )
    if skipped_symbols:
        print("跳过的非目标代码样本: " + ", ".join(skipped_symbols[:20]))
    if requested_symbols:
        benchmark_symbol_rows = []
        industry_symbol_rows = []
        reference_symbol_count = 0
    else:
        print("[stage] refreshing benchmark and industry symbol metadata...", flush=True)
        benchmark_symbol_rows = fetch_benchmark_index_symbols_from_juejin()
        industry_symbol_rows = fetch_industry_index_symbols_from_juejin()
        reference_symbol_count = upsert_symbol_info_rows([*benchmark_symbol_rows, *industry_symbol_rows])
        print(
            f"[stage] reference symbols ready: benchmarks={len(benchmark_symbol_rows)} industries={len(industry_symbol_rows)} upserts={reference_symbol_count}",
            flush=True,
        )

    benchmark_targets = build_reference_update_targets(benchmark_symbol_rows, target_date, args.mode)
    industry_targets = build_reference_update_targets(industry_symbol_rows, target_date, args.mode)

    market_tasks: list[dict[str, Any]] = []
    for symbol in symbols:
        market_tasks.append({
            "category": "stock",
            "symbol": symbol,
            "start_date": update_targets[symbol],
            "data_source": DATA_SOURCE_STOCK_DAILY_WITH_GM_ADJ,
        })
    for row in benchmark_symbol_rows:
        symbol = str(row.get("symbol") or "").strip()
        start_date = benchmark_targets.get(symbol)
        if start_date is None:
            continue
        market_tasks.append({
            "category": "benchmark",
            "symbol": symbol,
            "start_date": start_date,
            "data_source": DATA_SOURCE_JUEJIN_BENCHMARK_DAILY,
        })
    for row in industry_symbol_rows:
        symbol = str(row.get("symbol") or "").strip()
        start_date = industry_targets.get(symbol)
        if start_date is None:
            continue
        market_tasks.append({
            "category": "industry",
            "symbol": symbol,
            "start_date": start_date,
            "data_source": DATA_SOURCE_JUEJIN_INDUSTRY_DAILY,
        })

    if benchmark_targets or industry_targets:
        earliest_start = min(earliest_start, *(list(benchmark_targets.values()) + list(industry_targets.values())))

    if market_tasks:
        resolved_market_workers = resolve_market_worker_count(args.market_workers, len(market_tasks))
        print(
            f"[stage] start unified market update: task_count={len(market_tasks)} stock_count={len(update_targets)} "
            f"benchmark_count={len(benchmark_targets)} industry_count={len(industry_targets)} workers={resolved_market_workers}",
            flush=True,
        )
        with ThreadPoolExecutor(max_workers=resolved_market_workers) as executor:
            future_to_task = {
                executor.submit(process_market_update_task, task, target_date): task
                for task in market_tasks
            }
            for completed_count, future in enumerate(as_completed(future_to_task), start=1):
                task = future_to_task[future]
                symbol = task["symbol"]
                try:
                    task_result = future.result()
                    if task_result["status"] == "empty":
                        incomplete_symbols.append(f"{symbol}:empty")
                    else:
                        total_fetched_rows += int(task_result["fetched_rows"])
                        total_written_rows += int(task_result["written_rows"])
                        success_symbols += 1
                        latest_symbol_date = task_result["latest_symbol_date"]
                        if latest_symbol_date is not None and latest_symbol_date < target_date:
                            incomplete_symbols.append(f"{symbol}:{latest_symbol_date}")
                        failed_write_rows = task_result["failed_write_rows"]
                        if failed_write_rows:
                            partial_write_symbols.append(f"{symbol}:{'; '.join(failed_write_rows[:2])}")

                    if completed_count == 1 or completed_count % 20 == 0:
                        print(
                            f"[progress] completed={completed_count}/{len(market_tasks)} success_symbols={success_symbols} "
                            f"failed_symbols={len(failed_symbols)} partial_write_symbols={len(partial_write_symbols)} "
                            f"fetched_rows={total_fetched_rows} written_rows={total_written_rows}",
                            flush=True,
                        )
                except MarketDataUnavailableError as exc:
                    skipped_unavailable_symbols.append(symbol)
                    print(f"[skip] unavailable data for {symbol}: {exc}", flush=True)
                except Exception as exc:
                    failed_symbols.append(symbol)
                    print(f"Failed to get data for {symbol}: {exc}", flush=True)
    else:
        print("当前模式无需更新股票、基准指数和行业指数", flush=True)

    financial_fetched_rows = 0
    financial_written_rows = 0
    financial_aligned_rows = 0
    financial_stage_ran = False
    backfill_jobs = normalize_backfill_jobs(args.backfill, args.with_financial)
    if backfill_jobs:
        backfill_start_date, backfill_end_date = resolve_backfill_date_range(args.start_date, args.end_date)
        print(
            f"[stage] backfill range={backfill_start_date}..{backfill_end_date} jobs={','.join(backfill_jobs)}",
            flush=True,
        )
        for job in backfill_jobs:
            if job == "derived":
                run_command(
                    "派生字段回填",
                    [
                        sys.executable,
                        "tools/backfill_daily_derived_fields.py",
                        "--start-date",
                        backfill_start_date.isoformat(),
                        "--end-date",
                        backfill_end_date.isoformat(),
                        "--limit-sample",
                        "5",
                    ],
                )
            elif job == "valuation":
                valuation_command = [
                    sys.executable,
                    "tools/backfill_daily_valuation_from_ak.py",
                    "--start-date",
                    backfill_start_date.isoformat(),
                    "--end-date",
                    backfill_end_date.isoformat(),
                    "--only-missing",
                    "--sleep",
                    str(args.valuation_sleep),
                ]
                if args.valuation_limit_symbols > 0:
                    valuation_command.extend(["--limit-symbols", str(args.valuation_limit_symbols)])
                run_command("估值回填", valuation_command)
            elif job == "caps":
                run_command(
                    "市值回填",
                    [
                        sys.executable,
                        "tools/backfill_daily_caps_from_existing_shares.py",
                        "--start-date",
                        backfill_start_date.isoformat(),
                        "--end-date",
                        backfill_end_date.isoformat(),
                        "--limit-sample",
                        "5",
                    ],
                )
            elif job == "turnover":
                run_command(
                    "换手率回填",
                    [
                        sys.executable,
                        "tools/backfill_daily_turnover_rate.py",
                        "--start-date",
                        backfill_start_date.isoformat(),
                        "--end-date",
                        backfill_end_date.isoformat(),
                        "--limit-sample",
                        "5",
                    ],
                )
            elif job == "financial":
                print("[stage] 开始同步财报...", flush=True)
                financial_stage_ran = True
                financial_fetched_rows, financial_written_rows, financial_aligned_rows = run_financial_import(
                    target_date,
                    args.financial_limit,
                    args.financial_workers,
                    backfill_start_date,
                    backfill_end_date,
                    repair_roe_abnormal=args.repair_financial_roe_abnormal,
                    roe_abnormal_threshold=args.financial_roe_abnormal_threshold,
                )

    if args.repair_financial_roe_abnormal and not financial_stage_ran:
        _, _, _, _, _, financial_aligned_rows = run_financial_roe_repair(
            args.financial_workers,
            args.financial_roe_abnormal_threshold,
        )

    print(
        f"增量更新完成: mode={args.mode} range={earliest_start}..{target_date} success_symbols={success_symbols} "
        f"failed_symbols={len(failed_symbols)} skipped_unavailable_symbols={len(skipped_unavailable_symbols)} incomplete_symbols={len(incomplete_symbols)} partial_write_symbols={len(partial_write_symbols)} "
        f"a_share_symbols={share_type_counts['A']} bj_share_symbols={share_type_counts['BJ']} "
        f"skipped_non_target_symbols={len(skipped_symbols)} "
        f"fetched_rows={total_fetched_rows} written_rows={total_written_rows} "
        f"financial_fetched_rows={financial_fetched_rows} financial_written_rows={financial_written_rows} financial_aligned_rows={financial_aligned_rows}"
    )
    if skipped_unavailable_symbols:
        print("跳过的不可获取样本: " + ", ".join(skipped_unavailable_symbols[:20]))
    if failed_symbols:
        print("失败样本: " + ", ".join(failed_symbols[:20]))
    if incomplete_symbols:
        print("未达到目标交易日的样本: " + ", ".join(incomplete_symbols[:20]))
    if partial_write_symbols:
        print("部分逐行写入样本: " + ", ".join(partial_write_symbols[:20]))


if __name__ == "__main__":
    main()
