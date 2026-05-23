from __future__ import annotations

import argparse
import datetime as dt
import multiprocessing as mp
import os
import re
import sys
from collections import defaultdict
from dataclasses import dataclass
from functools import lru_cache
from io import BytesIO, StringIO
from pathlib import Path
from typing import Any, Iterable, Sequence

PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools.akshare_data_sources import BENCHMARK_INDEX_SYMBOLS, get_connection


INDEX_DEFAULTS = tuple(symbol for symbol, _ in BENCHMARK_INDEX_SYMBOLS)
CSINDEX_BASE_URL = "https://www.csindex.com.cn/csindex-home"
CSINDEX_SITE_BASE_URL = "https://www.csindex.com.cn"
GM_HISTORY_TIMEOUT_SECONDS = 20
GM_MAX_SNAPSHOT_FALLBACK_TRADE_DAYS = 120


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="多源回填 index_constituents；auto 默认只补生效区间，announcement_date 仅在显式公告源下透传真实值"
    )
    parser.add_argument(
        "--provider",
        default="auto",
        choices=["auto", "csindex-announcement", "tushare", "gm", "csindex-public"],
    )
    parser.add_argument(
        "--indices",
        default=",".join(INDEX_DEFAULTS),
        help="逗号分隔的指数代码，默认使用内置基准指数列表",
    )
    parser.add_argument("--start-date", default="2010-01-01")
    parser.add_argument("--end-date", default=dt.date.today().isoformat())
    parser.add_argument("--replace-mode", default="auto", choices=["auto", "latest-open", "all"])
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    return parser.parse_args()


def parse_date(value: dt.date | str) -> dt.date:
    if isinstance(value, dt.date):
        return value
    text = str(value).strip()
    if len(text) >= 10 and text[4] == "-" and text[7] == "-":
        return dt.date.fromisoformat(text[:10])
    if len(text) >= 8 and text[:8].isdigit():
        return dt.datetime.strptime(text[:8], "%Y%m%d").date()
    raise ValueError(f"无法解析日期: {value}")


def parse_optional_date(value: Any) -> dt.date | None:
    if value is None:
        return None
    if isinstance(value, dt.datetime):
        return value.date()
    if isinstance(value, dt.date):
        return value
    text = str(value).strip()
    if not text or text.lower() in {"none", "nan", "nat"}:
        return None
    try:
        return parse_date(text)
    except Exception:
        return None


def normalize_symbol(symbol: str) -> str:
    text = str(symbol or "").strip().upper()
    if not text:
        return text
    if text.startswith(("SHSE.", "SZSE.", "BJSE.", "BSE.")):
        return gm_to_local_symbol(text)
    if "." in text:
        code, exchange = text.split(".", 1)
        exchange = exchange.strip().upper()
        if exchange in {"SH", "SZ", "BJ"}:
            return f"{code.zfill(6)}.{exchange}"
    if text.isdigit() and len(text) == 6:
        if text.startswith(("5", "6", "9")):
            return f"{text}.SH"
        if text.startswith(("0", "1", "2", "3")):
            return f"{text}.SZ"
        if text.startswith(("4", "8")):
            return f"{text}.BJ"
    return text


def local_to_gm_symbol(symbol: str) -> str:
    normalized = normalize_symbol(symbol)
    if normalized.endswith(".SH"):
        return f"SHSE.{normalized[:6]}"
    if normalized.endswith(".SZ"):
        return f"SZSE.{normalized[:6]}"
    if normalized.endswith(".BJ"):
        return f"BJSE.{normalized[:6]}"
    raise ValueError(f"不支持的本地代码: {symbol}")


def gm_to_local_symbol(symbol: str) -> str:
    text = str(symbol or "").strip().upper()
    if text.startswith("SHSE."):
        return f"{text[5:]}.SH"
    if text.startswith("SZSE."):
        return f"{text[5:]}.SZ"
    if text.startswith(("BJSE.", "BSE.")):
        return f"{text.split('.', 1)[1]}.BJ"
    return normalize_symbol(text)


def local_to_tushare_index_code(symbol: str) -> str:
    normalized = normalize_symbol(symbol)
    mapping = {
        "000300.SH": "399300.SZ",
        "000905.SH": "399905.SZ",
        "000852.SH": "399852.SZ",
    }
    return mapping.get(normalized, normalized)


def previous_day(value: dt.date) -> dt.date:
    return value - dt.timedelta(days=1)


def safe_float(value: Any) -> float | None:
    if value is None:
        return None
    try:
        result = float(value)
    except Exception:
        return None
    if result != result:
        return None
    return result


def first_value(record: dict[str, Any], keys: Sequence[str]) -> Any:
    lower_map = {str(key).lower(): value for key, value in record.items()}
    for key in keys:
        if key in record:
            value = record[key]
            if value is not None and value != "":
                return value
        lowered = key.lower()
        if lowered in lower_map:
            value = lower_map[lowered]
            if value is not None and value != "":
                return value
    return None


