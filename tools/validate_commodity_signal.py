#!/usr/bin/env python3
"""
传导链因子核心假设验证
========================
验证逻辑: 排名靠前的商品，其映射的A股在下一期是否受益于涨价？

对于每个交易日:
1. 取 commodity_daily_rank 中的 top-N 商品
2. 获取其映射的 A 股列表
3. 计算这些股票在未来 T 日的超额收益(相对沪深300)
4. 对比: 高排名商品股 vs 低排名商品股 vs 全市场

用法:
  python validate_commodity_signal.py
  python validate_commodity_signal.py --top-n 3 --forward 5
"""

import argparse, sys, os, logging
from collections import defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import pandas as pd
from db_config import pg_connect

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("validate")

CSI300_ID = 3  # symbol_info.id for 000300.SH


def load_data(conn, top_n=3, forward_days=5):
    """加载所需全部数据"""
    cur = conn.cursor()

    # 1. 每日 top-N 商品排名
    cur.execute("""
        SELECT calc_date, product_id, rank_num, score
        FROM alpha.commodity_daily_rank
        WHERE rank_num <= %s ORDER BY calc_date, rank_num
    """, (top_n,))
    rank_rows = cur.fetchall()
    logger.info("商品排名: %d 条", len(rank_rows))

    # 2. 商品→股票映射 (只取有效期覆盖的)
    cur.execute("""
        SELECT product_id, symbol, weight, effective_date, expired_date
        FROM ref.product_stock_mapping
    """)
    mapping_rows = cur.fetchall()
    logger.info("股票映射: %d 条", len(mapping_rows))

    # 3. 沪深300日线
    cur.execute("SELECT trade_date, close FROM mkt.daily_bar WHERE symbol_id = %s ORDER BY trade_date", (CSI300_ID,))
    bench = pd.DataFrame(cur.fetchall(), columns=["trade_date", "close"])
    bench["close"] = bench["close"].astype(float)
    bench["trade_date"] = pd.to_datetime(bench["trade_date"])
    bench = bench.set_index("trade_date").sort_index()
    bench["bench_ret_fwd"] = bench["close"].shift(-forward_days) / bench["close"] - 1.0
    logger.info("沪深300: %d 条", len(bench))

    # 4. A股日线 (只取映射涉及到的股票)
    mapped_symbols = set(m[1] for m in mapping_rows)
    logger.info("涉及股票: %d 只, 加载日线...", len(mapped_symbols))

    # 批量查 symbol_id
    placeholders = ','.join(['%s'] * len(mapped_symbols))
    cur.execute(f"""
        SELECT id, symbol FROM ref.symbol_info WHERE symbol IN ({placeholders})
    """, list(mapped_symbols))
    sym_to_id = {r[1]: r[0] for r in cur.fetchall()}
    logger.info("symbol_info 匹配: %d/%d", len(sym_to_id), len(mapped_symbols))

    # 批量取日线
    ids = list(sym_to_id.values())
    stock_prices = {}
    batch_size = 500
    for i in range(0, len(ids), batch_size):
        batch = ids[i:i+batch_size]
        ph = ','.join(['%s'] * len(batch))
        cur.execute(f"""
            SELECT si.symbol, db.trade_date, db.close
            FROM mkt.daily_bar db
            JOIN ref.symbol_info si ON db.symbol_id = si.id
            WHERE db.symbol_id IN ({ph}) AND db.trade_date >= '2019-01-01'
            ORDER BY db.trade_date
        """, batch)
        for sym, dt, cl in cur.fetchall():
            if sym not in stock_prices:
                stock_prices[sym] = []
            stock_prices[sym].append((dt, float(cl)))
    logger.info("日线加载完成: %d 只股票", len(stock_prices))

    # 转为 DataFrame
    stock_dfs = {}
    for sym, rows in stock_prices.items():
        df = pd.DataFrame(rows, columns=["date", "close"])
        df["date"] = pd.to_datetime(df["date"])
        df = df.set_index("date").sort_index()
        df["fwd_ret"] = df["close"].shift(-forward_days) / df["close"] - 1.0
        stock_dfs[sym] = df

    return rank_rows, mapping_rows, bench, stock_dfs, sym_to_id


def build_mapping_index(mapping_rows):
    """构建 product_id -> [(symbol, weight)] 的查找索引"""
    idx = defaultdict(list)
    for pid, sym, w, eff, exp in mapping_rows:
        idx[pid].append((sym, w, eff, exp))
    return idx


def is_mapping_valid(eff_date, exp_date, calc_date):
    """检查映射在 calc_date 是否有效"""
    d = pd.Timestamp(calc_date)
    efd = pd.Timestamp(eff_date) if eff_date else pd.Timestamp.min
    exd = pd.Timestamp(exp_date) if exp_date else pd.Timestamp.max
    return efd <= d <= exd


