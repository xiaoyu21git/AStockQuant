"""
掘金 → MySQL 导入脚本骨架

用途：
- 使用你的掘金模拟账户/实盘账户，通过官方 SDK 或 HTTP API 拉取：
    - 全沪深股票代码列表（含 A/B 股）
  - 日线行情（daily_bar 所需字段）
  - 资金流向数据（money_flow_daily 所需字段）
  - 龙虎榜数据（dragon_tiger_list 所需字段）
- 写入 astock_quant 库中 astock_init.sql 已定义的表，供高位股监控 / 回测使用。

注意：
- 这里不包含任何真实掘金接口调用代码，只留出占位函数，避免泄露你的账号信息。
- 你需要根据掘金官方文档，在占位函数里补上具体的 SDK/API 调用和字段映射。
"""

from __future__ import annotations

import datetime as dt
from bisect import bisect_right
import math
from typing import List, Dict, Any, Iterable, Optional, Tuple

import json
import os
import sys
import time
from pathlib import Path
import pymysql

# 确保项目根目录在 sys.path 中，便于导入 astock_engine
PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

# 复用已有的掘金配置（token / 代码转换逻辑）
from astock_engine.broker.myquant_broker import (
    DEFAULT_GM_TOKEN,
    MyQuantBroker,
)
from tools.a_share_symbol_utils import classify_mainland_stock_symbol
from tools.daily_bar_quality import format_invalid_samples, sanitize_valuation_record


# =============== 配置区域（按需修改）================

MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
}

# 一次导入的时间范围，可以先用近 1~3 年验证，再扩到更久
DEFAULT_START_DATE = dt.date(2023, 1, 1)
DEFAULT_END_DATE = dt.date.today()

DATA_SOURCE_JUEJIN_GM_ENRICHED = "JUEJIN_GM_ENRICHED"
DAILY_BAR_WRITE_BATCH_SIZE = 200
MAX_DAILY_BAR_WRITE_RETRIES = 5
PE_PB_DB_LIMIT = 999999.9999
MARKET_CAP_DB_LIMIT = 9999999999999999.9999

BENCHMARK_INDEX_SYMBOLS = [
    ("000300.SH", "沪深300"),
    ("000001.SH", "上证指数"),
    ("399001.SZ", "深证成指"),
    ("399006.SZ", "创业板指"),
    ("000905.SH", "中证500"),
    ("000852.SH", "中证1000"),
    ("000016.SH", "上证50"),
]

INDUSTRY_INDEX_KEYWORDS = (
    "申万",
    "中信",
    "行业",
    "板块",
    "概念",
    "主题",
    "风格",
)

INDUSTRY_INDEX_CODE_PREFIXES = (
    "801",
    "802",
    "803",
    "804",
    "805",
    "806",
    "807",
    "808",
    "809",
    "885",
    "886",
    "887",
    "888",
    "889",
)


_gm_inited = False


def _load_token_from_runtime_config() -> Optional[str]:
    candidate_paths = [
        PROJECT_ROOT / "config" / "trading_connection.json",
        PROJECT_ROOT / "bin" / "Debug" / "config" / "trading_connection.json",
        PROJECT_ROOT / "build" / "tests" / "config" / "trading_connection.json",
    ]
    for path in candidate_paths:
        if not path.exists():
            continue
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
        except Exception:
            continue
        token = str(payload.get("token") or "").strip()
        if token:
            return token
    return None


def _ensure_gm_inited() -> None:
    """确保已为 gm.api 设置 token。

    优先使用 MyQuantBroker 中的 DEFAULT_GM_TOKEN，其次读取环境变量 GM_TOKEN / ASTOCK_GM_TOKEN。
    """

    global _gm_inited
    if _gm_inited:
        return

    try:
        from gm.api import set_token  # type: ignore[import]
    except Exception as exc:  # pragma: no cover - 运行期检查
        raise RuntimeError("导入 gm.api 失败，请确认已安装并可用掘金 SDK") from exc

    token = DEFAULT_GM_TOKEN or os.getenv("GM_TOKEN") or os.getenv("ASTOCK_GM_TOKEN") or _load_token_from_runtime_config()
    if not token:
        raise RuntimeError("未找到掘金 token，请在 MyQuantBroker、环境变量 GM_TOKEN / ASTOCK_GM_TOKEN 或 trading_connection.json 中配置")

    set_token(token)
    _gm_inited = True


def _normalize_trade_date(value) -> Optional[dt.date]:
    if value is None:
        return None
    try:
        if isinstance(value, str):
            return dt.date.fromisoformat(value[:10])
        return value.date()
    except Exception:
        return None


def _safe_float(value) -> Optional[float]:
    if value is None:
        return None
    try:
        result = float(value)
    except Exception:
        return None
    if result != result:
        return None
    return result