@dataclass(frozen=True)
class ConstituentRow:
    symbol: str
    weight: float | None


@dataclass(frozen=True)
class IntervalRow:
    index_symbol: str
    constituent_symbol: str
    start_date: dt.date
    end_date: dt.date | None
    announcement_date: dt.date | None
    weight: float | None
    status: str
    provider: str


@dataclass(frozen=True)
class RebalanceEvent:
    announcement_date: dt.date
    effective_date: dt.date
    added_symbols: tuple[str, ...]
    removed_symbols: tuple[str, ...]
    provider: str


def dedupe_constituents(rows: Iterable[ConstituentRow]) -> tuple[ConstituentRow, ...]:
    deduped: dict[str, ConstituentRow] = {}
    for row in rows:
        deduped[row.symbol] = row
    return tuple(sorted(deduped.values(), key=lambda item: item.symbol))


def interval_rows_from_snapshots(
    index_symbol: str,
    snapshots: Sequence[tuple[dt.date, tuple[ConstituentRow, ...]]],
    provider: str,
) -> list[IntervalRow]:
    ordered = sorted(snapshots, key=lambda item: item[0])
    compressed: list[tuple[dt.date, tuple[ConstituentRow, ...]]] = []
    for trade_date, rows in ordered:
        normalized_rows = dedupe_constituents(rows)
        if compressed and compressed[-1][1] == normalized_rows:
            continue
        compressed.append((trade_date, normalized_rows))

    result: list[IntervalRow] = []
    today = dt.date.today()
    for index, (trade_date, rows) in enumerate(compressed):
        end_date = None
        if index + 1 < len(compressed):
            end_date = previous_day(compressed[index + 1][0])
        status = "INACTIVE" if end_date and end_date < today else "ACTIVE"
        for row in rows:
            result.append(
                IntervalRow(
                    index_symbol=index_symbol,
                    constituent_symbol=row.symbol,
                    start_date=trade_date,
                    end_date=end_date,
                    announcement_date=None,
                    weight=row.weight,
                    status=status,
                    provider=provider,
                )
            )
    return result


def load_trade_dates(start_date: dt.date, end_date: dt.date) -> list[dt.date]:
    connection = get_connection()
    try:
        with connection.cursor() as cursor:
            cursor.execute(
                """
                SELECT DISTINCT trade_date
                FROM daily_bar
                WHERE trade_date BETWEEN %s AND %s
                ORDER BY trade_date
                """,
                (start_date, end_date),
            )
            return [parse_date(row[0]) for row in cursor.fetchall()]
    finally:
        connection.close()


def local_to_csindex_index_codes(symbol: str) -> tuple[str, ...]:
    normalized = normalize_symbol(symbol)
    primary_code = normalized.split(".", 1)[0]
    aliases = [primary_code]
    mapped = {
        "000300.SH": "399300",
        "000905.SH": "399905",
        "000852.SH": "399852",
    }.get(normalized)
    if mapped:
        aliases.append(mapped)
    return tuple(dict.fromkeys(aliases))


def csindex_index_aliases(symbol: str) -> tuple[str, ...]:
    normalized = normalize_symbol(symbol)
    aliases = list(local_to_csindex_index_codes(normalized))
    for candidate_symbol, name in BENCHMARK_INDEX_SYMBOLS:
        if normalize_symbol(candidate_symbol) != normalized:
            continue
        aliases.append(name)
        if not name.endswith("指数"):
            aliases.append(f"{name}指数")
        if "指数" in name:
            aliases.append(name.replace("指数", ""))
        break
    return tuple(dict.fromkeys(item for item in aliases if item))


def csindex_index_family_tokens(symbol: str) -> tuple[str, ...]:
    aliases = csindex_index_aliases(symbol)
    tokens: list[str] = []
    family_keywords = (
        "中证",
        "沪深",
        "上证",
        "深证",
        "创业板",
        "科创",
        "北证",
    )
    for alias in aliases:
        for keyword in family_keywords:
            if keyword in alias:
                tokens.append(keyword)
    return tuple(dict.fromkeys(tokens))


def normalize_csindex_numeric_code(value: Any) -> str:
    digits = "".join(ch for ch in str(value or "") if ch.isdigit())
    if not digits:
        return ""
    return digits[-6:].zfill(6)


def build_csindex_session():
    import requests

    session = requests.Session()
    session.headers.update(
        {
            "User-Agent": (
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36"
            ),
            "Referer": "https://www.csindex.com.cn/#/about/newsCenter?typelist=announcement&related_topics=index_rebalance",
            "Accept": "application/json, text/plain, */*",
        }
    )
    return session


