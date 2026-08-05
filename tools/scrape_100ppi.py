#!/usr/bin/env python3
"""
生意社商品现货价格爬虫
======================
从 mprice.100ppi.com 提取商品历史现货价格，写入 mkt.commodity_prices_daily。
反爬逻辑: JS 挑战 → 设 HW_CHECK cookie → 重定向 → 返回真实页面。

用法:
  python scrape_100ppi.py                           # 爬取所有映射商品
  python scrape_100ppi.py --product-id 1434         # 爬取指定商品
  python scrape_100ppi.py --product tungsten        # 按名称爬取
  python scrape_100ppi.py --list-products           # 列出映射表中的商品
  python scrape_100ppi.py --search 钨               # 搜索生意社商品ID
"""

import argparse
import re
import sys
import os
import time
import logging
from collections import defaultdict
from typing import Dict, List, Optional, Tuple

import requests
from bs4 import BeautifulSoup

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from db_config import pg_connect

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("scrape_100ppi")

BASE_URL = "https://mprice.100ppi.com/price/detail-{pid}.html"
SEARCH_URL = "https://www.100ppi.com/search/price-{keyword}.html"
REQUEST_DELAY = 2.0  # seconds between requests
MAX_RETRIES = 3


def bypass_anti_bot(session: requests.Session, url: str) -> Optional[str]:
    """绕过 JS 反爬：提取 HW_CHECK cookie → 设置 → 重试"""
    for attempt in range(MAX_RETRIES):
        r = session.get(url, timeout=15)
        html = r.text

        # Check if we got the real page
        if len(html) > 1000 and ('chart_data' in html or 'price' in html.lower()):
            return html

        # Check for anti-bot challenge
        match = re.search(r'var _0x2 = "([a-f0-9]+)"', html)
        if match:
            hw_hash = match.group(1)
            logger.debug("  Bypassing anti-bot: HW_CHECK=%s (attempt %d)", hw_hash, attempt + 1)
            session.cookies.set('HW_CHECK', hw_hash, domain='.100ppi.com', path='/')
            time.sleep(0.5)  # Wait for the JS redirect delay
            continue

        # Unknown response
        logger.warning("  Unexpected response: %d chars, status=%d", len(html), r.status_code)
        time.sleep(REQUEST_DELAY)
        continue

    return None


def extract_prices(html: str) -> List[Tuple[str, float]]:
    """从页面 HTML 中提取日期价格对"""
    prices = []

    # Method 1: chart_data XML embedded in script
    start = html.find('chart_data: "')
    if start > 0:
        start += len('chart_data: "')
        end = html.find('"', start)
        if end > start:
            chart_str = html[start:end]
            chart_str = chart_str.replace('&lt;', '<').replace('&gt;', '>')
            chart_str = chart_str.replace('&amp;', '&').replace('&quot;', '"')

            # Try XML parsing
            import xml.etree.ElementTree as ET
            try:
                root = ET.fromstring(chart_str)
                for elem in root.iter():
                    if elem.tag == 'data' and 'date' in elem.attrib:
                        date_str = elem.attrib['date']
                        # Value might be in 'close' or 'value' attribute
                        val = float(elem.attrib.get('close', elem.attrib.get('value', 0)))
                        if val > 0:
                            prices.append((date_str, val))
                if prices:
                    logger.debug("  XML parsed: %d data points", len(prices))
                    return prices
            except ET.ParseError:
                pass

            # Method 1b: regex fallback for simple key-value pairs
            pairs = re.findall(r'(\d{4}-\d{2}-\d{2})\D+?(\d+\.?\d*)', chart_str)
            if pairs:
                for date_str, val_str in pairs:
                    try:
                        prices.append((date_str, float(val_str)))
                    except ValueError:
                        continue
                if prices:
                    logger.debug("  Regex parsed: %d data points", len(prices))
                    return prices

    # Method 2: Look for data in table or list
    soup = BeautifulSoup(html, 'html.parser')
    # Check for HTML tables with prices
    tables = soup.select('table')
    for table in tables:
        rows = table.select('tr')
        for row in rows[1:]:  # skip header
            cells = row.select('td')
            if len(cells) >= 2:
                date_str = cells[0].get_text().strip()
                try:
                    val = float(cells[1].get_text().strip().replace(',', ''))
                    if re.match(r'\d{4}-\d{2}-\d{2}', date_str) and val > 0:
                        prices.append((date_str, val))
                except ValueError:
                    continue
    if prices:
        logger.debug("  Table parsed: %d data points", len(prices))
        return prices

    # Method 3: Any JSON data in scripts
    scripts = re.findall(r'<script[^>]*>(.*?)</script>', html, re.DOTALL)
    for script in scripts:
        # Look for arrays of [date, price]
        arr_match = re.findall(
            r'\["(\d{4}-\d{2}-\d{2})"\s*,\s*(\d+\.?\d*)\]', script)
        if arr_match:
            for date_str, val_str in arr_match:
                try:
                    prices.append((date_str, float(val_str)))
                except ValueError:
                    continue
            if len(prices) > 5:
                logger.debug("  Script array parsed: %d data points", len(prices))
                return prices

    return prices