def _fetch_daily_valuation_map(symbol: str, start: dt.date, end: dt.date) -> Dict[dt.date, Dict[str, Optional[float]]]:
    from gm.api import stk_get_daily_valuation  # type: ignore[import]

    _ensure_gm_inited()

    gm_symbol = MyQuantBroker._to_gm_symbol(symbol)  # type: ignore[attr-defined]
    rows = stk_get_daily_valuation(
        gm_symbol,
        fields="pe_ttm,pb_mrq",
        start_date=start.strftime("%Y-%m-%d"),
        end_date=end.strftime("%Y-%m-%d"),
        df=False,
    )

    result: Dict[dt.date, Dict[str, Optional[float]]] = {}
    for row in rows or []:
        trade_date = _normalize_trade_date(row.get("trade_date"))
        if trade_date is None:
            continue
        result[trade_date] = {
            "pe_ratio": _safe_float(row.get("pe_ttm")),
            "pb_ratio": _safe_float(row.get("pb_mrq")),
        }
    return result


def _fetch_daily_adjust_factor_map(symbol: str, start: dt.date, end: dt.date) -> Dict[dt.date, Dict[str, Optional[float]]]:
    from gm.api import stk_get_adj_factor  # type: ignore[import]

    _ensure_gm_inited()

    gm_symbol = MyQuantBroker._to_gm_symbol(symbol)  # type: ignore[attr-defined]
    rows = stk_get_adj_factor(
        gm_symbol,
        start_date=start.strftime("%Y-%m-%d"),
        end_date=end.strftime("%Y-%m-%d"),
        base_date=None,
    )

    if rows is None:
        return {}

    if hasattr(rows, "to_dict"):
        records = rows.to_dict("records")
    else:
        records = list(rows)

    result: Dict[dt.date, Dict[str, Optional[float]]] = {}
    for row in records:
        trade_date = _normalize_trade_date(row.get("trade_date"))
        if trade_date is None:
            continue
        result[trade_date] = {
            "pre_adjust_factor": _safe_float(row.get("adj_factor_fwd_acc")),
            "post_adjust_factor": _safe_float(row.get("adj_factor_bwd_acc")),
        }
    return result


def expand_adjust_factors_for_trade_dates(
    trade_dates: Iterable[dt.date],
    adjust_factor_by_date: Dict[dt.date, Dict[str, Optional[float]]],
    seed_pre_adjust_factor: Optional[float] = None,
    seed_post_adjust_factor: Optional[float] = None,
) -> Dict[dt.date, Dict[str, Optional[float]]]:
    ordered_dates = sorted({trade_date for trade_date in trade_dates if trade_date is not None})
    if not ordered_dates:
        return {}

    def _valid_factor(value: Optional[float]) -> Optional[float]:
        numeric = _safe_float(value)
        if numeric is None or not math.isfinite(numeric) or numeric <= 0.0:
            return None
        return numeric

    current_pre = _valid_factor(seed_pre_adjust_factor)
    current_post = _valid_factor(seed_post_adjust_factor)
    first_known_pre = current_pre
    first_known_post = current_post
    expanded: Dict[dt.date, Dict[str, Optional[float]]] = {}

    for trade_date in ordered_dates:
        row = adjust_factor_by_date.get(trade_date, {})
        row_pre = _valid_factor(row.get("pre_adjust_factor")) if row else None
        row_post = _valid_factor(row.get("post_adjust_factor")) if row else None
        if row_pre is not None:
            current_pre = row_pre
            if first_known_pre is None:
                first_known_pre = row_pre
        if row_post is not None:
            current_post = row_post
            if first_known_post is None:
                first_known_post = row_post
        expanded[trade_date] = {
            "pre_adjust_factor": current_pre,
            "post_adjust_factor": current_post,
        }

    if first_known_pre is not None or first_known_post is not None:
        for trade_date in ordered_dates:
            row = expanded[trade_date]
            if row["pre_adjust_factor"] is None and first_known_pre is not None:
                row["pre_adjust_factor"] = first_known_pre
            if row["post_adjust_factor"] is None and first_known_post is not None:
                row["post_adjust_factor"] = first_known_post

    return expanded


