from __future__ import annotations

import datetime as dt
from functools import lru_cache
from typing import Any, Dict, Iterable, List, Optional

import akshare as ak
import pandas as pd
import pymysql

from tools.a_share_symbol_utils import normalize_symbol


MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
}

BENCHMARK_INDEX_SYMBOLS = [
    ("000300.SH", "沪深300"),
    ("000001.SH", "上证指数"),
    ("399001.SZ", "深证成指"),
    ("399006.SZ", "创业板指"),
    ("000905.SH", "中证500"),
    ("000852.SH", "中证1000"),
    ("000016.SH", "上证50"),
]


def get_connection():
    return pymysql.connect(**MYSQL_CONFIG)


def _safe_date(value: object, default: Optional[dt.date] = None) -> Optional[dt.date]:
    if value is None:
        return default
    if isinstance(value, dt.date):
        return value
    try:
        text = str(value).strip()
        if not text or text.startswith("0000-00-00"):
            return default
        return dt.date.fromisoformat(text[:10])
    except Exception:
        return default


def _symbol_row(
    symbol: str,
    name: str,
    exchange: str,
    asset_class: str,
    list_date: Optional[dt.date],
    delist_date: Optional[dt.date],
    status: str,
) -> Dict[str, Any]:
    return {
        "symbol": normalize_symbol(symbol),
        "name": str(name or symbol).strip(),
        "exchange": exchange,
        "asset_class": asset_class,
        "list_date": list_date or dt.date(2000, 1, 1),
        "delist_date": delist_date,
        "status": status,
    }


def _append_unique(rows: List[Dict[str, Any]], row: Dict[str, Any]) -> None:
    existing_index = next((index for index, item in enumerate(rows) if item["symbol"] == row["symbol"]), None)
    if existing_index is None:
        rows.append(row)
        return
    current = rows[existing_index]
    if current.get("status") != "DELISTED" and row.get("status") == "DELISTED":
        return
    rows[existing_index] = row


@lru_cache()
def fetch_stock_symbol_rows(include_b_shares: bool = True, include_delisted: bool = True) -> List[Dict[str, Any]]:
    rows: List[Dict[str, Any]] = []

    try:
        sh_a = ak.stock_info_sh_name_code(symbol="主板A股")
        for _, item in sh_a.iterrows():
            code = str(item.get("证券代码") or "").strip().zfill(6)
            if not code:
                continue
            _append_unique(
                rows,
                _symbol_row(
                    f"{code}.SH",
                    item.get("证券简称") or code,
                    "SH",
                    "STOCK",
                    _safe_date(item.get("上市日期")),
                    None,
                    "ACTIVE",
                ),
            )
    except Exception:
        pass

    try:
        sz_a = ak.stock_info_sz_name_code(symbol="A股列表")
        for _, item in sz_a.iterrows():
            code = str(item.get("A股代码") or "").strip().zfill(6)
            if not code:
                continue
            _append_unique(
                rows,
                _symbol_row(
                    f"{code}.SZ",
                    item.get("A股简称") or code,
                    "SZ",
                    "STOCK",
                    _safe_date(item.get("A股上市日期")),
                    None,
                    "ACTIVE",
                ),
            )
    except Exception:
        pass

    try:
        bj = ak.stock_info_bj_name_code()
        for _, item in bj.iterrows():
            code = str(item.get("证券代码") or "").strip().zfill(6)
            if not code:
                continue
            _append_unique(
                rows,
                _symbol_row(
                    f"{code}.BJ",
                    item.get("证券简称") or code,
                    "BJ",
                    "STOCK",
                    _safe_date(item.get("上市日期")),
                    None,
                    "ACTIVE",
                ),
            )
    except Exception:
        pass

    if include_b_shares:
        try:
            sh_b = ak.stock_info_sh_name_code(symbol="主板B股")
            for _, item in sh_b.iterrows():
                code = str(item.get("证券代码") or "").strip().zfill(6)
                if not code:
                    continue
                _append_unique(
                    rows,
                    _symbol_row(
                        f"{code}.SH",
                        item.get("证券简称") or code,
                        "SH",
                        "STOCK",
                        _safe_date(item.get("上市日期")),
                        None,
                        "ACTIVE",
                    ),
                )
        except Exception:
            pass

        try:
            sz_b = ak.stock_info_sz_name_code(symbol="B股列表")
            for _, item in sz_b.iterrows():
                code = str(item.get("B股代码") or "").strip().zfill(6)
                if not code:
                    continue
                _append_unique(
                    rows,
                    _symbol_row(
                        f"{code}.SZ",
                        item.get("B股简称") or code,
                        "SZ",
                        "STOCK",
                        _safe_date(item.get("B股上市日期")),
                        None,
                        "ACTIVE",
                    ),
                )
        except Exception:
            pass

    if include_delisted:
        try:
            sh_delist = ak.stock_info_sh_delist(symbol="全部")
            for _, item in sh_delist.iterrows():
                code = str(item.get("公司代码") or "").strip().zfill(6)
                if not code:
                    continue
                _append_unique(
                    rows,
                    _symbol_row(
                        f"{code}.SH",
                        item.get("公司简称") or code,
                        "SH",
                        "STOCK",
                        _safe_date(item.get("上市日期")),
                        _safe_date(item.get("暂停上市日期")),
                        "DELISTED",
                    ),
                )
        except Exception:
            pass

        try:
            for label in ("终止上市公司", "暂停上市公司"):
                sz_delist = ak.stock_info_sz_delist(symbol=label)
                for _, item in sz_delist.iterrows():
                    code = str(item.get("证券代码") or "").strip().zfill(6)
                    if not code:
                        continue
                    _append_unique(
                        rows,
                        _symbol_row(
                            f"{code}.SZ",
                            item.get("证券简称") or code,
                            "SZ",
                            "STOCK",
                            _safe_date(item.get("上市日期")),
                            _safe_date(item.get("终止上市日期")),
                            "DELISTED",
                        ),
                    )
        except Exception:
            pass

    rows.sort(key=lambda item: item["symbol"])
    return rows