def is_csindex_rebalance_candidate_notice(title: str, theme: str, notice_type: str) -> bool:
    normalized_title = re.sub(r"\s+", "", title or "")
    normalized_theme = str(theme or "")
    if notice_type != "announcement":
        return False
    if normalized_theme == "指数调样":
        return True
    keywords = (
        "样本",
        "调样",
        "临时调整",
        "定期调整",
        "调整方案",
    )
    return any(keyword in normalized_title for keyword in keywords)


@lru_cache(maxsize=1)
def load_csindex_candidate_notices() -> tuple[dict[str, Any], ...]:
    session = build_csindex_session()
    rows_per_page = 500
    page_number = 1
    notices: list[dict[str, Any]] = []

    while True:
        response = session.post(
            f"{CSINDEX_BASE_URL}/announcement/queryAnnouncementByVonew",
            json={
                "lang": "cn",
                "classlist": [],
                "indexlist": [],
                "page": {"desc": "", "key": "", "page": page_number, "rows": rows_per_page},
                "related_topics": [],
                "typelist": [],
            },
            timeout=60,
        )
        response.raise_for_status()
        payload = response.json()
        page_rows = payload.get("data") or []
        for notice in page_rows:
            title = str(notice.get("title") or "")
            theme = str(notice.get("theme") or "")
            notice_type = str(notice.get("noticeType") or "")
            if not is_csindex_rebalance_candidate_notice(title, theme, notice_type):
                continue
            notices.append(
                {
                    "id": int(notice.get("id")),
                    "title": title,
                    "theme": theme,
                    "noticeType": notice_type,
                    "publishDate": str(notice.get("publishDate") or ""),
                }
            )

        if len(page_rows) < rows_per_page:
            break
        page_number += 1

    return tuple(notices)


@lru_cache(maxsize=None)
def load_csindex_notice_detail(notice_id: int) -> dict[str, Any]:
    session = build_csindex_session()
    response = session.get(
        f"{CSINDEX_BASE_URL}/announcement/queryAnnouncementById",
        params={"id": notice_id},
        timeout=60,
    )
    if response.status_code == 403 and "attack.jinxibei.com" in response.text:
        raise RuntimeError("CSIndex 公告详情接口被 WAF 拦截，当前环境无法稳定抓取公告明细")
    try:
        response.raise_for_status()
        detail = response.json().get("data") or {}
    except Exception:
        return {}
    if not isinstance(detail, dict):
        return {}
    return detail


def fetch_csindex_public_snapshot(
    index_symbol: str,
    start_date: dt.date,
    end_date: dt.date,
) -> tuple[dt.date | None, tuple[ConstituentRow, ...]]:
    import akshare as ak

    index_code = normalize_symbol(index_symbol).split(".", 1)[0]
    try:
        frame = ak.index_stock_cons_weight_csindex(symbol=index_code)
    except Exception:
        frame = None

    if frame is not None and not frame.empty:
        chosen_date = None
        available_dates = sorted(
            {
                trade_date
                for trade_date in (
                    parse_optional_date(first_value(record, ["日期"])) for record in frame.to_dict("records")
                )
                if trade_date is not None and trade_date <= end_date
            }
        )
        if available_dates:
            chosen_date = available_dates[-1]
        if chosen_date is not None:
            rows = []
            for record in frame.to_dict("records"):
                trade_date = parse_optional_date(first_value(record, ["日期"]))
                if trade_date != chosen_date:
                    continue
                symbol = normalize_symbol(first_value(record, ["成分券代码", "证券代码"]))
                if not symbol:
                    continue
                rows.append(
                    ConstituentRow(
                        symbol=symbol,
                        weight=safe_float(first_value(record, ["权重"])),
                    )
                )
            if rows:
                return chosen_date, dedupe_constituents(rows)

    frame = ak.index_stock_cons(symbol=index_code)
    if frame.empty:
        return None, ()

    today = dt.date.today()
    if not (start_date <= today <= end_date):
        return None, ()

    rows = []
    for record in frame.to_dict("records"):
        symbol = normalize_symbol(first_value(record, ["品种代码", "证券代码"]))
        if not symbol:
            continue
        rows.append(ConstituentRow(symbol=symbol, weight=None))
    return today, dedupe_constituents(rows)


def extract_effective_date_from_notice(content: str, publish_date: dt.date) -> dt.date | None:
    text = re.sub(r"<[^>]+>", " ", content or "")
    text = re.sub(r"\s+", " ", text)
    patterns = [
        r"于(?:(\d{4})年)?\s*(\d{1,2})月\s*(\d{1,2})日(?:收市后)?(?:正式)?(?:生效|实施)",
        r"自(?:(\d{4})年)?\s*(\d{1,2})月\s*(\d{1,2})日(?:起)?(?:收市后)?(?:生效|实施)?",
    ]
    for pattern in patterns:
        match = re.search(pattern, text)
        if not match:
            continue
        year_text, month_text, day_text = match.groups()
        year = int(year_text) if year_text else publish_date.year
        try:
            return dt.date(year, int(month_text), int(day_text))
        except ValueError:
            continue
    return None


