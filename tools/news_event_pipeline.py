#!/usr/bin/env python3
"""
商品突发事件检测 — 每日新闻流水线
==============================
从 akshare 获取财经新闻 → 关键词匹配检测商品事件 → 写入 PG 信号表

用法:
  python news_event_pipeline.py                  # 每天运行一次
  python news_event_pipeline.py --source sina     # 指定新闻源 (sina/eastmoney)
  python news_event_pipeline.py --backfill 30     # 回补近N天新闻
"""

import sys, os, json, re, logging, argparse
from datetime import datetime, timedelta
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from db_config import pg_connect

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("news_event")

# ══════════════════════════════════════════════════════════════════════════════
# 事件模式库 (与 commodity_event_detector.py 同步)
# ══════════════════════════════════════════════════════════════════════════════

EVENT_PATTERNS = [
    # 供给侧冲击 — 矿山/油田/气田事故停产
    (r"(矿|油田|气田|矿区|矿山).{0,8}(事故|崩塌|透水|爆炸|火灾|停产|停工|关闭|塌陷|滑坡)",
     "supply_disruption", "事故停产", 1.0, +1),
    (r"(地震|洪水|暴雨|台风|泥石流|暴雪|极端天气).{0,10}(矿|油田|厂区|产区|主产区|矿区|供应)",
     "supply_disruption", "自然灾害", 0.8, +1),
    # 政策限产
    (r"(环保|能耗|碳达峰|限产|错峰|压减|去产能|淘汰落后|产能置换).{0,15}(产能|产量|开工|生产|停产)",
     "policy_restriction", "政策限产", 0.7, +1),
    (r"(发改委|工信部|生态环境部|应急管理部|商务部).{0,30}(产能|限产|停产|整顿|关停|检查|督查|压减)",
     "policy_restriction", "行政限产", 0.8, +1),
    # 贸易限制
    (r"(禁止|限制|叫停|暂停).{0,6}(进口|出口|通关|报关|配额)",
     "trade_restriction", "贸易限制", 0.8, +1),
    (r"(反倾销|反补贴|加征关税|制裁|关税).{0,10}(进口|产品|商品|矿|材)",
     "trade_restriction", "贸易壁垒", 0.7, +1),
    # 运输中断
    (r"(港口|码头|铁路|管道|航运|海运).{0,8}(封闭|中断|停运|拥堵|罢工|延误|瘫痪)",
     "logistics_disruption", "运输中断", 0.8, +1),
    # 国储操作
    (r"(国储|收储|抛储|轮储|战略储备|储备).{0,6}(铜|铝|锌|镍|棉花|白糖|橡胶|原油|猪肉|稀土|锂|钴|钨)",
     "reserve_operation", "国储操作", 0.9, +1),
    # 装置检修/投产
    (r"(检修|大修|停产检修|投产|新产能|装置投产|扩产).{0,10}(万吨|产能|装置|PTA|甲醇|纯碱|尿素|乙烯|丙烯)",
     "capacity_event", "装置变动", 0.6, -1),  # 投产=供给增加=利空
    (r"(检修|停产检修|意外停车).{0,5}$",
     "capacity_event", "装置停产", 0.6, +1),  # 停产=供给减少=利多
    # 罢工/劳资
    (r"(罢工|劳资|工会|谈判破裂).{0,6}(矿|油田|港口|工厂)",
     "supply_disruption", "罢工停产", 0.7, +1),
    # 新能源政策
    (r"(新能源|储能|光伏|风电|电动车).{0,10}(补贴|政策|规划|目标|装机)",
     "policy_restriction", "新能源政策", 0.5, +1),
]

# 关键词 → product_id
COMMODITY_KEYWORDS = {
    "锂": ["lithium_carbonate", "lithium"], "碳酸锂": ["lithium_carbonate"],
    "铜": ["copper"], "铝": ["aluminum"], "锌": ["zinc"], "铅": ["lead"],
    "镍": ["nickel"], "锡": ["tin"], "黄金": ["gold"], "白银": ["silver"],
    "铁矿石": ["iron_ore"], "螺纹钢": ["rebar"], "热轧": ["hot_rolled_coil"],
    "焦煤": ["coking_coal"], "焦炭": ["coke"], "动力煤": ["thermal_coal"], "煤": ["thermal_coal"],
    "原油": ["crude_oil"], "石油": ["crude_oil"], "天然气": ["natural_gas"],
    "PTA": ["pta"], "乙二醇": ["ethylene_glycol"], "聚丙烯": ["polypropylene"],
    "PVC": ["pvc"], "甲醇": ["methanol"], "纯碱": ["soda_ash"], "烧碱": ["caustic_soda"],
    "尿素": ["urea"], "苯乙烯": ["styrene"], "醋酸": ["acetic_acid"],
    "钛白粉": ["titanium_dioxide"], "磷酸": ["phosphoric_acid"], "硫酸": ["sulfuric_acid"],
    "水泥": ["cement"], "玻璃": ["glass"], "浮法": ["float_glass"],
    "豆粕": ["soybean_meal"], "大豆": ["soybean"], "豆油": ["soybean_oil"],
    "玉米": ["corn"], "棕榈油": ["palm_oil"], "菜油": ["rapeseed_oil"],
    "棉花": ["cotton"], "白糖": ["sugar"], "橡胶": ["rubber"], "纸浆": ["pulp"],
    "生猪": ["live_hog"], "猪": ["live_hog"], "鸡蛋": ["egg"],
    "稀土": ["rare_earth"], "钨": ["tungsten"], "钴": ["cobalt"],
    "工业硅": ["silicon_metal"], "多晶硅": ["polysilicon"],
    "六氟磷酸锂": ["lipf6"], "电解液": ["electrolyte"],
    "锂矿": ["lithium_carbonate", "lithium"],
    "铜矿": ["copper"], "铝矿": ["aluminum"], "铁矿": ["iron_ore"],
}


