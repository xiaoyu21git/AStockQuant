from __future__ import annotations

import argparse
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
import datetime as dt
from pathlib import Path
import sys
import time
from typing import Iterable, Optional

import pandas as pd
import pymysql

PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools.import_from_juejin import _fetch_daily_adjust_factor_map, expand_adjust_factors_for_trade_dates


MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
}

MAX_FETCH_RETRIES = 3
DEFAULT_WORKERS = 8
PROGRESS_HEARTBEAT_SECONDS = 30


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="补齐 dataset 与 daily_bar 的日级复权因子")
    parser.add_argument("--dataset-json", required=True, help="dataset 数据文件路径，例如 bin/Debug/cache/datasets/dataset_62_data.json")
    parser.add_argument("--write-db", action="store_true", help="同时把补齐后的复权因子回写到 daily_bar")
    parser.add_argument("--workers", type=int, default=DEFAULT_WORKERS, help="按标的并发补齐复权因子，默认 8")
    return parser.parse_args()


def normalize_factor(value: object) -> Optional[float]:
    if value is None:
        return None
    try:
        numeric = float(value)
    except Exception:
        return None
    if not pd.notna(numeric) or numeric <= 0:
        return None
    return numeric


def fetch_previous_adjust_factors_from_db(symbol: str, before_date: dt.date) -> tuple[Optional[float], Optional[float]]:
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
    return normalize_factor(row[0]), normalize_factor(row[1])


def fetch_existing_adjust_factors_from_db(symbol: str, start_date: dt.date, end_date: dt.date) -> pd.DataFrame:
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
                "pre_adjust_factor": normalize_factor(row[1]),
                "post_adjust_factor": normalize_factor(row[2]),
            }
            for row in rows
        ]
    )


def build_effective_adjust_factor_frame(symbol: str, trade_dates: Iterable[dt.date]) -> pd.DataFrame:
    ordered_dates = sorted({trade_date for trade_date in trade_dates if trade_date is not None})
    if not ordered_dates:
        return pd.DataFrame(columns=["trade_date", "pre_adjust_factor", "post_adjust_factor"])

    start_date = ordered_dates[0]
    end_date = ordered_dates[-1]
    adjust_factor_by_date = fetch_adjust_factor_map_with_retry(symbol, start_date, end_date)
    seed_pre_adjust_factor, seed_post_adjust_factor = fetch_previous_adjust_factors_from_db(symbol, start_date)
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
    return merge_adjust_factor_frames(ordered_dates, effective_adjust_df, pd.DataFrame(columns=["trade_date", "pre_adjust_factor", "post_adjust_factor"]), existing_adjust_df)


def fetch_adjust_factor_map_with_retry(symbol: str, start_date: dt.date, end_date: dt.date) -> dict[dt.date, dict[str, Optional[float]]]:
    last_error: Exception | None = None
    for attempt in range(1, MAX_FETCH_RETRIES + 1):
        try:
            adjust_factor_by_date = _fetch_daily_adjust_factor_map(symbol, start_date, end_date)
            if adjust_factor_by_date:
                if attempt > 1:
                    print(
                        f"[recover] dataset adjust factor recovered {symbol} attempt={attempt}/{MAX_FETCH_RETRIES} fetched_dates={len(adjust_factor_by_date)}",
                        flush=True,
                    )
                return adjust_factor_by_date
            last_error = RuntimeError("empty adjust factor result")
        except Exception as exc:
            last_error = exc
        if attempt < MAX_FETCH_RETRIES:
            print(
                f"[retry] dataset adjust factor {symbol} attempt={attempt}/{MAX_FETCH_RETRIES} error={last_error}",
                flush=True,
            )
            time.sleep(0.75 * attempt)
    if last_error is not None:
        print(f"[warn] dataset adjust factor failed {symbol}: {last_error}", flush=True)
    return {}


def merge_adjust_factor_frames(
    ordered_dates: list[dt.date],
    fetched_adjust_df: pd.DataFrame,
    source_adjust_df: pd.DataFrame,
    existing_adjust_df: pd.DataFrame,
) -> pd.DataFrame:
    merged = pd.DataFrame({"trade_date": ordered_dates})
    sources = [
        ("fetched", fetched_adjust_df),
        ("source", source_adjust_df),
        ("existing", existing_adjust_df),
    ]
    for prefix, source_df in sources:
        if source_df.empty:
            continue
        merged = merged.merge(
            source_df.rename(
                columns={
                    "pre_adjust_factor": f"{prefix}_pre_adjust_factor",
                    "post_adjust_factor": f"{prefix}_post_adjust_factor",
                }
            ),
            on="trade_date",
            how="left",
        )

    merged["pre_adjust_factor"] = pd.Series([pd.NA] * len(merged), dtype="Float64")
    merged["post_adjust_factor"] = pd.Series([pd.NA] * len(merged), dtype="Float64")
    for prefix in ["fetched", "source", "existing"]:
        pre_column = f"{prefix}_pre_adjust_factor"
        post_column = f"{prefix}_post_adjust_factor"
        if pre_column in merged.columns:
            merged[pre_column] = pd.to_numeric(merged[pre_column], errors="coerce").astype("Float64")
            merged["pre_adjust_factor"] = merged["pre_adjust_factor"].fillna(merged[pre_column])
        if post_column in merged.columns:
            merged[post_column] = pd.to_numeric(merged[post_column], errors="coerce").astype("Float64")
            merged["post_adjust_factor"] = merged["post_adjust_factor"].fillna(merged[post_column])
    return merged[["trade_date", "pre_adjust_factor", "post_adjust_factor"]]


