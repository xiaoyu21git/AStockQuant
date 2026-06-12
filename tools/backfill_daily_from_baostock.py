"""
backfill_daily_from_baostock.py
使用 Baostock 批量回填 daily_bar 缺失字段：
  - pe_ratio, pb_ratio (Baostock peTTM/pbMRQ)
  - pre_adjust_factor, post_adjust_factor (Baostock adjust_factor)
  - change_amt, amplitude (可推算)
时间范围: 2015-01-01 起
覆盖: 全 A 股 symbol_info 中所有活跃股票
策略: 按月分段拉取，避免整年查询 payload 过大导致空返回
"""

from __future__ import annotations

import argparse
import datetime as dt
import logging
import sys
import time
from pathlib import Path
from typing import Any

import baostock as bs
import pandas as pd
import pymysql

PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools.import_from_baostock import convert_symbol_to_baostock_code, normalize_baostock_frame

MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
}

START_DATE = dt.date(2015, 1, 1)
CHUNK_SIZE = 300

logger = logging.getLogger(__name__)


def get_db_connection() -> pymysql.Connection:
    return pymysql.connect(**MYSQL_CONFIG)


def get_all_active_symbols() -> list[str]:
    conn = get_db_connection()
    try:
        with conn.cursor() as cur:
            cur.execute(
                "SELECT symbol FROM symbol_info WHERE asset_class='STOCK' AND status IN ('ACTIVE','ST','*ST') ORDER BY symbol"
            )
            return [row[0] for row in cur.fetchall()]
    finally:
        conn.close()


def fetch_chunk(
    bs_codes: list[str],
    start_date: dt.date,
    end_date: dt.date,
) -> pd.DataFrame:
    """拉取单个月份/批次的数据"""
    code_str = ",".join(bs_codes)
    rs = bs.query_history_k_data_plus(
        code_str,
        "date,code,open,high,low,close,preclose,volume,amount,adjustflag,turn,tradestatus,pctChg,peTTM,pbMRQ,isST",
        start_date=start_date.strftime("%Y-%m-%d"),
        end_date=end_date.strftime("%Y-%m-%d"),
        frequency="d",
        adjustflag="2",
    )
    if rs.error_code != "0":
        logger.warning(f"API error: {rs.error_msg}")
        return pd.DataFrame()

    rows = []
    while rs.next():
        rows.append(rs.get_row_data())
    if not rows:
        return pd.DataFrame()

    return pd.DataFrame(rows, columns=rs.fields)


def fetch_and_merge_baostock_data(
    symbols: list[str],
    year: int,
) -> pd.DataFrame:
    """
    按月分段拉取指定年份的全量数据
    """
    if not symbols:
        return pd.DataFrame()

    all_dfs = []
    total_symbols = len(symbols)

    for month in range(1, 13):
        start = dt.date(year, month, 1)
        if month == 12:
            end = dt.date(year, 12, 31)
        else:
            end = dt.date(year, month + 1, 1) - dt.timedelta(days=1)

        # 跳过未来月份
        today = dt.date.today()
        if start > today:
            continue
        if end > today:
            end = today

        for i in range(0, total_symbols, CHUNK_SIZE):
            chunk = symbols[i : i + CHUNK_SIZE]
            bs_codes = [convert_symbol_to_baostock_code(s) for s in chunk]
            batch_label = f"y{year}m{month:02d}s{i // CHUNK_SIZE + 1}"

            try:
                df = fetch_chunk(bs_codes, start, end)
                if not df.empty:
                    all_dfs.append(df)
            except Exception as e:
                logger.warning(f"{batch_label} failed: {e}")

            time.sleep(0.03)

    if not all_dfs:
        return pd.DataFrame()

    combined = pd.concat(all_dfs, ignore_index=True)
    return normalize_baostock_frame(combined)