def normalize_table_columns(columns: Sequence[Any]) -> list[str]:
    return [re.sub(r"\s+", "", str(column or "")) for column in columns]


def parse_csindex_notice_attachment_rows(
    content: bytes,
    target_codes: set[str],
) -> tuple[set[str], set[str]]:
    import pandas as pd

    adds: set[str] = set()
    removes: set[str] = set()
    workbook = pd.read_excel(BytesIO(content), sheet_name=None)
    for sheet_name, frame in workbook.items():
        if frame is None or frame.empty:
            continue
        sheet_direction = None
        if "调入" in str(sheet_name):
            sheet_direction = adds
        elif "调出" in str(sheet_name):
            sheet_direction = removes
        if sheet_direction is None:
            continue

        normalized_columns = normalize_table_columns(frame.columns)
        normalized_records = []
        for record in frame.to_dict("records"):
            normalized_records.append({normalized_columns[idx]: value for idx, value in enumerate(record.values())})
        for record in normalized_records:
            row_index_code = normalize_csindex_numeric_code(first_value(record, ["指数代码"]))
            if row_index_code not in target_codes:
                continue
            symbol = normalize_symbol(first_value(record, ["证券代码", "成分券代码", "股票代码"]))
            if symbol:
                sheet_direction.add(symbol)
    return adds, removes


def parse_csindex_notice_html_tables(
    content: str,
    target_codes: set[str],
) -> tuple[set[str], set[str]]:
    import pandas as pd

    adds: set[str] = set()
    removes: set[str] = set()
    try:
        tables = pd.read_html(StringIO(content))
    except Exception:
        return adds, removes

    for table in tables:
        if table is None or table.empty or table.shape[1] < 6 or table.shape[0] < 3:
            continue
        first_cell = str(table.iat[0, 0]).strip()
        if first_cell != "指数代码":
            continue
        for row_index in range(2, table.shape[0]):
            row = [str(item).strip() for item in table.iloc[row_index].tolist()]
            row_index_code = normalize_csindex_numeric_code(row[0])
            if row_index_code not in target_codes:
                continue
            removed_symbol = normalize_symbol(row[2]) if row[2] not in {"-", "", "nan"} else ""
            added_symbol = normalize_symbol(row[4]) if row[4] not in {"-", "", "nan"} else ""
            if removed_symbol:
                removes.add(removed_symbol)
            if added_symbol:
                adds.add(added_symbol)
    return adds, removes


def fetch_csindex_rebalance_events(
    index_symbol: str,
    start_date: dt.date,
    end_date: dt.date,
) -> list[RebalanceEvent]:
    target_codes = set(local_to_csindex_index_codes(index_symbol))
    aliases = csindex_index_aliases(index_symbol)
    family_tokens = csindex_index_family_tokens(index_symbol)
    session = build_csindex_session()
    notices = load_csindex_candidate_notices()

    events: list[RebalanceEvent] = []
    for notice in notices:
        publish_date = parse_optional_date(notice.get("publishDate"))
        if publish_date is None:
            continue
        if publish_date > end_date:
            continue
        if publish_date < start_date and publish_date < dt.date(2005, 1, 1):
            continue
        title = str(notice.get("title") or "")
        if aliases and any(alias in title for alias in aliases):
            title_matches_alias = True
        else:
            title_matches_alias = False
        title_matches_family = any(token in title for token in family_tokens) if family_tokens else False
        if not title_matches_alias and not title_matches_family:
            continue

        detail_payload = load_csindex_notice_detail(int(notice.get("id")))
        content = str(detail_payload.get("content") or "")
        enclosure_list = detail_payload.get("enclosureList") or []
        has_html_table = "<table" in content.lower()
        if not title_matches_alias and not enclosure_list and not has_html_table:
            continue
        effective_date = extract_effective_date_from_notice(content, publish_date)
        if effective_date is None:
            continue

        added_symbols: set[str] = set()
        removed_symbols: set[str] = set()
        for enclosure in enclosure_list:
            file_url = str(enclosure.get("fileUrl") or "").strip()
            file_name = str(enclosure.get("fileName") or "").lower()
            if not file_url or not file_name.endswith((".xlsx", ".xls")):
                continue
            attachment_url = file_url if file_url.startswith("http") else f"{CSINDEX_SITE_BASE_URL}{file_url}"
            attachment_response = session.get(attachment_url, timeout=60)
            attachment_response.raise_for_status()
            attachment_adds, attachment_removes = parse_csindex_notice_attachment_rows(
                attachment_response.content,
                target_codes,
            )
            added_symbols.update(attachment_adds)
            removed_symbols.update(attachment_removes)

        if not added_symbols and not removed_symbols:
            table_adds, table_removes = parse_csindex_notice_html_tables(content, target_codes)
            added_symbols.update(table_adds)
            removed_symbols.update(table_removes)

        if not added_symbols and not removed_symbols:
            continue
        if effective_date < start_date:
            continue

        events.append(
            RebalanceEvent(
                announcement_date=publish_date,
                effective_date=effective_date,
                added_symbols=tuple(sorted(added_symbols)),
                removed_symbols=tuple(sorted(removed_symbols)),
                provider="CSINDEX_ANNOUNCEMENT",
            )
        )

    deduped_events: dict[tuple[dt.date, dt.date, tuple[str, ...], tuple[str, ...]], RebalanceEvent] = {}
    for event in events:
        deduped_events[(event.announcement_date, event.effective_date, event.added_symbols, event.removed_symbols)] = event
    return sorted(deduped_events.values(), key=lambda item: (item.effective_date, item.announcement_date))