def _fetch_share_change_events(symbol: str, start: dt.date, end: dt.date) -> List[Tuple[dt.date, Optional[float], Optional[float]]]:
    from gm.api import stk_get_share_change  # type: ignore[import]

    _ensure_gm_inited()

    gm_symbol = MyQuantBroker._to_gm_symbol(symbol)  # type: ignore[attr-defined]
    rows = stk_get_share_change(
        gm_symbol,
        start_date="1990-01-01",
        end_date=end.strftime("%Y-%m-%d"),
    )

    if rows is None:
        return []

    if hasattr(rows, "to_dict"):
        records = rows.to_dict("records")
    else:
        records = list(rows)

    events: List[Tuple[dt.date, Optional[float], Optional[float]]] = []
    for row in records:
        effective_date = (
            _normalize_trade_date(row.get("chg_date"))
            or _normalize_trade_date(row.get("share_list_date"))
            or _normalize_trade_date(row.get("pub_date"))
        )
        if effective_date is None:
            continue
        events.append(
            (
                effective_date,
                _safe_float(row.get("share_total")),
                _safe_float(row.get("share_circ")),
            )
        )

    events.sort(key=lambda item: item[0])
    deduped: List[Tuple[dt.date, Optional[float], Optional[float]]] = []
    for event in events:
        if deduped and deduped[-1][0] == event[0]:
            deduped[-1] = event
        else:
            deduped.append(event)
    return deduped


def _resolve_share_snapshot(
    trade_date: dt.date,
    share_dates: List[dt.date],
    share_events: List[Tuple[dt.date, Optional[float], Optional[float]]],
) -> Tuple[Optional[float], Optional[float]]:
    index = bisect_right(share_dates, trade_date) - 1
    if index < 0:
        return None, None
    _, total_share, circulating_share = share_events[index]
    return total_share, circulating_share


# =============== 掘金真实实现区域 ====================


def fetch_all_mainland_stock_symbols_from_juejin(include_b_shares: bool = True) -> List[Dict[str, Any]]:
    """从掘金获取沪深股票标的列表，并转换为内部统一代码格式。

    默认返回沪深 A/B 股；若 include_b_shares=False，则只返回 A 股。
    """

    from gm.api import get_instrumentinfos, SEC_TYPE_STOCK  # type: ignore[import]

    _ensure_gm_inited()

    infos = get_instrumentinfos(
        symbols=None,
        exchanges=["SZSE", "SHSE"],
        sec_types=[SEC_TYPE_STOCK],
        names=None,
        fields=None,
        df=False,
    )

    results: List[Dict[str, Any]] = []
    for item in infos:
        raw_symbol = str(item.get("symbol") or "")
        symbol = MyQuantBroker._from_gm_symbol(raw_symbol)  # type: ignore[attr-defined]
        if not symbol:
            continue

        share_type = classify_mainland_stock_symbol(symbol)
        if share_type is None:
            continue
        if not include_b_shares and share_type != "A":
            continue

        exchange = ""
        if "." in symbol:
            _, exch = symbol.split(".", 1)
            exchange = exch.upper()

        list_date = dt.date(2000, 1, 1)
        ld = item.get("listed_date")
        if ld:
            try:
                # 可能是字符串或 datetime
                if isinstance(ld, str):
                    list_date = dt.date.fromisoformat(ld[:10])
                else:
                    list_date = ld.date()
            except Exception:
                pass

        delist_date = None
        raw_delist_date = item.get("delisted_date")
        if raw_delist_date:
            try:
                if isinstance(raw_delist_date, str):
                    text = raw_delist_date.strip()
                    if text and not text.startswith("0000-00-00"):
                        delist_date = dt.date.fromisoformat(text[:10])
                else:
                    delist_date = raw_delist_date.date()
            except Exception:
                delist_date = None

        status = "ACTIVE"
        if delist_date is not None and delist_date <= dt.date.today():
            status = "DELISTED"

        results.append({
            "symbol": symbol,
            "name": item.get("sec_name") or item.get("sec_abbr") or symbol,
            "exchange": exchange,
            "asset_class": "STOCK",
            "list_date": list_date,
            "delist_date": delist_date,
            "status": status,
        })

    return results


def fetch_all_a_share_symbols_from_juejin() -> List[Dict[str, Any]]:
    """兼容旧接口：仅获取 A 股标的列表。"""

    return fetch_all_mainland_stock_symbols_from_juejin(include_b_shares=False)


