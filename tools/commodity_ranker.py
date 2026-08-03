#!/usr/bin/env python3
"""
传导链因子 — 商品排名计算器
=============================
每天评估商品价格动量，计算综合评分后排名，写入 alpha.commodity_daily_rank。

模式:
  --mode update_prices  : 从 akshare 拉取商品期货日线 → mkt.commodity_prices_daily
  --mode compute_rank   : 基于价格动量评分排名 → alpha.commodity_daily_rank
  --mode backfill       : 历史回填 (先 update_prices 再 compute_rank)

使用:
  python commodity_ranker.py --mode update_prices
  python commodity_ranker.py --mode compute_rank --date 2026-08-01 --top-n 5
  python commodity_ranker.py --mode backfill --start 2020-01-01 --end 2026-07-31
"""

import argparse
import sys
import os
from datetime import date, datetime, timedelta
from typing import Dict, List, Optional, Tuple

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import psycopg2
import psycopg2.extras
import pandas as pd
import numpy as np
import logging

from db_config import pg_connect

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("commodity_ranker")

# ══════════════════════════════════════════════════════════════════════════════
# 商品注册表 — product_id → akshare 期货代码
# akshare 使用新浪期货接口，代码格式: 品种缩写 + 合约代码
# 主力合约用 "0"，如 RB0=螺纹钢主力合约
#
# 【2026-08-03 状态】
#   ✅ 可用: 48 个品种 (全部能通过 akshare 拉到期货日线)
#   ❌ 待接入: ref.product_stock_mapping 中另有 54 个品种无期货数据
#      包括: 钨(tungsten), 钴(cobalt), 稀土(rare_earth), 六氟磷酸锂(lipf6),
#            电解液(electrolyte), 多晶硅(polysilicon), 正极(cathode), 负极(anode),
#            gold(已改为sge现货), silver(已改为sge现货) 等
#      需通过 SMM 上海有色网 API (platform.smm.cn) 获取现货日价 + 库存数据后接入
#      SMM 联系: 021-31330333 | 月付约500元起 | REST API + JSON
# ══════════════════════════════════════════════════════════════════════════════

