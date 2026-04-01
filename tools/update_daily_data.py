"""
update_daily_data.py
按最近已收盘交易日增量拉取并更新 A 股日线行情数据，写入数据库。
"""

from __future__ import annotations

import argparse
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
from tools.a_share_symbol_utils import is_mainland_a_share_symbol, normalize_symbol, to_akshare_symbol
from tools.trading_day_utils import (
    DEFAULT_MARKET_CLOSE_TIME,
    parse_time_text,
    resolve_latest_closed_trade_date,
    wait_until_market_close,
)


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


def load_symbol_update_targets(target_date: dt.date) -> tuple[dict[str, dt.date], list[str]]:
    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            cursor.execute(
                """
                SELECT s.symbol, MAX(d.trade_date) AS latest_trade_date
                FROM symbol_info s
                LEFT JOIN daily_bar d ON d.symbol = s.symbol
                WHERE s.asset_class = 'STOCK' AND s.status = 'ACTIVE'
                GROUP BY s.symbol
                HAVING latest_trade_date IS NULL OR latest_trade_date < %s
                ORDER BY s.symbol
                """,
                (target_date,),
            )
            targets: dict[str, dt.date] = {}
            skipped_symbols: list[str] = []
            for symbol, latest_trade_date in cursor.fetchall():
                symbol = str(symbol).strip()
                if not is_mainland_a_share_symbol(symbol):
                    skipped_symbols.append(symbol)
                    continue
                start_date = target_date if latest_trade_date is None else latest_trade_date + dt.timedelta(days=1)
                targets[symbol] = start_date
            return targets, skipped_symbols
    finally:
        conn.close()


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
        "pre_close": None,
        "amplitude": None,
        "turnover_rate": None,
        "pe_ratio": None,
        "pb_ratio": None,
        "market_cap": None,
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
            "amplitude",
            "turnover_rate",
            "pe_ratio",
            "pb_ratio",
            "market_cap",
        ]
    ]


def fetch_symbol_daily(symbol: str, start_date: dt.date, end_date: dt.date) -> pd.DataFrame:
    ak_symbol = to_akshare_symbol(symbol)
    if not ak_symbol:
        raise RuntimeError(f"{symbol}: unsupported market")

    last_error: Exception | None = None
    query_start = start_date - dt.timedelta(days=10)

    for attempt in range(1, 4):
        try:
            df = ak.stock_zh_a_daily(
                symbol=ak_symbol,
                start_date=query_start.strftime("%Y%m%d"),
                end_date=end_date.strftime("%Y%m%d"),
                adjust="",
            )
            if df is None or df.empty:
                return pd.DataFrame()

            df["date"] = pd.to_datetime(df["date"])
            df = df.sort_values("date").reset_index(drop=True)
            df["pre_close"] = df["close"].shift(1)
            df["change_pct"] = ((df["close"] - df["pre_close"]) / df["pre_close"] * 100).round(4)
            df["amplitude"] = ((df["high"] - df["low"]) / df["pre_close"] * 100).round(4)
            df["turnover_rate"] = (df["turnover"] * 100).round(4)
            df = df[df["date"] >= pd.Timestamp(start_date)]
            df["symbol"] = symbol
            return df[[
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
                "amplitude",
                "turnover_rate",
            ]]
        except Exception as exc:
            last_error = exc
            time.sleep(1.5 * attempt)

    raise RuntimeError(f"{symbol}: {last_error}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="按最近已收盘交易日一次性更新全部 A 股日线数据"
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
    update_targets, skipped_symbols = load_symbol_update_targets(target_date)
    if not update_targets:
        latest = latest_trade_date()
        print(
            f"数据库已是最新，latest_trade_date={latest}, target_trade_date={target_date}, "
            f"skipped_non_a_share_symbols={len(skipped_symbols)}"
        )
        if skipped_symbols:
            print("跳过的非A股代码样本: " + ", ".join(skipped_symbols[:20]))
        return

    symbols = list(update_targets.keys())
    if not symbols:
        print("数据库股票池为空，无法执行更新")
        return

    total_fetched_rows = 0
    total_inserted_rows = 0
    success_symbols = 0
    failed_symbols: list[str] = []
    incomplete_symbols: list[str] = []

    earliest_start = min(update_targets.values())
    print(
        f"开始更新: range={earliest_start}..{target_date}, symbol_count={len(symbols)}, "
        f"mode=close-of-day, skipped_non_a_share_symbols={len(skipped_symbols)}"
    )
    if skipped_symbols:
        print("跳过的非A股代码样本: " + ", ".join(skipped_symbols[:20]))
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
        f"增量更新完成: range={earliest_start}..{target_date} success_symbols={success_symbols} "
        f"failed_symbols={len(failed_symbols)} incomplete_symbols={len(incomplete_symbols)} "
        f"skipped_non_a_share_symbols={len(skipped_symbols)} "
        f"fetched_rows={total_fetched_rows} inserted_rows={total_inserted_rows}"
    )
    if failed_symbols:
        print("失败样本: " + ", ".join(failed_symbols[:20]))
    if incomplete_symbols:
        print("未达到目标交易日的样本: " + ", ".join(incomplete_symbols[:20]))


if __name__ == "__main__":
    main()