def main():
    p = argparse.ArgumentParser(description="验证传导链因子核心假设")
    p.add_argument("--top-n", type=int, default=3, help="Top-N 商品数")
    p.add_argument("--forward", type=int, default=5, help="前瞻天数")
    p.add_argument("--min-dates", type=int, default=50, help="最少日期数")
    args = p.parse_args()

    conn = pg_connect()
    try:
        rank_rows, mapping_rows, bench, stock_dfs, sym_to_id = load_data(
            conn, args.top_n, args.forward)
        mapping_idx = build_mapping_index(mapping_rows)

        # 按日期组织数据
        dates = sorted(set(r[0] for r in rank_rows))
        logger.info("分析日期: %d 天 (%s ~ %s)", len(dates), dates[0], dates[-1])

        # 累计超额收益
        top_cum_excess = 0.0
        mkt_cum_excess = 0.0
        signal_returns = []  # (date, commodity, avg_excess_return)
        market_returns = []

        valid_count = 0
        for calc_date in dates:
            d_str = str(calc_date)
            d_ts = pd.Timestamp(d_str)

            # 基准收益
            bench_ret = None
            if d_ts in bench.index:
                bench_ret = bench.loc[d_ts, "bench_ret_fwd"]
            if bench_ret is None or np.isnan(bench_ret):
                continue

            # 当日 top-N 商品及其股票
            top_products = [r for r in rank_rows if str(r[0]) == d_str]
            signal_stocks = set()
            for _, pid, rank, score in top_products:
                for sym, w, eff, exp in mapping_idx.get(pid, []):
                    if is_mapping_valid(eff, exp, d_str):
                        signal_stocks.add(sym)

            # 计算信号股的平均 forward return
            stock_rets = []
            for sym in signal_stocks:
                sdf = stock_dfs.get(sym)
                if sdf is not None and d_ts in sdf.index:
                    ret = sdf.loc[d_ts, "fwd_ret"]
                    if not np.isnan(ret):
                        stock_rets.append(ret)

            if not stock_rets:
                continue

            avg_ret = np.mean(stock_rets)
            excess = avg_ret - bench_ret
            top_cum_excess += excess
            mkt_cum_excess += bench_ret
            signal_returns.append((d_str, avg_ret, bench_ret, excess, len(stock_rets)))
            valid_count += 1

        logger.info("有效日期: %d/%d", valid_count, len(dates))

        if valid_count < args.min_dates:
            logger.warning("有效日期不足 %d, 无法分析", args.min_dates)
            return

        # 分析
        excesses = [s[3] for s in signal_returns]
        avg_excess = np.mean(excesses)
        t_stat = avg_excess / (np.std(excesses, ddof=1) / np.sqrt(len(excesses))) if np.std(excesses, ddof=1) > 0 else 0
        from scipy import stats as sp_stats
        p_val = 2 * sp_stats.t.sf(abs(t_stat), df=len(excesses) - 1)
        win_rate = np.mean([1 if e > 0 else 0 for e in excesses])

        print("\n" + "=" * 70)
        print(f"  传导链因子 核心假设验证 (top-{args.top_n}, forward-{args.forward}日)")
        print("=" * 70)
        print(f"  分析区间: {dates[0]} ~ {dates[-1]}  ({valid_count} 个有效交易日)")
        print()
        print(f"  信号股日均超额收益: {avg_excess*100:.3f}%")
        print(f"  信号股日均绝对收益: {np.mean([s[1] for s in signal_returns])*100:.3f}%")
        print(f"  同期基准日均收益:   {np.mean([s[2] for s in signal_returns])*100:.3f}%")
        print(f"  超额收益胜率:       {win_rate*100:.1f}%")
        print(f"  t统计量:            {t_stat:.4f}")
        print(f"  p值:                {p_val:.4f}")
        print(f"  累计超额收益:       {top_cum_excess*100:.2f}%")
        print(f"  累计基准收益:       {mkt_cum_excess*100:.2f}%")
        print(f"  平均信号股数/日:    {np.mean([s[4] for s in signal_returns]):.1f}")

        # 分商品统计
        print("\n" + "-" * 70)
        print("  按商品分组 (出现频率 + 平均超额收益)")
        print("-" * 70)
        product_stats = defaultdict(list)
        for d_str, _, _, excess, _ in signal_returns:
            top_products = [r for r in rank_rows if str(r[0]) == d_str]
            for _, pid, rank, score in top_products:
                product_stats[pid].append(excess)

        print(f"  {'商品':<25} {'出现天数':>6} {'日均超额':>10} {'累计超额':>10}")
        print(f"  {'-'*53}")
        for pid in sorted(product_stats, key=lambda x: -len(product_stats[x])):
            excesses_p = product_stats[pid]
            if len(excesses_p) < 10:
                continue
            avg = np.mean(excesses_p)
            cum = np.sum(excesses_p)
            print(f"  {pid:<25} {len(excesses_p):>6} {avg*100:>9.3f}% {cum*100:>9.2f}%")

        # 分月统计
        print("\n" + "-" * 70)
        print("  月度超额收益")
        print("-" * 70)
        monthly = defaultdict(list)
        for d_str, _, _, excess, _ in signal_returns:
            ym = d_str[:7]
            monthly[ym].append(excess)

        for ym in sorted(monthly.keys()):
            exs = monthly[ym]
            print(f"  {ym}: {np.mean(exs)*100:+.3f}% ({len(exs)}天) 累计={np.sum(exs)*100:+.2f}%")

    finally:
        conn.close()


if __name__ == "__main__":
    main()
