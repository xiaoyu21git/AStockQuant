#!/usr/bin/env python3
"""
分时段子区间 IC 分析脚本
=========================
按基准（沪深300）走势将回测区间切分为 上涨市/下跌市/震荡市，
分别统计因子 IC 表现，验证因子是否为趋势追涨型。

用法:
  python sub_period_ic_analysis.py                    # 分析最近一次回测
  python sub_period_ic_analysis.py --run-id <id>      # 分析指定回测
  python sub_period_ic_analysis.py --lookback 120      # 调整牛熊判定窗口（默认60日）
  python sub_period_ic_analysis.py --bull 0.15 --bear -0.10  # 调整牛熊阈值
"""

import argparse
import sys
import os
from typing import Dict, Optional

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import psycopg2
import pandas as pd
import numpy as np
from scipy import stats
import logging

from db_config import pg_connect

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("sub_period_ic")

CSI300_SYMBOL = "000300.SH"
BULL_THRESHOLD = 0.10
BEAR_THRESHOLD = -0.10


def fetch_benchmark(conn, lookback: int, bull_thresh: float, bear_thresh: float) -> pd.DataFrame:
    """读取沪深300日线，计算滚动收益率用于牛熊分类"""
    cur = conn.cursor()
    cur.execute("SELECT id FROM ref.symbol_info WHERE symbol = %s", (CSI300_SYMBOL,))
    r = cur.fetchone()
    if not r:
        logger.error("未找到 %s", CSI300_SYMBOL)
        return pd.DataFrame()
    sid = r[0]

    cur.execute(
        "SELECT trade_date, close FROM mkt.daily_bar "
        "WHERE symbol_id = %s ORDER BY trade_date",
        (sid,))
    rows = cur.fetchall()
    df = pd.DataFrame(rows, columns=["trade_date", "close"])
    df["close"] = df["close"].astype(float)
    df["trade_date"] = pd.to_datetime(df["trade_date"])
    df["roll_return"] = df["close"] / df["close"].shift(lookback) - 1.0

    def classify(r_val):
        if pd.isna(r_val):
            return "unknown"
        if r_val > bull_thresh:
            return "bull"
        elif r_val < bear_thresh:
            return "bear"
        else:
            return "sideways"

    df["regime"] = df["roll_return"].apply(classify)
    return df[["trade_date", "close", "roll_return", "regime"]]


def fetch_ic_daily(conn, run_id: Optional[str] = None) -> pd.DataFrame:
    """读取因子每日 IC"""
    cur = conn.cursor()
    if run_id:
        cur.execute(
            "SELECT trade_date, rank_ic FROM alpha.factor_backtest_ic_daily "
            "WHERE run_id = %s ORDER BY trade_date",
            (run_id,))
    else:
        cur.execute(
            "SELECT trade_date, rank_ic FROM alpha.factor_backtest_ic_daily "
            "WHERE run_id = (SELECT run_id FROM alpha.factor_backtest_ic_daily "
            " GROUP BY run_id ORDER BY MAX(trade_date) DESC LIMIT 1) "
            "ORDER BY trade_date")
    rows = cur.fetchall()
    df = pd.DataFrame(rows, columns=["trade_date", "ic"])
    df["trade_date"] = pd.to_datetime(df["trade_date"])
    return df


def compute_regime_stats(ic_series: pd.Series, regime_name: str) -> dict:
    """计算单个市场阶段的 IC 统计"""
    valid = ic_series.dropna()
    n = len(valid)
    if n < 5:
        return {"regime": regime_name, "n_days": n, "error": "样本不足"}

    mean_ic = float(valid.mean())
    std_ic = float(valid.std(ddof=1))
    ir_val = mean_ic / std_ic if std_ic > 0 else 0.0
    win_rate = float((valid > 0).sum() / n)

    if std_ic > 0:
        t_stat = mean_ic / (std_ic / np.sqrt(n))
        p_value = 2.0 * stats.t.sf(abs(t_stat), df=n - 1)
    else:
        t_stat = 0.0
        p_value = 1.0

    cum_ic = float(valid.sum())

    return {
        "regime": regime_name,
        "n_days": n,
        "mean_ic": round(mean_ic, 6),
        "std_ic": round(std_ic, 4),
        "ir": round(ir_val, 4),
        "win_rate": round(win_rate, 4),
        "t_stat": round(t_stat, 4),
        "p_value": round(p_value, 6),
        "cum_ic": round(cum_ic, 4),
        "significant": p_value < 0.05,
    }


