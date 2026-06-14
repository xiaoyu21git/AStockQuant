#!/usr/bin/env python
"""
backfill_daily_from_baostock.py
使用 Baostock 批量回填 daily_bar 缺失字段（PE/PB/复权/振幅/涨跌额）
支持 tqdm 进度条，逐只请求（Baostock 不支持批量拼接）
用法:
  python tools/backfill_daily_from_baostock.py --start-year 2015
  python tools/backfill_daily_from_baostock.py --start-year 2015 --end-year 2024
"""

from __future__ import annotations

import argparse
import datetime as dt
import logging
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Any

import baostock as bs
import pandas as pd
import pymysql
import tqdm

PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
}

DB_WRITE_WORKERS = 4      # 并发写 DB 线程
BATCH_SIZE = 200           # 每批处理的股票数量（控制内存）
BAOSTOCK_RECONNECT_INTERVAL = 300  # 每 N 只股票重连一次 Baostock（防止连接超时断开）
PROGRESS_BAR_WIDTH = 80   # tqdm 宽度

logger = logging.getLogger(__name__)
logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")


# ── 代码转换 ───────────────────────────────────────────────
def to_bs_code(symbol: str) -> str:
    code, ex = symbol.split(".")
    return f"{ex.lower()}.{code}"


def from_bs_code(bs_code: str) -> str:
    ex, code = bs_code.split(".")
    return f"{code}.{ex.upper()}"


# ── DB 连接 ────────────────────────────────────────────────
def get_db_connection() -> pymysql.Connection:
    return pymysql.connect(**MYSQL_CONFIG)


def get_all_active_symbols() -> list[str]:
    conn = get_db_connection()
    try:
        with conn.cursor() as cur:
            cur.execute(
                "SELECT symbol FROM symbol_info WHERE asset_class='STOCK' "
                "AND status IN ('ACTIVE','ST','*ST') ORDER BY symbol"
            )
            return [r[0] for r in cur.fetchall()]
    finally:
        conn.close()


# ── 单只股票拉取 ────────────────────────────────────────────
def fetch_one_stock(symbol: str, start: dt.date, end: dt.date) -> pd.DataFrame:
    """
    从 Baostock 拉取一只股票的数据
    返回包含 date, code, open, high, low, close, preclose,
              volume, amount, turn, tradestatus, pctChg, peTTM, pbMRQ, isST 的 DataFrame
    """
    bs_code = to_bs_code(symbol)
    fields = (
        "date,code,open,high,low,close,preclose,"
        "volume,amount,adjustflag,turn,tradestatus,"
        "pctChg,peTTM,pbMRQ,isST"
    )
    rs = bs.query_history_k_data_plus(
        bs_code,
        fields,
        start_date=start.strftime("%Y-%m-%d"),
        end_date=end.strftime("%Y-%m-%d"),
        frequency="d",
        adjustflag="2",
    )

    if rs.error_code != "0":
        return pd.DataFrame()

    rows = []
    while rs.next():
        rows.append(rs.get_row_data())

    if not rows:
        return pd.DataFrame()

    return pd.DataFrame(rows, columns=rs.fields)


