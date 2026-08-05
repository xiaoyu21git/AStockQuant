#!/usr/bin/env python3
"""
从东方财富概念板块同步最新商品→A股映射 (绕过akshare,直接调API)
=================================================================
每天更新, 远优于静态知识图谱。

API:
  概念列表: push2.eastmoney.com/api/qt/clist/get?fs=m:90+t:3
  成分股:   push2.eastmoney.com/api/qt/clist/get?fs=b:BKxxxx

使用: python tools/sync_concept_to_commodity.py [--dry-run]
"""

import sys, os, json, time, re
from collections import defaultdict

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from db_config import pg_connect
import logging
logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("sync_concept")

# ══════════════════════════════════════════════════════════════════════════════
# 概念名 → product_id  (东方财富概念板块名称)
# ══════════════════════════════════════════════════════════════════════════════

CONCEPT_TO_PRODUCT = {
    # 金属
    "铜":"copper","铜缆高速连接":"copper","铜箔":"copper","黄金概念":"gold","黄金":"gold",
    "白银":"silver","铝":"aluminum","铅":"lead","锌":"zinc","镍":"nickel","锡":"tin",
    "稀土永磁":"rare_earth","稀土":"rare_earth",
    "钨":"tungsten","钼":"molybdenum","钴":"cobalt","金属钴":"cobalt",
    "锂矿":"lithium","锂电池":"lithium","锂电":"lithium","碳酸锂":"lithium_carbonate",
    "锰":"manganese","锰硅":"manganese","金属镁":"magnesium","镁":"magnesium",
    "海绵钛":"titanium","钛":"titanium","钛白粉概念":"titanium_dioxide","钛白粉":"titanium_dioxide",
    "工业硅":"silicon_metal","有机硅":"silicon","多晶硅":"polysilicon",
    "镓":"gallium","锗":"germanium","锑":"antimony","钒电池":"vanadium",
    # 黑色
    "钢铁":"rebar","钢铁行业":"rebar","普钢":"rebar","特钢":"rebar","铁矿石":"iron_ore",
    "硅钢":"silicon_steel","不锈钢":"rebar",
    # 能源
    "石油":"crude_oil","石油行业":"crude_oil","原油":"crude_oil","油气":"crude_oil",
    "天然气":"natural_gas","页岩气":"natural_gas","可燃冰":"natural_gas","油气设服":"crude_oil",
    "燃料油":"fuel_oil","沥青":"asphalt","液化气":"lpg","LPG":"lpg",
    # 煤炭
    "煤炭":"thermal_coal","煤炭行业":"thermal_coal","动力煤":"thermal_coal","煤化工":"coke",
    "焦煤":"coking_coal","焦炭":"coke",
    # 化工
    "PTA":"pta","乙二醇":"ethylene_glycol","聚丙烯":"polypropylene","聚乙烯":"polyethylene",
    "PVC":"pvc","聚氯乙烯":"pvc","甲醇":"methanol","甲醇概念":"methanol",
    "纯碱":"soda_ash","烧碱":"caustic_soda","尿素":"urea","化肥":"urea",
    "苯乙烯":"styrene","醋酸":"acetic_acid","氟化工":"hydrofluoric_acid","氢氟酸":"hydrofluoric_acid",
    "硫酸":"sulfuric_acid","磷化工":"phosphoric_acid","磷矿":"phosphoric_acid","磷酸":"phosphoric_acid",
    "MDI":"mdi","TDI":"tdi","丙烯酸":"acrylic_acid","环氧丙烷":"propylene_oxide",
    # 建材
    "水泥":"cement","水泥建材":"cement","玻璃":"glass","玻璃玻纤":"glass",
    "浮法玻璃":"float_glass","光伏玻璃":"pv_glass","钢管":"steel_pipe",
    # 农产品
    "大豆":"soybean","豆粕":"soybean_meal","豆油":"soybean_oil",
    "玉米":"corn","玉米淀粉":"corn_starch","小麦":"wheat",
    "棕榈油":"palm_oil","菜籽油":"rapeseed_oil","棉花":"cotton",
    "白糖":"sugar","糖":"sugar","棉纱":"cotton_yarn",
    "橡胶":"rubber","纸浆":"pulp","猪肉":"live_hog","猪肉概念":"live_hog",
    "养殖":"live_hog","鸡肉":"live_hog","鸡蛋":"egg","苹果":"apple","红枣":"jujube",
    "饲料":"soybean_meal",
    # 新能源材料
    "六氟磷酸锂":"lipf6","电解液":"electrolyte","隔膜":"separator",
    "正极材料":"cathode","磷酸铁锂":"lfp","三元材料":"ncm","负极材料":"anode",
    "钠电池":"electrolyte","固态电池":"electrolyte","麒麟电池":"electrolyte",
    # 光伏
    "光伏":"solar_cell","光伏建筑":"solar_cell","太阳能":"solar_cell",
    "光伏电池":"solar_cell","光伏组件":"solar_module",
    "硅片":"solar_wafer","HIT电池":"solar_cell","HJT电池":"solar_cell",
    "TOPCon电池":"solar_cell","钙钛矿电池":"solar_cell",
}