def _fetch_index_symbol_infos_from_juejin() -> List[Dict[str, Any]]:
    """从掘金拉取全部指数标的，并转换为内部代码格式。"""

    from gm.api import get_instrumentinfos, SEC_TYPE_INDEX  # type: ignore[import]

    _ensure_gm_inited()

    infos = get_instrumentinfos(
        symbols=None,
        exchanges=["SZSE", "SHSE"],
        sec_types=[SEC_TYPE_INDEX],
        names=None,
        fields=None,
        df=False,
    )

    results: List[Dict[str, Any]] = []
    for item in infos or []:
        raw_symbol = str(item.get("symbol") or "")
        symbol = MyQuantBroker._from_gm_symbol(raw_symbol)  # type: ignore[attr-defined]
        if not symbol:
            continue

        exchange = ""
        if "." in symbol:
            _, exch = symbol.split(".", 1)
            exchange = exch.upper()

        list_date = dt.date(2000, 1, 1)
        ld = item.get("listed_date")
        if ld:
            try:
                if isinstance(ld, str):
                    list_date = dt.date.fromisoformat(ld[:10])
                else:
                    list_date = ld.date()
            except Exception:
                pass

        delist_date = None
        raw_delist_date = item.get("delisted_date")
        if raw_delist_date:
            try:
                if isinstance(raw_delist_date, str):
                    text = raw_delist_date.strip()
                    if text and not text.startswith("0000-00-00"):
                        delist_date = dt.date.fromisoformat(text[:10])
                else:
                    delist_date = raw_delist_date.date()
            except Exception:
                delist_date = None

        status = "ACTIVE"
        if delist_date is not None and delist_date <= dt.date.today():
            status = "DELISTED"

        results.append({
            "symbol": symbol,
            "name": item.get("sec_name") or item.get("sec_abbr") or symbol,
            "exchange": exchange,
            "asset_class": "INDEX",
            "list_date": list_date,
            "delist_date": delist_date,
            "status": status,
        })

    return results


def fetch_benchmark_index_symbols_from_juejin() -> List[Dict[str, Any]]:
    """返回需要写入的常用基准指数清单。"""

    name_by_symbol = {symbol: name for symbol, name in BENCHMARK_INDEX_SYMBOLS}
    gm_index_map: Dict[str, Dict[str, Any]] = {}

    try:
        infos = _fetch_index_symbol_infos_from_juejin()
        for item in infos:
            symbol = item["symbol"]
            if symbol in name_by_symbol:
                gm_index_map[symbol] = {
                    **item,
                    "name": item.get("name") or name_by_symbol[symbol],
                }
    except Exception as exc:
        print(f"[warn] index instrumentinfos 拉取失败，改用固定基准清单: {exc}")

    results: List[Dict[str, Any]] = []
    for symbol, name in BENCHMARK_INDEX_SYMBOLS:
        if symbol in gm_index_map:
            results.append(gm_index_map[symbol])
        else:
            results.append({
                "symbol": symbol,
                "name": name,
                "exchange": symbol.split(".", 1)[1].upper() if "." in symbol else "",
                "asset_class": "INDEX",
                "list_date": dt.date(2000, 1, 1),
                "delist_date": None,
                "status": "ACTIVE",
            })

    return results


def fetch_industry_index_symbols_from_juejin() -> List[Dict[str, Any]]:
    """返回常见行业/板块指数清单。"""

    benchmark_symbols = {symbol for symbol, _ in BENCHMARK_INDEX_SYMBOLS}
    results: List[Dict[str, Any]] = []

    try:
        infos = _fetch_index_symbol_infos_from_juejin()
    except Exception as exc:
        print(f"[warn] industry index instrumentinfos 拉取失败: {exc}")
        return results

    for item in infos:
        symbol = item["symbol"]
        if symbol in benchmark_symbols:
            continue

        name = str(item.get("name") or "")
        code = symbol.split(".", 1)[0] if "." in symbol else symbol
        is_industry_index = (
            code.startswith(INDUSTRY_INDEX_CODE_PREFIXES)
            or any(keyword in name for keyword in INDUSTRY_INDEX_KEYWORDS)
        )
        if not is_industry_index:
            continue

        results.append(item)

    return results


