"""
金融情感词典 — 批量扩充工具

从 AKShare 历史新闻中提取候选金融词汇，导出 CSV 供人工标注，
然后合并回 financial_sentiment_lexicon.json。

用法:
  # Step 1: 拉取新闻 + 提取候选词 → 导出 CSV
  python tools/build_lexicon_from_history.py fetch --days 90 --output tools/lexicon_candidates.csv

  # Step 2: 人工标注 CSV (在 Excel/WPS 中填写 sentiment_score 列, -1.0 ~ 1.0)

  # Step 3: 合并已标注词 → 更新词典 JSON
  python tools/build_lexicon_from_history.py merge --input tools/lexicon_candidates.csv
"""

import argparse
import csv
import json
import logging
import os
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# ── 配置 ──

LEXICON_PATH = Path(__file__).parent.parent / "astock_engine" / "events" / "financial_sentiment_lexicon.json"

# 过滤规则: 排除这些模式的词 (噪音)
EXCLUDE_PATTERNS = [
    re.compile(r'^\d+$'),                # 纯数字
    re.compile(r'^[a-zA-Z]+$'),           # 纯英文
    re.compile(r'^.{1}$'),               # 单字
    re.compile(r'^[的得了是这在和与或之也而但所其以可于为被把到向从对等]$'),  # 停用词
    re.compile(r'^[年月日时分秒个只次元百千万亿]$'),                       # 量词
    re.compile(r'^[左右前后上下中内外出入]$'),                            # 方位词
]

# 金融领域情感/事件模式 (匹配到的词标注 is_finance=Y)
FINANCE_PATTERNS = [
    re.compile(r'[增减持回购分送转派重组并购停复牌清仓满仓]'),  # 资本运作
    re.compile(r'[盈亏损利增收降超低]$'),                      # 财务结果
    re.compile(r'[涨跌停涨停跌开放量缩量突破破位新高新低跳水拉升]'),  # 行情
    re.compile(r'^.{1,2}[函告示书令案]$'),                     # 监管文函: 问询函/警示函/立案书
    re.compile(r'(立案|处罚|罚款|没收|整改|退市|ST|风险警示)'),     # 处罚风险
    re.compile(r'(业绩|年报|季报|半年报|预告|快报)'),              # 报告
    re.compile(r'(中标|大单|合同|协议|战略合作)'),                 # 重大事项
    re.compile(r'(质押|解押|冻结|轮候)'),                        # 股份状态
    re.compile(r'(分红|派息|送转|高送转)'),                      # 分红
    re.compile(r'(减持|增持|回购|举牌|要约)'),                    # 股东行为
    re.compile(r'(预增|预减|预亏|扭亏|首亏|续亏|续盈|预盈)'),       # 业绩预告
    re.compile(r'(超预期|低于预期|符合预期)'),                     # 预期对比
]


def get_tokenizer():
    """获取分词器: jieba > HanLP > regex"""
    try:
        import jieba
        jieba.setLogLevel(logging.WARNING)
        return lambda text: list(jieba.cut(text))
    except ImportError:
        pass
    try:
        import hanlp
        tok = hanlp.load(hanlp.pretrained.tok.COARSE_ELECTRA_SMALL_ZH)
        return tok
    except ImportError:
        pass
    # 最终降级: 中文字符 n-gram (2-4字)
    def re_tokenize(text: str) -> list:
        words = []
        for n in (4, 3, 2):
            for i in range(len(text) - n + 1):
                chunk = text[i:i + n]
                if re.match(r'^[一-鿿]+$', chunk):
                    words.append(chunk)
        return words
    return re_tokenize


def is_noise(word: str) -> bool:
    """检查是否噪音词"""
    for pat in EXCLUDE_PATTERNS:
        if pat.match(word):
            return True
    return False


def is_finance(word: str) -> bool:
    """检查是否金融相关"""
    for pat in FINANCE_PATTERNS:
        if pat.search(word):
            return True
    return False


# ═══════════════════════════════════════════════════════════════
# Step 1: 拉取新闻 + 提取候选词
# ═══════════════════════════════════════════════════════════════

def fetch_news(days: int = 90) -> List[str]:
    """从 AKShare 拉取近期新闻"""
    texts = []
    try:
        import akshare as ak
        logging.info("拉取东方财富个股新闻...")
        df = ak.stock_news_em()
        if df is not None and not df.empty:
            # 合并标题 + 内容
            for _, row in df.iterrows():
                title = str(row.get('新闻标题', row.get('标题', '')))
                content = str(row.get('新闻内容', row.get('内容', '')))
                if title:
                    texts.append(title)
                if content and content != 'nan':
                    texts.append(content)
            logging.info("  获取 %d 条文本", len(texts))
    except Exception as e:
        logging.error("东方财富新闻失败: %s", e)

    try:
        import akshare as ak
        logging.info("拉取巨潮资讯公告...")
        df = ak.stock_notice_report()
        if df is not None and not df.empty:
            titles = df['公告标题'].dropna().tolist()
            texts.extend(titles)
            logging.info("  获取 %d 条公告标题", len(titles))
    except Exception as e:
        logging.error("巨潮公告失败: %s", e)

    return texts


def load_existing_lexicon() -> set:
    """加载已有词典的所有词和词组"""
    known = set()
    if LEXICON_PATH.exists():
        with open(LEXICON_PATH, 'r', encoding='utf-8') as f:
            data = json.load(f)
        for k in data.get('words', {}):
            known.add(k)
        for k in data.get('phrases', {}):
            known.add(k)
    return known


