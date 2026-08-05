#!/usr/bin/env python3
"""
从baostock行业分类同步商品→A股映射
===================================
baostock提供证监会(CSRC)行业分类, 免费无需API密钥。
5536只A股按84个行业分类, 行业代码→商品品种映射。

使用: python tools/sync_baostock_industry.py [--dry-run]
"""

import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from db_config import pg_connect
import logging
logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("baostock_sync")

# ══════════════════════════════════════════════════════════════════════════════
# CSRC行业代码 → product_id 映射
# 行业代码说明: B=采矿业 C=制造业 A=农/林/牧/渔
# ══════════════════════════════════════════════════════════════════════════════

INDUSTRY_TO_PRODUCTS = {
    # ── 采矿业 ──
    "B06": ["thermal_coal", "coke", "coking_coal"],  # 煤炭开采和洗选业
    "B07": ["crude_oil", "natural_gas"],              # 石油和天然气开采业
    "B08": ["iron_ore", "manganese"],                 # 黑色金属矿采选业
    "B09": ["copper", "aluminum", "zinc", "lead", "nickel", "tin",
            "gold", "silver", "rare_earth", "tungsten", "molybdenum",
            "cobalt", "lithium", "magnesium", "titanium", "zirconium",
            "germanium", "gallium", "antimony"],      # 有色金属矿采选业
    "B10": ["phosphoric_acid"],                       # 非金属矿采选业 (磷矿等)
    "B11": ["silver"],                                # 开采辅助 (银等)

    # ── 制造业 ──
    "C13": ["soybean_meal", "corn", "sugar", "palm_oil", "rapeseed_oil",
            "soybean_oil", "corn_starch", "wheat", "cotton", "live_hog"],  # 农副食品加工业
    "C14": ["soybean_meal", "corn", "live_hog"],      # 食品制造业 (饲料等)
    "C15": ["sugar", "wheat", "soybean_oil", "palm_oil"],  # 酒/饮料/精制茶 (糖等)

    "C17": ["cotton", "cotton_yarn"],                 # 纺织业
    "C18": ["cotton", "cotton_yarn"],                 # 纺织服装
    "C19": ["rubber"],                                # 皮革/毛皮 (橡胶制品)

    "C20": ["pulp", "wood_pulp"],                     # 木材加工
    "C21": ["wood_pulp", "pulp"],                     # 家具制造
    "C22": ["pulp", "wood_pulp"],                     # 造纸和纸制品业

    "C25": ["crude_oil", "fuel_oil", "asphalt", "lpg"],  # 石油/煤炭/燃料加工
    "C26": ["methanol", "urea", "soda_ash", "caustic_soda", "styrene",
            "acetic_acid", "pta", "mdi", "tdi", "acrylic_acid",
            "propylene_oxide", "ethylene_oxide", "titanium_dioxide",
            "phosphoric_acid", "sulfuric_acid", "hydrofluoric_acid",
            "silicon", "polysilicon"],                 # 化学原料和化学制品
    "C27": ["urea", "phosphoric_acid"],               # 医药 (化肥相关)
    "C28": ["ethylene_glycol", "polyester", "acrylic_acid"],  # 化学纤维
    "C29": ["rubber", "polypropylene", "polyethylene", "pvc",
            "polypropylene", "polyethylene"],          # 橡胶和塑料制品业

    "C30": ["cement", "glass", "float_glass", "pv_glass", "soda_ash",
            "titanium_dioxide"],                       # 非金属矿物制品业
    "C31": ["rebar", "hot_rolled_coil", "iron_ore", "silicon_steel",
            "steel_pipe", "wire_rod", "plate", "cold_rolled",
            "section_steel", "coke"],                  # 黑色金属冶炼和压延加工
    "C32": ["copper", "aluminum", "zinc", "lead", "nickel", "tin",
            "gold", "silver", "rare_earth", "tungsten", "molybdenum",
            "cobalt", "lithium", "magnesium", "titanium",
            "silicon_metal", "manganese"],             # 有色金属冶炼和压延加工
    "C33": ["copper", "aluminum", "steel_pipe", "tin"],  # 金属制品业

    "C34": ["solar_wafer", "solar_cell", "solar_module"],  # 通用设备
    "C35": ["solar_wafer"],                           # 专用设备
    "C36": ["solar_module", "lithium"],               # 汽车制造 (含锂电池车)
    "C37": ["solar_cell"],                            # 铁路/船舶
    "C38": ["lithium", "electrolyte", "separator", "cathode", "anode",
            "solar_cell", "polysilicon", "solar_wafer",
            "pv_glass", "silicon"],                   # 电气机械和器材制造 (含电池/光伏)
    "C39": ["cathode", "anode", "electrolyte", "separator",
            "solar_module", "solar_cell", "copper"],  # 计算机/通信/电子 (含锂电材料)

    "C40": ["silicon"],                               # 仪器仪表
    "C41": ["silicon"],                               # 其他制造
    "C42": ["pulp"],                                  # 废弃资源综合利用

    # ── 农林牧渔 ──
    "A01": ["soybean", "corn", "wheat", "cotton", "sugar",
            "rubber", "soybean_meal", "apple", "jujube"],  # 农业
    "A02": ["wood_pulp", "pulp"],                     # 林业
    "A03": ["live_hog", "egg"],                       # 畜牧业
    "A04": ["soybean_meal"],                          # 渔业 (水产饲料)
}


