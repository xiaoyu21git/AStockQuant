#!/usr/bin/env python3
"""
合成商品价格指数 — 无期货商品的替代方案
==========================================
对没有期货合约的商品，用其映射的高权重 A 股构建合成价格指数。
逻辑: 取 top-5 龙头股的等权日收益，复合成价格序列。

用法:
  python build_synthetic_prices.py                    # 构建全部无期货商品的合成价格
  python build_synthetic_prices.py --product tungsten  # 只构建指定商品
"""

import argparse, sys, os, logging
from collections import defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import pandas as pd
from db_config import pg_connect

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("synthetic")

TOP_K = 5  # 取前 K 只龙头股合成


def build_synthetic(conn, product_id: str, top_k: int = TOP_K):
    """为一个商品构建合成价格指数并写入 mkt.commodity_prices_daily"""
    cur = conn.cursor()

    # 1. 获取映射股
    cur.execute("""
        SELECT symbol, weight FROM ref.product_stock_mapping
        WHERE product_id = %s ORDER BY weight DESC LIMIT %s
    """, (product_id, top_k))
    stocks = cur.fetchall()
    if not stocks:
        logger.warning("  %s: 无映射股", product_id)
        return 0

    syms = [s[0] for s in stocks]
    weights = np.array([float(s[1]) for s in stocks])
    weights = weights / weights.sum()  # 归一化

    # 2. 获取 symbol_id
    cur.execute("SELECT symbol, id FROM ref.symbol_info WHERE symbol IN (SELECT UNNEST(%s::varchar[]))", (syms,))
    sym_to_id = {r[0]: r[1] for r in cur.fetchall()}
    ids = [sym_to_id[s] for s in syms if s in sym_to_id]
    if not ids:
        logger.warning("  %s: symbol_info 无匹配", product_id)
        return 0

    # 3. 取这些股票的全部日线
    ph = ','.join(['%s'] * len(ids))
    cur.execute(f"""
        SELECT si.symbol, db.trade_date, db.close
        FROM mkt.daily_bar db
        JOIN ref.symbol_info si ON db.symbol_id = si.id
        WHERE db.symbol_id IN ({ph})
        ORDER BY db.trade_date
    """, ids)

    prices = defaultdict(dict)  # symbol -> {date: close}
    for sym, dt, cl in cur.fetchall():
        prices[sym][dt] = float(cl)

    # 4. 找所有股票共享的日期
    date_sets = [set(prices[s].keys()) for s in syms if s in prices]
    if not date_sets:
        logger.warning("  %s: 无股价数据", product_id)
        return 0
    common_dates = sorted(set.intersection(*date_sets))
    if len(common_dates) < 20:
        logger.warning("  %s: 共同日期仅 %d 天", product_id, len(common_dates))
        return 0

    # 5. 构建合成价格: 加权平均日收益 → 累积价格 (基准=100)
    # 取第一个共同日期为基日
    base_date = common_dates[0]
    synthetic = {base_date: 100.0}

    for i in range(1, len(common_dates)):
        prev_d = common_dates[i-1]
        curr_d = common_dates[i]
        # 加权日收益
        daily_ret = 0.0
        for j, sym in enumerate(syms):
            if sym in prices and prev_d in prices[sym] and curr_d in prices[sym]:
                ret = prices[sym][curr_d] / prices[sym][prev_d] - 1.0
                daily_ret += weights[j] * ret
        synthetic[curr_d] = synthetic[prev_d] * (1.0 + daily_ret)

    # 6. 写入 mkt.commodity_prices_daily
    written = 0
    with conn.cursor() as wcur:
        for dt, pr in synthetic.items():
            wcur.execute("""
                INSERT INTO mkt.commodity_prices_daily (product_id, trade_date, close_price)
                VALUES (%s, %s, %s)
                ON CONFLICT (product_id, trade_date) DO UPDATE SET close_price = EXCLUDED.close_price
            """, (product_id, dt, float(round(pr, 4))))
            written += 1
    conn.commit()

    logger.info("  %s: 合成价格 %d 条 (%s ~ %s), 成分股: %s",
                product_id, written,
                common_dates[0], common_dates[-1],
                ", ".join(f"{s}({w:.2f})" for s, w in zip(syms, weights)))
    return written


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--product", help="只构建指定商品")
    args = p.parse_args()

    conn = pg_connect()
    cur = conn.cursor()

    # 找有映射但没有价格数据的商品
    cur.execute("SELECT DISTINCT product_id FROM mkt.commodity_prices_daily")
    has_prices = set(r[0] for r in cur.fetchall())

    cur.execute("SELECT DISTINCT product_id FROM ref.product_stock_mapping")
    all_mapped = set(r[0] for r in cur.fetchall())

    missing = all_mapped - has_prices

    if args.product:
        targets = {args.product} & missing
        if not targets:
            logger.error("%s: 已有价格或不在映射表中", args.product)
            return
    else:
        targets = missing

    logger.info("无价格数据的映射商品: %d 个, 开始构建合成价格...", len(targets))

    total = 0
    for pid in sorted(targets):
        n = build_synthetic(conn, pid)
        total += n

    logger.info("完成: %d 个商品, 共写入 %d 条合成价格", len(targets), total)

    # 重算全部排名
    from commodity_ranker import compute_rank
    cur.execute("SELECT DISTINCT trade_date FROM mkt.commodity_prices_daily ORDER BY trade_date")
    dates = [str(r[0]) for r in cur.fetchall()]
    logger.info("重算 %d 个交易日的排名 (top_n=8)...", len(dates))
    for i, d in enumerate(dates):
        compute_rank(conn, d, top_n=8)
        if (i+1) % 500 == 0:
            logger.info("  %d/%d", i+1, len(dates))

    # 统计
    cur.execute("SELECT COUNT(DISTINCT product_id) FROM alpha.commodity_daily_rank")
    logger.info("排名中商品数: %d", cur.fetchone()[0])

    conn.close()


if __name__ == "__main__":
    main()
