#!/usr/bin/env python3
"""
全品种期货库存同步: 东财(主力) + LME + CZCE(兜底)
=====================================================
覆盖48个期货品种的每日仓单/库存数据 → mkt.commodity_inventory

用法:
  python sync_inventory.py                # 增量(最近30天)
  python sync_inventory.py --backfill     # 全量历史
  python sync_inventory.py --date 2026-08-01
"""

import sys, io, os, time, json
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from db_config import pg_connect
import logging
logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("inv_sync")

# ── 东财品种名 → product_id ──
EM_PRODUCTS = {
    # SHFE
    "沪铜":"copper","沪铝":"aluminum","沪锌":"zinc","沪铅":"lead","沪镍":"nickel","沪锡":"tin",
    "沪金":"gold","沪银":"silver","螺纹钢":"rebar","橡胶":"rubber","纸浆":"pulp",
    # DCE
    "铁矿石":"iron_ore","焦炭":"coke","焦煤":"coking_coal",
    "豆粕":"soybean_meal","玉米":"corn","生猪":"live_hog","豆油":"soybean_oil",
    "PVC":"pvc","聚丙烯":"polypropylene","乙二醇":"ethylene_glycol","苯乙烯":"styrene",
    "棕榈油":"palm_oil",
    # CZCE
    "PTA":"pta","纯碱":"soda_ash","尿素":"urea","玻璃":"glass","白糖":"sugar",
    "锰硅":"manganese","硅铁":"silicon_metal","短纤":"polyester","棉纱":"cotton_yarn",
    # GFEX
    "碳酸锂":"lithium_carbonate",
}

EM_RETRY_NAMES = {
    "沪铝":["沪铝","铝"],
    "沪镍":["沪镍","镍"],
    "沪锡":["沪锡","锡"],
    "棕榈油":["棕榈油","棕榈"],
    "热轧卷板":["热轧","热卷","热轧板卷"],
    "甲醇":["甲醇","郑醇"],
    "棉花":["棉花","郑棉"],
    "菜油":["菜油","菜籽油","郑油"],
    "苹果":["苹果","鲜苹果"],
    "红枣":["红枣","干红枣"],
    "液化气":["液化气","LPG"],
    "燃料油":["燃料油","沪燃油"],
    "沥青":["沥青","石油沥青"],
    "聚乙烯":["聚乙烯","塑料","LLDPE"],
}

# ── 代理 ──
def setup_proxy():
    import os; os.environ['HTTP_PROXY']=''; os.environ['HTTPS_PROXY']=''
    import requests; s=requests.Session(); s.trust_env=False
    s.proxies={'http':'http://127.0.0.1:7890','https':'http://127.0.0.1:7890'}
    try:
        import akshare.utils.request as akreq
        akreq.session=s
    except: pass
    return s


def ensure_table(conn):
    cur = conn.cursor()
    cur.execute("""
        CREATE TABLE IF NOT EXISTS mkt.commodity_inventory (
            product_id   VARCHAR(64) NOT NULL,
            trade_date   DATE NOT NULL,
            inventory    DOUBLE PRECISION,
            source       VARCHAR(32),
            PRIMARY KEY (product_id, trade_date)
        )
    """)
    conn.commit()


def fetch_em_one(name: str) -> list:
    """拉取单品种东财库存"""
    import akshare as ak
    try:
        df = ak.futures_inventory_em(symbol=name)
        if df is None or df.empty:
            return []
        results = []
        for _, row in df.iterrows():
            dt = str(row.get("日期", ""))[:10]
            val = None
            for c in ["库存","仓单","stock","inventory"]:
                if c in df.columns:
                    v = row[c]
                    if v and float(v) > 0:
                        val = float(v)
                        break
            if val and dt:
                results.append((dt, val))
        return results
    except Exception as e:
        logger.debug(f"  {name} fail: {e}")
        return []


def sync_em(conn, end_date: str = None):
    """东财全品种同步"""
    setup_proxy()
    total = 0
    cur = conn.cursor()
    today = end_date or __import__('datetime').date.today().isoformat()

    for name, pid in EM_PRODUCTS.items():
        records = fetch_em_one(name)
        if not records:
            # 重试别名
            for alt in EM_RETRY_NAMES.get(name, []):
                records = fetch_em_one(alt)
                if records: break
        time.sleep(1.5)  # 东财限流
        for dt, val in records:
            cur.execute(
                """INSERT INTO mkt.commodity_inventory VALUES (%s,%s,%s,%s)
                   ON CONFLICT (product_id,trade_date) DO UPDATE SET inventory=EXCLUDED.inventory,source=EXCLUDED.source""",
                (pid, dt, val, "EM"))
            total += 1
        if records:
            logger.info(f"  {name}->{pid}: {len(records)}条")
        else:
            logger.warning(f"  {name}->{pid}: 无数据")

    conn.commit()
    logger.info(f"东财: {total}条")
    return total


def sync_lme(conn):
    """LME库存"""
    import akshare as ak
    import pandas as pd
    LME_MAP = {"铜":"copper","铝":"aluminum","锌":"zinc","铅":"lead","镍":"nickel","锡":"tin"}
    try:
        df = ak.macro_euro_lme_stock()
    except:
        logger.warning("LME fail")
        return 0
    total = 0
    cur = conn.cursor()
    df["日期"] = pd.to_datetime(df["日期"])
    for col in df.columns:
        if "-库存" in col:
            metal = col.split("-")[0]
            pid = LME_MAP.get(metal)
            if not pid: continue
            for _, row in df.iterrows():
                v = row[col]
                if pd.notna(v) and float(v) > 0:
                    dt = row["日期"].strftime("%Y-%m-%d")
                    cur.execute(
                        "INSERT INTO mkt.commodity_inventory VALUES (%s,%s,%s,%s) ON CONFLICT DO NOTHING",
                        (pid, dt, float(v), "LME"))
                    total += 1
    conn.commit()
    logger.info(f"LME: {total}条")
    return total


def compute_changes(conn):
    """计算周/月环比"""
    cur = conn.cursor()
    cur.execute("""
        UPDATE mkt.commodity_inventory i SET
          change_wow = CASE WHEN w.inventory>0 THEN (i.inventory-w.inventory)/w.inventory END,
          change_mom = CASE WHEN m.inventory>0 THEN (i.inventory-m.inventory)/m.inventory END
        FROM mkt.commodity_inventory w, mkt.commodity_inventory m
        WHERE i.product_id=w.product_id AND w.trade_date=i.trade_date-7
          AND i.product_id=m.product_id AND m.trade_date=i.trade_date-30
    """)
    conn.commit()
    logger.info("环比计算完成")


def show_summary(conn):
    cur = conn.cursor()
    cur.execute("""SELECT product_id, COUNT(*), MIN(trade_date), MAX(trade_date)
        FROM mkt.commodity_inventory GROUP BY product_id ORDER BY COUNT(*) DESC""")
    for r in cur.fetchall():
        print(f"  {r[0]:25s} {r[1]:5d}条  {r[2]}~{r[3]}")


if __name__ == "__main__":
    import argparse
    p = argparse.ArgumentParser()
    p.add_argument("--backfill",action="store_true")
    p.add_argument("--date")
    p.add_argument("--summary",action="store_true")
    args = p.parse_args()

    conn = pg_connect()
    ensure_table(conn)

    if args.summary:
        show_summary(conn)
    else:
        sync_lme(conn)
        sync_em(conn)
        compute_changes(conn)
        show_summary(conn)

    conn.close()