def process_symbol_group(symbol: str, group: pd.DataFrame, write_db: bool) -> tuple[pd.DataFrame, int, int, list[tuple[Optional[float], Optional[float], str, dt.date]]]:
    patched = group.copy()
    patched["__source_index"] = patched.index
    effective_adjust_df = build_effective_adjust_factor_frame(symbol, patched["trade_date"].tolist())
    if effective_adjust_df.empty:
        return patched, 0, 0, []

    patched = patched.merge(
        effective_adjust_df.rename(
            columns={
                "pre_adjust_factor": "resolved_pre_adjust_factor",
                "post_adjust_factor": "resolved_post_adjust_factor",
            }
        ),
        on="trade_date",
        how="left",
    )

    pre_valid = pd.to_numeric(patched.get("pre_adjust_factor"), errors="coerce") > 0
    post_valid = pd.to_numeric(patched.get("post_adjust_factor"), errors="coerce") > 0
    filled_pre = int((~pre_valid & patched["resolved_pre_adjust_factor"].notna()).sum())
    filled_post = int((~post_valid & patched["resolved_post_adjust_factor"].notna()).sum())

    patched.loc[~pre_valid, "pre_adjust_factor"] = patched.loc[~pre_valid, "resolved_pre_adjust_factor"]
    patched.loc[~post_valid, "post_adjust_factor"] = patched.loc[~post_valid, "resolved_post_adjust_factor"]

    db_updates: list[tuple[Optional[float], Optional[float], str, dt.date]] = []
    if write_db:
        for _, row in patched.iterrows():
            db_updates.append(
                (
                    normalize_factor(row.get("pre_adjust_factor")),
                    normalize_factor(row.get("post_adjust_factor")),
                    str(row["symbol"]),
                    row["trade_date"],
                )
            )

    patched = patched.drop(columns=["resolved_pre_adjust_factor", "resolved_post_adjust_factor"])
    return patched, filled_pre, filled_post, db_updates


def update_daily_bar_adjust_factors(rows: list[tuple[Optional[float], Optional[float], str, dt.date]]) -> int:
    if not rows:
        return 0

    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            cursor.executemany(
                """
                UPDATE daily_bar
                SET pre_adjust_factor = COALESCE(%s, pre_adjust_factor),
                    post_adjust_factor = COALESCE(%s, post_adjust_factor)
                WHERE symbol = %s
                  AND trade_date = %s
                  AND (
                      (pre_adjust_factor IS NULL OR pre_adjust_factor <= 0)
                      OR (post_adjust_factor IS NULL OR post_adjust_factor <= 0)
                  )
                """,
                rows,
            )
            affected = cursor.rowcount
        conn.commit()
        return affected
    finally:
        conn.close()


def main() -> None:
    args = parse_args()
    dataset_path = Path(args.dataset_json)
    if not dataset_path.exists():
        raise FileNotFoundError(f"dataset 文件不存在: {dataset_path}")

    df = pd.read_json(dataset_path)
    if df.empty:
        print("dataset 为空，无需补齐")
        return

    df["trade_date"] = pd.to_datetime(df["trade_date"]).dt.date
    total_filled_pre = 0
    total_filled_post = 0
    db_updates: list[tuple[Optional[float], Optional[float], str, dt.date]] = []
    patched_groups: list[pd.DataFrame] = []
    grouped_items = list(df.groupby("symbol", sort=False))
    resolved_workers = max(1, min(args.workers, len(grouped_items) or 1))
    print(
        f"[stage] dataset adjust factor backfill symbols={len(grouped_items)} workers={resolved_workers} write_db={args.write_db}",
        flush=True,
    )

    with ThreadPoolExecutor(max_workers=resolved_workers) as executor:
        future_to_symbol = {
            executor.submit(process_symbol_group, symbol, group, args.write_db): symbol
            for symbol, group in grouped_items
        }
        pending_futures = set(future_to_symbol)
        completed_count = 0
        while pending_futures:
            completed_batch, pending_futures = wait(
                pending_futures,
                timeout=PROGRESS_HEARTBEAT_SECONDS,
                return_when=FIRST_COMPLETED,
            )
            if not completed_batch:
                print(
                    f"[heartbeat] dataset adjust factor running completed={completed_count}/{len(future_to_symbol)} pending={len(pending_futures)} filled_pre={total_filled_pre} filled_post={total_filled_post}",
                    flush=True,
                )
                continue

            for future in completed_batch:
                completed_count += 1
                symbol = future_to_symbol[future]
                patched, filled_pre, filled_post, symbol_updates = future.result()
                total_filled_pre += filled_pre
                total_filled_post += filled_post
                patched_groups.append(patched)
                db_updates.extend(symbol_updates)
                if completed_count == 1 or completed_count % 20 == 0:
                    print(
                        f"[progress] dataset adjust factor completed={completed_count}/{len(future_to_symbol)} filled_pre={total_filled_pre} filled_post={total_filled_post}",
                        flush=True,
                    )
                print(
                    f"[symbol] dataset adjust factor symbol={symbol} filled_pre={filled_pre} filled_post={filled_post}",
                    flush=True,
                )

    patched_df = pd.concat(patched_groups, ignore_index=True)
    if "__source_index" in patched_df.columns:
        patched_df = patched_df.sort_values("__source_index").drop(columns=["__source_index"]).reset_index(drop=True)
    patched_df["trade_date"] = pd.to_datetime(patched_df["trade_date"]).dt.strftime("%Y-%m-%d")
    patched_df.to_json(dataset_path, orient="records", force_ascii=False, indent=2)

    db_affected = update_daily_bar_adjust_factors(db_updates) if args.write_db else 0
    print(
        f"dataset_adjust_factor_backfill done dataset={dataset_path} filled_pre={total_filled_pre} filled_post={total_filled_post} db_affected={db_affected}"
    )


if __name__ == "__main__":
    main()
