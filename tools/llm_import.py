#!/usr/bin/env python3
"""
验证拦截 + 导入PG
==================
1. CSRC硬约束: 金融/地产/教育 → 阻断
2. manual_fix_v2对照: 核心品种方向冲突 → conflict
3. 写入 ref.product_stock_mapping (version=llm_v1, valid_from/to)
"""

import sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tools.db_config import pg_connect
import baostock as bs
import json
import logging
from datetime import date
from typing import Dict, List, Tuple, Set

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("llm_import")

# ── CSRC行业黑名单 (这些行业映射任何商品都是错误的) ──
BLOCKED_CSRC: Set[str] = {
    "J66", "J67", "J68", "J69",  # 金融(银行/证券/保险/其他金融)
    "K70",                         # 房地产
    "P83",                         # 教育
    "R86", "R87", "R88", "R89",  # 新闻/广播/文化/体育
    "I63",                         # 电信
    "H61", "H62",                 # 住宿/餐饮
    "L71", "L72",                 # 租赁/商务服务
    "O81",                         # 机动车维修
}

# ── 核心期货品种 (必须与manual_fix对照) ──
CORE_FUTURES_PRODUCTS: Set[str] = {
    "iron_ore", "coke", "coking_coal", "rebar", "hot_rolled_coil",
    "copper", "aluminum", "zinc", "lead", "nickel", "tin", "gold", "silver",
    "crude_oil", "fuel_oil", "asphalt", "lpg",
    "pta", "ethylene_glycol", "polypropylene", "polyethylene", "pvc",
    "methanol", "soda_ash", "urea", "styrene",
    "glass", "float_glass",
    "soybean_meal", "soybean", "soybean_oil", "corn", "corn_starch",
    "palm_oil", "rapeseed_oil", "cotton", "cotton_yarn", "sugar",
    "rubber", "pulp", "wood_pulp", "live_hog", "egg", "apple", "jujube",
    "thermal_coal", "manganese", "silicon_metal", "lithium_carbonate",
}


class ValidationResult:
    """单条映射的验证结果"""
    def __init__(self, symbol: str, product_id: str, direction: int,
                 sensitivity: float, confidence: float):
        self.symbol = symbol
        self.product_id = product_id
        self.direction = direction
        self.sensitivity = sensitivity
        self.weight = round(direction * sensitivity, 3)
        self.confidence = confidence
        self.status = "draft"
        self.block_reason = None
        self.conflict_with_manual = None

    def to_dict(self):
        return {
            "symbol": self.symbol,
            "product_id": self.product_id,
            "direction": self.direction,
            "sensitivity": self.sensitivity,
            "weight": self.weight,
            "confidence": self.confidence,
            "status": self.status,
            "block_reason": self.block_reason,
        }


class Validator:
    """映射验证器"""

    def __init__(self, conn):
        self.conn = conn
        self.cur = conn.cursor()

        # 加载CSRC分类
        logger.info("加载CSRC行业分类...")
        bs.login()
        rs = bs.query_stock_industry()
        self.sym_csrc: Dict[str, str] = {}
        while (rs.error_code == '0') and rs.next():
            row = rs.get_row_data()
            s = row[1].replace("sh.", "").replace("sz.", "")
            ex = "SH" if "sh." in row[1] else "SZ"
            self.sym_csrc[f"{s}.{ex}"] = row[3][:3]
        bs.logout()
        logger.info(f"  {len(self.sym_csrc)} 只股票")

        # 加载manual_fix对照
        self.manual_fix: Dict[Tuple[str, str], int] = {}  # (sym,pid) -> direction
        self.cur.execute(
            "SELECT symbol, product_id, weight FROM ref.product_stock_mapping "
            "WHERE version IN ('manual_fix_v2','manual_fix_v3')")
        for sym, pid, w in self.cur.fetchall():
            self.manual_fix[(sym, pid)] = 1 if float(w) > 0 else -1
        logger.info(f"  manual_fix对照: {len(self.manual_fix)} 条")

    def validate(self, result: ValidationResult) -> ValidationResult:
        """对单条映射执行所有验证规则"""
        csrc = self.sym_csrc.get(result.symbol, "")

        # ── 规则1: CSRC行业黑名单 ──
        if csrc in BLOCKED_CSRC:
            result.status = "blocked"
            result.block_reason = f"CSRC行业黑名单: {csrc}"
            return result

        # ── 规则2: 核心期货品种 direction 不能与manual_fix冲突 ──
        if result.product_id in CORE_FUTURES_PRODUCTS:
            manual_dir = self.manual_fix.get((result.symbol, result.product_id))
            if manual_dir is not None and manual_dir != result.direction:
                result.status = "conflict"
                result.conflict_with_manual = f"manual_fix direction={manual_dir:+d} vs llm={result.direction:+d}"

        # ── 规则3: 置信度过滤 ──
        if result.confidence < 0.4:
            result.status = "low_confidence"

        if result.status == "draft":
            result.status = "auto_validated"

        return result

    def import_results(self, results: List[ValidationResult], version: str = "llm_v1",
                       dry_run: bool = False):
        """批量导入"""
        if not dry_run:
            pass  # 改用UPSERT, 不再DELETE旧数据

        imported = 0
        blocked = 0
        conflicts = 0
        low_conf = 0

        for r in results:
            if r.status == "blocked":
                blocked += 1
                continue
            elif r.status == "conflict":
                conflicts += 1
                # conflict也导入，但标记状态供后续review
            elif r.status == "low_confidence":
                low_conf += 1

            if not dry_run:
                today = date.today().isoformat()
                self.cur.execute(
                    """INSERT INTO ref.product_stock_mapping
                       (product_id, symbol, weight, effective_date, expired_date, version)
                       VALUES (%s,%s,%s,%s,'2099-12-31',%s)
                       ON CONFLICT (product_id, symbol, effective_date) DO NOTHING""",
                    (r.product_id, r.symbol, r.weight, today, version))
            imported += 1

        if not dry_run:
            self.conn.commit()

        logger.info(f"导入完成 (version={version}):")
        logger.info(f"  导入: {imported} 条")
        logger.info(f"  阻断: {blocked} 条 (金融/地产/教育等黑名单)")
        logger.info(f"  冲突: {conflicts} 条 (与manual_fix方向不一致)")
        logger.info(f"  低置信: {low_conf} 条")
        return imported, blocked, conflicts


# ── 端到端示例 ──
if __name__ == "__main__":
    conn = pg_connect()
    v = Validator(conn)

    # 模拟几条LLM输出
    mock_results = [
        ValidationResult("601899.SH", "copper", +1, 0.70, 0.9),   # 紫金-铜矿 ✓
        ValidationResult("000630.SZ", "copper", -1, 0.56, 0.85),   # 铜陵-铜冶炼 ✓
        ValidationResult("600036.SH", "copper", +1, 0.30, 0.7),    # 招商银行-铜? ✗金融
        ValidationResult("000002.SZ", "rebar", -1, 0.20, 0.5),     # 万科-钢材? ✗地产
        ValidationResult("002709.SZ", "electrolyte", -1, 0.43, 0.9), # 天赐 ✓
    ]

    for r in mock_results:
        v.validate(r)
        csrc = v.sym_csrc.get(r.symbol, "?")
        print(f"{r.symbol}[{csrc}] {r.product_id} w={r.weight:+.3f} "
              f"conf={r.confidence} → {r.status} {r.block_reason or ''}")

    conn.close()