def normalize_frame(df: pd.DataFrame) -> pd.DataFrame:
    """将 Baostock 原始数据转换为 daily_bar 标准字段"""
    if df.empty:
        return df

    norm = df.copy()

    # 数值转换
    num_cols = [
        "open", "high", "low", "close", "preclose",
        "volume", "amount", "turn", "pctChg", "peTTM", "pbMRQ",
    ]
    for col in num_cols:
        if col in norm.columns:
            norm[col] = pd.to_numeric(norm[col], errors="coerce")

    # 映射字段
    norm["trade_date"] = pd.to_datetime(norm["date"]).dt.date
    norm["symbol"] = norm["code"].apply(from_bs_code)
    norm["pre_close"] = norm["preclose"]
    norm["turnover"] = norm["amount"]
    norm["change_pct"] = norm["pctChg"]
    norm["turnover_rate"] = norm["turn"]
    norm["pe_ratio"] = norm["peTTM"]
    norm["pb_ratio"] = norm["pbMRQ"]

    # 可推算字段
    valid_pre = norm["pre_close"].notna() & (norm["pre_close"] > 0)
    norm["change_amt"] = (norm["close"] - norm["pre_close"]).round(4)
    norm["amplitude"] = None
    norm.loc[valid_pre, "amplitude"] = (
        (norm.loc[valid_pre, "high"] - norm.loc[valid_pre, "low"])
        / norm.loc[valid_pre, "pre_close"]
        * 100
    ).round(4)

    # Baostock 不提供的字段
    norm["market_cap"] = None
    norm["circulating_market_cap"] = None
    norm["pre_adjust_factor"] = None
    norm["post_adjust_factor"] = None

    # 只保留交易日
    if "tradestatus" in norm.columns:
        norm = norm[norm["tradestatus"] == "1"]

    output_cols = [
        "symbol", "trade_date",
        "open", "high", "low", "close", "pre_close",
        "volume", "turnover", "change_pct", "change_amt", "amplitude",
        "turnover_rate", "pe_ratio", "pb_ratio",
        "market_cap", "circulating_market_cap",
        "pre_adjust_factor", "post_adjust_factor",
    ]
    return norm[[c for c in output_cols if c in norm.columns]].copy()


# ── DB 写入 ────────────────────────────────────────────────
def get_missing_dates(cur, symbol: str, trade_dates: list) -> set:
    if not trade_dates:
        return set()
    ph = ",".join(["%s"] * len(trade_dates))
    cur.execute(
        f"""
        SELECT trade_date FROM daily_bar
        WHERE symbol = %s AND trade_date IN ({ph})
          AND (pe_ratio IS NULL OR pe_ratio = 0
               OR pb_ratio IS NULL OR pb_ratio = 0
               OR pre_adjust_factor IS NULL OR pre_adjust_factor = 0
               OR post_adjust_factor IS NULL OR post_adjust_factor = 0)
        """,
        [symbol] + trade_dates,
    )
    return {r[0] for r in cur.fetchall()}


def update_stock(symbol: str, group: pd.DataFrame) -> dict[str, int]:
    """更新单只股票的缺失字段"""
    conn = get_db_connection()
    checked = 0
    updated = 0
    try:
        with conn.cursor() as cur:
            missing = get_missing_dates(cur, symbol, group["trade_date"].tolist())
            for _, row in group.iterrows():
                td = row["trade_date"]
                if isinstance(td, pd.Timestamp):
                    td = td.date()
                if td not in missing:
                    continue
                checked += 1

                updates = []
                params = []
                field_map = [
                    ("pe_ratio", "pe_ratio"),
                    ("pb_ratio", "pb_ratio"),
                    ("pre_adjust_factor", "pre_adjust_factor"),
                    ("post_adjust_factor", "post_adjust_factor"),
                    ("change_amt", "change_amt"),
                    ("amplitude", "amplitude"),
                ]
                for field, col in field_map:
                    val = row.get(col)
                    if val is not None and pd.notna(val):
                        updates.append(f"{field} = %s")
                        params.append(float(val))

                if updates:
                    sql = f"UPDATE daily_bar SET {', '.join(updates)} WHERE symbol = %s AND trade_date = %s"
                    params.extend([symbol, td])
                    try:
                        cur.execute(sql, params)
                        updated += 1
                    except Exception:
                        pass

            conn.commit()
    except Exception:
        conn.rollback()
    finally:
        conn.close()

    return {"checked": checked, "updated": updated}


# ── Baostock 连接管理 ──────────────────────────────────────
_bs_reconnect_counter = 0


