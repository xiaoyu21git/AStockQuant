"""
backfill_daily_from_baostock.py
使用 Baostock 批量回填 daily_bar 缺失字段（PE/PB/复权/振幅/涨跌额）
注意: Baostock API 不支持逗号分隔批量查询，只能逐只请求
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

WRITE_WORKERS = 4
PROGRESS_INTERVAL = 100
FETCH_WORKERS = 4   # 并发拉取线程数

logger = logging.getLogger(__name__)


def to_bs_code(symbol: str) -> str:
    code, ex = symbol.split(".")
    return f"{ex.lower()}.{code}"


def from_bs_code(bs_code: str) -> str:
    ex, code = bs_code.split(".")
    return f"{code}.{ex.upper()}"


def get_db_connection() -> pymysql.Connection:
    return pymysql.connect(**MYSQL_CONFIG)


def get_all_active_symbols() -> list[str]:
    conn = get_db_connection()
    try:
        with conn.cursor() as cur:
            cur.execute(
                "SELECT symbol FROM symbol_info WHERE asset_class='STOCK' AND status IN ('ACTIVE','ST','*ST') ORDER BY symbol")
            return [r[0] for r in cur.fetchall()]
    finally:
        conn.close()


# ── 单只股票拉取 ──
def fetch_one_stock(symbol: str, start: dt.date, end: dt.date) -> pd.DataFrame:
    bs_code = to_bs_code(symbol)
    fields = "date,code,open,high,low,close,preclose,volume,amount,adjustflag,turn,tradestatus,pctChg,peTTM,pbMRQ,isST"
    rs = bs.query_history_k_data_plus(
        bs_code, fields,
        start_date=start.strftime("%Y-%m-%d"),
        end_date=end.strftime("%Y-%m-%d"),
        frequency="d", adjustflag="2",
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
    if df.empty:
        return df
    norm = df.copy()
    num = ["open","high","low","close","preclose","volume","amount","turn","pctChg","peTTM","pbMRQ"]
    for c in num:
        if c in norm.columns:
            norm[c] = pd.to_numeric(norm[c], errors="coerce")
    norm["trade_date"] = pd.to_datetime(norm["date"]).dt.date
    norm["symbol"] = norm["code"].apply(from_bs_code)
    norm["pre_close"] = norm["preclose"]
    norm["turnover"] = norm["amount"]
    norm["change_pct"] = norm["pctChg"]
    norm["turnover_rate"] = norm["turn"]
    norm["pe_ratio"] = norm["peTTM"]
    norm["pb_ratio"] = norm["pbMRQ"]
    vp = norm["pre_close"].notna() & (norm["pre_close"] > 0)
    norm["change_amt"] = (norm["close"] - norm["pre_close"]).round(4)
    norm["amplitude"] = None
    norm.loc[vp, "amplitude"] = ((norm.loc[vp,"high"] - norm.loc[vp,"low"]) / norm.loc[vp,"pre_close"] * 100).round(4)
    norm["market_cap"] = None
    norm["circulating_market_cap"] = None
    norm["pre_adjust_factor"] = None
    norm["post_adjust_factor"] = None
    if "tradestatus" in norm.columns:
        norm = norm[norm["tradestatus"] == "1"]
    cols = ["symbol","trade_date","open","high","low","close","pre_close","volume","turnover",
            "change_pct","change_amt","amplitude","turnover_rate","pe_ratio","pb_ratio",
            "market_cap","circulating_market_cap","pre_adjust_factor","post_adjust_factor"]
    return norm[[c for c in cols if c in norm.columns]].copy()


# ── DB ──
def get_missing_dates(cur, symbol: str, trade_dates: list) -> set:
    if not trade_dates:
        return set()
    ph = ",".join(["%s"] * len(trade_dates))
    cur.execute(f"""
        SELECT trade_date FROM daily_bar
        WHERE symbol=%s AND trade_date IN ({ph})
          AND (pe_ratio IS NULL OR pe_ratio=0
               OR pb_ratio IS NULL OR pb_ratio=0
               OR pre_adjust_factor IS NULL OR pre_adjust_factor=0
               OR post_adjust_factor IS NULL OR post_adjust_factor=0)
    """, [symbol] + trade_dates)
    return {r[0] for r in cur.fetchall()}


def update_stock(symbol: str, group: pd.DataFrame) -> dict:
    conn = get_db_connection()
    checked = 0; updated = 0
    try:
        with conn.cursor() as cur:
            missing = get_missing_dates(cur, symbol, group["trade_date"].tolist())
            for _, row in group.iterrows():
                td = row["trade_date"]
                if isinstance(td, pd.Timestamp): td = td.date()
                if td not in missing: continue
                checked += 1
                ups = []; prs = []
                for f,c in [("pe_ratio","pe_ratio"),("pb_ratio","pb_ratio"),
                            ("pre_adjust_factor","pre_adjust_factor"),
                            ("post_adjust_factor","post_adjust_factor"),
                            ("change_amt","change_amt"),("amplitude","amplitude")]:
                    v = row.get(c)
                    if v is not None and pd.notna(v): ups.append(f"{f}=%s"); prs.append(float(v))
                if ups:
                    cur.execute(f"UPDATE daily_bar SET {','.join(ups)} WHERE symbol=%s AND trade_date=%s",
                                prs + [symbol, td])
                    updated += 1
            conn.commit()
    except Exception: conn.rollback()
    finally: conn.close()
    return {"symbol":symbol,"checked":checked,"updated":updated}


def backfill_year(symbols: list[str], year: int) -> dict:
    logger.info(f"=== Year {year}: {len(symbols)} stocks ===")
    start = dt.date(year, 1, 1)
    end = min(dt.date(year, 12, 31), dt.date.today())
    if start > end: return {"fetched":0,"updated":0,"checked":0}

    t0 = time.time()

    # Step 1: 并发拉取
    all_groups = []
    completed_fetch = 0; total_sym = len(symbols)
    with ThreadPoolExecutor(max_workers=FETCH_WORKERS) as ex:
        futures = {ex.submit(fetch_one_stock, s, start, end): s for s in symbols}
        for f in as_completed(futures):
            s = futures[f]
            completed_fetch += 1
            df = f.result()
            if not df.empty:
                df = normalize_frame(df)
                if not df.empty:
                    all_groups.append((s, df))
            if completed_fetch % PROGRESS_INTERVAL == 0:
                logger.info(f"Fetch: {completed_fetch}/{total_sym} ({completed_fetch/total_sym*100:.1f}%)")

    t1 = time.time()
    fetched = sum(len(g[1]) for g in all_groups)
    logger.info(f"Fetch done in {t1-t0:.1f}s: {fetched} rows, {len(all_groups)} symbols")

    if not all_groups: return {"fetched":0,"updated":0,"checked":0}

    # Step 2: 并发写入 DB
    total_checked = 0; total_updated = 0; completed = 0; total = len(all_groups)
    with ThreadPoolExecutor(max_workers=WRITE_WORKERS) as ex:
        futures = {ex.submit(update_stock, s, g): s for s, g in all_groups}
        for f in as_completed(futures):
            r = f.result()
            total_checked += r["checked"]; total_updated += r["updated"]; completed += 1
            if completed % PROGRESS_INTERVAL == 0 or completed == total:
                ela = time.time() - t1
                eta = ela / completed * (total - completed) if completed > 0 else 0
                logger.info(f"Write: {completed}/{total} ({completed/total*100:.1f}%) "
                            f"checked={total_checked} updated={total_updated} elapsed={ela:.0f}s eta={eta:.0f}s")

    logger.info(f"Year {year} done: fetched={fetched} checked={total_checked} updated={total_updated}")
    return {"fetched":fetched,"updated":total_updated,"checked":total_checked}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--start-year", type=int, default=2015)
    parser.add_argument("--end-year", type=int, default=None)
    parser.add_argument("--symbols", type=str, default=None)
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    logger.info("Starting Baostock backfill...")
    bs.login()
    try:
        symbols = [s.strip() for s in args.symbols.split(",")] if args.symbols else get_all_active_symbols()
        logger.info(f"Target: {len(symbols)} symbols")
        end_year = args.end_year or dt.date.today().year
        totals = {"fetched":0,"updated":0,"checked":0}
        for y in range(args.start_year, end_year + 1):
            r = backfill_year(symbols, y)
            for k in totals: totals[k] += r.get(k,0)
        logger.info(f"=== DONE: fetched={totals['fetched']} checked={totals['checked']} updated={totals['updated']} ===")
    finally:
        bs.logout()


if __name__ == "__main__":
    main()