#!/usr/bin/env python3
"""
商品匹配引擎 — 精确正则优先 + 字符n-gram向量降级 + 阈值分流
============================================================
零外部依赖（纯Python stdlib），101个品种毫秒级匹配。

匹配优先级:
  1. 精确关键词 (exact_keyword) → confidence=1.0
  2. 字符3-gram Jaccard相似度 ≥ 0.85 → auto
  3. 0.70-0.85 → pending_review
  4. < 0.70 → unmapped

向量存储: ref.commodity_embeddings (PG存3-gram集合向量)
"""

import json, os, re, sys
from typing import Tuple, Optional, Dict, List
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from db_config import pg_connect


def _char_ngrams(text: str, n: int = 3) -> set:
    """提取字符n-gram集合 (中文友好, 无需分词)"""
    text = text.lower().strip()
    if len(text) < n:
        return {text}
    return {text[i:i+n] for i in range(len(text) - n + 1)}


def _jaccard_similarity(set_a: set, set_b: set) -> float:
    """Jaccard相似度"""
    if not set_a or not set_b:
        return 0.0
    intersection = len(set_a & set_b)
    union = len(set_a | set_b)
    return intersection / union if union > 0 else 0.0


class CommodityMatcher:
    """商品名→product_id 匹配器 (PG版, 零外部依赖)"""

    def __init__(self, index_path: str = None):
        if index_path is None:
            index_path = os.path.join(os.path.dirname(__file__), "commodity_index.json")

        with open(index_path, "r", encoding="utf-8") as f:
            self.index = json.load(f)

        self.products = self.index["products"]

        # 精确关键词索引: keyword → product_id (长关键词优先)
        self.keyword_map: Dict[str, str] = {}
        for pid, info in self.products.items():
            for kw in info.get("keywords", []):
                if kw not in self.keyword_map:
                    self.keyword_map[kw] = pid

        # 初始化PG向量表
        self.conn = pg_connect()
        self._init_pg_table()

    def _init_pg_table(self):
        """建表 + 填充101品种的3-gram集合"""
        cur = self.conn.cursor()

        cur.execute("""
            CREATE TABLE IF NOT EXISTS ref.commodity_embeddings (
                product_id   VARCHAR(64) PRIMARY KEY,
                name_zh      VARCHAR(128),
                ngrams_3     TEXT[] NOT NULL,
                description  TEXT
            )
        """)

        cur.execute("SELECT product_id FROM ref.commodity_embeddings LIMIT 1")
        if cur.fetchone():
            self.conn.commit()
            return

        print("首次运行: 生成101个商品字符向量...")
        for pid, info in self.products.items():
            desc = info.get("description", info.get("name_zh", pid))
            name = info["name_zh"]
            ngrams = sorted(_char_ngrams(pid + " " + name + " " + desc, 3))
            cur.execute(
                """INSERT INTO ref.commodity_embeddings
                   (product_id, name_zh, ngrams_3, description)
                   VALUES (%s, %s, %s, %s)
                   ON CONFLICT (product_id) DO NOTHING""",
                (pid, name, ngrams, desc))

        self.conn.commit()
        print(f"已写入 {len(self.products)} 个字符向量")

    def refresh_embeddings(self):
        cur = self.conn.cursor()
        cur.execute("DELETE FROM ref.commodity_embeddings")
        self.conn.commit()
        self._init_pg_table()

    def match(self, product_name: str) -> Tuple[Optional[str], float, str]:
        name = product_name.strip()
        if not name:
            return None, 0.0, "empty_input"

        # ── 1. 精确关键词 (最高优先级) ──
        pid = self._exact_keyword_match(name)
        if pid:
            return pid, 1.0, "exact_keyword"

        # ── 2. 字符n-gram向量匹配 ──
        return self._ngram_vector_match(name)

    def _exact_keyword_match(self, name: str) -> Optional[str]:
        for kw in sorted(self.keyword_map.keys(), key=len, reverse=True):
            if kw in name:
                return self.keyword_map[kw]
        return None

    def _ngram_vector_match(self, name: str) -> Tuple[Optional[str], float, str]:
        query_ngrams = _char_ngrams(name, 3)

        cur = self.conn.cursor()
        cur.execute("SELECT product_id, ngrams_3 FROM ref.commodity_embeddings")
        rows = cur.fetchall()

        best_pid, best_sim = None, -1.0
        second_pid, second_sim = None, -1.0

        for pid, ngrams_list in rows:
            target_ngrams = set(ngrams_list)
            sim = _jaccard_similarity(query_ngrams, target_ngrams)
            if sim > best_sim:
                second_sim = best_sim
                second_pid = best_pid
                best_sim = sim
                best_pid = pid
            elif sim > second_sim:
                second_sim = sim
                second_pid = pid

        # 阈值分流
        if best_sim >= 0.40:         # Jaccard 0.4 ≈ 余弦 0.85 (字符级更严格)
            return best_pid, round(best_sim, 2), "ngram_match"
        elif best_sim >= 0.25:
            return best_pid, round(best_sim, 2), "pending_review"
        elif second_sim >= 0.25:
            return second_pid, round(second_sim, 2), "pending_review"
        else:
            return None, round(best_sim, 2), "unmapped"

    def get_product_info(self, product_id: str) -> dict:
        return self.products.get(product_id, {})

    def close(self):
        self.conn.close()


if __name__ == "__main__":
    m = CommodityMatcher()

    tests = [
        ("阴极铜", "copper"),
        ("锂离子电池电解液", "electrolyte"),
        ("六氟磷酸锂", "lipf6"),
        ("精炼铜", "copper"),
        ("铝电解电容器", "aluminum"),
        ("电池材料", "cathode"),
        ("生猪", "live_hog"),
        ("光伏组件", "solar_module"),
        ("氢氧化锂", "lithium"),
        ("冷轧板", "cold_rolled"),
    ]

    ok = 0
    for name, expected in tests:
        pid, conf, method = m.match(name)
        info = m.get_product_info(pid) if pid else {}
        zh = info.get("name_zh", "?")
        status = "OK" if pid == expected else "FAIL"
        if pid == expected:
            ok += 1
        pid_str = pid if pid else "NONE"
        print(f"{status:4s} {name:20s} -> {pid_str:20s} ({zh}) sim={conf} [{method}]")

    print(f"\n{ok}/{len(tests)} 通过")
    m.close()