def fetch_daily_bars_from_juejin(symbol: str, start: dt.date, end: dt.date) -> List[Dict[str, Any]]:
    """从掘金获取某一标的在 [start, end] 之间的日线数据。

    基于 gm.api.history("1d")，并映射到 daily_bar 的字段定义。
    """

    from gm.api import history  # type: ignore[import]

    _ensure_gm_inited()

    gm_symbol = MyQuantBroker._to_gm_symbol(symbol)  # type: ignore[attr-defined]
    if not gm_symbol:
        return []

    start_str = start.strftime("%Y-%m-%d")
    end_str = end.strftime("%Y-%m-%d")

    valuation_by_date: Dict[dt.date, Dict[str, Optional[float]]] = {}
    adjust_factor_by_date: Dict[dt.date, Dict[str, Optional[float]]] = {}
    share_events: List[Tuple[dt.date, Optional[float], Optional[float]]] = []
    share_dates: List[dt.date] = []

    try:
        valuation_by_date = _fetch_daily_valuation_map(symbol, start, end)
    except Exception as exc:
        print(f"[warn] daily valuation 拉取失败 {symbol}: {exc}")

    try:
        adjust_factor_by_date = _fetch_daily_adjust_factor_map(symbol, start, end)
    except Exception as exc:
        print(f"[warn] adjust factor 拉取失败 {symbol}: {exc}")

    try:
        share_events = _fetch_share_change_events(symbol, start, end)
        share_dates = [event[0] for event in share_events]
    except Exception as exc:
        print(f"[warn] share change 拉取失败 {symbol}: {exc}")

    try:
        rows = history(
            symbol=gm_symbol,
            frequency="1d",
            start_time=start_str,
            end_time=end_str,
            fields=None,
            skip_suspended=True,
            df=False,
        )
    except Exception as exc:  # pragma: no cover - 运行期错误
        print(f"[warn] history 拉取失败 {symbol}: {exc}")
        return []

    prepared_rows: List[Tuple[Dict[str, Any], dt.date]] = []
    for row in rows or []:
        eob = row.get("eob") or row.get("bob") or row.get("bar_time")
        trade_date: dt.date | None = None
        if eob is not None:
            try:
                if isinstance(eob, str):
                    trade_date = dt.date.fromisoformat(eob[:10])
                else:
                    trade_date = eob.date()
            except Exception:
                trade_date = None
        if trade_date is None:
            continue

        prepared_rows.append((row, trade_date))

    adjust_factor_by_date = expand_adjust_factors_for_trade_dates(
        (trade_date for _, trade_date in prepared_rows),
        adjust_factor_by_date,
    )

    results: List[Dict[str, Any]] = []
    for row, trade_date in prepared_rows:
        def _f(name: str, *alts: str, default=None):
            for key in (name, *alts):
                try:
                    v = row.get(key)
                except Exception:
                    v = None
                if v is not None:
                    try:
                        return float(v)
                    except Exception:
                        continue
            return default

        valuation_row = valuation_by_date.get(trade_date, {})
        adjust_factor_row = adjust_factor_by_date.get(trade_date, {})
        close = _f("close", default=0.0)
        total_share, circulating_share = _resolve_share_snapshot(trade_date, share_dates, share_events)
        market_cap = None
        circulating_market_cap = None
        if close is not None and close > 0:
            if total_share is not None and total_share > 0:
                market_cap = close * total_share
            if circulating_share is not None and circulating_share > 0:
                circulating_market_cap = close * circulating_share

        results.append({
            "trade_date": trade_date,
            "open": _f("open", default=0.0),
            "high": _f("high", default=0.0),
            "low": _f("low", default=0.0),
            "close": close,
            "pre_close": _f("pre_close", default=0.0),
            "volume": _f("volume", default=0.0),
            # gm 中通常使用 amount 表示成交额
            "turnover": _f("amount", "turnover", default=0.0),
            "change_pct": _f("chg_pct", default=0.0),
            "change_amt": _f("chg", default=0.0),
            "amplitude": _f("amp", "amplitude", default=0.0),
            "turnover_rate": _f("turnover_rate"),
            "pe_ratio": valuation_row.get("pe_ratio", _f("pe", "pe_ttm")),
            "pb_ratio": valuation_row.get("pb_ratio", _f("pb", "pb_mrq")),
            "market_cap": market_cap if market_cap is not None else _f("market_value", "market_cap"),
            "circulating_market_cap": circulating_market_cap if circulating_market_cap is not None else _f("float_market_value", "circulating_market_cap"),
            "pre_adjust_factor": adjust_factor_row.get("pre_adjust_factor"),
            "post_adjust_factor": adjust_factor_row.get("post_adjust_factor"),
        })

    return results


def fetch_money_flow_from_juejin(symbol: str, start: dt.date, end: dt.date) -> List[Dict[str, Any]]:
    """从掘金获取某一标的在 [start, end] 之间的资金流向数据。

    返回示例：对应 money_flow_daily 的字段。
    [
        {
            "trade_date": dt.date(2025, 1, 2),
            "main_inflow": 8e7,
            "main_outflow": 5e7,
            "net_main_inflow": 3e7,
            "large_inflow": 5e7,
            "large_outflow": 3e7,
            "medium_inflow": 2e7,
            "medium_outflow": 1e7,
            "small_inflow": 1e7,
            "small_outflow": 1e7,
            "net_amount": 3e7,
        },
        ...
    ]
    """
    # TODO: 先返回空列表，后续接入掘金资金流向接口。
    return []


def fetch_lhb_from_juejin(symbol: str, start: dt.date, end: dt.date) -> List[Dict[str, Any]]:
    """从掘金获取某一标的在 [start, end] 之间的龙虎榜数据。

    返回示例：对应 dragon_tiger_list 的字段。
    [
        {
            "trade_date": dt.date(2025, 1, 2),
            "reason": "日涨幅偏离值达7%",
            "buy_amount": 1.2e8,
            "sell_amount": 9e7,
            "net_amount": 3e7,
            "buy_count": 3,
            "sell_count": 2,
            "institution_buy": 2e7,
            "institution_sell": 1e7,
            "turnover_rate": 8.5,
        },
        ...
    ]
    """
    # TODO: 先返回空列表，后续接入掘金龙虎榜接口。
    return []