@lru_cache()
def fetch_benchmark_index_symbol_rows() -> List[Dict[str, Any]]:
    rows: List[Dict[str, Any]] = []
    for symbol, name in BENCHMARK_INDEX_SYMBOLS:
        rows.append(
            {
                "symbol": symbol,
                "name": name,
                "exchange": symbol.split(".", 1)[1] if "." in symbol else "",
                "asset_class": "INDEX",
                "list_date": dt.date(2000, 1, 1),
                "delist_date": None,
                "status": "ACTIVE",
            }
        )
    return rows


def stock_value_symbol(symbol: str) -> str:
    return normalize_symbol(symbol).split(".", 1)[0]


def fetch_stock_valuation_rows(symbol: str, start: dt.date, end: dt.date) -> List[Dict[str, Any]]:
    try:
        df = ak.stock_value_em(symbol=stock_value_symbol(symbol))
    except Exception:
        return []

    if df is None or df.empty or "数据日期" not in df.columns:
        return []

    normalized = df.copy()
    normalized["数据日期"] = pd.to_datetime(normalized["数据日期"], errors="coerce").dt.date
    normalized = normalized[(normalized["数据日期"] >= start) & (normalized["数据日期"] <= end)].copy()
    if normalized.empty:
        return []

    normalized["PE(TTM)"] = pd.to_numeric(normalized.get("PE(TTM)"), errors="coerce")
    normalized["市净率"] = pd.to_numeric(normalized.get("市净率"), errors="coerce")
    normalized["总市值"] = pd.to_numeric(normalized.get("总市值"), errors="coerce")
    normalized["流通市值"] = pd.to_numeric(normalized.get("流通市值"), errors="coerce")

    results: List[Dict[str, Any]] = []
    for _, item in normalized.iterrows():
        trade_date = item.get("数据日期")
        if trade_date is None:
            continue
        results.append(
            {
                "trade_date": trade_date,
                "pe_ratio": item.get("PE(TTM)"),
                "pb_ratio": item.get("市净率"),
                "market_cap": item.get("总市值"),
                "circulating_market_cap": item.get("流通市值"),
            }
        )
    return results


__all__ = [
    "BENCHMARK_INDEX_SYMBOLS",
    "fetch_benchmark_index_symbol_rows",
    "fetch_stock_valuation_rows",
    "fetch_stock_symbol_rows",
    "get_connection",
    "stock_value_symbol",
]