def ensure_bs_connected():
    """检测 Baostock 连接状态，超时/断开时自动重连"""
    global _bs_reconnect_counter
    _bs_reconnect_counter += 1
    if _bs_reconnect_counter % BAOSTOCK_RECONNECT_INTERVAL == 0:
        try:
            bs.logout()
        except Exception:
            pass
        lg = bs.login()
        if lg.error_code != "0":
            logger.warning(f"Baostock reconnect failed: {lg.error_msg}")
        else:
            logger.info(f"Baostock reconnected after {_bs_reconnect_counter} queries")


def fetch_one_stock_safe(symbol: str, start: dt.date, end: dt.date) -> pd.DataFrame:
    """带重连保护的 fetch_one_stock 包装"""
    ensure_bs_connected()
    try:
        return fetch_one_stock(symbol, start, end)
    except Exception as e:
        logger.warning(f"fetch_one_stock failed for {symbol}: {e}, retrying after reconnect...")
        try:
            bs.logout()
        except Exception:
            pass
        lg = bs.login()
        if lg.error_code != "0":
            logger.error(f"Baostock reconnect after error failed: {lg.error_msg}")
            return pd.DataFrame()
        time.sleep(1)
        try:
            return fetch_one_stock(symbol, start, end)
        except Exception as e2:
            logger.error(f"fetch_one_stock retry failed for {symbol}: {e2}")
            return pd.DataFrame()


def update_stock_safe(symbol: str, group: pd.DataFrame) -> dict[str, int]:
    """带重试的 update_stock 包装"""
    try:
        return update_stock(symbol, group)
    except Exception as e:
        logger.warning(f"update_stock failed for {symbol}: {e}")
        return {"checked": 0, "updated": 0}


# ── 回填主流程 ──────────────────────────────────────────────
def process_batch(batch_groups: list[tuple[str, pd.DataFrame]],
                  batch_index: int,
                  year: int) -> tuple[int, int]:
    """处理一批股票的 DB 写入，返回 (checked, updated)"""
    if not batch_groups:
        return 0, 0

    total_checked = 0
    total_updated = 0

    with ThreadPoolExecutor(max_workers=DB_WRITE_WORKERS) as executor:
        futures = {
            executor.submit(update_stock_safe, sym, grp): sym
            for sym, grp in batch_groups
        }
        for future in as_completed(futures):
            result = future.result()
            total_checked += result["checked"]
            total_updated += result["updated"]

    return total_checked, total_updated


