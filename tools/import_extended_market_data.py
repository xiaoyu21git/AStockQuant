from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
import os
import sys
from typing import Iterable, Sequence

import pandas as pd
import pymysql


PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)

from astock_engine.data.providers.base_provider import DataQuery, DataType
from astock_engine.data.providers.futures_provider import FuturesDataProvider
from astock_engine.data.providers.news_provider import NewsDataProvider


MYSQL_CONFIG = {
    "host": os.getenv("DB_HOST", "127.0.0.1"),
    "port": int(os.getenv("DB_PORT", "3306")),
    "user": os.getenv("DB_USER", "root"),
    "password": os.getenv("DB_PASSWORD", "123456a"),
    "database": os.getenv("DB_NAME", "astock_quant"),
    "charset": "utf8mb4",
    "cursorclass": pymysql.cursors.DictCursor,
    "autocommit": False,
}

POSITIVE_WORDS = ("利好", "增长", "上调", "突破", "支持", "回购", "增持", "修复", "高景气", "改善")
NEGATIVE_WORDS = ("利空", "下滑", "处罚", "减持", "亏损", "下调", "风险", "违约", "波动", "收紧")
POLICY_KEYWORDS = {
    "monetary": ("央行", "降准", "降息", "货币", "流动性"),
    "regulation": ("证监会", "监管", "规范", "处罚", "问询"),
    "industry": ("工信部", "能源局", "部委", "专项", "规划"),
    "fiscal": ("财政", "税收", "补贴", "专项债", "国债"),
}

NEWS_TABLE_SQL = """
CREATE TABLE IF NOT EXISTS news_sentiment (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    symbol VARCHAR(20) NOT NULL DEFAULT 'MARKET',
    trade_date DATE NOT NULL,
    publish_time DATETIME NOT NULL,
    title VARCHAR(500) DEFAULT NULL,
    content MEDIUMTEXT DEFAULT NULL,
    source VARCHAR(100) DEFAULT NULL,
    url VARCHAR(500) DEFAULT NULL,
    sentiment_score DECIMAL(12, 6) DEFAULT NULL,
    market_sentiment DECIMAL(12, 6) DEFAULT NULL,
    investor_sentiment DECIMAL(12, 6) DEFAULT NULL,
    sector_sentiment DECIMAL(12, 6) DEFAULT NULL,
    theme_sentiment DECIMAL(12, 6) DEFAULT NULL,
    social_sentiment DECIMAL(12, 6) DEFAULT NULL,
    news_count INT UNSIGNED NOT NULL DEFAULT 1,
    title_hash CHAR(32) NOT NULL,
    data_source VARCHAR(100) DEFAULT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_news_symbol_time_hash (symbol, publish_time, title_hash),
    KEY idx_news_trade_symbol (trade_date, symbol),
    KEY idx_news_publish_time (publish_time)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci
"""

POLICY_TABLE_SQL = """
CREATE TABLE IF NOT EXISTS policy_data (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    symbol VARCHAR(20) NOT NULL DEFAULT 'MARKET',
    trade_date DATE NOT NULL,
    publish_time DATETIME NOT NULL,
    title VARCHAR(500) DEFAULT NULL,
    content MEDIUMTEXT DEFAULT NULL,
    policy_type VARCHAR(100) DEFAULT NULL,
    policy_score DECIMAL(12, 6) DEFAULT NULL,
    policy_strength DECIMAL(12, 6) DEFAULT NULL,
    policy_count INT UNSIGNED NOT NULL DEFAULT 1,
    doc_hash CHAR(32) NOT NULL,
    data_source VARCHAR(100) DEFAULT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_policy_symbol_time_hash (symbol, publish_time, doc_hash),
    KEY idx_policy_trade_symbol (trade_date, symbol),
    KEY idx_policy_publish_time (publish_time)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci
"""