COMMODITY_REGISTRY: Dict[str, dict] = {
    # ── 黑色金属 ──
    "iron_ore":           {"akshare_symbol": "I0",   "name": "铁矿石"},
    "coke":               {"akshare_symbol": "J0",   "name": "焦炭"},
    "coking_coal":        {"akshare_symbol": "JM0",  "name": "焦煤"},
    "rebar":              {"akshare_symbol": "RB0",  "name": "螺纹钢"},
    "hot_rolled_coil":    {"akshare_symbol": "HC0",  "name": "热轧卷板"},
    # ── 有色金属 ──
    "copper":             {"akshare_symbol": "CU0",  "name": "铜"},
    "aluminum":           {"akshare_symbol": "AL0",  "name": "铝"},
    "zinc":               {"akshare_symbol": "ZN0",  "name": "锌"},
    "lead":               {"akshare_symbol": "PB0",  "name": "铅"},
    "nickel":             {"akshare_symbol": "NI0",  "name": "镍"},
    "tin":                {"akshare_symbol": "SN0",  "name": "锡"},
    "gold":               {"akshare_symbol": "AU0",  "name": "黄金"},
    "silver":             {"akshare_symbol": "AG0",  "name": "白银"},
    # ── 能源 ──
    "crude_oil":          {"akshare_symbol": "SC0",  "name": "原油"},
    "fuel_oil":           {"akshare_symbol": "FU0",  "name": "燃料油"},
    "asphalt":            {"akshare_symbol": "BU0",  "name": "沥青"},
    "lpg":                {"akshare_symbol": "PG0",  "name": "液化气"},
    # ── 化工 ──
    "pta":                {"akshare_symbol": "TA0",  "name": "PTA"},
    "ethylene_glycol":    {"akshare_symbol": "EG0",  "name": "乙二醇"},
    "polypropylene":      {"akshare_symbol": "PP0",  "name": "聚丙烯"},
    "polyethylene":       {"akshare_symbol": "PE0",  "name": "聚乙烯"},
    "pvc":                {"akshare_symbol": "V0",   "name": "PVC"},
    "methanol":           {"akshare_symbol": "MA0",  "name": "甲醇"},
    "soda_ash":           {"akshare_symbol": "SA0",  "name": "纯碱"},
    "urea":               {"akshare_symbol": "UR0",  "name": "尿素"},
    "styrene":            {"akshare_symbol": "EB0",  "name": "苯乙烯"},
    # ── 建材 ──
    "glass":              {"akshare_symbol": "FG0",  "name": "玻璃"},
    "float_glass":        {"akshare_symbol": "FG0",  "name": "浮法玻璃"},
    # ── 农产品 ──
    "soybean_meal":       {"akshare_symbol": "M0",   "name": "豆粕"},
    "soybean":            {"akshare_symbol": "A0",   "name": "大豆"},
    "soybean_oil":        {"akshare_symbol": "Y0",   "name": "豆油"},
    "corn":               {"akshare_symbol": "C0",   "name": "玉米"},
    "corn_starch":        {"akshare_symbol": "CS0",  "name": "玉米淀粉"},
    "palm_oil":           {"akshare_symbol": "P0",   "name": "棕榈油"},
    "rapeseed_oil":       {"akshare_symbol": "OI0",  "name": "菜籽油"},
    "cotton":             {"akshare_symbol": "CF0",  "name": "棉花"},
    "cotton_yarn":        {"akshare_symbol": "CY0",  "name": "棉纱"},
    "sugar":              {"akshare_symbol": "SR0",  "name": "白糖"},
    "rubber":             {"akshare_symbol": "RU0",  "name": "天然橡胶"},
    "pulp":               {"akshare_symbol": "SP0",  "name": "纸浆"},
    "wood_pulp":          {"akshare_symbol": "SP0",  "name": "木浆"},
    "live_hog":           {"akshare_symbol": "LH0",  "name": "生猪"},
    "apple":              {"akshare_symbol": "AP0",  "name": "苹果"},
    "jujube":             {"akshare_symbol": "CJ0",  "name": "红枣"},
    # ── 煤 ──
    "thermal_coal":       {"akshare_symbol": "ZC0",  "name": "动力煤"},
    # ── 硅/锰 ──
    "manganese":          {"akshare_symbol": "SM0",  "name": "锰硅"},
    "silicon_metal":      {"akshare_symbol": "SI0",  "name": "工业硅"},
    # ── 新能源 ──
    "lithium_carbonate":  {"akshare_symbol": "LC0",  "name": "碳酸锂"},
}

# 动量计算窗口（交易日）
DEFAULT_LOOKBACK = 20

# 评分权重: 动量(0.7) + 库存(0.3)
# 库存信号: 取自 mkt.commodity_inventory.change_wow, 库存↓=供给收紧=利好(+)
# 景气(0.2占位)已移除 — 常量兜底导致 score 全部分布在 0.2~0.5 窄带，无法区分强弱
WEIGHT_MOMENTUM   = 0.7
WEIGHT_INVENTORY  = 0.3
PLACEHOLDER_PROSPERITY = 0.0  # 景气数据接入前不参与评分


def try_import_akshare():
    """尝试导入 akshare，失败时给出明确提示"""
    try:
        import akshare as ak
        return ak
    except ImportError:
        logger.error("akshare 未安装。请执行: pip install akshare")
        sys.exit(1)