def update_missing_fields(
    cursor: pymysql.cursors.Cursor,
    symbol: str,
    row: dict[str, Any],
) -> int:
    """更新 single row 的缺失字段"""
    updates = []
    params = []

    def add(name: str, value: Any):
        if value is not None and pd.notna(value):
            updates.append(f"{name} = %s")
            params.append(float(value))

    add("pe_ratio", row.get("pe_ratio"))
    add("pb_ratio", row.get("pb_ratio"))
    add("pre_adjust_factor", row.get("pre_adjust_factor"))
    add("post_adjust_factor", row.get("post_adjust_factor"))
    add("change_amt", row.get("change_amt"))
    add("amplitude", row.get("amplitude"))

    if not updates:
        return 0

    trade_date = row["trade_date"]
    if isinstance(trade_date, pd.Timestamp):
        trade_date = trade_date.date()

    sql = f"UPDATE daily_bar SET {', '.join(updates)} WHERE symbol = %s AND trade_date = %s"
    params.extend([symbol, trade_date])

    try:
        cursor.execute(sql, params)
        return cursor.rowcount
    except Exception as e:
        logger.error(f"Update failed {symbol} {trade_date}: {e}")
        return 0


def backfill_year(symbols: list[str], year: int) -> dict[str, int]:
    logger.info(f"=== Year {year}: {len(symbols)} stocks ===")

    df = fetch_and_merge_baostock_data(symbols, year)
    if df.empty:
        logger.warning(f"Year {year}: no data fetched")
        return {"fetched_rows": 0, "updated_rows": 0, "checked_rows": 0}

    logger.info(f"Year {year}: fetched {len(df)} rows for {df['symbol'].nunique()} symbols")

    conn = get_db_connection()
    updated = 0
    checked = 0

    try:
        with conn.cursor() as cur:
            for symbol, group in df.groupby("symbol"):
                trade_dates = group["trade_date"].tolist()
                if not trade_dates:
                    continue

                placeholders = ",".join(["%s"] * len(trade_dates))
                cur.execute(
                    f"""
                    SELECT trade_date FROM daily_bar
                    WHERE symbol = %s AND trade_date IN ({placeholders})
                      AND (
                         pe_ratio IS NULL OR pe_ratio = 0
                         OR pb_ratio IS NULL OR pb_ratio = 0
                         OR pre_adjust_factor IS NULL OR pre_adjust_factor = 0
                         OR post_adjust_factor IS NULL OR post_adjust_factor = 0
                      )
                    """,
                    [symbol] + trade_dates,
                )
                missing_dates = {row[0] for row in cur.fetchall()}

                for _, row_data in group.iterrows():
                    td = row_data["trade_date"]
                    if isinstance(td, pd.Timestamp):
                        td = td.date()
                    if td in missing_dates:
                        checked += 1
                        updated += update_missing_fields(cur, symbol, row_data.to_dict())

                if checked > 0 and checked % 500 == 0:
                    conn.commit()
                    logger.info(f"Progress: checked={checked}, updated={updated}")

            conn.commit()
    except Exception as e:
        conn.rollback()
        logger.error(f"Year {year} transaction failed: {e}")
        raise
    finally:
        conn.close()

    logger.info(f"Year {year} complete: checked={checked}, updated={updated}")
    return {"fetched_rows": len(df), "updated_rows": updated, "checked_rows": checked}


def main():
    parser = argparse.ArgumentParser(description="Baostock 批量回填 daily_bar 缺失字段")
    parser.add_argument("--start-year", type=int, default=2015)
    parser.add_argument("--end-year", type=int, default=None)
    parser.add_argument("--symbols", type=str, default=None)
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    logger.info("Starting Baostock backfill...")

    bs.login()

    try:
        symbols = [s.strip() for s in args.symbols.split(",")] if args.symbols else get_all_active_symbols()
        logger.info(f"Target symbols: {len(symbols)}")

        end_year = args.end_year or dt.date.today().year
        totals = {"fetched_rows": 0, "updated_rows": 0, "checked_rows": 0}

        for y in range(args.start_year, end_year + 1):
            result = backfill_year(symbols, y)
            for k in totals:
                totals[k] += result[k]

        logger.info(f"=== ALL DONE ===")
        logger.info(f"Total fetched: {totals['fetched_rows']}, checked: {totals['checked_rows']}, updated: {totals['updated_rows']}")

    finally:
        bs.logout()


if __name__ == "__main__":
    main()