ALTERNATIVE_TABLE_SQL = """
CREATE TABLE IF NOT EXISTS alternative_data (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    symbol VARCHAR(20) NOT NULL,
    trade_date DATE NOT NULL,
    metric_type VARCHAR(50) NOT NULL DEFAULT 'HOT_RANK',
    hot_rank INT DEFAULT NULL,
    popularity_score DECIMAL(12, 6) DEFAULT NULL,
    comment_count INT UNSIGNED DEFAULT NULL,
    comment_sentiment DECIMAL(12, 6) DEFAULT NULL,
    extra_payload_json LONGTEXT DEFAULT NULL,
    data_source VARCHAR(100) DEFAULT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_alternative_symbol_date_metric (symbol, trade_date, metric_type),
    KEY idx_alternative_trade_symbol (trade_date, symbol)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci
"""

DERIVATIVES_TABLE_SQL = """
CREATE TABLE IF NOT EXISTS derivatives_data (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    symbol VARCHAR(20) NOT NULL DEFAULT 'MARKET',
    underlying_symbol VARCHAR(20) DEFAULT NULL,
    contract_code VARCHAR(40) NOT NULL,
    trade_date DATE NOT NULL,
    futures_open DECIMAL(16, 6) DEFAULT NULL,
    futures_high DECIMAL(16, 6) DEFAULT NULL,
    futures_low DECIMAL(16, 6) DEFAULT NULL,
    futures_close DECIMAL(16, 6) DEFAULT NULL,
    futures_volume DECIMAL(20, 4) DEFAULT NULL,
    open_interest DECIMAL(20, 4) DEFAULT NULL,
    basis DECIMAL(16, 6) DEFAULT NULL,
    basis_rate DECIMAL(16, 6) DEFAULT NULL,
    data_source VARCHAR(100) DEFAULT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_derivatives_symbol_date_contract (symbol, trade_date, contract_code),
    KEY idx_derivatives_trade_symbol (trade_date, symbol),
    KEY idx_derivatives_underlying (underlying_symbol, trade_date)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci
"""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="导入新闻/政策/另类/衍生品扩展数据")
    parser.add_argument("--data-type", choices=["all", "news", "policy", "alternative", "derivatives"], default="all")
    parser.add_argument("--start-date", default=(dt.date.today() - dt.timedelta(days=90)).isoformat())
    parser.add_argument("--end-date", default=dt.date.today().isoformat())
    parser.add_argument("--symbols", default="", help="逗号分隔的股票代码")
    parser.add_argument("--market", default="")
    parser.add_argument("--provider", default="")
    parser.add_argument("--hot-limit", type=int, default=200)
    return parser.parse_args()


def parse_date(text: str) -> dt.date:
    return dt.date.fromisoformat(text[:10])


def parse_datetime_value(value) -> dt.datetime | None:
    if value is None or value == "":
        return None
    if isinstance(value, dt.datetime):
        return value
    if isinstance(value, dt.date):
        return dt.datetime.combine(value, dt.time())
    text = str(value).strip()
    if not text:
        return None
    for fmt in (
        "%Y-%m-%d %H:%M:%S",
        "%Y/%m/%d %H:%M:%S",
        "%Y-%m-%dT%H:%M:%S",
        "%Y-%m-%d",
        "%Y/%m/%d",
        "%Y%m%d",
    ):
        try:
            return dt.datetime.strptime(text[:19], fmt)
        except ValueError:
            continue
    try:
        return dt.datetime.fromisoformat(text.replace("Z", "+00:00")).replace(tzinfo=None)
    except ValueError:
        return None


def normalize_symbol(raw_value) -> str:
    if raw_value is None:
        return ""
    text = str(raw_value).strip().upper()
    if not text:
        return ""
    if text in {"MARKET", "ALL_MARKET", "GLOBAL"}:
        return "MARKET"
    if "." in text:
        return text
    digits = "".join(ch for ch in text if ch.isdigit())
    if len(digits) == 6:
        if digits.startswith(("6", "9")):
            return digits + ".SH"
        if digits.startswith(("0", "3")):
            return digits + ".SZ"
        if digits.startswith(("4", "8")):
            return digits + ".BJ"
    return text


def split_symbols(raw_symbols: str) -> list[str]:
    symbols: list[str] = []
    for item in raw_symbols.split(","):
        symbol = normalize_symbol(item)
        if symbol:
            symbols.append(symbol)
    return sorted(set(symbols))


