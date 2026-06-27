#!/usr/bin/env python3
"""AKShare 日线数据获取 — 供 import_from_akshare 使用"""
from __future__ import annotations
import datetime as dt
import pandas as pd
import time

import akshare as ak


def _ak_symbol(symbol: str) -> str:
    """将 000001.SZ 格式转为 akshare 需要的纯数字代码"""
    return symbol.split(".")[0] if "." in symbol else symbol


def _market(symbol: str) -> str:
    """判断市场: SH / SZ / BJ"""
    if "." in symbol:
        suffix = symbol.split(".")[1].upper()
        if suffix in ("SH", "SZ", "BJ"):
            return suffix
    code = symbol.split(".")[0]
    if code.startswith(("6", "5", "9")):
        return "SH"
    if code.startswith(("0", "3", "2")):
        return "SZ"
    if code.startswith(("8", "4")):
        return "BJ"
    return "SZ"


def fetch_symbol_daily(symbol: str, start_date: str, end_date: str) -> pd.DataFrame | None:
    """获取个股日线 OHLCV 数据
    Args:
        symbol: 如 000001.SZ
        start_date: 如 2024-01-01
        end_date: 如 2024-12-31
    Returns:
        DataFrame with columns: date, open, high, low, close, volume, amount
    """
    code = _ak_symbol(symbol)
    period = "daily"
    adjust = "qfq"  # 前复权

    try:
        df = ak.stock_zh_a_hist(
            symbol=code,
            period=period,
            start_date=start_date.replace("-", ""),
            end_date=end_date.replace("-", ""),
            adjust=adjust,
        )
    except Exception:
        time.sleep(0.3)
        try:
            df = ak.stock_zh_a_hist(
                symbol=code,
                period=period,
                start_date=start_date.replace("-", ""),
                end_date=end_date.replace("-", ""),
                adjust="",
            )
        except Exception:
            return None

    if df is None or df.empty:
        return None

    df = df.rename(columns={
        "日期": "date",
        "开盘": "open",
        "最高": "high",
        "最低": "low",
        "收盘": "close",
        "成交量": "volume",
        "成交额": "amount",
    })
    wanted = ["date", "open", "high", "low", "close", "volume", "amount"]
    df = df[[c for c in wanted if c in df.columns]]
    return df


def fetch_benchmark_daily(symbol: str, start_date: str, end_date: str) -> pd.DataFrame | None:
    """获取指数日线数据（如 000300.SH 沪深300）
    Args:
        symbol: 如 000300.SH
    """
    code = _ak_symbol(symbol)

    try:
        df = ak.stock_zh_index_daily(symbol=f"sh{code}" if _market(symbol) == "SH" else f"sz{code}")
    except Exception:
        time.sleep(0.3)
        try:
            df = ak.stock_zh_index_daily(symbol=f"sz{code}")
        except Exception:
            return None

    if df is None or df.empty:
        return None

    df = df.rename(columns={
        "date": "date",
        "open": "open",
        "high": "high",
        "low": "low",
        "close": "close",
        "volume": "volume",
        "amount": "amount",
    })
    wanted = ["date", "open", "high", "low", "close", "volume", "amount"]
    df = df[[c for c in wanted if c in df.columns]]

    if start_date and end_date:
        df = df[(df["date"] >= start_date) & (df["date"] <= end_date)]
    return df
