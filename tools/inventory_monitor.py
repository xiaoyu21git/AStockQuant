#!/usr/bin/env python3
"""
库存信号监控框架 — 每日自动筛查 + 传导链映射
===============================================
1. 检测库存异常变化 (>1σ / >2σ)
2. 识别结构性趋势 (连续3周同向)
3. 输出: 商品信号 + 受益/受损A股标的

用法:
  python inventory_monitor.py              # 今日筛查
  python inventory_monitor.py --brief      # 仅异常信号
"""

import sys, io, os
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from db_config import pg_connect
import logging
logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("inv_monitor")


def screen_signals(conn):
    """筛查所有品种的库存信号"""
    cur = conn.cursor()
    cur.execute("""
    WITH latest AS (
        SELECT DISTINCT ON (product_id) product_id, trade_date,
               change_wow, change_mom, inventory
        FROM mkt.commodity_inventory WHERE change_wow IS NOT NULL
        ORDER BY product_id, trade_date DESC
    ),
    hist AS (
        SELECT product_id,
               COALESCE(STDDEV(change_wow),0.03) as vol,
               AVG(change_wow) as avg_chg,
               COUNT(*) as n
        FROM mkt.commodity_inventory WHERE change_wow IS NOT NULL
        GROUP BY product_id HAVING COUNT(*) >= 5
    ),
    trend AS (
        SELECT product_id,
               MAX(CASE WHEN r=1 THEN change_wow END) as w1,
               MAX(CASE WHEN r=2 THEN change_wow END) as w2,
               MAX(CASE WHEN r=3 THEN change_wow END) as w3,
               MAX(CASE WHEN r=4 THEN change_wow END) as w4
        FROM (SELECT product_id, change_wow,
              ROW_NUMBER() OVER (PARTITION BY product_id ORDER BY trade_date DESC) r
              FROM mkt.commodity_inventory WHERE change_wow IS NOT NULL) t
        WHERE r<=4 GROUP BY product_id
    )
    SELECT l.product_id, l.trade_date,
           ROUND(l.change_wow::numeric,3) as wow,
           ROUND(l.change_mom::numeric,3) as mom,
           l.inventory,
           ROUND(h.vol::numeric,3) as vol,
           h.n as history_days,
           ROUND(t.w1::numeric,3), ROUND(t.w2::numeric,3),
           ROUND(t.w3::numeric,3), ROUND(t.w4::numeric,3)
    FROM latest l JOIN hist h USING(product_id) JOIN trend t USING(product_id)
    ORDER BY l.change_wow
    """)
    return cur.fetchall()


def classify(row):
    """分类信号强度"""
    product_id, trade_date, wow, mom, inv, vol, n, t1, t2, t3, t4 = row
    wow = float(wow); mom = float(mom); vol = float(vol or 0.03)
    t1 = float(t1 or 0); t2 = float(t2 or 0); t3 = float(t3 or 0); t4 = float(t4 or 0)

    # 方向
    direction = "收紧" if wow < 0 else "过剩"

    # 强度: 相对历史波动率
    if abs(wow) > 3 * vol:
        strength = "🔴极强"
    elif abs(wow) > 2 * vol:
        strength = "🟠强"
    elif abs(wow) > vol:
        strength = "🟡中"
    else:
        strength = "⚪弱"

    # 趋势: 连续同向
    weeks = [t1, t2, t3, t4]
    trend_count = 0
    for i in range(len(weeks)-1):
        if (weeks[i] < -0.01 and weeks[i+1] < -0.01) or (weeks[i] > 0.01 and weeks[i+1] > 0.01):
            trend_count += 1
    if trend_count >= 3:
        trend = "结构性"
    elif trend_count >= 1:
        trend = "持续性"
    else:
        trend = "脉冲性"

    return direction, strength, trend


def get_affected_stocks(conn, product_id: str, direction: str):
    """获取受益/受损A股 (上游受益于涨价=库存收紧利好)"""
    cur = conn.cursor()
    # 库存收紧→涨价→上游受益(+), 下游受损(-)
    # 库存过剩→跌价→上游受损(-), 下游受益(+)
    benefit_sign = 1 if direction == "收紧" else -1

    cur.execute("""
        SELECT symbol, weight FROM ref.product_stock_mapping
        WHERE product_id=%s AND version IN ('llm_v1','llm_v2_pricing','manual_fix_v2','manual_fix_v3')
        ORDER BY ABS(weight)*SIGN(weight)*%s DESC LIMIT 5
    """, (product_id, benefit_sign))
    return cur.fetchall()


def run(brief=False):
    conn = pg_connect()

    print("=" * 75)
    print("库存信号监控 — 传导链筛查")
    print("=" * 75)

    rows = screen_signals(conn)
    signals = []

    for row in rows:
        direction, strength, trend = classify(row)
        product_id, trade_date, wow, mom = row[0], row[1], float(row[2]), float(row[3])

        # brief模式: 只显示非弱信号
        if brief and strength == "⚪弱":
            continue

        signals.append((product_id, direction, strength, trend, wow, mom, trade_date))

    # 按信号强度排序
    signals.sort(key=lambda x: abs(x[4]), reverse=True)

    # 分组输出
    for direction_label in ["收紧", "过剩"]:
        group = [s for s in signals if s[1] == direction_label]
        if not group:
            continue

        emoji = "🟢" if direction_label == "收紧" else "🔴"
        print(f"\n{'─'*75}")
        print(f"{emoji} 库存{direction_label}")
        print(f"{'─'*75}")

        for pid, d, strength, trend, wow, mom, dt in group:
            stocks = get_affected_stocks(conn, pid, d)
            stock_str = ", ".join(f"{s}({float(w):+.1f})" for s, w in stocks[:4]) if stocks else "无映射"

            print(f"\n  {strength} {trend} | {pid} | {str(dt)}")
            print(f"  周环比: {wow:+.1%}  月环比: {mom:+.1%}")
            if d == "收紧":
                print(f"  受益标的: {stock_str}")
            else:
                print(f"  受损/做空标的: {stock_str}")

    # 汇总
    structural = [s for s in signals if s[3] == "结构性"]
    print(f"\n{'='*75}")
    print(f"汇总: {len(signals)}品种检出信号, {len(structural)}结构性趋势")
    conn.close()


if __name__ == "__main__":
    import argparse
    p = argparse.ArgumentParser()
    p.add_argument("--brief", action="store_true", help="仅异常信号")
    args = p.parse_args()
    run(brief=args.brief)