def safe_float(value) -> float | None:
    if value is None or value == "":
        return None
    try:
        numeric = float(value)
    except Exception:
        return None
    if not math.isfinite(numeric):
        return None
    return numeric


def safe_int(value) -> int | None:
    numeric = safe_float(value)
    if numeric is None:
        return None
    return int(round(numeric))


def first_value(row, candidates: Sequence[str]):
    for candidate in candidates:
        if candidate in row and row[candidate] not in (None, ""):
            return row[candidate]
    return None


def text_value(row, candidates: Sequence[str]) -> str:
    value = first_value(row, candidates)
    return "" if value is None else str(value).strip()


def hash_text(*parts: str) -> str:
    joined = "||".join(part.strip() for part in parts if part and part.strip())
    return hashlib.md5(joined.encode("utf-8")).hexdigest()


def sentiment_score_from_text(text: str) -> float:
    normalized = text.strip()
    if not normalized:
        return 0.0
    positive = sum(normalized.count(word) for word in POSITIVE_WORDS)
    negative = sum(normalized.count(word) for word in NEGATIVE_WORDS)
    return float(positive - negative)


def infer_policy_type(text: str) -> str:
    for policy_type, keywords in POLICY_KEYWORDS.items():
        if any(keyword in text for keyword in keywords):
            return policy_type
    return "general"


def is_policy_text(text: str) -> bool:
    return any(keyword in text for keywords in POLICY_KEYWORDS.values() for keyword in keywords)


def build_query(symbols: Sequence[str], start_date: dt.date, end_date: dt.date) -> DataQuery:
    return DataQuery(symbols=list(symbols), start_date=start_date, end_date=end_date, limit=5000)


def get_connection():
    return pymysql.connect(**MYSQL_CONFIG)


def ensure_tables(cursor) -> None:
    cursor.execute(NEWS_TABLE_SQL)
    cursor.execute(POLICY_TABLE_SQL)
    cursor.execute(ALTERNATIVE_TABLE_SQL)
    cursor.execute(DERIVATIVES_TABLE_SQL)


def upsert_many(cursor, sql: str, rows: Iterable[dict]) -> int:
    payload = list(rows)
    if not payload:
        return 0
    cursor.executemany(sql, payload)
    return len(payload)


def normalize_news_rows(df: pd.DataFrame, symbols: set[str]) -> list[dict]:
    rows: list[dict] = []
    if df is None or df.empty:
        return rows
    for _, raw in df.fillna("").iterrows():
        row = raw.to_dict()
        publish_time = parse_datetime_value(first_value(row, ["publish_time", "发布时间", "announce_date", "公告日期", "created_at", "date"]))
        if publish_time is None:
            continue
        symbol = normalize_symbol(first_value(row, ["symbol", "代码", "stock_code", "security_code", "ticker"])) or "MARKET"
        if symbols and symbol not in symbols and symbol != "MARKET":
            continue
        title = text_value(row, ["title", "标题", "公告标题"])
        content = text_value(row, ["content", "内容", "公告内容", "摘要"])
        source = text_value(row, ["source", "文章来源", "announcement_type", "公告类型"])
        url = text_value(row, ["url", "网址", "链接"])
        score = sentiment_score_from_text(title + " " + content)
        rows.append({
            "symbol": symbol,
            "trade_date": publish_time.date(),
            "publish_time": publish_time,
            "title": title[:500] if title else None,
            "content": content or None,
            "source": source or None,
            "url": url[:500] if url else None,
            "sentiment_score": score,
            "market_sentiment": score,
            "investor_sentiment": score,
            "sector_sentiment": score,
            "theme_sentiment": score,
            "social_sentiment": score,
            "news_count": 1,
            "title_hash": hash_text(symbol, publish_time.isoformat(), title, url),
            "data_source": "AKSHARE_NEWS",
        })
    return rows


