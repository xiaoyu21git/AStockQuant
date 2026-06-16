"""
import_from_baostock.py
Baostock 数据源模块 — 批量拉取全 A 股日线数据
解决当前逐只请求导致的频率限制问题，实现 1 次批量请求替代 5400+ 次逐只请求。
"""

from __future__ import annotations

import datetime as dt
import logging
import time
from typing import Any, Optional

import baostock as bs
import pandas as pd

logger = logging.getLogger(__name__)

# Baostock 代码 → 内部代码 ("sh.600000" → "600000.SH")
def convert_baostock_code_to_symbol(bs_code: str) -> str:
    """'sh.600000' → '600000.SH'"""
    exchange, code = bs_code.split(".")
    return f"{code}.{exchange.upper()}"

def convert_symbol_to_baostock_code(symbol: str) -> str:
    """'600000.SH' → 'sh.600000'"""
    code, exchange = symbol.split(".")
    return f"{exchange.lower()}.{code}"

# 交易日历
def fetch_trade_calendar(start_date: dt.date, end_date: dt.date) -> list[dt.date]:
    """从 Baostock 获取交易日历"""
    rs = bs.query_trade_dates(
        start_date=start_date.strftime("%Y-%m-%d"),
        end_date=end_date.strftime("%Y-%m-%d")
    )
    dates = []
    while (rs.error_code == '0') and rs.next():
        row = rs.get_row_data()
        if row[1] == '1':  # is_trading_day
            dates.append(dt.date.fromisoformat(row[0]))
    return dates

# 基础信息
def fetch_all_stock_basic_info() -> pd.DataFrame:
    """获取全 A 股基础信息（替代 symbol_info 更新）"""
    rs = bs.query_stock_basic()
    rows = []
    while (rs.error_code == '0') and rs.next():
        rows.append(rs.get_row_data())
    
    if not rows:
        return pd.DataFrame()
    
    df = pd.DataFrame(rows, columns=rs.fields)
    df['symbol'] = df['code'].apply(convert_baostock_code_to_symbol)
    df['ipoDate'] = pd.to_datetime(df['ipoDate']).dt.date
    df['outDate'] = pd.to_datetime(df['outDate']).dt.date
    return df

# 日线数据批量拉取
def fetch_daily_k_data_batch(
    symbols: list[str],
    start_date: dt.date,
    end_date: dt.date,
    adjustflag: str = "2"
) -> pd.DataFrame:
    """
    批量拉取全 A 股日线数据
    symbols: ["600000.SH", "000001.SZ", ...] — 内部格式
    adjustflag: "1"=后复权, "2"=前复权, "3"=不复权
    返回 DataFrame with columns: date, code, symbol, open, high, low, close, 
                                   preclose, volume, amount, adjustflag, turn, 
                                   tradestatus, pctChg, peTTM, pbMRQ, isST
    """
    if not symbols:
        return pd.DataFrame()
    
    bs_codes = [convert_symbol_to_baostock_code(s) for s in symbols]
    code_str = ','.join(bs_codes)
    
    fields = "date,code,open,high,low,close,preclose,volume,amount,adjustflag,turn,tradestatus,pctChg,peTTM,pbMRQ,psTTM,pcfNcfTTM,isST"
    
    rs = bs.query_history_k_data_plus(
        code_str,
        fields,
        start_date=start_date.strftime("%Y-%m-%d"),
        end_date=end_date.strftime("%Y-%m-%d"),
        frequency="d",
        adjustflag=adjustflag
    )
    
    rows = []
    while (rs.error_code == '0') and rs.next():
        rows.append(rs.get_row_data())
    
    if not rows:
        return pd.DataFrame()
    
    df = pd.DataFrame(rows, columns=rs.fields)
    
    numeric_cols = ['open', 'high', 'low', 'close', 'preclose', 'volume', 'amount', 'turn', 'pctChg', 'peTTM', 'pbMRQ']
    for col in numeric_cols:
        if col in df.columns:
            df[col] = pd.to_numeric(df[col], errors='coerce')
    
    df['date'] = pd.to_datetime(df['date']).dt.date
    df['symbol'] = df['code'].apply(convert_baostock_code_to_symbol)
    
    df = df[df['tradestatus'] == '1'].copy()
    
    return df

def fetch_daily_k_data_all_stocks(
    symbols: list[str],
    start_date: dt.date,
    end_date: dt.date,
    chunk_size: int = 1000
) -> pd.DataFrame:
    """分块拉取全 A 股日线数据"""
    all_dfs = []
    total = len(symbols)
    
    for i in range(0, total, chunk_size):
        chunk = symbols[i:i + chunk_size]
        logger.info(f"Fetching batch {i//chunk_size + 1}/{(total + chunk_size - 1)//chunk_size}: {len(chunk)} stocks")
        
        try:
            df = fetch_daily_k_data_batch(chunk, start_date, end_date)
            if not df.empty:
                all_dfs.append(df)
        except Exception as e:
            logger.warning(f"Batch {i//chunk_size + 1} failed: {e}")
        
        if i + chunk_size < total:
            time.sleep(0.1)
    
    if not all_dfs:
        return pd.DataFrame()
    
    return pd.concat(all_dfs, ignore_index=True)