def backfill_year(symbols: list[str], year: int) -> dict[str, int]:
    """
    回填指定年份的数据，使用分批加载策略避免内存溢出。
    每 BATCH_SIZE 只股票拉取后立即写入 DB，释放内存后再处理下一批。
    返回: {"fetched": int, "updated": int, "checked": int}
    """
    start = dt.date(year, 1, 1)
    end = min(dt.date(year, 12, 31), dt.date.today())
    if start > end:
        return {"fetched": 0, "updated": 0, "checked": 0}

    total_symbols = len(symbols)
    logger.info(f"Year {year}: fetching {total_symbols} stocks (batch_size={BATCH_SIZE})...")

    total_fetched_rows = 0
    total_checked = 0
    total_updated = 0
    total_fetch_errors = 0
    batch_count = (total_symbols + BATCH_SIZE - 1) // BATCH_SIZE
    t0 = time.time()

    # 外层进度条：批次
    with tqdm.tqdm(
        total=total_symbols,
        desc=f"Year {year}",
        unit="stock",
        ncols=PROGRESS_BAR_WIDTH,
        bar_format="{l_bar}{bar}| {n_fmt}/{total_fmt} [{elapsed}<{remaining}]",
    ) as pbar:
        for batch_idx in range(batch_count):
            batch_start = batch_idx * BATCH_SIZE
            batch_end = min(batch_start + BATCH_SIZE, total_symbols)
            batch_symbols = symbols[batch_start:batch_end]

            # Step 1: 逐只拉取当前批次（不累积全局列表），每只股票更新进度条
            batch_groups: list[tuple[str, pd.DataFrame]] = []
            batch_fetch_errors = 0

            for i, symbol in enumerate(batch_symbols):
                try:
                    raw_df = fetch_one_stock_safe(symbol, start, end)
                    if not raw_df.empty:
                        norm_df = normalize_frame(raw_df)
                        if not norm_df.empty:
                            batch_groups.append((symbol, norm_df))
                        else:
                            batch_fetch_errors += 1
                    else:
                        batch_fetch_errors += 1
                except Exception as e:
                    batch_fetch_errors += 1
                    logger.debug(f"fetch error {symbol}: {e}")

                # 每只股票立即更新进度条，避免长间隔无响应
                pbar.update(1)
                batch_fetched = sum(len(g[1]) for g in batch_groups)
                pbar.set_postfix_str(
                    f"rows={total_fetched_rows + batch_fetched} "
                    f"got={len(batch_groups)} err={total_fetch_errors + batch_fetch_errors}"
                )

            total_fetch_errors += batch_fetch_errors
            batch_fetched_rows = sum(len(g[1]) for g in batch_groups)
            total_fetched_rows += batch_fetched_rows

            # Step 2: 立即写入本批次
            if batch_groups:
                batch_checked, batch_updated = process_batch(
                    batch_groups, batch_idx, year
                )
                total_checked += batch_checked
                total_updated += batch_updated

            # 释放批次内存
            del batch_groups

            # 更新批次汇总信息（per-stock 循环已更新 pbar，这里只刷新状态文字）
            logger.info(
                f"Year {year} batch {batch_idx + 1}/{batch_count} done: "
                f"fetched_rows={batch_fetched_rows} checked={batch_checked} updated={batch_updated}"
            )

    t1 = time.time()
    logger.info(
        f"Year {year}: fetched {total_fetched_rows} rows from "
        f"{total_symbols - total_fetch_errors} stocks "
        f"({total_fetch_errors} errors), "
        f"checked {total_checked}, updated {total_updated} "
        f"in {t1 - t0:.1f}s"
    )

    return {"fetched": total_fetched_rows, "updated": total_updated, "checked": total_checked}


def main():
    parser = argparse.ArgumentParser(
        description="Baostock 批量回填 daily_bar 缺失字段"
    )
    parser.add_argument(
        "--start-year", type=int, default=2015, help="起始年份 (默认 2015)"
    )
    parser.add_argument(
        "--end-year", type=int, default=None, help="结束年份 (默认今年)"
    )
    parser.add_argument(
        "--symbols", type=str, default=None, help="仅处理指定股票 (逗号分隔)"
    )
    args = parser.parse_args()

    print("=" * 60)
    print("  Baostock daily_bar 回填工具")
    print("  回填字段: PE, PB, 复权因子, 振幅, 涨跌额")
    print("=" * 60)
    print()

    logger.info("Starting Baostock backfill...")

    # 登录
    lg = bs.login()
    if lg.error_code != "0":
        print(f"[ERROR] Baostock login failed: {lg.error_msg}")
        return

    try:
        # 获取符号列表
        if args.symbols:
            symbols = [s.strip() for s in args.symbols.split(",") if s.strip()]
        else:
            symbols = get_all_active_symbols()

        logger.info(f"Target: {len(symbols)} symbols")

        end_year = args.end_year or dt.date.today().year
        totals = {"fetched": 0, "updated": 0, "checked": 0}

        years = list(range(args.start_year, end_year + 1))
        print(f"Years to process: {years[0]} ~ {years[-1]} ({len(years)} years)")
        print()

        for y in years:
            result = backfill_year(symbols, y)
            for k in totals:
                totals[k] += result.get(k, 0)
            print()

        print("=" * 60)
        print(f"  ALL DONE")
        print(f"  Fetched:  {totals['fetched']:>12,} rows")
        print(f"  Checked:  {totals['checked']:>12,} records")
        print(f"  Updated:  {totals['updated']:>12,} fields")
        print("=" * 60)

    finally:
        bs.logout()


if __name__ == "__main__":
    main()