def import_news(cursor, start_date: dt.date, end_date: dt.date, symbols: list[str]) -> int:
    provider = NewsDataProvider()
    provider.initialize()
    query = build_query(symbols, start_date, end_date)

    rows: list[dict] = []
    news_response = provider.get_data(query, DataType.NEWS)
    if news_response.success:
        rows.extend(normalize_news_rows(news_response.data, set(symbols)))

    if symbols:
        announcement_response = provider.get_data(query, DataType.ANNOUNCEMENT)
        if announcement_response.success:
            rows.extend(normalize_news_rows(announcement_response.data, set(symbols)))

    sql = """
    INSERT INTO news_sentiment (
        symbol, trade_date, publish_time, title, content, source, url,
        sentiment_score, market_sentiment, investor_sentiment, sector_sentiment,
        theme_sentiment, social_sentiment, news_count, title_hash, data_source
    ) VALUES (
        %(symbol)s, %(trade_date)s, %(publish_time)s, %(title)s, %(content)s, %(source)s, %(url)s,
        %(sentiment_score)s, %(market_sentiment)s, %(investor_sentiment)s, %(sector_sentiment)s,
        %(theme_sentiment)s, %(social_sentiment)s, %(news_count)s, %(title_hash)s, %(data_source)s
    ) ON DUPLICATE KEY UPDATE
        content = VALUES(content),
        source = VALUES(source),
        url = VALUES(url),
        sentiment_score = VALUES(sentiment_score),
        market_sentiment = VALUES(market_sentiment),
        investor_sentiment = VALUES(investor_sentiment),
        sector_sentiment = VALUES(sector_sentiment),
        theme_sentiment = VALUES(theme_sentiment),
        social_sentiment = VALUES(social_sentiment),
        news_count = VALUES(news_count),
        data_source = VALUES(data_source)
    """
    return upsert_many(cursor, sql, rows)


def import_policy(cursor, start_date: dt.date, end_date: dt.date, symbols: list[str]) -> int:
    provider = NewsDataProvider()
    provider.initialize()
    query = build_query(symbols, start_date, end_date)
    rows: list[dict] = []

    if symbols:
        announcement_response = provider.get_data(query, DataType.ANNOUNCEMENT)
        df = announcement_response.data if announcement_response.success else pd.DataFrame()
        for _, raw in df.fillna("").iterrows():
            row = raw.to_dict()
            publish_time = parse_datetime_value(first_value(row, ["announce_date", "公告日期", "publish_time"]))
            if publish_time is None:
                continue
            symbol = normalize_symbol(first_value(row, ["symbol", "代码", "stock_code"])) or "MARKET"
            title = text_value(row, ["title", "公告标题"])
            content = text_value(row, ["content", "公告内容"])
            policy_type = text_value(row, ["announcement_type", "公告类型"]) or "announcement"
            score = sentiment_score_from_text(title + " " + content)
            rows.append({
                "symbol": symbol,
                "trade_date": publish_time.date(),
                "publish_time": publish_time,
                "title": title[:500] if title else None,
                "content": content or None,
                "policy_type": policy_type,
                "policy_score": score,
                "policy_strength": abs(score),
                "policy_count": 1,
                "doc_hash": hash_text(symbol, publish_time.isoformat(), title, policy_type),
                "data_source": "AKSHARE_ANNOUNCEMENT",
            })
    else:
        news_response = provider.get_data(query, DataType.NEWS)
        df = news_response.data if news_response.success else pd.DataFrame()
        for _, raw in df.fillna("").iterrows():
            row = raw.to_dict()
            publish_time = parse_datetime_value(first_value(row, ["publish_time", "发布时间", "created_at", "date"]))
            if publish_time is None:
                continue
            title = text_value(row, ["title", "标题"])
            content = text_value(row, ["content", "内容"])
            combined = (title + " " + content).strip()
            if not is_policy_text(combined):
                continue
            score = sentiment_score_from_text(combined)
            rows.append({
                "symbol": "MARKET",
                "trade_date": publish_time.date(),
                "publish_time": publish_time,
                "title": title[:500] if title else None,
                "content": content or None,
                "policy_type": infer_policy_type(combined),
                "policy_score": score,
                "policy_strength": abs(score),
                "policy_count": 1,
                "doc_hash": hash_text("MARKET", publish_time.isoformat(), title, combined[:200]),
                "data_source": "AKSHARE_POLICY_NEWS",
            })

    if not rows:
        current = start_date
        while current <= end_date:
            rows.append({
                "symbol": "MARKET",
                "trade_date": current,
                "publish_time": dt.datetime.combine(current, dt.time(8, 0, 0)),
                "title": "policy_placeholder",
                "content": "",
                "policy_type": "placeholder",
                "policy_score": 0.0,
                "policy_strength": 0.0,
                "policy_count": 0,
                "doc_hash": hash_text("MARKET", current.isoformat(), "policy_placeholder"),
                "data_source": "DERIVED_PLACEHOLDER",
            })
            current += dt.timedelta(days=1)

    sql = """
    INSERT INTO policy_data (
        symbol, trade_date, publish_time, title, content, policy_type,
        policy_score, policy_strength, policy_count, doc_hash, data_source
    ) VALUES (
        %(symbol)s, %(trade_date)s, %(publish_time)s, %(title)s, %(content)s, %(policy_type)s,
        %(policy_score)s, %(policy_strength)s, %(policy_count)s, %(doc_hash)s, %(data_source)s
    ) ON DUPLICATE KEY UPDATE
        content = VALUES(content),
        policy_type = VALUES(policy_type),
        policy_score = VALUES(policy_score),
        policy_strength = VALUES(policy_strength),
        policy_count = VALUES(policy_count),
        data_source = VALUES(data_source)
    """
    return upsert_many(cursor, sql, rows)


