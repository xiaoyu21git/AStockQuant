#!/usr/bin/env python3
"""
商品突发事件检测 — 前置信号层
================================
在库存数据(周度滞后)之前, 通过新闻/公告检测供给冲击事件。

事件 → 关键词匹配 → 商品product_id → 方向+紧急度 → 关联标的

用法:
  python commodity_event_detector.py --test "澳洲锂矿突发停产"     # 单条测试
  python commodity_event_detector.py --dry-run                      # 接入事件流(模拟)
"""

import sys, io, os, json, re
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from db_config import pg_connect

# ══════════════════════════════════════════════════════════════════════════════
# 事件模式库: 触发关键词 → (商品, 方向, 紧急度)
# 方向: +1=供给冲击(涨价), -1=需求崩塌(跌价)
# 紧急度: 0-1 (1=即刻影响, 0.3=需后续确认)
# ══════════════════════════════════════════════════════════════════════════════

EVENT_PATTERNS = [
    # ── 矿山/油田事故停产 ──
    (r"(矿|油田|气田).{0,8}(事故|崩塌|透水|爆炸|火灾|停产|停工|关闭)",
     lambda m: ("supply_disruption", "事故停产", 1.0)),
    (r"(地震|洪水|暴雨|台风|泥石流|暴雪).{0,10}(矿|油田|厂区|产区|主产区)",
     lambda m: ("supply_disruption", "自然灾害", 0.8)),

    # ── 政策/环保限产 ──
    (r"(环保|能耗|碳达峰|限产|错峰|压减|去产能|淘汰落后).{0,8}(产能|产量|开工|生产)",
     lambda m: ("policy_restriction", "政策限产", 0.7)),
    (r"(发改委|工信部|生态环境部|应急管理部).{0,10}(产能|限产|停产|整顿|关停)",
     lambda m: ("policy_restriction", "行政限产", 0.8)),

    # ── 进口/出口限制 ──
    (r"(禁止|限制|叫停|暂停).{0,6}(进口|出口|通关|报关)",
     lambda m: ("trade_restriction", "贸易限制", 0.8)),
    (r"(反倾销|反补贴|加征关税|制裁).{0,6}(进口|产品|商品)",
     lambda m: ("trade_restriction", "贸易壁垒", 0.7)),

    # ── 运输中断 ──
    (r"(港口|码头|铁路|管道).{0,8}(封闭|中断|停运|拥堵|罢工)",
     lambda m: ("logistics_disruption", "运输中断", 0.8)),

    # ── 国储收储/抛储 ──
    (r"(国储|收储|抛储|轮储|战略储备).{0,6}(铜|铝|锌|镍|棉花|白糖|橡胶|原油|猪肉)",
     lambda m: ("reserve_operation", "国储操作", 0.9)),

    # ── 大型装置检修/投产 ──
    (r"(检修|大修|停产检修|投产|新产能|装置投产).{0,8}(万吨|产能|装置|PTA|甲醇|纯碱|尿素|乙烯|丙烯)",
     lambda m: ("capacity_event", "装置变动", 0.6)),

    # ── 矿山罢工/劳资纠纷 ──
    (r"(罢工|劳资|工会).{0,6}(矿|油田|港口)",
     lambda m: ("supply_disruption", "罢工停产", 0.7)),

    # ── 疫情/公共卫生 ──
    (r"(疫情|封控|静默).{0,10}(产区|工厂|矿|码头|运输)",
     lambda m: ("supply_disruption", "疫情封控", 0.6)),
]

# 关键词 → product_id 映射
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


def detect_events(text: str) -> list:
    """从文本中检测商品突发事件"""
    results = []

    # 1. 匹配事件模式
    triggered_patterns = []
    for pattern, resolver in EVENT_PATTERNS:
        m = re.search(pattern, text)
        if m:
            event_type, event_name, urgency = resolver(m)
            triggered_patterns.append((event_type, event_name, urgency))

    if not triggered_patterns:
        return []

    # 2. 匹配商品
    commodities = set()
    for kw, pids in COMMODITY_KEYWORDS.items():
        if kw in text:
            for pid in pids:
                commodities.add(pid)

    if not commodities:
        return []

    # 3. 取最高紧急度
    event_type, event_name, urgency = max(triggered_patterns, key=lambda x: x[2])

    for pid in commodities:
        results.append({
            "product_id": pid,
            "event_type": event_type,
            "event_name": event_name,
            "urgency": urgency,
            "direction": +1 if event_type in ("supply_disruption","policy_restriction",
                "trade_restriction","logistics_disruption","reserve_operation") else -1,
            "matched_text": text[:200],
        })

    return results


def get_signal_text(result: dict) -> str:
    """生成信号描述"""
    direction_text = "利多(供给冲击)" if result["direction"] > 0 else "利空"
    return (f"[{result['event_name']}] {result['product_id']} "
            f"{direction_text} 紧急度={result['urgency']:.0%}")


def get_affected_stocks(conn, product_id: str, direction: int) -> list:
    """获取受影响标的"""
    cur = conn.cursor()
    sign = 1 if direction > 0 else -1
    cur.execute("""
        SELECT symbol, weight FROM ref.product_stock_mapping
        WHERE product_id=%s AND version IN ('llm_v1','llm_v2_pricing','manual_fix_v2')
        ORDER BY weight*%s DESC LIMIT 5
    """, (product_id, sign))
    return cur.fetchall()


def run_on_text(conn, text: str, verbose=True):
    """对单条文本运行检测"""
    events = detect_events(text)
    if not events:
        if verbose:
            print("  未检测到商品突发事件")
        return []

    for evt in events:
        if verbose:
            print(f"  {get_signal_text(evt)}")
            stocks = get_affected_stocks(conn, evt["product_id"], evt["direction"])
            if stocks:
                stock_str = ", ".join(f"{s}({float(w):+.1f})" for s, w in stocks)
                print(f"    关联标的: {stock_str}")

    return events


def run_dry_run():
    """模拟接入事件流"""
    test_cases = [
        "突发! 澳洲Greenbushes锂矿发生事故停产, 预计影响全球15%锂精矿供应",
        "发改委印发钢铁行业碳达峰实施方案, 要求2026年底前压减粗钢产能5000万吨",
        "印尼宣布禁止镍矿出口, 即日起暂停发放出口许可证",
        "四川盆地强降雨导致多座水电站停运, 当地电解铝企业限产30%",
        "国储局公告: 将收储30万吨棉花, 稳定市场价格",
        "华东港口因大雾封闭, 大量PTA船货滞留无法通关",
        "内蒙古煤矿发生透水事故, 当地全部煤矿停产整顿",
        "欧盟对中国电动汽车加征反补贴关税46%, 电池产业链需求预期下调",
    ]

    conn = pg_connect()
    print("=" * 70)
    print("商品突发事件检测 — 模拟事件流")
    print("=" * 70)

    for i, text in enumerate(test_cases, 1):
        print(f"\n[{i}] {text}")
        run_on_text(conn, text)

    conn.close()


if __name__ == "__main__":
    import argparse
    p = argparse.ArgumentParser()
    p.add_argument("--test", help="测试单条文本")
    p.add_argument("--dry-run", action="store_true", help="模拟事件流")
    args = p.parse_args()

    conn = pg_connect() if args.test else None

    if args.test:
        print(f"输入: {args.test}")
        run_on_text(pg_connect(), args.test)
    else:
        run_dry_run()

    if conn:
        conn.close()