def fetch_and_store_prices(conn, product_id: str, info: dict, end_date: str):
    """从 akshare 拉取单个商品的历史日线，写入 mkt.commodity_prices_daily"""
    ak = try_import_akshare()
    akshare_sym = info.get("akshare_symbol")

    if akshare_sym is None:
        note = info.get("_note", "无数据源")
        logger.info(f"  跳过 {product_id}: {note}")
        return 0

    logger.info(f"  拉取 {product_id} ({akshare_sym}) 日线...")
    try:
        df = ak.futures_zh_daily_sina(symbol=akshare_sym)
    except Exception as e:
        logger.warning(f"  {product_id}: akshare 拉取失败: {e}")
        return 0

    if df is None or df.empty:
        logger.warning(f"  {product_id}: 无数据返回")
        return 0

    df = df.rename(columns={"date": "trade_date", "close": "close_price"})
    df["trade_date"] = pd.to_datetime(df["trade_date"]).dt.strftime("%Y-%m-%d")
    df = df[df["trade_date"] <= end_date]

    if df.empty:
        return 0

    written = 0
    with conn.cursor() as cur:
        for _, row in df.iterrows():
            cur.execute(
                """INSERT INTO mkt.commodity_prices_daily (product_id, trade_date, close_price)
                   VALUES (%s, %s, %s)
                   ON CONFLICT (product_id, trade_date) DO UPDATE SET close_price = EXCLUDED.close_price""",
                (product_id, row["trade_date"], float(row["close_price"]))
            )
            written += 1
    conn.commit()
    logger.info(f"  {product_id}: 写入 {written} 条 (截止 {end_date})")
    return written


def compute_momentum(conn, product_id: str, calc_date: str, lookback: int) -> Optional[float]:
    """计算单个商品在 calc_date 的 lookback 日动量"""
    with conn.cursor() as cur:
        cur.execute(
            """SELECT close_price
               FROM mkt.commodity_prices_daily
               WHERE product_id = %s AND trade_date <= %s
               ORDER BY trade_date DESC
               LIMIT %s""",
            (product_id, calc_date, lookback))
        rows = cur.fetchall()
    if len(rows) < 2:
        return None
    # rows[0] 是最新，rows[-1] 是最早 → 动量 = 最新/最早 - 1
    latest = float(rows[0][0])
    earliest = float(rows[-1][0])
    if earliest == 0:
        return None
    return (latest / earliest) - 1.0


def query_inventory_change(conn, product_id: str, calc_date: str) -> Optional[float]:
    """查询库存周环比变化, 返回库存信号(库存↓=利好=正值)"""
    with conn.cursor() as cur:
        cur.execute(
            """SELECT change_wow FROM mkt.commodity_inventory
               WHERE product_id = %s AND trade_date <= %s
               ORDER BY trade_date DESC LIMIT 1""",
            (product_id, calc_date))
        row = cur.fetchone()
    if row and row[0] is not None:
        change_wow = float(row[0])
        # 库存减少→正向信号, 库存增加→负向信号, 限制在[-0.3, 0.3]
        return max(-0.3, min(0.3, -change_wow))
    return None


