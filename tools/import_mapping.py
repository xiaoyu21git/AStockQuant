#!/usr/bin/env python3
"""
传导链因子 — 商品→股票映射导入器 (知识图谱版)
===============================================
数据源: ChainKnowledgeGraph (https://github.com/liuhuanyong/ChainKnowledgeGraph)
       company_product.json → 提取大宗商品→A股映射

覆盖: 98种大宗商品 × 970只A股 × 1653条映射

使用:
  python import_mapping.py --dry-run
  python import_mapping.py              # 正式导入
  python import_mapping.py --summary    # 查看当前数据
"""

import argparse
import json
import sys
import os
from collections import defaultdict
from typing import Dict, List, Tuple

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import logging
from db_config import pg_connect

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("import_mapping")

# ══════════════════════════════════════════════════════════════════════════════
# 从知识图谱提取的映射数据 (JSON)
# 格式: {product_id: [[symbol, weight], ...]}
# ══════════════════════════════════════════════════════════════════════════════

KG_DATA_FILE = os.path.join(os.path.dirname(__file__), "commodity_stock_mapping_from_kg.json")

# 已退市/被合并股票
DELISTED: Dict[str, str] = {
    "600005.SH": "2017-02-14",  # 武钢股份被宝钢吸收合并
}


def load_mappings() -> Dict[str, List[Tuple[str, float]]]:
    """从JSON加载映射"""
    if not os.path.exists(KG_DATA_FILE):
        logger.error(f"数据文件不存在: {KG_DATA_FILE}")
        logger.error("请先运行: python tools/build_commodity_tools_from_kg.py")
        return {}

    with open(KG_DATA_FILE, "r", encoding="utf-8") as f:
        raw = json.load(f)

    result = {}
    for pid, stock_list in raw.items():
        result[pid] = [(s, float(w)) for s, w in stock_list]
    return result


def import_mappings(conn, dry_run: bool = False) -> int:
    """导入到 ref.product_stock_mapping"""
    mappings = load_mappings()
    if not mappings:
        return 0

    total = 0
    with conn.cursor() as cur:
        if not dry_run:
            cur.execute("DELETE FROM ref.product_stock_mapping")
            logger.info("已清空旧映射")

        for product_id, stock_list in mappings.items():
            for symbol, weight in stock_list:
                eff_date = "2000-01-01"  # 保守默认值
                exp_date = DELISTED.get(symbol, "2099-12-31")

                if not dry_run:
                    cur.execute(
                        """INSERT INTO ref.product_stock_mapping
                           (product_id, symbol, weight, effective_date, expired_date, version)
                           VALUES (%s, %s, %s, %s, %s, 'ckg_v2')
                           ON CONFLICT (product_id, symbol, effective_date)
                           DO UPDATE SET weight = EXCLUDED.weight,
                                         expired_date = EXCLUDED.expired_date,
                                         version = EXCLUDED.version""",
                        (product_id, symbol, weight, eff_date, exp_date))
                total += 1

    if not dry_run:
        conn.commit()

    n_products = len(mappings)
    n_stocks = len(set(s for sl in mappings.values() for s, _ in sl))
    logger.info(f"导入完成: {total} 条映射, {n_products} 商品, {n_stocks} 股票")
    return total


def show_summary(conn):
    """显示汇总"""
    with conn.cursor() as cur:
        cur.execute("SELECT COUNT(*) FROM ref.product_stock_mapping")
        total = cur.fetchone()[0]
        cur.execute("SELECT COUNT(DISTINCT product_id) FROM ref.product_stock_mapping")
        products = cur.fetchone()[0]
        cur.execute("SELECT COUNT(DISTINCT symbol) FROM ref.product_stock_mapping")
        stocks = cur.fetchone()[0]
        cur.execute("""SELECT product_id, COUNT(*) n FROM ref.product_stock_mapping
                       GROUP BY product_id ORDER BY n DESC LIMIT 25""")
        top = cur.fetchall()

    logger.info(f"数据库: {total} 条, {products} 商品, {stocks} 股票")
    logger.info("Top 25 商品(按股票数):")
    for r in top:
        logger.info(f"  {r[0]:30s} {r[1]:4d}只")


def main():
    parser = argparse.ArgumentParser(description="传导链因子 — 映射导入器")
    parser.add_argument("--dry-run", action="store_true", help="预览")
    parser.add_argument("--summary", action="store_true", help="汇总")
    args = parser.parse_args()

    conn = pg_connect()
    try:
        if args.summary:
            show_summary(conn)
            return
        import_mappings(conn, dry_run=args.dry_run)
        if not args.dry_run:
            show_summary(conn)
    finally:
        conn.close()


if __name__ == "__main__":
    main()