def sync():
    import baostock as bs
    bs.login()
    rs = bs.query_stock_industry()
    if rs.error_code != '0':
        logger.error(f"baostock error: {rs.error_msg}")
        return {}

    # 聚合: product_id → {symbol}
    from collections import defaultdict
    product_stocks = defaultdict(set)

    total = 0
    while (rs.error_code == '0') and rs.next():
        total += 1
        row = rs.get_row_data()
        ind_full = row[3]  # e.g. "C32有色金属冶炼和压延加工业"
        ind_code = ind_full[:3]  # CSRC行业代码前3位 e.g. "C32"
        symbol = row[1].replace("sh.", "").replace("sz.", "")  # 股票代码
        exchange = "SH" if "sh." in row[1] else "SZ"
        full_symbol = f"{symbol}.{exchange}"

        products = INDUSTRY_TO_PRODUCTS.get(ind_code, [])
        for pid in products:
            product_stocks[pid].add(full_symbol)

    bs.logout()

    # 转成带权重
    result = {}
    for pid, stocks in product_stocks.items():
        result[pid] = [(s, 0.5) for s in sorted(stocks)]  # 行业映射权重统一0.5

    logger.info(f"baostock: {total} 只股票, {len(result)} 个商品品种, "
                f"{sum(len(v) for v in result.values())} 条映射")
    return result


def import_to_db(conn, product_stocks, dry_run=False):
    cur = conn.cursor()
    version = "baostock_v1"

    if not dry_run:
        cur.execute(f"DELETE FROM ref.product_stock_mapping WHERE version='{version}'")
        logger.info(f"已清空 {version} 旧数据")

    total = 0
    all_symbols = set()

    for pid in sorted(product_stocks.keys()):
        for sym, weight in product_stocks[pid]:
            all_symbols.add(sym)
            if not dry_run:
                cur.execute(
                    """INSERT INTO ref.product_stock_mapping
                       (product_id, symbol, weight, effective_date, expired_date, version)
                       VALUES (%s,%s,%s,'2000-01-01','2099-12-31',%s)
                       ON CONFLICT (product_id, symbol, effective_date) DO NOTHING""",
                    (pid, sym, weight, version))
            total += 1

    if not dry_run:
        conn.commit()

    logger.info(f"\n{'[预览]' if dry_run else '导入'}完成:")
    logger.info(f"  {len(product_stocks)} 商品, {len(all_symbols)} 股票, {total} 条映射")

    # 前15
    top = sorted(product_stocks.items(), key=lambda x: -len(x[1]))[:20]
    logger.info("  Top 20:")
    for pid, stocks in top:
        logger.info(f"    {pid:25s} {len(stocks):4d}只")


def main():
    dry_run = "--dry-run" in sys.argv
    product_stocks = sync()
    if not product_stocks:
        return

    conn = pg_connect()
    try:
        import_to_db(conn, product_stocks, dry_run=dry_run)
    finally:
        conn.close()


if __name__ == "__main__":
    main()
