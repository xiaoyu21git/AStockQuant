#!/usr/bin/env python3
"""
CrudeOilNews → 传导链事件分类训练数据导出
=============================================
Gold-standard 2,943事件 → 映射到6类商品事件 → JSONL训练集
"""

import json, os, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
from collections import Counter

CORPUS_DIR = "CrudeOilNews-Corpus/annotations/Gold-standard"
OUTPUT_FILE = "tools/commodity_event_train.jsonl"

EVENT_MAP = {
    "Shortage":                  ("supply_disruption", "供给短缺"),
    "Movement-down-loss":        ("supply_disruption", "产量下降"),
    "Movement-up-gain":          ("supply_disruption", "产量上升"),
    "Cause-movement-down-loss":  ("supply_disruption", "事件导致减产"),
    "Cause-movement-up-gain":    ("supply_disruption", "事件导致增产"),
    "Civil-unrest":              ("supply_disruption", "地缘动荡"),
    "Geopolitical-tension":      ("supply_disruption", "地缘紧张"),
    "Crisis":                    ("supply_disruption", "危机事件"),
    "Embargo":                   ("trade_restriction", "制裁/禁运"),
    "Prohibiting":               ("trade_restriction", "贸易禁令"),
    "Trade-tensions":            ("trade_restriction", "贸易摩擦"),
    "trade-financial-tension":   ("trade_restriction", "贸易金融摩擦"),
    "Oversupply":                ("inventory_build", "供给过剩"),
}

# 商品关键词
COMMODITY_KW = [
    "crude","oil","gas","petroleum","LNG","OPEC","barrel","brent","WTI",
    "copper","aluminum","zinc","lead","nickel","tin","gold","silver",
    "iron ore","steel","rebar","coal","coke",
    "soybean","corn","wheat","cotton","sugar","coffee","cocoa",
    "lithium","cobalt","rare earth",
]


def export():
    count = 0
    type_count = Counter()
    with open(OUTPUT_FILE, "w", encoding="utf-8") as out:
        for fname in sorted(os.listdir(CORPUS_DIR)):
            if not fname.endswith(".json"):
                continue
            with open(os.path.join(CORPUS_DIR, fname)) as f:
                data = json.load(f)

            for sent in data:
                events = sent.get("golden-event-mentions", [])
                text = sent.get("sentence", "")
                entities = sent.get("golden-entity-mentions", [])

                for evt in events:
                    etype = evt.get("event_type", "")
                    mapped = EVENT_MAP.get(etype)
                    if not mapped:
                        continue

                    trigger = evt.get("trigger", {}).get("text", "")
                    polarity = evt.get("polarity", "Neutral")
                    intensity = evt.get("intensity", "Neutral")

                    # 找关联的实体
                    args = evt.get("arguments", [])
                    related_entities = []
                    for arg in args:
                        related_entities.append({
                            "text": arg.get("text", ""),
                            "role": arg.get("role", ""),
                            "entity_type": arg.get("entity-type", ""),
                        })

                    # 构建训练样本
                    sample = {
                        "text": text,
                        "label": mapped[0],
                        "label_zh": mapped[1],
                        "original_type": etype,
                        "trigger": trigger,
                        "polarity": polarity,
                        "intensity": intensity,
                        "entities": related_entities,
                        "source_file": fname,
                    }
                    out.write(json.dumps(sample, ensure_ascii=False) + "\n")
                    count += 1
                    type_count[mapped[0]] += 1

    print(f"导出: {count} 条训练样本")
    print(f"输出: {OUTPUT_FILE}")
    print(f"\n类别分布:")
    for t, c in type_count.most_common():
        print(f"  {t:25s} {c:4d}")


if __name__ == "__main__":
    export()