def build_interval_rows_from_rebalance_events(
    index_symbol: str,
    start_date: dt.date,
    current_rows: Sequence[ConstituentRow],
    events: Sequence[RebalanceEvent],
) -> list[IntervalRow]:
    current_members = {row.symbol: row for row in dedupe_constituents(current_rows)}
    interval_end_by_symbol: dict[str, dt.date | None] = {symbol: None for symbol in current_members}
    result: list[IntervalRow] = []
    today = dt.date.today()

    for event in sorted(events, key=lambda item: item.effective_date, reverse=True):
        event_end = previous_day(event.effective_date)
        for symbol in event.added_symbols:
            if symbol not in current_members:
                continue
            current_row = current_members.pop(symbol)
            end_date = interval_end_by_symbol.pop(symbol, None)
            status = "INACTIVE" if end_date and end_date < today else "ACTIVE"
            result.append(
                IntervalRow(
                    index_symbol=index_symbol,
                    constituent_symbol=symbol,
                    start_date=event.effective_date,
                    end_date=end_date,
                    announcement_date=event.announcement_date,
                    weight=current_row.weight,
                    status=status,
                    provider=event.provider,
                )
            )
        for symbol in event.removed_symbols:
            if symbol in current_members:
                continue
            current_members[symbol] = ConstituentRow(symbol=symbol, weight=None)
            interval_end_by_symbol[symbol] = event_end

    for symbol, row in current_members.items():
        end_date = interval_end_by_symbol.get(symbol)
        status = "INACTIVE" if end_date and end_date < today else "ACTIVE"
        result.append(
            IntervalRow(
                index_symbol=index_symbol,
                constituent_symbol=symbol,
                start_date=start_date,
                end_date=end_date,
                announcement_date=None,
                weight=row.weight,
                status=status,
                provider="CSINDEX_ANNOUNCEMENT_BASELINE",
            )
        )
    return sorted(result, key=lambda item: (item.start_date, item.constituent_symbol))


def fetch_csindex_announcement_rows(
    index_symbol: str,
    start_date: dt.date,
    end_date: dt.date,
) -> tuple[list[IntervalRow], bool]:
    snapshot_date, current_rows = fetch_csindex_public_snapshot(index_symbol, start_date, end_date)
    if snapshot_date is None or not current_rows:
        return [], True

    events = fetch_csindex_rebalance_events(index_symbol, start_date, end_date)
    applicable_events = [event for event in events if event.effective_date <= snapshot_date]
    if not applicable_events:
        return [], True

    rows = build_interval_rows_from_rebalance_events(index_symbol, start_date, current_rows, applicable_events)
    earliest_effective_date = min((event.effective_date for event in applicable_events), default=snapshot_date)
    latest_only = earliest_effective_date > start_date
    return rows, latest_only


def fetch_csindex_public_rows(
    index_symbol: str,
    start_date: dt.date,
    end_date: dt.date,
) -> tuple[list[IntervalRow], bool]:
    snapshot_date, current_rows = fetch_csindex_public_snapshot(index_symbol, start_date, end_date)
    if snapshot_date is None or not current_rows:
        return [], True

    rows = []
    for record in current_rows:
        rows.append(
            IntervalRow(
                index_symbol=index_symbol,
                constituent_symbol=record.symbol,
                start_date=snapshot_date,
                end_date=None,
                announcement_date=None,
                weight=record.weight,
                status="ACTIVE",
                provider="CSINDEX_PUBLIC",
            )
        )
    return rows, True