def search_product(keyword: str) -> List[Tuple[str, str]]:
    """搜索商品名称 → 返回 [(名称, product_id), ...]"""
    session = requests.Session()
    session.headers.update({
        'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36',
        'Accept-Language': 'zh-CN,zh;q=0.9',
    })

    url = SEARCH_URL.format(keyword=keyword)
    html = bypass_anti_bot(session, url)
    if not html:
        return []

    soup = BeautifulSoup(html, 'html.parser')
    results = []
    for a in soup.select('a[href*="detail-"]'):
        href = a['href']
        name = a.get_text().strip()
        pid_match = re.search(r'detail-(\d+)', href)
        if pid_match and name:
            results.append((name, pid_match.group(1)))
    return results


def scrape_product(session: requests.Session, product_id: str) -> List[Tuple[str, float]]:
    """爬取单个商品的历史价格"""
    url = BASE_URL.format(pid=product_id)
    html = bypass_anti_bot(session, url)
    if not html:
        logger.error("  Failed to bypass anti-bot for product %s", product_id)
        return []

    prices = extract_prices(html)
    if not prices:
        # Save HTML for debugging
        logger.warning("  No prices extracted from product %s (html=%d chars)", product_id, len(html))
        return []

    return prices


def save_prices(conn, product_id: str, prices: List[Tuple[str, float]]):
    """将价格写入数据库"""
    cur = conn.cursor()
    written = 0
    for date_str, price in prices:
        cur.execute("""
            INSERT INTO mkt.commodity_prices_daily (product_id, trade_date, close_price)
            VALUES (%s, %s, %s)
            ON CONFLICT (product_id, trade_date) DO UPDATE SET close_price = EXCLUDED.close_price
        """, (product_id, date_str, price))
        written += 1
    conn.commit()
    return written


def get_product_id_mapping(conn) -> Dict[str, str]:
    """构建 product_stock_mapping.product_id → 生意社 product ID 的映射"""
    # 生意社已知的商品ID映射（手动维护）
    known_mapping = {
        # 有色金属
        "tungsten": "1434",      # 钨精矿
        "cobalt": "1450",        # 钴
        "rare_earth": "2350",    # 氧化镨钕
        "germanium": "2288",     # 锗
        "gallium": "2286",       # 镓
        "manganese_ore": "1439", # 锰矿
        "silicon": "2165",       # 硅
        "titanium": "1746",      # 钛
        # 新能源
        "lithium": "1899",       # 碳酸锂(现货)
        "lipf6": "2418",         # 六氟磷酸锂
        "electrolyte": "2262",   # 电解液
        "cathode": "2345",       # 正极材料
        "anode": "2346",         # 负极材料
        "separator": "2289",     # 隔膜
        "polysilicon": "2328",   # 多晶硅
        "solar_wafer": "2342",   # 硅片
        "solar_cell": "2343",    # 电池片
        "solar_module": "2344",  # 组件
        # 化工
        "caustic_soda": "1520",  # 烧碱
        "sulfuric_acid": "1835", # 硫酸
        "acetic_acid": "1836",   # 醋酸
        "titanium_dioxide": "1838", # 钛白粉
        "phosphoric_acid": "1958",  # 磷酸
        # 黑色
        "manganese_silicon": "1440", # 锰硅
        "ferrosilicon": "1455",  # 硅铁
        # 农产品
        "egg": "1293",           # 鸡蛋
    }
    return known_mapping