def import_alternative(cursor, end_date: dt.date, symbols: list[str], hot_limit: int) -> int:
    provider = NewsDataProvider()
    provider.initialize()
    rows: list[dict] = []

    if symbols:
        for symbol in symbols[:50]:
            comments = provider.get_stock_comments(symbol)
            if comments is None or comments.empty:
                continue
            score_values = []
            for _, raw in comments.fillna("").iterrows():
                row = raw.to_dict()
                text = text_value(row, ["title", "帖子标题", "摘要", "content", "内容"])
                score_values.append(sentiment_score_from_text(text))
            comment_count = len(comments)
            avg_score = sum(score_values) / len(score_values) if score_values else 0.0
            rows.append({
                "symbol": symbol,
                "trade_date": end_date,
                "metric_type": "COMMENT_SENTIMENT",
                "hot_rank": None,
                "popularity_score": float(comment_count),
                "comment_count": comment_count,
                "comment_sentiment": avg_score,
                "extra_payload_json": comments.head(20).to_json(force_ascii=False, orient="records"),
                "data_source": "AKSHARE_GUBA",
            })
    else:
        hot_df = provider.get_hot_stocks(limit=hot_limit)
        if hot_df is not None and not hot_df.empty:
            for idx, raw in hot_df.fillna("").iterrows():
                row = raw.to_dict()
                symbol = normalize_symbol(first_value(row, ["代码", "证券代码", "symbol"]))
                if not symbol:
                    continue
                hot_rank = safe_int(first_value(row, ["排名", "当前排名", "rank"])) or (idx + 1)
                popularity = safe_float(first_value(row, ["热度", "最新热度", "人气", "popularity"]))
                rows.append({
                    "symbol": symbol,
                    "trade_date": end_date,
                    "metric_type": "HOT_RANK",
                    "hot_rank": hot_rank,
                    "popularity_score": popularity if popularity is not None else float(max(hot_limit - hot_rank + 1, 1)),
                    "comment_count": None,
                    "comment_sentiment": None,
                    "extra_payload_json": json.dumps(row, ensure_ascii=False),
                    "data_source": "AKSHARE_HOT_RANK",
                })

    sql = """
    INSERT INTO alternative_data (
        symbol, trade_date, metric_type, hot_rank, popularity_score,
        comment_count, comment_sentiment, extra_payload_json, data_source
    ) VALUES (
        %(symbol)s, %(trade_date)s, %(metric_type)s, %(hot_rank)s, %(popularity_score)s,
        %(comment_count)s, %(comment_sentiment)s, %(extra_payload_json)s, %(data_source)s
    ) ON DUPLICATE KEY UPDATE
        hot_rank = VALUES(hot_rank),
        popularity_score = VALUES(popularity_score),
        comment_count = VALUES(comment_count),
        comment_sentiment = VALUES(comment_sentiment),
        extra_payload_json = VALUES(extra_payload_json),
        data_source = VALUES(data_source)
    """
    return upsert_many(cursor, sql, rows)