def fetch_news_akshare(date_str: str = None) -> list[dict]:
    """从 akshare 获取财经新闻"""
    try:
        import akshare as ak
        date_str = date_str or datetime.now().strftime("%Y%m%d")
        df = ak.stock_info_global_em()
        if df is None or df.empty:
            logger.warning("akshare stock_info_global_em 返回空")
            return []
        # 取所有标题
        news = []
        for _, row in df.iterrows():
            title = str(row.get("title", row.get("content", "")))
            if title and len(title) > 5:
                news.append({"title": title, "source": "eastmoney", "date": date_str})
        return news
    except Exception as e:
        logger.warning("akshare 新闻获取失败: %s", e)
        return []


def fetch_news_fallback() -> list[dict]:
    """备用: 直接请求东方财富新闻 API"""
    import requests
    try:
        headers = {"User-Agent": "Mozilla/5.0", "Referer": "https://finance.eastmoney.com/"}
        url = "https://push2.eastmoney.com/api/qt/ulist.np/get?fltt=2&fields=f3,f12,f14&secids=1.000001,0.399001&_=0"
        # 这API不直接给新闻, 换一个
        url2 = "https://finance.eastmoney.com/a/czqyw.html"
        resp = requests.get(url2, headers=headers, timeout=10)
        if resp.status_code == 200:
            import re
            titles = re.findall(r'<a[^>]*>([^<]{10,200})</a>', resp.text)
            return [{"title": t.strip(), "source": "eastmoney", "date": datetime.now().strftime("%Y%m%d")}
                    for t in titles if len(t.strip()) > 10]
    except Exception as e:
        logger.warning("备用新闻源失败: %s", e)
    return []


def detect_events(news_items: list[dict]) -> list[dict]:
    """对新闻列表做关键词匹配, 返回检测到的事件"""
    events = []
    for item in news_items:
        title = item["title"]
        # 1. 匹配事件模式
        for pattern, event_type, event_name, urgency, direction in EVENT_PATTERNS:
            if re.search(pattern, title):
                # 2. 匹配商品
                commodities = set()
                for kw, pids in COMMODITY_KEYWORDS.items():
                    if kw in title:
                        commodities.update(pids)

                if not commodities:
                    continue

                for pid in commodities:
                    events.append({
                        "product_id": pid,
                        "event_type": event_type,
                        "event_name": event_name,
                        "urgency": urgency,
                        "direction": direction,
                        "title": title[:500],
                        "source": item.get("source", "auto"),
                    })
                break  # 一个标题只匹配第一个事件类型
    return events


def save_events(conn, events: list[dict]) -> int:
    """写入 PG alpha.commodity_event_signals"""
    cur = conn.cursor()
    written = 0
    for evt in events:
        cur.execute("""
            INSERT INTO alpha.commodity_event_signals
            (product_id, event_type, event_name, urgency, direction, title, source)
            VALUES (%s, %s, %s, %s, %s, %s, %s)
        """, (evt["product_id"], evt["event_type"], evt["event_name"],
              evt["urgency"], evt["direction"], evt["title"], evt["source"]))
        written += 1
    conn.commit()
    return written


def main():
    parser = argparse.ArgumentParser(description="商品突发事件检测流水线")
    parser.add_argument("--source", default="eastmoney", choices=["eastmoney", "sina", "akshare"])
    parser.add_argument("--backfill", type=int, default=0, help="回补近N天")
    args = parser.parse_args()

    conn = pg_connect()

    try:
        if args.backfill > 0:
            logger.info("回补模式: 近%d天", args.backfill)
            all_events = []
            for d in range(args.backfill):
                date_str = (datetime.now() - timedelta(days=d)).strftime("%Y%m%d")
                news = fetch_news_akshare(date_str) if args.source == "akshare" else fetch_news_fallback()
                events = detect_events(news)
                all_events.extend(events)
                if news:
                    logger.info("  %s: %d条新闻 → %d个事件", date_str, len(news), len(events))
            if all_events:
                n = save_events(conn, all_events)
                logger.info("回补完成: 写入%d个事件信号", n)
            else:
                logger.info("回补完成: 未检测到商品事件")
        else:
            # 当日模式
            logger.info("获取新闻...")
            news = fetch_news_akshare() if args.source == "akshare" else fetch_news_fallback()
            logger.info("获取到%d条新闻", len(news))
            events = detect_events(news)
            logger.info("检测到%d个商品事件", len(events))
            if events:
                n = save_events(conn, events)
                logger.info("写入%d个事件信号", n)
                for evt in events[:5]:
                    logger.info("  %s [%s] %s dir=%+d urgency=%.1f",
                               evt["product_id"], evt["event_name"],
                               evt["title"][:60], evt["direction"], evt["urgency"])
            else:
                logger.info("无商品事件")
    finally:
        conn.close()


if __name__ == "__main__":
    main()
