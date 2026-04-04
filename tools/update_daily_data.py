"""
update_daily_data.py
按最近已收盘交易日增量拉取并更新沪深 A/B 股日线行情数据，写入数据库。
"""

from __future__ import annotations

import argparse
import bisect
import datetime as dt
import os
import sys
import time
from pathlib import Path
from typing import Optional

import akshare as ak
import pandas as pd
import pymysql

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
from tools.daily_bar_quality import detect_daily_price_anomalies, filter_valid_records, format_invalid_samples
from tools.trading_day_utils import (
    DEFAULT_MARKET_CLOSE_TIME,
    get_trade_calendar,
    parse_time_text,
    resolve_latest_closed_trade_date,
    wait_until_market_close,
)


MAX_FETCH_RETRIES = 3
INVALID_SAMPLE_LIMIT = 3


def latest_trade_date() -> Optional[dt.date]:
    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            cursor.execute("SELECT MAX(trade_date) FROM daily_bar")
            row = cursor.fetchone()
            return row[0] if row and row[0] else None
    finally:
        conn.close()


def load_symbols_from_db() -> list[str]:
    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            cursor.execute(
                """
                SELECT symbol
                FROM symbol_info
                WHERE asset_class = 'STOCK' AND status = 'ACTIVE'
                ORDER BY symbol
                """
            )
            return [str(row[0]).strip() for row in cursor.fetchall() if row and row[0]]
    finally:
        conn.close()


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
                       MIN(d.trade_date) AS earliest_trade_date,
                       MAX(d.trade_date) AS latest_trade_date,
                       COUNT(DISTINCT d.trade_date) AS trade_date_count
                FROM symbol_info s
                LEFT JOIN daily_bar d ON d.symbol = s.symbol
                WHERE s.asset_class = 'STOCK' AND s.status = 'ACTIVE'
                GROUP BY s.symbol
                ORDER BY s.symbol
                """,
            )
            targets: dict[str, dt.date] = {}
            skipped_symbols: list[str] = []
            for symbol, earliest_trade_date, latest_trade_date, trade_date_count in cursor.fetchall():
                symbol = str(symbol).strip()
                if not is_supported_akshare_stock_symbol(symbol):
                    skipped_symbols.append(symbol)
                    continue

                if latest_trade_date is None:
                    if mode in {"latest", "all"}:
                        targets[symbol] = target_date
                    continue

                if earliest_trade_date is None:
                    targets[symbol] = target_date
                    continue

                expected_dates = calendar_dates_between(earliest_trade_date, target_date)
                if not expected_dates:
                    continue

                latest_covered_dates = calendar_dates_between(earliest_trade_date, latest_trade_date)
                has_tail_gap_only = latest_trade_date < target_date and int(trade_date_count or 0) == len(latest_covered_dates)
                if has_tail_gap_only:
                    if mode in {"latest", "all"}:
                        targets[symbol] = expected_dates[len(latest_covered_dates)]
                    continue

                expected_count = len(expected_dates)
                if latest_trade_date >= target_date and int(trade_date_count or 0) >= expected_count:
                    continue

                cursor.execute(
                    """
                    SELECT trade_date
                    FROM daily_bar
                    WHERE symbol = %s AND trade_date BETWEEN %s AND %s
                    ORDER BY trade_date
                    """,
                    (symbol, earliest_trade_date, target_date),
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
    counts = {"A": 0, "B": 0}
    for symbol in symbols:
        share_type = classify_mainland_stock_symbol(symbol)
        if share_type in counts:
            counts[share_type] += 1
    return counts


def normalize_daily_frame(df: pd.DataFrame) -> pd.DataFrame:
    normalized = df.copy()
    normalized = normalized.rename(
        columns={
            "date": "trade_date",
            "amount": "turnover",
            "pct_change": "change_pct",
            "turnover": "turnover_rate",
        }
    )
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
            "data_source",
        ]
    ]


def fetch_symbol_daily(symbol: str, start_date: dt.date, end_date: dt.date) -> pd.DataFrame:
    ak_symbol = to_akshare_symbol(symbol)
    share_type = classify_mainland_stock_symbol(symbol)
    if not ak_symbol or share_type not in {"A", "B"}:
        raise RuntimeError(f"{symbol}: unsupported market")

    fetchers = [("stock_zh_a_daily", ak.stock_zh_a_daily)]
    if share_type == "B":
        fetchers.append(("stock_zh_b_daily", ak.stock_zh_b_daily))

    error_messages: list[str] = []
    query_start = start_date - dt.timedelta(days=10)

    for fetcher_name, fetcher in fetchers:
        last_error: Exception | None = None
        for attempt in range(1, MAX_FETCH_RETRIES + 1):
            try:
                df = fetcher(
                    symbol=ak_symbol,
                    start_date=query_start.strftime("%Y%m%d"),
                    end_date=end_date.strftime("%Y%m%d"),
                    adjust="",
                )
                if df is None or df.empty:
                    break

                df["date"] = pd.to_datetime(df["date"])
                df = df.sort_values("date").reset_index(drop=True)
                df["pre_close"] = df["close"].shift(1)
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

                df["symbol"] = symbol
                df["data_source"] = "AKSHARE_STOCK_DAILY_B" if share_type == "B" else "AKSHARE_STOCK_DAILY_A"
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
                    "data_source",
                ]]
                valid_records, invalid_samples = filter_valid_records(
                    result_df.to_dict("records"),
                    detect_daily_price_anomalies,
                )
                if invalid_samples:
                    summary = format_invalid_samples(invalid_samples, INVALID_SAMPLE_LIMIT)
                    if attempt < MAX_FETCH_RETRIES:
                        raise ValueError(f"abnormal_rows={len(invalid_samples)} samples={summary}")
                    if not valid_records:
                        raise ValueError(f"abnormal_rows={len(invalid_samples)} samples={summary}")
                    print(
                        f"[warn] {symbol} {fetcher_name} abnormal_rows={len(invalid_samples)} use_valid_rows={len(valid_records)} samples={summary}",
                        flush=True,
                    )
                if not valid_records:
                    return pd.DataFrame()
                return pd.DataFrame(valid_records)
            except Exception as exc:
                last_error = exc
                time.sleep(1.5 * attempt)

        if last_error is not None:
            error_messages.append(f"{fetcher_name}: {last_error}")

    if error_messages:
        raise RuntimeError(f"{symbol}: {'; '.join(error_messages)}")
    return pd.DataFrame()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="按最近已收盘交易日更新沪深 A/B 股日线数据，可选仅对齐最新、仅补历史缺口或两者都做"
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
    update_targets, skipped_symbols = load_symbol_update_targets(target_date, args.mode)
    if not update_targets:
        latest = latest_trade_date()
        print(
            f"当前模式无需更新，mode={args.mode} latest_trade_date={latest}, target_trade_date={target_date}, "
            f"skipped_non_a_share_symbols={len(skipped_symbols)}"
        )
        if skipped_symbols:
            print("跳过的非A股代码样本: " + ", ".join(skipped_symbols[:20]))
        return

    symbols = list(update_targets.keys())
    if not symbols:
        print("数据库股票池为空，无法执行更新")
        return

    share_type_counts = summarize_share_type_counts(symbols)
    total_fetched_rows = 0
    total_inserted_rows = 0
    success_symbols = 0
    failed_symbols: list[str] = []
    incomplete_symbols: list[str] = []

    earliest_start = min(update_targets.values())
    print(
        f"开始更新: mode={args.mode} range={earliest_start}..{target_date}, symbol_count={len(symbols)}, "
        f"a_share_symbols={share_type_counts['A']} b_share_symbols={share_type_counts['B']} "
        f"mode=close-of-day, skipped_unsupported_symbols={len(skipped_symbols)}"
    )
    if skipped_symbols:
        print("跳过的当前脚本不支持代码样本: " + ", ".join(skipped_symbols[:20]))
    for index, symbol in enumerate(symbols, start=1):
        try:
            raw_df = fetch_symbol_daily(symbol, update_targets[symbol], target_date)
            if raw_df.empty:
                incomplete_symbols.append(f"{symbol}:empty")
                continue

            normalized = normalize_daily_frame(raw_df)
            normalized = normalized[normalized["trade_date"] <= target_date].copy()
            if normalized.empty:
                incomplete_symbols.append(f"{symbol}:empty")
                continue

            latest_symbol_date = normalized["trade_date"].max()
            if latest_symbol_date < target_date:
                incomplete_symbols.append(f"{symbol}:{latest_symbol_date}")

            inserted_rows = DatabaseRepository.save_daily_bars(normalized)
            total_fetched_rows += len(normalized)
            total_inserted_rows += inserted_rows
            success_symbols += 1

            if index % 50 == 0:
                print(
                    f"progress index={index}/{len(symbols)} success_symbols={success_symbols} "
                    f"failed_symbols={len(failed_symbols)} fetched_rows={total_fetched_rows} "
                    f"inserted_rows={total_inserted_rows}"
                )

            time.sleep(0.15)
        except Exception as exc:
            failed_symbols.append(symbol)
            print(f"Failed to get data for {symbol}: {exc}")

    print(
        f"增量更新完成: mode={args.mode} range={earliest_start}..{target_date} success_symbols={success_symbols} "
        f"failed_symbols={len(failed_symbols)} incomplete_symbols={len(incomplete_symbols)} "
        f"a_share_symbols={share_type_counts['A']} b_share_symbols={share_type_counts['B']} "
        f"skipped_unsupported_symbols={len(skipped_symbols)} "
        f"fetched_rows={total_fetched_rows} inserted_rows={total_inserted_rows}"
    )
    if failed_symbols:
        print("失败样本: " + ", ".join(failed_symbols[:20]))
    if incomplete_symbols:
        print("未达到目标交易日的样本: " + ", ".join(incomplete_symbols[:20]))


if __name__ == "__main__":
    main()