def extract_candidates(texts: List[str], known_words: set,
                       top_n: int = 1500) -> List[Tuple[str, int, bool]]:
    """从文本中提取候选词

    Returns:
        [(word, freq, is_finance), ...] 按频率降序
    """
    tokenize = get_tokenizer()
    counter = Counter()
    total = len(texts)

    for i, text in enumerate(texts):
        if i % 500 == 0:
            logging.info("  分词进度: %d/%d", i, total)
        if not text or not isinstance(text, str):
            continue
        try:
            tokens = tokenize(str(text))
            for t in tokens:
                t = t.strip()
                if len(t) >= 2 and not is_noise(t) and t not in known_words:
                    counter[t] += 1
        except Exception:
            continue

    # 取 top_n, 标注是否金融相关
    results = []
    for word, freq in counter.most_common(top_n):
        results.append((word, freq, is_finance(word)))
    return results


def export_candidates(candidates: List[Tuple[str, int, bool]],
                      output_path: str):
    """导出候选词 CSV"""
    with open(output_path, 'w', newline='', encoding='utf-8-sig') as f:
        writer = csv.writer(f)
        writer.writerow(['word', 'frequency', 'is_finance',
                         'sentiment_score', 'is_phrase', 'notes'])
        for word, freq, fin in candidates:
            writer.writerow([word, freq, 'Y' if fin else 'N',
                             '', '', ''])

    logging.info("导出 %d 个候选词 → %s", len(candidates), output_path)
    print(f"\n下一步: 在 Excel/WPS 中打开 {output_path}")
    print("  1. 填写 sentiment_score 列 (-1.0=强负面 ~ 1.0=强正面)")
    print("  2. 是词组而非单词的, 填 is_phrase=Y")
    print("  3. 保存后运行: python tools/build_lexicon_from_history.py merge")


# ═══════════════════════════════════════════════════════════════
# Step 2: 合并已标注词 → 词典 JSON
# ═══════════════════════════════════════════════════════════════

def merge_annotated(input_path: str, output_path: Optional[str] = None):
    """将人工标注的 CSV 合并回 lexicon JSON"""
    if output_path is None:
        output_path = str(LEXICON_PATH)

    # 加载现有词典
    if LEXICON_PATH.exists():
        with open(LEXICON_PATH, 'r', encoding='utf-8') as f:
            data = json.load(f)
    else:
        data = {"meta": {}, "words": {}, "phrases": {}}

    words = data.setdefault('words', {})
    phrases = data.setdefault('phrases', {})
    added_words = 0
    added_phrases = 0
    skipped = 0

    with open(input_path, 'r', encoding='utf-8-sig') as f:
        reader = csv.DictReader(f)
        for row in reader:
            word = row.get('word', '').strip()
            score_str = row.get('sentiment_score', '').strip()
            is_phrase = row.get('is_phrase', '').strip().upper()

            if not word or not score_str:
                skipped += 1
                continue

            try:
                score = float(score_str)
            except ValueError:
                skipped += 1
                continue

            # Clamp to [-1, 1]
            score = max(-1.0, min(1.0, score))

            if is_phrase == 'Y':
                phrases[word] = score
                added_phrases += 1
            else:
                words[word] = score
                added_words += 1

    # 排序 (保证词典可读性)
    data['words'] = dict(sorted(words.items(), key=lambda x: -abs(x[1])))
    data['phrases'] = dict(sorted(phrases.items(), key=lambda x: -abs(x[1])))
    data['meta']['total_words'] = len(data['words'])
    data['meta']['total_phrases'] = len(data['phrases'])
    data['meta']['updated'] = 'auto-merged from lexicon builder'

    with open(output_path, 'w', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=2)

    print(f"合并完成:")
    print(f"  新增单词: {added_words} (总计 {len(data['words'])})")
    print(f"  新增词组: {added_phrases} (总计 {len(data['phrases'])})")
    print(f"  跳过: {skipped} (未标注或格式错误)")
    print(f"  输出: {output_path}")


# ═══════════════════════════════════════════════════════════════
# CLI
# ═══════════════════════════════════════════════════════════════

def main():
    logging.basicConfig(level=logging.INFO, format='%(levelname)s: %(message)s')

    parser = argparse.ArgumentParser(
        description='金融情感词典批量扩充工具')
    sub = parser.add_subparsers(dest='command', required=True)

    # fetch
    p_fetch = sub.add_parser('fetch', help='拉取新闻 + 提取候选词 → CSV')
    p_fetch.add_argument('--days', type=int, default=90,
                         help='拉取天数 (默认90)')
    p_fetch.add_argument('--top', type=int, default=1500,
                         help='候选词数量 (默认1500)')
    p_fetch.add_argument('--output', default='tools/lexicon_candidates.csv',
                         help='输出 CSV 路径')

    # merge
    p_merge = sub.add_parser('merge', help='合并已标注 CSV → 词典 JSON')
    p_merge.add_argument('--input', default='tools/lexicon_candidates.csv',
                         help='已标注 CSV 路径')
    p_merge.add_argument('--output', default=None,
                         help='输出 JSON (默认覆盖原词典)')

    args = parser.parse_args()

    if args.command == 'fetch':
        known = load_existing_lexicon()
        print(f"已有词典: {len(known)} 词")
        texts = fetch_news(args.days)
        print(f"拉取新闻: {len(texts)} 条")
        candidates = extract_candidates(texts, known, args.top)
        print(f"候选词: {len(candidates)} 个")
        export_candidates(candidates, args.output)

    elif args.command == 'merge':
        merge_annotated(args.input, args.output)


if __name__ == '__main__':
    main()