# =============== MySQL 工具函数 ====================


def get_connection():
    return pymysql.connect(
        host=MYSQL_CONFIG["host"],
        port=MYSQL_CONFIG["port"],
        user=MYSQL_CONFIG["user"],
        password=MYSQL_CONFIG["password"],
        database=MYSQL_CONFIG["database"],
        charset=MYSQL_CONFIG["charset"],
        autocommit=False,
    )


def _is_retryable_write_error(exc: Exception) -> bool:
    if not isinstance(exc, pymysql.err.OperationalError):
        return False
    errno = exc.args[0] if exc.args else None
    return errno in {1205, 1213}


def _sanitize_daily_bar_payload(row: Dict[str, Any]) -> tuple[Dict[str, Any], list[str]]:
    sanitized_row, anomalies = sanitize_valuation_record(row)
    for field_name, limit in {
        "pe_ratio": PE_PB_DB_LIMIT,
        "pb_ratio": PE_PB_DB_LIMIT,
        "market_cap": MARKET_CAP_DB_LIMIT,
        "circulating_market_cap": MARKET_CAP_DB_LIMIT,
    }.items():
        field_value = sanitized_row.get(field_name)
        if field_value is None:
            continue
        try:
            numeric_value = float(field_value)
        except Exception:
            sanitized_row[field_name] = None
            anomalies.append(f"invalid {field_name}")
            continue
        if abs(numeric_value) > limit:
            sanitized_row[field_name] = None
            anomalies.append(f"{field_name} out_of_range")
    return sanitized_row, anomalies


def upsert_symbol_info(cursor, symbols: Iterable[Dict[str, Any]]):
    sql = """
    INSERT INTO symbol_info (
        symbol, name, exchange, asset_class, list_date, delist_date, status
    ) VALUES (
        %(symbol)s, %(name)s, %(exchange)s, %(asset_class)s,
        %(list_date)s, %(delist_date)s, %(status)s
    )
    ON DUPLICATE KEY UPDATE
        name = VALUES(name),
        exchange = VALUES(exchange),
        asset_class = VALUES(asset_class),
        list_date = VALUES(list_date),
        delist_date = VALUES(delist_date),
        status = VALUES(status)
    """
    data = []
    for s in symbols:
        data.append({
            "symbol": s["symbol"],
            "name": s.get("name", ""),
            "exchange": s.get("exchange", ""),
            "asset_class": s.get("asset_class", "STOCK"),
            "list_date": s.get("list_date", dt.date(2000, 1, 1)),
            "delist_date": s.get("delist_date"),
            "status": s.get("status", "ACTIVE"),
        })
    if data:
        cursor.executemany(sql, data)