def fetch_tushare_rows(
    index_symbol: str,
    start_date: dt.date,
    end_date: dt.date,
) -> tuple[list[IntervalRow], bool]:
    token = (os.getenv("TUSHARE_TOKEN") or os.getenv("ASTOCK_TUSHARE_TOKEN") or "").strip()
    if not token:
        raise RuntimeError("未配置 TUSHARE_TOKEN / ASTOCK_TUSHARE_TOKEN")

    import tushare as ts

    pro = ts.pro_api(token)
    index_code = local_to_tushare_index_code(index_symbol)
    member_frame = pro.index_member(index_code=index_code)
    if member_frame.empty:
        return [], False

    latest_weight_by_symbol: dict[str, float | None] = {}
    try:
        weight_frame = pro.index_weight(
            index_code=index_code,
            start_date=max(start_date, end_date - dt.timedelta(days=60)).strftime("%Y%m%d"),
            end_date=end_date.strftime("%Y%m%d"),
        )
    except Exception:
        weight_frame = None

    if weight_frame is not None and not weight_frame.empty:
        ordered_weights = sorted(
            weight_frame.to_dict("records"),
            key=lambda record: parse_optional_date(first_value(record, ["trade_date"])) or dt.date.min,
        )
        for record in ordered_weights:
            symbol = normalize_symbol(first_value(record, ["con_code", "symbol"]))
            if not symbol:
                continue
            latest_weight_by_symbol[symbol] = safe_float(first_value(record, ["weight"]))

    rows: list[IntervalRow] = []
    today = dt.date.today()
    for record in member_frame.to_dict("records"):
        symbol = normalize_symbol(first_value(record, ["con_code", "symbol"]))
        member_start = parse_optional_date(first_value(record, ["in_date", "start_date"]))
        member_end = parse_optional_date(first_value(record, ["out_date", "end_date"]))
        if not symbol or member_start is None:
            continue
        if member_start > end_date:
            continue
        interval_end = previous_day(member_end) if member_end else None
        if interval_end is not None and interval_end < start_date:
            continue
        rows.append(
            IntervalRow(
                index_symbol=index_symbol,
                constituent_symbol=symbol,
                start_date=member_start,
                end_date=interval_end,
                announcement_date=None,
                weight=latest_weight_by_symbol.get(symbol),
                status="INACTIVE" if interval_end and interval_end < today else "ACTIVE",
                provider="TUSHARE",
            )
        )
    return rows, False


def parse_gm_history_rows(index_symbol: str, records: Sequence[dict[str, Any]]) -> list[IntervalRow]:
    if not records:
        return []

    if any("constituents" in record for record in records):
        snapshots: list[tuple[dt.date, tuple[ConstituentRow, ...]]] = []
        for record in records:
            trade_date = parse_optional_date(first_value(record, ["trade_date", "created_at", "date"]))
            constituents = record.get("constituents") or {}
            if trade_date is None or not isinstance(constituents, dict):
                continue
            rows = tuple(
                ConstituentRow(symbol=gm_to_local_symbol(symbol), weight=safe_float(weight))
                for symbol, weight in constituents.items()
            )
            snapshots.append((trade_date, rows))
        return interval_rows_from_snapshots(index_symbol, snapshots, "GM_HISTORY")

    direct_rows: list[IntervalRow] = []
    snapshot_groups: defaultdict[dt.date, list[ConstituentRow]] = defaultdict(list)
    today = dt.date.today()
    for record in records:
        symbol = normalize_symbol(
            first_value(record, ["constituent_symbol", "symbol", "stock_code", "sec_code", "con_code"])
        )
        if not symbol:
            continue

        start_date = parse_optional_date(first_value(record, ["start_date", "in_date", "effective_date"]))
        end_date = parse_optional_date(first_value(record, ["end_date", "out_date"]))
        announcement_date = parse_optional_date(
            first_value(record, ["announcement_date", "announce_date", "ann_date", "publish_date", "notice_date"])
        )
        trade_date = parse_optional_date(first_value(record, ["trade_date", "created_at", "date"]))
        weight = safe_float(first_value(record, ["weight"]))

        if start_date is not None or end_date is not None or announcement_date is not None:
            effective_start = start_date or trade_date
            if effective_start is None:
                continue
            direct_rows.append(
                IntervalRow(
                    index_symbol=index_symbol,
                    constituent_symbol=symbol,
                    start_date=effective_start,
                    end_date=end_date,
                    announcement_date=announcement_date,
                    weight=weight,
                    status="INACTIVE" if end_date and end_date < today else "ACTIVE",
                    provider="GM_HISTORY",
                )
            )
            continue

        if trade_date is not None:
            snapshot_groups[trade_date].append(ConstituentRow(symbol=symbol, weight=weight))

    if direct_rows:
        return direct_rows

    snapshots = [
        (trade_date, tuple(rows))
        for trade_date, rows in sorted(snapshot_groups.items(), key=lambda item: item[0])
    ]
    return interval_rows_from_snapshots(index_symbol, snapshots, "GM")