def compute_bucket_analysis(ic_s, bench_ret_s, n_buckets=5):
    """按基准收益分桶"""
    df = pd.DataFrame({"ic": ic_s, "bench_ret": bench_ret_s}).dropna()
    if len(df) < 20:
        return pd.DataFrame()
    df["bucket"] = pd.qcut(df["bench_ret"], n_buckets, labels=False, duplicates="drop")
    labels_map = {0: "极熊", 1: "偏熊", 2: "震荡", 3: "偏牛", 4: "极牛"}
    result = df.groupby("bucket").agg(
        n_days=("ic", "count"),
        mean_bench_ret=("bench_ret", "mean"),
        mean_ic=("ic", "mean"),
        std_ic=("ic", "std"),
        win_rate=("ic", lambda x: (x > 0).mean()),
        cum_ic=("ic", "sum"),
    ).reset_index()
    result["bucket_label"] = result["bucket"].apply(lambda b: labels_map.get(b, str(b)))
    return result


def main():
    parser = argparse.ArgumentParser(description="分时段子区间 IC 分析")
    parser.add_argument("--run-id", help="指定回测 run_id（默认取最新）")
    parser.add_argument("--lookback", type=int, default=60,
                        help="牛熊判定窗口（交易日，默认60）")
    parser.add_argument("--bull", type=float, default=0.10,
                        help="牛市阈值（默认0.10）")
    parser.add_argument("--bear", type=float, default=-0.10,
                        help="熊市阈值（默认-0.10）")
    parser.add_argument("--json", action="store_true", help="输出 JSON 格式")
    args = parser.parse_args()

    conn = pg_connect()
    try:
        logger.info("读取基准数据 (lookback=%d)...", args.lookback)
        bench_df = fetch_benchmark(conn, args.lookback, args.bull, args.bear)
        if bench_df.empty:
            logger.error("基准数据为空")
            return

        logger.info("读取因子 IC 数据...")
        ic_df = fetch_ic_daily(conn, args.run_id)
        if ic_df.empty:
            logger.error("IC 数据为空")
            return

        merged = ic_df.merge(bench_df, on="trade_date", how="inner")
        dmin = merged.trade_date.min().date()
        dmax = merged.trade_date.max().date()
        logger.info("合并后: %d 个交易日 (%s ~ %s)", len(merged), dmin, dmax)

        regime_labels = {"bull": "上涨市", "sideways": "震荡市", "bear": "下跌市"}
        regime_marks = {"bull": "[UP]", "sideways": "[--]", "bear": "[DN]"}

        print("\n" + "=" * 80)
        print(f"  分时段 IC 分析 | 牛熊判定窗口: {args.lookback}日")
        print(f"  阈值: >{args.bull:.0%}(牛) / <{args.bear:.0%}(熊)")
        print(f"  区间: {dmin} ~ {dmax}")
        print("=" * 80)

        all_stats = []
        for regime in ["bull", "sideways", "bear"]:
            subset = merged[merged["regime"] == regime]
            st = compute_regime_stats(subset["ic"], regime_labels[regime])
            all_stats.append(st)

            print(f"\n  {regime_marks[regime]} {regime_labels[regime]}: {st['n_days']} 天")
            if "error" in st:
                print(f"    !! {st['error']}")
                continue
            sig = " **显著**" if st["significant"] else " 不显著"
            print(f"    IC均值: {st['mean_ic']:.6f}  |  IC标准差: {st['std_ic']:.4f}")
            print(f"    ICIR:   {st['ir']:.4f}  |  胜率: {st['win_rate']:.2%}")
            print(f"    t统计量: {st['t_stat']:.4f}  |  p值: {st['p_value']:.6f}{sig}")
            print(f"    累计IC: {st['cum_ic']:.4f}")

        full = compute_regime_stats(merged["ic"], "全样本")
        print(f"\n  ── 全样本: {full['n_days']} 天 ──")
        print(f"    IC均值: {full['mean_ic']:.6f}  |  ICIR: {full['ir']:.4f}")
        print(f"    胜率: {full['win_rate']:.2%}  |  p值: {full['p_value']:.6f}")

        # 五分桶
        print("\n" + "-" * 80)
        print("  五分桶分析（按沪深300收益率从低到高排序）")
        print("-" * 80)
        merged_v = merged.dropna(subset=["ic", "roll_return"])
        bucket_df = compute_bucket_analysis(merged_v["ic"], merged_v["roll_return"])
        if not bucket_df.empty:
            header = f"  {'分桶':<8} {'天数':>5} {'基准均收益':>10} {'IC均值':>10} {'ICIR':>8} {'胜率':>8} {'累计IC':>10}"
            print(header)
            print(f"  {'-'*60}")
            for _, row in bucket_df.iterrows():
                b_std = row["std_ic"]
                icir_val = row["mean_ic"] / b_std if b_std and b_std > 0 else 0
                print(f"  {row['bucket_label']:<8} {int(row['n_days']):>5} "
                      f"{row['mean_bench_ret']:>10.2%} {row['mean_ic']:>10.6f} "
                      f"{icir_val:>8.4f} {row['win_rate']:>8.2%} {row['cum_ic']:>10.4f}")

            ic_by_bucket = bucket_df["mean_ic"].values
            if len(ic_by_bucket) >= 4:
                if ic_by_bucket[-1] > ic_by_bucket[0]:
                    print("\n  >>> 趋势: IC 随大盘上涨而上升 -> 因子有追涨特征")
                elif ic_by_bucket[-1] < ic_by_bucket[0]:
                    print("\n  >>> 趋势: IC 随大盘上涨而下降 -> 因子有逆市特征")
                else:
                    print("\n  >>> 趋势: IC 与大盘关系不单调")

        # 年度x月度 IC 热力
        print("\n" + "-" * 80)
        print("  年度 x 月度 IC 均值热力")
        print("-" * 80)
        merged["year"] = merged["trade_date"].dt.year
        merged["month"] = merged["trade_date"].dt.month
        heatmap = merged.pivot_table(values="ic", index="year", columns="month", aggfunc="mean")
        months = list(range(1, 13))
        hdr = "  年份  " + " ".join(f"{m:>7}月" for m in months)
        print(hdr)
        print("  " + "-" * (8 + 8 * 12))
        for yr in sorted(heatmap.index, reverse=True):
            row_str = f"  {yr:<6}"
            for m in months:
                val = heatmap.loc[yr, m] if m in heatmap.columns else np.nan
                row_str += f" {val:>7.4f}" if pd.notna(val) else f" {'·':>7}"
            print(row_str)

        if args.json:
            import json as _json
            output = {
                "config": {"lookback": args.lookback, "bull_threshold": args.bull,
                           "bear_threshold": args.bear,
                           "date_range": [str(dmin), str(dmax)]},
                "regime_stats": all_stats,
                "full_sample": full,
                "bucket_analysis": bucket_df.to_dict(orient="records") if not bucket_df.empty else [],
            }
            print("\n--- JSON ---")
            print(_json.dumps(output, ensure_ascii=False, indent=2, default=str))

    finally:
        conn.close()


if __name__ == "__main__":
    main()