def upsert_daily_bars(cursor, symbol: str, bars: Iterable[Dict[str, Any]]):
    sql = """
    INSERT INTO daily_bar (
        symbol, trade_date, open, high, low, close, pre_close,
        volume, turnover, change_pct, change_amt, amplitude,
        turnover_rate, pe_ratio, pb_ratio, market_cap, circulating_market_cap,
        pre_adjust_factor, post_adjust_factor, data_source
    ) VALUES (
        %(symbol)s, %(trade_date)s, %(open)s, %(high)s, %(low)s, %(close)s, %(pre_close)s,
        %(volume)s, %(turnover)s, %(change_pct)s, %(change_amt)s, %(amplitude)s,
        %(turnover_rate)s, %(pe_ratio)s, %(pb_ratio)s, %(market_cap)s, %(circulating_market_cap)s,
        %(pre_adjust_factor)s, %(post_adjust_factor)s, %(data_source)s
    )
    ON DUPLICATE KEY UPDATE
        open = VALUES(open), high = VALUES(high), low = VALUES(low), close = VALUES(close),
        pre_close = VALUES(pre_close), volume = VALUES(volume), turnover = VALUES(turnover),
        change_pct = VALUES(change_pct), change_amt = VALUES(change_amt),
        amplitude = VALUES(amplitude), turnover_rate = VALUES(turnover_rate),
        pe_ratio = VALUES(pe_ratio), pb_ratio = VALUES(pb_ratio), market_cap = VALUES(market_cap),
        circulating_market_cap = VALUES(circulating_market_cap),
        pre_adjust_factor = COALESCE(VALUES(pre_adjust_factor), pre_adjust_factor),
        post_adjust_factor = COALESCE(VALUES(post_adjust_factor), post_adjust_factor),
        data_source = VALUES(data_source)
    """
    data = []
    invalid_samples: list[tuple[dict[str, Any], list[str]]] = []
    for b in bars:
        payload = {
            "symbol": symbol,
            "trade_date": b["trade_date"],
            "open": b.get("open", 0.0),
            "high": b.get("high", 0.0),
            "low": b.get("low", 0.0),
            "close": b.get("close", 0.0),
            "pre_close": b.get("pre_close", 0.0),
            "volume": b.get("volume", 0.0),
            "turnover": b.get("turnover", 0.0),
            "change_pct": b.get("change_pct", 0.0),
            "change_amt": b.get("change_amt", 0.0),
            "amplitude": b.get("amplitude", 0.0),
            "turnover_rate": b.get("turnover_rate"),
            "pe_ratio": b.get("pe_ratio", b.get("pe")),
            "pb_ratio": b.get("pb_ratio", b.get("pb")),
            "market_cap": b.get("market_cap"),
            "circulating_market_cap": b.get("circulating_market_cap"),
            "pre_adjust_factor": b.get("pre_adjust_factor"),
            "post_adjust_factor": b.get("post_adjust_factor"),
            "data_source": b.get("data_source", DATA_SOURCE_JUEJIN_GM_ENRICHED),
        }
        sanitized_payload, anomalies = _sanitize_daily_bar_payload(payload)
        if anomalies:
            invalid_samples.append((dict(sanitized_payload), anomalies))
        data.append(sanitized_payload)

    if invalid_samples:
        summary = format_invalid_samples(invalid_samples, limit=3)
        print(
            f"[warn] {symbol} daily_bar valuation abnormal_rows={len(invalid_samples)} sanitized_before_write samples={summary}",
            flush=True,
        )

    if not data:
        return

    data.sort(key=lambda item: (item["symbol"], item["trade_date"]))
    connection = cursor.connection
    last_error: Exception | None = None
    for attempt in range(1, MAX_DAILY_BAR_WRITE_RETRIES + 1):
        try:
            for batch_start in range(0, len(data), DAILY_BAR_WRITE_BATCH_SIZE):
                batch = data[batch_start:batch_start + DAILY_BAR_WRITE_BATCH_SIZE]
                cursor.executemany(sql, batch)
            return
        except Exception as exc:
            last_error = exc
            if not _is_retryable_write_error(exc) or attempt >= MAX_DAILY_BAR_WRITE_RETRIES:
                raise
            connection.rollback()
            print(
                f"[warn] {symbol} daily_bar upsert retry attempt={attempt}/{MAX_DAILY_BAR_WRITE_RETRIES} error={exc}",
                flush=True,
            )
            time.sleep(0.5 * attempt)

    if last_error is not None:
        raise last_error


def upsert_money_flow(cursor, symbol: str, rows: Iterable[Dict[str, Any]]):
    sql = """
    INSERT INTO money_flow_daily (
        symbol, trade_date,
        main_inflow, main_outflow, net_main_inflow,
        large_inflow, large_outflow,
        medium_inflow, medium_outflow,
        small_inflow, small_outflow,
        net_amount
    ) VALUES (
        %(symbol)s, %(trade_date)s,
        %(main_inflow)s, %(main_outflow)s, %(net_main_inflow)s,
        %(large_inflow)s, %(large_outflow)s,
        %(medium_inflow)s, %(medium_outflow)s,
        %(small_inflow)s, %(small_outflow)s,
        %(net_amount)s
    )
    ON DUPLICATE KEY UPDATE
        main_inflow = VALUES(main_inflow),
        main_outflow = VALUES(main_outflow),
        net_main_inflow = VALUES(net_main_inflow),
        large_inflow = VALUES(large_inflow),
        large_outflow = VALUES(large_outflow),
        medium_inflow = VALUES(medium_inflow),
        medium_outflow = VALUES(medium_outflow),
        small_inflow = VALUES(small_inflow),
        small_outflow = VALUES(small_outflow),
        net_amount = VALUES(net_amount)
    """
    data = []
    for r in rows:
        data.append({
            "symbol": symbol,
            "trade_date": r["trade_date"],
            "main_inflow": r.get("main_inflow", 0.0),
            "main_outflow": r.get("main_outflow", 0.0),
            "net_main_inflow": r.get("net_main_inflow", 0.0),
            "large_inflow": r.get("large_inflow", 0.0),
            "large_outflow": r.get("large_outflow", 0.0),
            "medium_inflow": r.get("medium_inflow", 0.0),
            "medium_outflow": r.get("medium_outflow", 0.0),
            "small_inflow": r.get("small_inflow", 0.0),
            "small_outflow": r.get("small_outflow", 0.0),
            "net_amount": r.get("net_amount", 0.0),
        })
    if data:
        cursor.executemany(sql, data)