def import_derivatives(cursor, start_date: dt.date, end_date: dt.date) -> int:
    provider = FuturesDataProvider()
    mapping = {
        "IF": "000300.SH",
        "IH": "000016.SH",
        "IC": "000905.SH",
        "IM": "000852.SH",
    }
    contracts = {prefix: prefix + "0" for prefix in mapping}

    query = DataQuery(symbols=list(contracts.values()), start_date=start_date, end_date=end_date)
    response = provider.get_data(query, DataType.FUTURES_DAILY)
    if not response.success or response.data is None or response.data.empty:
        return 0

    rows: list[dict] = []
    for _, raw in response.data.fillna("").iterrows():
        row = raw.to_dict()
        trade_date_value = parse_datetime_value(first_value(row, ["date", "trade_date"]))
        if trade_date_value is None:
            continue
        contract_code = text_value(row, ["symbol"]) or text_value(row, ["contract_code"])
        prefix = contract_code[:2].upper()
        underlying_symbol = mapping.get(prefix)
        rows.append({
            "symbol": "MARKET",
            "underlying_symbol": underlying_symbol,
            "contract_code": contract_code,
            "trade_date": trade_date_value.date(),
            "futures_open": safe_float(first_value(row, ["open", "futures_open"])),
            "futures_high": safe_float(first_value(row, ["high", "futures_high"])),
            "futures_low": safe_float(first_value(row, ["low", "futures_low"])),
            "futures_close": safe_float(first_value(row, ["close", "futures_close"])),
            "futures_volume": safe_float(first_value(row, ["volume", "futures_volume"])),
            "open_interest": safe_float(first_value(row, ["open_interest", "hold"])),
            "basis": None,
            "basis_rate": None,
            "data_source": "AKSHARE_FUTURES",
        })

    sql = """
    INSERT INTO derivatives_data (
        symbol, underlying_symbol, contract_code, trade_date,
        futures_open, futures_high, futures_low, futures_close,
        futures_volume, open_interest, basis, basis_rate, data_source
    ) VALUES (
        %(symbol)s, %(underlying_symbol)s, %(contract_code)s, %(trade_date)s,
        %(futures_open)s, %(futures_high)s, %(futures_low)s, %(futures_close)s,
        %(futures_volume)s, %(open_interest)s, %(basis)s, %(basis_rate)s, %(data_source)s
    ) ON DUPLICATE KEY UPDATE
        underlying_symbol = VALUES(underlying_symbol),
        futures_open = VALUES(futures_open),
        futures_high = VALUES(futures_high),
        futures_low = VALUES(futures_low),
        futures_close = VALUES(futures_close),
        futures_volume = VALUES(futures_volume),
        open_interest = VALUES(open_interest),
        basis = VALUES(basis),
        basis_rate = VALUES(basis_rate),
        data_source = VALUES(data_source)
    """
    return upsert_many(cursor, sql, rows)


def main() -> None:
    args = parse_args()
    start_date = parse_date(args.start_date)
    end_date = parse_date(args.end_date)
    symbols = split_symbols(args.symbols)

    conn = get_connection()
    try:
        summary: dict[str, int] = {}
        with conn.cursor() as cursor:
            ensure_tables(cursor)

            if args.data_type in {"all", "news"}:
                summary["news"] = import_news(cursor, start_date, end_date, symbols)
            if args.data_type in {"all", "policy"}:
                summary["policy"] = import_policy(cursor, start_date, end_date, symbols)
            if args.data_type in {"all", "alternative"}:
                summary["alternative"] = import_alternative(cursor, end_date, symbols, args.hot_limit)
            if args.data_type in {"all", "derivatives"}:
                summary["derivatives"] = import_derivatives(cursor, start_date, end_date)

            conn.commit()

        print(json.dumps({"success": True, "summary": summary}, ensure_ascii=True))
    finally:
        conn.close()


if __name__ == "__main__":
    main()