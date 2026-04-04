"""
verify_daily_update.py
校验 daily_bar 是否已更新到最近已收盘交易日，并输出落后样本。
"""

from __future__ import annotations

import argparse
import datetime as dt
import sys
from pathlib import Path

import pymysql

PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools.a_share_symbol_utils import classify_mainland_stock_symbol
from tools.trading_day_utils import DEFAULT_MARKET_CLOSE_TIME, parse_time_text, resolve_latest_closed_trade_date

MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="校验 daily_bar 是否已更新到最近已收盘交易日，并输出落后样本"
    )
    parser.add_argument(
        "--target-date",
        help="指定目标交易日，格式 YYYY-MM-DD；未提供时自动解析最近已收盘交易日",
    )
    parser.add_argument(
        "--close-time",
        default=DEFAULT_MARKET_CLOSE_TIME,
        help="收盘判断时间，格式 HH:MM，默认 15:30",
    )
    parser.add_argument(
        "--sample-limit",
        type=int,
        default=20,
        help="最多输出多少条落后股票样本，默认 20",
    )
    return parser.parse_args()


def resolve_target_date(args: argparse.Namespace) -> dt.date:
    if args.target_date:
        return dt.date.fromisoformat(args.target_date)
    return resolve_latest_closed_trade_date(
        dt.datetime.now(),
        parse_time_text(args.close_time),
    )


def fetch_verification_summary(target_date: dt.date, sample_limit: int) -> dict:
    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            cursor.execute("SELECT MAX(trade_date) FROM daily_bar")
            latest_row = cursor.fetchone()
            latest_date = latest_row[0] if latest_row and latest_row[0] else None

            cursor.execute(
                """
                SELECT s.symbol, MAX(d.trade_date) AS latest_trade_date
                FROM symbol_info s
                LEFT JOIN daily_bar d ON d.symbol = s.symbol
                WHERE s.asset_class = 'STOCK' AND s.status = 'ACTIVE'
                GROUP BY s.symbol
                """
            )
            rows = cursor.fetchall()

            active_symbols: list[dict] = []
            share_type_counts = {"A": 0, "B": 0}
            skipped_symbols: list[str] = []
            lagging_samples: list[dict] = []
            for symbol, latest_trade_date in rows:
                symbol_text = str(symbol).strip()
                share_type = classify_mainland_stock_symbol(symbol_text)
                if share_type not in {"A", "B"}:
                    skipped_symbols.append(symbol_text)
                    continue

                share_type_counts[share_type] += 1

                active_symbols.append(
                    {
                        "symbol": symbol_text,
                        "share_type": share_type,
                        "latest_trade_date": latest_trade_date,
                    }
                )
                if latest_trade_date is None or latest_trade_date < target_date:
                    lagging_samples.append(
                        {
                            "symbol": symbol_text,
                            "share_type": share_type,
                            "latest_trade_date": latest_trade_date,
                        }
                    )

            lagging_samples.sort(
                key=lambda item: (
                    item["latest_trade_date"] is not None,
                    item["latest_trade_date"] or dt.date.min,
                    item["symbol"],
                )
            )

            return {
                "target_date": target_date,
                "latest_date": latest_date,
                "active_symbol_count": len(active_symbols),
                "a_share_symbol_count": share_type_counts["A"],
                "b_share_symbol_count": share_type_counts["B"],
                "skipped_symbol_count": len(skipped_symbols),
                "skipped_symbols": skipped_symbols[:sample_limit],
                "lagging_symbol_count": len(lagging_samples),
                "lagging_samples": lagging_samples[:sample_limit],
            }
    finally:
        conn.close()


def main() -> int:
    args = parse_args()
    target_date = resolve_target_date(args)
    summary = fetch_verification_summary(target_date, args.sample_limit)

    print(
        f"校验目标日={summary['target_date']} latest_trade_date={summary['latest_date']} "
        f"active_symbols={summary['active_symbol_count']} a_share_symbols={summary['a_share_symbol_count']} "
        f"b_share_symbols={summary['b_share_symbol_count']} skipped_unsupported_symbols={summary['skipped_symbol_count']} "
        f"lagging_symbols={summary['lagging_symbol_count']}"
    )

    if summary["skipped_symbol_count"] > 0:
        print("跳过的当前脚本不支持代码样本:")
        for symbol in summary["skipped_symbols"]:
            print(f"- {symbol}")

    if summary["lagging_symbol_count"] > 0:
        print("落后样本:")
        for sample in summary["lagging_samples"]:
            print(f"- {sample['symbol']} ({sample['share_type']}): {sample['latest_trade_date']}")
        return 2

    if summary["latest_date"] != summary["target_date"]:
        print("告警: daily_bar 最大交易日未达到目标交易日")
        return 2

    print("校验通过: daily_bar 已更新到目标交易日")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())