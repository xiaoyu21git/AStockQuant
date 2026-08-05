#!/usr/bin/env python3
"""从知识图谱JSON生成最终版 import_mapping.py"""
import json

with open('tools/commodity_stock_mapping_from_kg.json', 'r', encoding='utf-8') as f:
    data = json.load(f)

# IPO日期参考 (知识图谱没有, 用主要IPO日期)
# 这里暂时用保守估计, 大部分股票的IPO都在2000-2010之间
# 后续可以从tushare/akshare拉取精确IPO日期

lines = []
lines.append('''#!/usr/bin/env python3
"""
传导链因子 — 商品→股票映射导入器 (知识图谱版)
===============================================
数据源: ChainKnowledgeGraph (https://github.com/liuhuanyong/ChainKnowledgeGraph)
覆盖: 98种大宗商品 × 970只A股 × 1653条映射

使用:
  python import_mapping.py --dry-run
  python import_mapping.py              # 正式导入
  python import_mapping.py --audit
"""

import argparse
import sys
import os
from datetime import date
from typing import Dict, List, Tuple

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import logging
from db_config import pg_connect

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("import_mapping")

MAPPINGS: Dict[str, List[Tuple[str, float, str, str]]] = {
''')

for pid in sorted(data.keys()):
    stocks = data[pid]
    lines.append(f'    "{pid}": [')
    for sym, w in stocks[:50]:  # 最多50只
        lines.append(f'        ("{sym}", {w:+.1f}, "2000-01-01", "2099-12-31"),')
    lines.append(f'    ],')

lines.append('''}

DELISTED: Dict[str, str] = {
    "600005.SH": "2017-02-14",  # 武钢股份被宝钢吸收合并
    # 后续维护补充...
}

def import_mappings(conn, dry_run: bool = False) -> int:
    total = 0
    with conn.cursor() as cur:
        if not dry_run:
            cur.execute("DELETE FROM ref.product_stock_mapping WHERE version = 'ckg_v1'")
            logger.info("已清空旧映射")

        for product_id, entries in MAPPINGS.items():
            for symbol, weight, eff_date, exp_date in entries:
                actual_exp = DELISTED.get(symbol, exp_date)
                if not dry_run:
                    cur.execute(
                        """INSERT INTO ref.product_stock_mapping
                           (product_id, symbol, weight, effective_date, expired_date, version)
                           VALUES (%s, %s, %s, %s, %s, 'ckg_v1')
                           ON CONFLICT (product_id, symbol, effective_date)
                           DO UPDATE SET weight = EXCLUDED.weight,
                                         expired_date = EXCLUDED.expired_date,
                                         version = EXCLUDED.version""",
                        (product_id, symbol, weight, eff_date, actual_exp))
                total += 1
    if not dry_run:
        conn.commit()
    logger.info(f"导入完成: {total} 条映射, {len(MAPPINGS)} 商品, {len(set(s for p in MAPPINGS.values() for s,_,_,_ in p))} 股票")
    return total

def show_summary(conn):
    with conn.cursor() as cur:
        cur.execute("SELECT COUNT(*) FROM ref.product_stock_mapping")
        total = cur.fetchone()[0]
        cur.execute("SELECT COUNT(DISTINCT product_id) FROM ref.product_stock_mapping")
        products = cur.fetchone()[0]
        cur.execute("SELECT COUNT(DISTINCT symbol) FROM ref.product_stock_mapping")
        stocks = cur.fetchone()[0]
        cur.execute("""SELECT product_id, COUNT(*) n FROM ref.product_stock_mapping
                       GROUP BY product_id ORDER BY n DESC LIMIT 20""")
        top = cur.fetchall()
    logger.info(f"数据库: {total} 条, {products} 商品, {stocks} 股票")
    logger.info("Top 20: " + ", ".join(f"{r[0]}={r[1]}" for r in top))

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--summary", action="store_true")
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
''')

with open('tools/import_mapping.py', 'w', encoding='utf-8') as f:
    f.write('\n'.join(lines))

print(f"已生成 import_mapping.py: {len(data)} 商品, {sum(len(v) for v in data.values())} 条映射")