def _run_gm_stage_worker(
    stage: str,
    gm_symbol: str,
    start_date_text: str,
    end_date_text: str,
    result_queue: mp.queues.Queue,
) -> None:
    try:
        from tools.import_from_juejin import _ensure_gm_inited

        _ensure_gm_inited()

        if stage == "history-frame":
            from gm.api import stk_get_index_history_constituents

            frame = stk_get_index_history_constituents(
                index=gm_symbol,
                start_date=start_date_text,
                end_date=end_date_text,
            )
            payload = [] if frame is None or frame.empty else frame.to_dict("records")
        elif stage == "history-rows":
            from gm.api import get_history_constituents

            payload = list(
                get_history_constituents(
                    index=gm_symbol,
                    start_date=start_date_text,
                    end_date=end_date_text,
                )
                or []
            )
        elif stage == "snapshot":
            from gm.api import stk_get_index_constituents

            frame = stk_get_index_constituents(index=gm_symbol, trade_date=start_date_text)
            payload = [] if frame is None or frame.empty else frame.to_dict("records")
        else:
            raise ValueError(f"未知 GM stage: {stage}")

        result_queue.put(("ok", payload))
    except Exception as exc:
        result_queue.put(("error", str(exc)))


def run_gm_stage_with_timeout(
    stage: str,
    gm_symbol: str,
    start_date: dt.date,
    end_date: dt.date,
    timeout_seconds: int = GM_HISTORY_TIMEOUT_SECONDS,
) -> list[dict[str, Any]]:
    ctx = mp.get_context("spawn")
    result_queue = ctx.Queue()
    process = ctx.Process(
        target=_run_gm_stage_worker,
        args=(stage, gm_symbol, start_date.isoformat(), end_date.isoformat(), result_queue),
    )
    process.start()
    process.join(timeout_seconds)

    if process.is_alive():
        process.terminate()
        process.join()
        raise TimeoutError(f"GM {stage} 超时 {timeout_seconds}s")

    try:
        status, payload = result_queue.get_nowait()
    except Exception:
        raise RuntimeError(f"GM {stage} 未返回结果")

    if status == "error":
        raise RuntimeError(f"GM {stage} 失败: {payload}")
    return list(payload)


def fetch_gm_rows(
    index_symbol: str,
    start_date: dt.date,
    end_date: dt.date,
) -> tuple[list[IntervalRow], bool]:
    gm_symbol = local_to_gm_symbol(index_symbol)

    try:
        history_frame_rows = run_gm_stage_with_timeout("history-frame", gm_symbol, start_date, end_date)
    except Exception:
        history_frame_rows = []

    if history_frame_rows:
        rows = parse_gm_history_rows(index_symbol, history_frame_rows)
        if rows:
            return rows, False

    try:
        history_rows = run_gm_stage_with_timeout("history-rows", gm_symbol, start_date, end_date)
    except Exception:
        history_rows = []

    rows = parse_gm_history_rows(index_symbol, history_rows)
    if rows:
        return rows, False

    trade_dates = load_trade_dates(start_date, end_date)
    if len(trade_dates) > GM_MAX_SNAPSHOT_FALLBACK_TRADE_DAYS:
        raise RuntimeError(
            "GM 历史区间不可用且交易日跨度过大，已拒绝逐日快照回退；请缩小日期范围或改用其他 provider"
        )

    snapshots: list[tuple[dt.date, tuple[ConstituentRow, ...]]] = []
    for trade_date in trade_dates:
        try:
            snapshot_rows = run_gm_stage_with_timeout("snapshot", gm_symbol, trade_date, trade_date)
        except Exception:
            continue
        if not snapshot_rows:
            continue
        rows_for_date = []
        for record in snapshot_rows:
            symbol = normalize_symbol(first_value(record, ["symbol", "stock_code", "sec_code"]))
            if not symbol:
                continue
            rows_for_date.append(
                ConstituentRow(
                    symbol=symbol,
                    weight=safe_float(first_value(record, ["weight"])),
                )
            )
        if rows_for_date:
            snapshots.append((trade_date, tuple(rows_for_date)))

    return interval_rows_from_snapshots(index_symbol, snapshots, "GM"), False


def select_interval_rows(
    provider: str,
    index_symbol: str,
    start_date: dt.date,
    end_date: dt.date,
) -> tuple[list[IntervalRow], bool, list[str]]:
    providers: list[tuple[str, Any]] = []
    if provider == "auto":
        providers = [
            ("tushare", fetch_tushare_rows),
            ("gm", fetch_gm_rows),
            ("csindex-public", fetch_csindex_public_rows),
        ]
    elif provider == "csindex-announcement":
        providers = [("csindex-announcement", fetch_csindex_announcement_rows)]
    elif provider == "tushare":
        providers = [("tushare", fetch_tushare_rows)]
    elif provider == "gm":
        providers = [("gm", fetch_gm_rows)]
    elif provider == "csindex-public":
        providers = [("csindex-public", fetch_csindex_public_rows)]

    checks: list[str] = []
    for provider_name, fetch in providers:
        try:
            rows, latest_only = fetch(index_symbol, start_date, end_date)
        except Exception as exc:
            checks.append(f"{provider_name}: {exc}")
            continue
        checks.append(f"{provider_name}: {len(rows)}")
        if rows:
            return rows, latest_only, checks
    return [], False, checks