def upsert_lhb(cursor, symbol: str, rows: Iterable[Dict[str, Any]]):
    sql = """
    INSERT INTO dragon_tiger_list (
        symbol, trade_date, reason,
        buy_amount, sell_amount, net_amount,
        buy_count, sell_count,
        institution_buy, institution_sell,
        turnover_rate
    ) VALUES (
        %(symbol)s, %(trade_date)s, %(reason)s,
        %(buy_amount)s, %(sell_amount)s, %(net_amount)s,
        %(buy_count)s, %(sell_count)s,
        %(institution_buy)s, %(institution_sell)s,
        %(turnover_rate)s
    )
    ON DUPLICATE KEY UPDATE
        reason = VALUES(reason),
        buy_amount = VALUES(buy_amount),
        sell_amount = VALUES(sell_amount),
        net_amount = VALUES(net_amount),
        buy_count = VALUES(buy_count),
        sell_count = VALUES(sell_count),
        institution_buy = VALUES(institution_buy),
        institution_sell = VALUES(institution_sell),
        turnover_rate = VALUES(turnover_rate)
    """
    data = []
    for r in rows:
        data.append({
            "symbol": symbol,
            "trade_date": r["trade_date"],
            "reason": r.get("reason", ""),
            "buy_amount": r.get("buy_amount", 0.0),
            "sell_amount": r.get("sell_amount", 0.0),
            "net_amount": r.get("net_amount", 0.0),
            "buy_count": r.get("buy_count", 0),
            "sell_count": r.get("sell_count", 0),
            "institution_buy": r.get("institution_buy", 0.0),
            "institution_sell": r.get("institution_sell", 0.0),
            "turnover_rate": r.get("turnover_rate", 0.0),
        })
    if data:
        cursor.executemany(sql, data)


# =============== 主流程 ====================


def import_all_from_juejin(start: dt.date | None = None, end: dt.date | None = None):
    if start is None:
        start = DEFAULT_START_DATE
    if end is None:
        end = DEFAULT_END_DATE

    conn = get_connection()
    cur = conn.cursor()
    try:
        print(f"[import] 获取沪深股票列表（含 A/B 股）……")
        symbols = fetch_all_mainland_stock_symbols_from_juejin(include_b_shares=True)
        print(f"[import] 共 {len(symbols)} 个标的")
        upsert_symbol_info(cur, symbols)
        conn.commit()

        print(f"[import] 获取常用基准指数列表……")
        benchmark_symbols = fetch_benchmark_index_symbols_from_juejin()
        print(f"[import] 共 {len(benchmark_symbols)} 个基准指数")
        upsert_symbol_info(cur, benchmark_symbols)
        conn.commit()

        print(f"[import] 获取行业/板块指数列表……")
        industry_symbols = fetch_industry_index_symbols_from_juejin()
        print(f"[import] 共 {len(industry_symbols)} 个行业/板块指数")
        upsert_symbol_info(cur, industry_symbols)
        conn.commit()

        for idx, s in enumerate(symbols, 1):
            symbol = s["symbol"]
            print(f"[import] ({idx}/{len(symbols)}) {symbol} {s.get('name', '')} 日线/资金流向/龙虎榜……")

            # 日线
            daily = fetch_daily_bars_from_juejin(symbol, start, end)
            if daily:
                upsert_daily_bars(cur, symbol, daily)

            # 资金流向
            mf = fetch_money_flow_from_juejin(symbol, start, end)
            if mf:
                upsert_money_flow(cur, symbol, mf)

            # 龙虎榜
            lhb = fetch_lhb_from_juejin(symbol, start, end)
            if lhb:
                upsert_lhb(cur, symbol, lhb)

            conn.commit()

        for idx, s in enumerate(benchmark_symbols, 1):
            symbol = s["symbol"]
            print(f"[import] ({idx}/{len(benchmark_symbols)}) {symbol} {s.get('name', '')} 基准指数日线……")

            daily = fetch_daily_bars_from_juejin(symbol, start, end)
            if daily:
                upsert_daily_bars(cur, symbol, daily)

            conn.commit()

        for idx, s in enumerate(industry_symbols, 1):
            symbol = s["symbol"]
            print(f"[import] ({idx}/{len(industry_symbols)}) {symbol} {s.get('name', '')} 行业/板块指数日线……")

            daily = fetch_daily_bars_from_juejin(symbol, start, end)
            if daily:
                upsert_daily_bars(cur, symbol, daily)

            conn.commit()
    except Exception as e:
        conn.rollback()
        raise
    finally:
        cur.close()
        conn.close()


__all__ = [
    "fetch_daily_bars_from_juejin",
    "fetch_all_mainland_stock_symbols_from_juejin",
    "fetch_all_a_share_symbols_from_juejin",
    "fetch_benchmark_index_symbols_from_juejin",
    "fetch_industry_index_symbols_from_juejin",
    "get_connection",
    "upsert_daily_bars",
]


if __name__ == "__main__":
    # 示例：默认导入 2023-01-01 至今的沪深 A/B 股数据
    import_all_from_juejin()