# 复权因子
def fetch_adjust_factors(symbol: str, start_date: dt.date, end_date: dt.date) -> dict[dt.date, dict[str, Optional[float]]]:
    """获取单只股票的复权因子"""
    bs_code = convert_symbol_to_baostock_code(symbol)
    
    rs = bs.query_adjust_factor(
        bs_code,
        start_date=start_date.strftime("%Y-%m-%d"),
        end_date=end_date.strftime("%Y-%m-%d")
    )
    
    result = {}
    while (rs.error_code == '0') and rs.next():
        row = rs.get_row_data()
        date = dt.date.fromisoformat(row[1])
        result[date] = {
            "pre_adjust_factor": float(row[2]) if row[2] else None,
            "post_adjust_factor": float(row[3]) if row[3] else None,
        }
    
    return result

# Baostock 字段 → daily_bar 标准字段映射
def normalize_baostock_frame(df: pd.DataFrame) -> pd.DataFrame:
    """
    将 Baostock 原始字段映射为 daily_bar 标准字段。
    输入: date, code, open, high, low, close, preclose, volume, amount,
          turn, tradestatus, pctChg, peTTM, pbMRQ, isST
    输出: symbol, trade_date, open, high, low, close, pre_close, volume,
          turnover, change_pct, change_amt, amplitude, turnover_rate,
          pe_ratio, pb_ratio, market_cap, circulating_market_cap,
          pre_adjust_factor, post_adjust_factor, data_source
    """
    if df.empty:
        return df

    normalized = df.copy()

    # 代码转换
    if "code" in normalized.columns:
        normalized["symbol"] = normalized["code"].apply(
            lambda x: convert_baostock_code_to_symbol(x) if isinstance(x, str) else x
        )

    # 日期字段
    normalized["trade_date"] = pd.to_datetime(normalized["date"]).dt.date

    # 数值转换
    numeric_fields = ["open", "high", "low", "close", "preclose", "volume", "amount", "turn", "pctChg",
                      "peTTM", "pbMRQ", "psTTM", "pcfNcfTTM"]
    for col in numeric_fields:
        if col in normalized.columns:
            normalized[col] = pd.to_numeric(normalized[col], errors="coerce")

    # 直接映射
    normalized["pre_close"] = normalized["preclose"]
    normalized["turnover"] = normalized["amount"]
    normalized["change_pct"] = normalized["pctChg"]
    normalized["turnover_rate"] = normalized["turn"]
    normalized["pe_ratio"] = normalized["peTTM"]
    normalized["pb_ratio"] = normalized["pbMRQ"]
    # 新增: 市销率, 市现率, ST标记
    if "psTTM" in normalized.columns:
        normalized["ps_ratio"] = normalized["psTTM"]
    if "pcfNcfTTM" in normalized.columns:
        normalized["pcf_ratio"] = normalized["pcfNcfTTM"]
    if "isST" in normalized.columns:
        normalized["is_st"] = normalized["isST"]

    # 可推算字段
    valid_preclose = normalized["pre_close"].notna() & (normalized["pre_close"] > 0)
    normalized["change_amt"] = (normalized["close"] - normalized["pre_close"]).round(4)
    normalized["amplitude"] = None
    normalized.loc[valid_preclose, "amplitude"] = (
        (normalized.loc[valid_preclose, "high"] - normalized.loc[valid_preclose, "low"])
        / normalized.loc[valid_preclose, "pre_close"] * 100
    ).round(4)

    # 缺失字段（Baostock 不提供）
    normalized["market_cap"] = None
    normalized["circulating_market_cap"] = None
    normalized["pre_adjust_factor"] = None
    normalized["post_adjust_factor"] = None

    # 数据源标识
    normalized["data_source"] = "BAOSTOCK"

    # 去除非交易日
    if "tradestatus" in normalized.columns:
        normalized = normalized[normalized["tradestatus"] == "1"].copy()

    # 输出标准列
    output_columns = [
        "symbol", "trade_date",
        "open", "high", "low", "close", "pre_close",
        "volume", "turnover", "change_pct", "change_amt", "amplitude",
        "turnover_rate", "pe_ratio", "pb_ratio",
        "market_cap", "circulating_market_cap",
        "pre_adjust_factor", "post_adjust_factor",
        "data_source",
    ]
    # 可选新增字段
    for opt_col in ["ps_ratio", "pcf_ratio", "is_st"]:
        if opt_col in normalized.columns:
            output_columns.append(opt_col)
    available_columns = [col for col in output_columns if col in normalized.columns]
    return normalized[available_columns].copy()

# 连接管理
def login() -> bool:
    """登录 Baostock"""
    lg = bs.login()
    if lg.error_code != '0':
        logger.error(f"Baostock login failed: {lg.error_msg}")
        return False
    logger.info("Baostock login success")
    return True

def logout():
    """登出 Baostock"""
    bs.logout()
    logger.info("Baostock logout")