def infer_replace_mode(requested: str, latest_only: bool) -> str:
    if requested != "auto":
        return requested
    return "latest-open" if latest_only else "all"


def ensure_table(cursor) -> None:
    cursor.execute("SHOW TABLES LIKE 'index_constituents'")
    if cursor.fetchone() is None:
        raise RuntimeError("index_constituents 表不存在，请先初始化数据库结构")


def load_columns(cursor, table_name: str) -> set[str]:
    cursor.execute(f"SHOW COLUMNS FROM {table_name}")
    return {str(row[0]) for row in cursor.fetchall()}


def delete_existing_rows(cursor, index_symbol: str, replace_mode: str, cutoff_start_date: dt.date) -> int:
    if replace_mode == "all":
        cursor.execute("DELETE FROM index_constituents WHERE index_symbol = %s", (index_symbol,))
        return int(cursor.rowcount)

    cursor.execute(
        "DELETE FROM index_constituents WHERE index_symbol = %s AND (end_date IS NULL OR end_date >= %s)",
        (index_symbol, cutoff_start_date),
    )
    return int(cursor.rowcount)


def insert_rows(cursor, rows: Sequence[IntervalRow], columns: set[str]) -> int:
    ordered_columns = [
        column
        for column in [
            "index_symbol",
            "constituent_symbol",
            "weight",
            "announcement_date",
            "start_date",
            "end_date",
            "status",
        ]
        if column in columns
    ]
    if "announcement_date" not in ordered_columns:
        raise RuntimeError(
            "index_constituents.announcement_date 缺失；请先执行 tools/migrate_index_constituents_announcement_date.sql"
        )

    sql = (
        f"INSERT INTO index_constituents ({', '.join(ordered_columns)}) "
        f"VALUES ({', '.join(['%s'] * len(ordered_columns))})"
    )
    payload = []
    for row in rows:
        payload.append(tuple(getattr(row, column) for column in ordered_columns))
    if not payload:
        return 0
    cursor.executemany(sql, payload)
    return int(cursor.rowcount)


def summarize(
    index_symbol: str,
    rows: Sequence[IntervalRow],
    replace_mode: str,
    provider_checks: Sequence[str],
) -> None:
    announcement_rows = sum(1 for row in rows if row.announcement_date is not None)
    providers = "; ".join(provider_checks)
    first_start = min((row.start_date for row in rows), default=None)
    last_end = max((row.end_date or row.start_date for row in rows), default=None)
    print(
        f"[{index_symbol}] provider_checks={providers}; rows={len(rows)}; "
        f"announcement_rows={announcement_rows}; replace_mode={replace_mode}; "
        f"first_start={first_start}; last_end={last_end}"
    )


def main() -> int:
    args = parse_args()
    start_date = parse_date(args.start_date)
    end_date = parse_date(args.end_date)
    indices = [normalize_symbol(item) for item in args.indices.split(",") if item.strip()]

    connection = get_connection()
    failures: list[str] = []
    try:
        with connection.cursor() as cursor:
            ensure_table(cursor)
            columns = load_columns(cursor, "index_constituents")
            if "announcement_date" not in columns:
                raise RuntimeError(
                    "index_constituents.announcement_date 缺失；请先执行 tools/migrate_index_constituents_announcement_date.sql"
                )

            for index_symbol in indices:
                try:
                    rows, latest_only, provider_checks = select_interval_rows(
                        args.provider,
                        index_symbol,
                        start_date,
                        end_date,
                    )
                    replace_mode = infer_replace_mode(args.replace_mode, latest_only)
                    cutoff_start_date = min((row.start_date for row in rows), default=start_date)
                    summarize(index_symbol, rows, replace_mode, provider_checks)
                    if args.dry_run:
                        continue
                    deleted_rows = delete_existing_rows(cursor, index_symbol, replace_mode, cutoff_start_date)
                    inserted_rows = insert_rows(cursor, rows, columns)
                    connection.commit()
                    print(f"[{index_symbol}] deleted_rows={deleted_rows}; inserted_rows={inserted_rows}")
                except Exception as exc:
                    connection.rollback()
                    failures.append(f"{index_symbol}: {exc}")
                    print(f"[{index_symbol}] failed: {exc}")
                    if args.verbose:
                        raise
    finally:
        connection.close()

    if failures:
        print("failures=")
        for failure in failures:
            print(f"backfill_failed: {failure}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())