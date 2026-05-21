from __future__ import annotations

import argparse
import datetime as dt
from pathlib import Path
import sys
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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="补齐 dataset 与 daily_bar 的日级复权因子")
    parser.add_argument("--dataset-json", required=True, help="dataset 数据文件路径，例如 bin/Debug/cache/datasets/dataset_62_data.json")
    parser.add_argument("--write-db", action="store_true", help="同时把补齐后的复权因子回写到 daily_bar")
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
    adjust_factor_by_date = _fetch_daily_adjust_factor_map(symbol, start_date, end_date)
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

    for symbol, group in df.groupby("symbol", sort=False):
        patched = group.copy()
        effective_adjust_df = build_effective_adjust_factor_frame(symbol, patched["trade_date"].tolist())
        if effective_adjust_df.empty:
            patched_groups.append(patched)
            continue

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
        total_filled_pre += int((~pre_valid & patched["resolved_pre_adjust_factor"].notna()).sum())
        total_filled_post += int((~post_valid & patched["resolved_post_adjust_factor"].notna()).sum())

        patched.loc[~pre_valid, "pre_adjust_factor"] = patched.loc[~pre_valid, "resolved_pre_adjust_factor"]
        patched.loc[~post_valid, "post_adjust_factor"] = patched.loc[~post_valid, "resolved_post_adjust_factor"]

        if args.write_db:
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
        patched_groups.append(patched)

    patched_df = pd.concat(patched_groups, ignore_index=True)
    patched_df["trade_date"] = pd.to_datetime(patched_df["trade_date"]).dt.strftime("%Y-%m-%d")
    patched_df.to_json(dataset_path, orient="records", force_ascii=False, indent=2)

    db_affected = update_daily_bar_adjust_factors(db_updates) if args.write_db else 0
    print(
        f"dataset_adjust_factor_backfill done dataset={dataset_path} filled_pre={total_filled_pre} filled_post={total_filled_post} db_affected={db_affected}"
    )


if __name__ == "__main__":
    main()