def compute_rank(conn, calc_date: str, top_n: int):
    """计算 calc_date 所有商品的综合评分 → 排名写入 alpha.commodity_daily_rank"""
    logger.info(f"计算 {calc_date} 商品排名 (Top-{top_n})...")

    scores: List[Tuple[str, float]] = []

    with conn.cursor() as cur:
        cur.execute("SELECT DISTINCT product_id FROM mkt.commodity_prices_daily")
        products = [r[0] for r in cur.fetchall()]

    for pid in products:
        momentum = compute_momentum(conn, pid, calc_date, DEFAULT_LOOKBACK)
        if momentum is None:
            continue
        # 库存信号
        inv_signal = query_inventory_change(conn, pid, calc_date)
        if inv_signal is None:
            inv_signal = 0.0  # 无库存数据时中性

        # 评分: 0.7×momentum + 0.3×库存信号
        score = (WEIGHT_MOMENTUM * momentum
                 + WEIGHT_INVENTORY * inv_signal)
        scores.append((pid, score))

    if not scores:
        logger.warning(f"  {calc_date}: 无商品有足够价格数据")
        return 0

    # 降序排名
    scores.sort(key=lambda x: x[1], reverse=True)

    # 只保留 top_n
    top_scores = scores[:top_n]

    with conn.cursor() as cur:
        # 删除当天旧排名
        cur.execute("DELETE FROM alpha.commodity_daily_rank WHERE calc_date = %s", (calc_date,))
        for rank, (pid, score) in enumerate(top_scores, start=1):
            cur.execute(
                """INSERT INTO alpha.commodity_daily_rank (calc_date, product_id, rank_num, score)
                   VALUES (%s, %s, %s, %s)""",
                (calc_date, pid, rank, round(score, 4)))
    conn.commit()

    logger.info(f"  {calc_date}: 写入 {len(top_scores)} 条排名")
    for rank, (pid, score) in enumerate(top_scores, start=1):
        info = COMMODITY_REGISTRY.get(pid, {})
        # 查询动量+库存用于日志
        mom = compute_momentum(conn, pid, calc_date, DEFAULT_LOOKBACK) or 0
        inv = query_inventory_change(conn, pid, calc_date) or 0
        logger.info(f"    #{rank} {pid} ({info.get('name','?')}): score={score:.4f} mom={mom:+.3f} inv={inv:+.3f}")

    return len(top_scores)


def update_prices(conn, end_date: str):
    """拉取所有注册商品的日线数据"""
    total = 0
    for pid, info in COMMODITY_REGISTRY.items():
        total += fetch_and_store_prices(conn, pid, info, end_date)
    logger.info(f"update_prices 完成: 共写入 {total} 条价格记录")


def compute_rank_for_date(conn, calc_date: str, top_n: int):
    """单日排名计算（实盘路径）"""
    return compute_rank(conn, calc_date, top_n)


def backfill(conn, start_date: str, end_date: str, top_n: int):
    """历史回填: 先拉取历史价格，再逐日计算排名"""
    logger.info(f"回填开始: {start_date} → {end_date}")

    # Step 1: 拉取完整历史
    update_prices(conn, end_date)

    # Step 2: 生成交易日列表（从价格表中提取）
    with conn.cursor() as cur:
        cur.execute(
            """SELECT DISTINCT trade_date FROM mkt.commodity_prices_daily
               WHERE trade_date >= %s AND trade_date <= %s
               ORDER BY trade_date""",
            (start_date, end_date))
        trade_dates = [r[0].strftime("%Y-%m-%d") if hasattr(r[0], 'strftime') else str(r[0])
                       for r in cur.fetchall()]

    if not trade_dates:
        logger.error(f"  日期范围 {start_date}→{end_date} 无价格数据")
        return

    # Step 3: 逐日排名
    for td in trade_dates:
        compute_rank(conn, td, top_n)

    logger.info(f"回填完成: {len(trade_dates)} 个交易日")


def main():
    parser = argparse.ArgumentParser(description="传导链因子商品排名计算器")
    parser.add_argument("--mode", required=True,
                        choices=["update_prices", "compute_rank", "backfill"],
                        help="运行模式")
    parser.add_argument("--date", default=date.today().isoformat(),
                        help="计算日期 (compute_rank 模式, 默认今天)")
    parser.add_argument("--start", default="2020-01-01",
                        help="起始日期 (backfill 模式)")
    parser.add_argument("--end", default=date.today().isoformat(),
                        help="结束日期 (backfill 模式)")
    parser.add_argument("--top-n", type=int, default=5,
                        help="Top-N 排名数量 (默认 5)")
    args = parser.parse_args()

    conn = pg_connect()
    try:
        if args.mode == "update_prices":
            update_prices(conn, args.end)
        elif args.mode == "compute_rank":
            compute_rank_for_date(conn, args.date, args.top_n)
        elif args.mode == "backfill":
            backfill(conn, args.start, args.end, args.top_n)
    finally:
        conn.close()


if __name__ == "__main__":
    main()