def main():
    parser = argparse.ArgumentParser(description="生意社商品价格爬虫")
    parser.add_argument("--product-id", help="爬取指定 product_id（生意社数字ID）")
    parser.add_argument("--product", help="按 product_stock_mapping 中的产品名爬取")
    parser.add_argument("--search", help="搜索生意社商品")
    parser.add_argument("--list-products", action="store_true", help="列出映射表中的商品")
    parser.add_argument("--all", action="store_true", help="爬取所有已知映射的商品")
    parser.add_argument("--re-rank", action="store_true", help="爬取后自动重算排名")
    args = parser.parse_args()

    conn = pg_connect()
    session = requests.Session()
    session.headers.update({
        'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36',
        'Accept': 'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8',
        'Accept-Language': 'zh-CN,zh;q=0.9,en;q=0.8',
        'Accept-Encoding': 'gzip, deflate',
        'Connection': 'keep-alive',
    })

    try:
        # --search
        if args.search:
            results = search_product(args.search)
            print(f"搜索 '{args.search}': {len(results)} 个结果")
            for name, pid in results:
                print(f"  {pid}: {name}")
            return

        # --list-products
        if args.list_products:
            cur = conn.cursor()
            cur.execute("""
                SELECT DISTINCT psm.product_id, COUNT(*) as n
                FROM ref.product_stock_mapping psm
                WHERE psm.product_id NOT IN (SELECT DISTINCT product_id FROM mkt.commodity_prices_daily)
                GROUP BY psm.product_id ORDER BY n DESC
            """)
            rows = cur.fetchall()
            known = get_product_id_mapping(conn)
            print(f"无价格数据的商品 ({len(rows)}):")
            for pid, n in rows:
                kpid = known.get(pid, "?")
                print(f"  {pid:<30} {n:>4}只股  生意社ID: {kpid}")
            return

        # --product-id
        if args.product_id:
            logger.info("爬取商品 %s...", args.product_id)
            prices = scrape_product(session, args.product_id)
            if prices:
                written = save_prices(conn, args.product_id, prices)
                logger.info("  写入 %d 条价格 (%s ~ %s)", written, prices[0][0], prices[-1][0])
            else:
                logger.warning("  未提取到价格数据")
            return

        # --product or --all
        known = get_product_id_mapping(conn)
        targets = {}
        if args.product:
            if args.product in known:
                targets[args.product] = known[args.product]
            else:
                logger.error("未知商品: %s", args.product)
                return
        elif args.all:
            targets = known

        if not targets:
            parser.print_help()
            return

        total_prices = 0
        succeeded = 0

        for prod_name, prod_id in sorted(targets.items()):
            logger.info("爬取 %s (ID=%s)...", prod_name, prod_id)
            time.sleep(REQUEST_DELAY)

            prices = scrape_product(session, prod_id)
            if prices:
                written = save_prices(conn, prod_name, prices)
                logger.info("  %s: %d 条 (%s ~ %s)", prod_name, written,
                           prices[0][0], prices[-1][0])
                total_prices += written
                succeeded += 1
            else:
                logger.warning("  %s: 无数据", prod_name)

        logger.info("完成: %d/%d 商品成功, 共 %d 条价格", succeeded, len(targets), total_prices)

        # Re-rank if requested
        if args.re_rank and succeeded > 0:
            logger.info("重算排名...")
            from commodity_ranker import compute_rank
            cur = conn.cursor()
            cur.execute("SELECT DISTINCT trade_date FROM mkt.commodity_prices_daily ORDER BY trade_date")
            dates = [str(r[0]) for r in cur.fetchall()]
            for i, d in enumerate(dates):
                compute_rank(conn, d, top_n=8)
                if (i + 1) % 500 == 0:
                    logger.info("  rank %d/%d", i + 1, len(dates))
            cur.execute("SELECT COUNT(DISTINCT product_id) FROM alpha.commodity_daily_rank")
            logger.info("排名完成: %d 个商品", cur.fetchone()[0])

    finally:
        session.close()
        conn.close()


if __name__ == "__main__":
    main()
