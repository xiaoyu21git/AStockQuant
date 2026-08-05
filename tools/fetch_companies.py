#!/usr/bin/env python3
"""
批量拉取A股公司主营业务 (Clash代理 + 本地缓存 + 限速)
=====================================================
用法:
  python fetch_companies.py --symbols 601899.SH,000630.SZ   # 指定股票
  python fetch_companies.py --batch csi800 --limit 100       # 中证800前100只
  python fetch_companies.py --from-cache                     # 从缓存读取
"""

import sys, io, os, json, time, requests
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import logging
logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("fetch")

# ── 配置 ──
CACHE_FILE = os.path.join(os.path.dirname(__file__), "company_cache.json")
PROXY = "http://127.0.0.1:7890"
DELAY = 0.3  # 每次请求间隔(秒)

# akshare CNINFO 直接API (绕过akshare, 用代理直连)
CNINFO_URL = "http://www.cninfo.com.cn/new/disclosure"


def load_cache() -> dict:
    if os.path.exists(CACHE_FILE):
        with open(CACHE_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    return {}


def save_cache(cache: dict):
    with open(CACHE_FILE, "w", encoding="utf-8") as f:
        json.dump(cache, f, ensure_ascii=False, indent=2)


def fetch_one_akshare(symbol: str) -> dict:
    """通过akshare + 代理拉取单只股票概况"""
    import akshare as ak
    code = symbol.replace(".SH", "").replace(".SZ", "")

    # 设置代理 (akshare底层用requests)
    # monkey-patch: 给akshare的request session设置代理
    try:
        import akshare.utils.request as akreq
        if not hasattr(akreq, '_proxied'):
            akreq.session.proxies = {'http': PROXY, 'https': PROXY}
            akreq.session.trust_env = False
            akreq._proxied = True
    except: pass

    try:
        df = ak.stock_profile_cninfo(symbol=code)
        row = df.iloc[0]
        return {
            "symbol": symbol,
            "name": str(row.get("公司名称", "")),
            "business": str(row.get("主营业务", "")),
            "scope": str(row.get("经营范围", "")),
            "industry": str(row.get("所属行业", "")),
        }
    except Exception as e:
        logger.warning(f"akshare failed for {symbol}: {e}")
        return {"symbol": symbol, "name": symbol, "business": "", "error": str(e)}


def fetch_batch(symbols: list, use_cache: bool = True, limit: int = None):
    """批量拉取, 代理+限速+缓存"""
    cache = load_cache() if use_cache else {}
    results = {}

    if limit:
        symbols = symbols[:limit]

    for i, sym in enumerate(symbols):
        if sym in cache and cache[sym].get("business"):
            results[sym] = cache[sym]
            continue

        logger.info(f"[{i+1}/{len(symbols)}] {sym}")
        data = fetch_one_akshare(sym)
        results[sym] = data
        cache[sym] = data

        # 每10只保存一次缓存
        if (i + 1) % 10 == 0:
            save_cache(cache)

        time.sleep(DELAY)

    save_cache(cache)
    return results


def get_csi800_symbols() -> list:
    """中证800成分股 (从缓存或akshare)"""
    import akshare as ak
    try:
        import akshare.utils.request as akreq
        akreq.session.proxies = {'http': PROXY, 'https': PROXY}
        akreq.session.trust_env = False
    except: pass

    df = ak.index_stock_cons(symbol="000906")
    codes = []
    for _, row in df.iterrows():
        c = str(row["品种代码"])
        if c.endswith(".SH") or c.endswith(".SZ"):
            codes.append(c)
        elif c.startswith("6"):
            codes.append(f"{c}.SH")
        else:
            codes.append(f"{c}.SZ")
    return codes


def get_commodity_stocks(symbols: list) -> list:
    """过滤出商品相关行业的股票"""
    import baostock as bs
    bs.login()
    rs = bs.query_stock_industry()
    csrc = {}
    while (rs.error_code == '0') and rs.next():
        r = rs.get_row_data()
        s = r[1].replace("sh.", "").replace("sz.", "")
        ex = "SH" if "sh." in r[1] else "SZ"
        csrc[f"{s}.{ex}"] = r[3][:3]
    bs.logout()

    relevant = {'B06','B07','B08','B09','B10','B11','A01','A02','A03','A04',
                'C13','C17','C18','C19','C22','C25','C26','C27','C28','C29',
                'C30','C31','C32','C33','C34','C35','C36','C37','C38','C39','C40',
                'D44','D45'}
    return [(s, csrc.get(s, '?')) for s in symbols if csrc.get(s, '?') in relevant]


if __name__ == "__main__":
    import argparse
    p = argparse.ArgumentParser()
    p.add_argument("--symbols", help="逗号分隔")
    p.add_argument("--batch", choices=["csi800"])
    p.add_argument("--limit", type=int, default=10)
    p.add_argument("--from-cache", action="store_true")
    args = p.parse_args()

    if args.symbols:
        syms = [s.strip() for s in args.symbols.split(",")]
    elif args.batch == "csi800":
        all_syms = get_csi800_symbols()
        comm = get_commodity_stocks(all_syms)
        syms = [s for s, _ in comm]
        logger.info(f"中证800商品股: {len(syms)}只")
    else:
        syms = []

    results = fetch_batch(syms, use_cache=not args.from_cache, limit=args.limit)

    # 打印摘要
    for sym, data in results.items():
        if data.get("error"):
            print(f"{sym}: ERROR {data['error'][:60]}")
        else:
            biz = data.get("business", "")[:80]
            print(f"{sym} {data['name']}: {biz}")