def em_request(fs: str, fields: str = "f12,f14", pz: int = 5000):
    """直接请求东方财富API (绕过akshare代理问题)"""
    import requests
    session = requests.Session()
    session.trust_env = False
    url = "https://push2.eastmoney.com/api/qt/clist/get"
    params = {
        "pn": "1", "pz": str(pz), "po": "1", "np": "1",
        "ut": "bd1d9ddb04089700cf9c27f6f7426281",
        "fltt": "2", "invt": "2",
        "fid": "f12", "fs": fs, "fields": fields,
    }
    r = session.get(url, params=params, timeout=30)
    r.raise_for_status()
    data = r.json()
    if data.get("rc") != 0:
        raise RuntimeError(f"API error: {data}")
    return data.get("data", {}).get("diff", [])


def fetch_all_concept_stocks():
    """拉取东方财富概念板块 + 成分股"""
    # 1. 概念列表
    logger.info("拉取东方财富概念板块列表...")
    concepts = em_request("m:90+t:3", "f12,f14")
    logger.info(f"  共 {len(concepts)} 个概念板块")

    # 匹配商品概念
    product_stocks = defaultdict(lambda: defaultdict(float))
    matched = 0
    skipped = 0

    for item in concepts:
        name = item.get("f14", "")
        pid = CONCEPT_TO_PRODUCT.get(name)
        if pid is None:
            skipped += 1
            continue

        matched += 1
        bk_code = item.get("f12", "")
        logger.info(f"  [{pid}] {name} ({bk_code})")

        # 2. 拉取成分股
        try:
            members = em_request(f"b:{bk_code}", "f12")
        except Exception as e:
            logger.warning(f"    拉取失败: {e}")
            continue

        for m in members:
            code = m.get("f12", "")
            if not code or not re.match(r'^[0-9]{6}$', str(code)):
                continue
            product_stocks[pid][code] += 1.0

        time.sleep(0.3)  # 节制频率

    logger.info(f"  匹配 {matched} 个商品概念, 跳过 {skipped} 个无关概念")
    return product_stocks


def normalize_weights(product_stocks):
    for pid, stocks in product_stocks.items():
        if not stocks: continue
        max_v = max(stocks.values())
        if max_v > 0:
            for sym in stocks:
                stocks[sym] = round(stocks[sym] / max_v, 2)


def import_to_db(conn, product_stocks, dry_run=False):
    cur = conn.cursor()
    version = "concept_v1"

    if not dry_run:
        cur.execute(f"DELETE FROM ref.product_stock_mapping WHERE version='{version}'")
        logger.info(f"已清空 {version} 旧数据")

    total = 0
    all_symbols = set()
    n_products = 0

    for pid in sorted(product_stocks.keys()):
        stocks = product_stocks[pid]
        if not stocks: continue
        n_products += 1
        for sym, weight in sorted(stocks.items(), key=lambda x: -x[1]):
            all_symbols.add(sym)
            if not dry_run:
                cur.execute(
                    """INSERT INTO ref.product_stock_mapping
                       (product_id, symbol, weight, effective_date, expired_date, version)
                       VALUES (%s,%s,%s,'2000-01-01','2099-12-31',%s)
                       ON CONFLICT (product_id, symbol, effective_date) DO NOTHING""",
                    (pid, sym, weight, version))
            total += 1

    if not dry_run: conn.commit()

    logger.info(f"\n{'[预览]' if dry_run else '导入'}完成:")
    logger.info(f"  {n_products} 商品, {len(all_symbols)} 股票, {total} 条映射")

    top = sorted(product_stocks.items(), key=lambda x: -len(x[1]))[:25]
    logger.info("  Top 25:")
    for pid, stocks in top:
        logger.info(f"    {pid:25s} {len(stocks):4d}只")


def main():
    dry_run = "--dry-run" in sys.argv
    product_stocks = fetch_all_concept_stocks()
    normalize_weights(product_stocks)

    conn = pg_connect()
    try:
        import_to_db(conn, product_stocks, dry_run=dry_run)
    finally:
        conn.close()


if __name__ == "__main__":
    main()
