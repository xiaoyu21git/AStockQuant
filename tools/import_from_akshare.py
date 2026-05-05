from __future__ import annotations

import argparse
import datetime as dt
import math
import sys
from pathlib import Path
from typing import Any, Dict, Iterable

PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools.akshare_data_sources import (
    fetch_benchmark_index_symbol_rows,
    fetch_stock_symbol_rows,
    get_connection,
)
from tools.update_daily_data import fetch_benchmark_daily, fetch_symbol_daily


DEFAULT_START_DATE = dt.date(1998, 1, 1)
DEFAULT_END_DATE = dt.date.today()
DATA_SOURCE_AKSHARE = "AKSHARE_MARKET_SUPPLEMENT"


def mysql_safe_number(value: Any, default: Any = None) -> Any:
    if value is None:
        return default
    try:
        numeric = float(value)
    except Exception:
        return default
    if math.isnan(numeric) or math.isinf(numeric):
        return default
    return value


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="使用 AkShare 补充行情、基准指数日线与 symbol_info")
    parser.add_argument("--start-date", default=DEFAULT_START_DATE.isoformat(), help="开始日期，格式 yyyy-mm-dd")
    parser.add_argument("--end-date", default=DEFAULT_END_DATE.isoformat(), help="结束日期，格式 yyyy-mm-dd")
    parser.add_argument("--include-b-shares", action="store_true", default=True, help="导入 B 股")
    parser.add_argument("--skip-b-shares", action="store_true", help="跳过 B 股")
    parser.add_argument("--limit-symbols", type=int, default=0, help="仅导入前 N 个股票，0 表示不限")
    return parser.parse_args()


def upsert_symbol_info(cursor, symbols: Iterable[Dict[str, Any]]):
    sql = """
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
    """
    data = list(symbols)
    if data:
        cursor.executemany(sql, data)


def upsert_daily_bars(cursor, symbol: str, bars: Iterable[Dict[str, Any]]):
    sql = """
    INSERT INTO daily_bar (
        symbol, trade_date, open, high, low, close, pre_close,
        volume, turnover, change_pct, change_amt, amplitude,
        turnover_rate, pe_ratio, pb_ratio, market_cap, circulating_market_cap,
        pre_adjust_factor, post_adjust_factor, data_source
    ) VALUES (
        %(symbol)s, %(trade_date)s, %(open)s, %(high)s, %(low)s, %(close)s, %(pre_close)s,
        %(volume)s, %(turnover)s, %(change_pct)s, %(change_amt)s, %(amplitude)s,
        %(turnover_rate)s, %(pe_ratio)s, %(pb_ratio)s, %(market_cap)s, %(circulating_market_cap)s,
        %(pre_adjust_factor)s, %(post_adjust_factor)s, %(data_source)s
    )
    ON DUPLICATE KEY UPDATE
        open = VALUES(open), high = VALUES(high), low = VALUES(low), close = VALUES(close),
        pre_close = VALUES(pre_close), volume = VALUES(volume), turnover = VALUES(turnover),
        change_pct = VALUES(change_pct), change_amt = VALUES(change_amt),
        amplitude = VALUES(amplitude), turnover_rate = VALUES(turnover_rate),
        pe_ratio = VALUES(pe_ratio), pb_ratio = VALUES(pb_ratio), market_cap = VALUES(market_cap),
        circulating_market_cap = VALUES(circulating_market_cap),
        pre_adjust_factor = VALUES(pre_adjust_factor), post_adjust_factor = VALUES(post_adjust_factor),
        data_source = VALUES(data_source)
    """
    data = []
    for b in bars:
        trade_date = b.get("trade_date", b.get("date"))
        turnover = b.get("turnover", b.get("amount", 0.0))
        data.append({
            "symbol": symbol,
            "trade_date": trade_date,
            "open": mysql_safe_number(b.get("open", 0.0), 0.0),
            "high": mysql_safe_number(b.get("high", 0.0), 0.0),
            "low": mysql_safe_number(b.get("low", 0.0), 0.0),
            "close": mysql_safe_number(b.get("close", 0.0), 0.0),
            "pre_close": mysql_safe_number(b.get("pre_close", 0.0), 0.0),
            "volume": mysql_safe_number(b.get("volume", 0.0), 0.0),
            "turnover": mysql_safe_number(turnover, 0.0),
            "change_pct": mysql_safe_number(b.get("change_pct", 0.0), None),
            "change_amt": mysql_safe_number(b.get("change_amt", 0.0), None),
            "amplitude": mysql_safe_number(b.get("amplitude", 0.0), None),
            "turnover_rate": mysql_safe_number(b.get("turnover_rate"), None),
            "pe_ratio": mysql_safe_number(b.get("pe_ratio"), None),
            "pb_ratio": mysql_safe_number(b.get("pb_ratio"), None),
            "market_cap": mysql_safe_number(b.get("market_cap"), None),
            "circulating_market_cap": mysql_safe_number(b.get("circulating_market_cap"), None),
            "pre_adjust_factor": mysql_safe_number(b.get("pre_adjust_factor"), None),
            "post_adjust_factor": mysql_safe_number(b.get("post_adjust_factor"), None),
            "data_source": b.get("data_source", DATA_SOURCE_AKSHARE),
        })
    if data:
        cursor.executemany(sql, data)


def main() -> None:
    args = parse_args()
    start_date = dt.date.fromisoformat(args.start_date)
    end_date = dt.date.fromisoformat(args.end_date)
    include_b_shares = args.include_b_shares and not args.skip_b_shares

    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            stock_rows = fetch_stock_symbol_rows(include_b_shares=include_b_shares, include_delisted=True)
            if args.limit_symbols > 0:
                stock_rows = stock_rows[: args.limit_symbols]
            benchmark_rows = fetch_benchmark_index_symbol_rows()

            print(f"[import] upsert symbol_info: stocks={len(stock_rows)} benchmarks={len(benchmark_rows)}", flush=True)
            upsert_symbol_info(cursor, stock_rows)
            upsert_symbol_info(cursor, benchmark_rows)
            conn.commit()

            for index, row in enumerate(stock_rows, start=1):
                symbol = row["symbol"]
                print(f"[import] ({index}/{len(stock_rows)}) {symbol} {row.get('name', '')} 补充日线……", flush=True)
                daily = fetch_symbol_daily(symbol, start_date, end_date)
                if daily is not None and not daily.empty:
                    upsert_daily_bars(cursor, symbol, daily.to_dict("records"))
                conn.commit()

            for index, row in enumerate(benchmark_rows, start=1):
                symbol = row["symbol"]
                print(f"[import] ({index}/{len(benchmark_rows)}) {symbol} {row.get('name', '')} 基准指数补充日线……", flush=True)
                daily = fetch_benchmark_daily(symbol, start_date, end_date)
                if daily is not None and not daily.empty:
                    upsert_daily_bars(cursor, symbol, daily.to_dict("records"))
                conn.commit()
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


if __name__ == "__main__":
    main()
