from __future__ import annotations

import argparse
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
import datetime as dt
import os
import sys
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Set, Tuple

import akshare as ak
import jqdatasdk as jq
import pymysql
import pandas as pd


PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools.history_start_policy import resolve_history_date_bounds


MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
    "autocommit": False,
}

DEFAULT_JQ_USER = os.getenv("JQ_USERNAME") or "13552314165"
DEFAULT_JQ_PASS = os.getenv("JQ_PASSWORD") or "xiaoyu21A"
PROGRESS_HEARTBEAT_SECONDS = 30


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="按 AkShare 历史报表增量回填财务数据到 financial_indicator")
    parser.add_argument("--start-year", type=int, default=None, help="回填起始年份，默认与统一历史起点 2015-01-01 对齐")
    parser.add_argument("--end-year", type=int, default=None, help="回填结束年份，默认与 daily_bar 最晚日期对齐")
    parser.add_argument("--limit", type=int, default=10000, help="单次聚宽查询返回上限")
    parser.add_argument("--workers", type=int, default=8, help="财报抓取并发线程数，默认 8")
    parser.add_argument(
        "--repair-dividend-fields",
        "--repair-null-fields",
        action="store_true",
        dest="repair_dividend_fields",
        help="仅回填分红与空值字段，不重新抓取财报历史",
    )
    return parser.parse_args()


def auth_jq() -> None:
    jq.auth(DEFAULT_JQ_USER, DEFAULT_JQ_PASS)


def jq_to_local_symbol(code: str) -> Optional[str]:
    if code.endswith(".XSHE"):
        return code[:-5] + ".SZ"
    if code.endswith(".XSHG"):
        return code[:-5] + ".SH"
    return None


def report_type_from_stat_date(stat_date: dt.date) -> str:
    if stat_date.month == 3:
        return "Q1"
    if stat_date.month == 6:
        return "Q2"
    if stat_date.month == 9:
        return "Q3"
    if stat_date.month == 12:
        return "FY"
    return "Q4"


def safe_ratio(numerator, denominator):
    if numerator is None or denominator in (None, 0):
        return None
    try:
        return float(numerator) / float(denominator)
    except Exception:
        return None


def get_connection():
    return pymysql.connect(**MYSQL_CONFIG)


def load_symbol_map(cursor) -> Dict[str, int]:
    cursor.execute("SELECT symbol, symbol_id FROM symbol_info")
    return {row[0]: int(row[1]) for row in cursor.fetchall()}


def load_backfill_symbols(cursor) -> List[Tuple[str, int, Optional[dt.date]]]:
    cursor.execute(
        """
        SELECT
            si.symbol,
            si.symbol_id,
            MAX(fi.report_date) AS latest_report_date
        FROM symbol_info si
        LEFT JOIN financial_indicator fi
            ON fi.symbol_id = si.symbol_id
        WHERE si.asset_class = 'STOCK'
          AND UPPER(COALESCE(si.status, 'ACTIVE')) <> 'DELISTED'
        GROUP BY si.symbol, si.symbol_id
        ORDER BY si.symbol
        """
    )
    symbols: List[Tuple[str, int, Optional[dt.date]]] = []
    for symbol, symbol_id, latest_report_date in cursor.fetchall():
        local_symbol = str(symbol)
        if local_to_akshare_symbol(local_symbol) is None:
            continue
        symbols.append((local_symbol, int(symbol_id), latest_report_date))
    return symbols


def load_latest_trade_date(cursor) -> Optional[dt.date]:
    cursor.execute("SELECT MAX(trade_date) FROM daily_bar")
    row = cursor.fetchone()
    return row[0] if row and row[0] else None


def load_daily_trade_date_bounds(cursor) -> Tuple[dt.date, dt.date]:
    cursor.execute("SELECT MIN(trade_date), MAX(trade_date) FROM daily_bar")
    row = cursor.fetchone()
    if not row or row[0] is None or row[1] is None:
        raise RuntimeError("daily_bar 为空，无法对齐财报起止日期")
    return resolve_history_date_bounds(row[0], row[1], "daily_bar")


def local_to_akshare_symbol(symbol: str) -> Optional[str]:
    if symbol.endswith(".SZ"):
        return f"SZ{symbol[:6]}"
    if symbol.endswith(".SH"):
        return f"SH{symbol[:6]}"
    if symbol.endswith(".BJ"):
        return f"BJ{symbol[:6]}"
    return None


def normalize_report_type_label(report_type: object, report_date: Optional[dt.date]) -> str:
    text = str(report_type or "").strip().upper()
    if text in {"Q1", "一季报", "第一季报", "1Q"}:
        return "Q1"
    if text in {"Q2", "中报", "半年报", "半年度报告", "2Q"}:
        return "Q2"
    if text in {"Q3", "三季报", "第三季报", "3Q"}:
        return "Q3"
    if text in {"Q4", "FY", "年报", "年度报告", "ANNUAL"}:
        return "FY"
    if report_date is not None:
        return report_type_from_stat_date(report_date)
    return "FY"


def to_float(value: object) -> Optional[float]:
    normalized = normalize_value(value)
    if normalized is None:
        return None
    try:
        return float(normalized)
    except Exception:
        return None


def parse_report_date(value: object) -> Optional[dt.date]:
    if value is None:
        return None
    if isinstance(value, dt.datetime):
        return value.date()
    if isinstance(value, dt.date):
        return value
    text = str(value).strip()
    if not text:
        return None
    try:
        return dt.date.fromisoformat(text[:10])
    except Exception:
        return None


def frame_row_by_report_date(df: Optional[pd.DataFrame], date_column: str = "REPORT_DATE") -> Dict[dt.date, pd.Series]:
    rows: Dict[dt.date, pd.Series] = {}
    if df is None or getattr(df, "empty", True):
        return rows
    for _, row in df.iterrows():
        report_date = parse_report_date(row.get(date_column))
        if report_date is None:
            continue
        rows[report_date] = row
    return rows


def frame_metric_values_by_report_date(
    df: Optional[pd.DataFrame],
    metric_names: Iterable[str],
) -> Dict[str, Dict[dt.date, Optional[float]]]:
    values: Dict[str, Dict[dt.date, Optional[float]]] = {str(name): {} for name in metric_names}
    if df is None or getattr(df, "empty", True):
        return values

    metric_name_set = {str(name) for name in metric_names}
    report_columns: Dict[dt.date, str] = {}
    for column in df.columns:
        text = str(column).strip()
        if len(text) == 8 and text.isdigit():
            report_date = parse_report_date(f"{text[:4]}-{text[4:6]}-{text[6:8]}")
            if report_date is not None:
                report_columns[report_date] = column

    if not report_columns:
        return values

    for _, row in df.iterrows():
        metric_name = str(row.get("指标") or "").strip()
        if metric_name not in metric_name_set:
            continue
        metric_values = values.setdefault(metric_name, {})
        for report_date, column in report_columns.items():
            metric_values[report_date] = to_float(row.get(column))
    return values


def resolve_effective_disclosure_date(*rows: Optional[pd.Series]) -> Optional[dt.date]:
    candidates: List[dt.date] = []
    for row in rows:
        if row is None:
            continue
        notice_date = parse_report_date(row.get("NOTICE_DATE"))
        if notice_date is not None:
            candidates.append(notice_date)
    return min(candidates) if candidates else None


def table_exists(cursor, table_name: str) -> bool:
    cursor.execute(
        """
        SELECT COUNT(*)
        FROM information_schema.tables
        WHERE table_schema = DATABASE()
          AND table_name = %s
        """,
        (table_name,),
    )
    return int(cursor.fetchone()[0] or 0) > 0


def load_table_columns(cursor, table_name: str) -> Set[str]:
    if not table_exists(cursor, table_name):
        return set()
    cursor.execute(
        """
        SELECT column_name
        FROM information_schema.columns
        WHERE table_schema = DATABASE()
          AND table_name = %s
        """,
        (table_name,),
    )
    return {str(row[0]) for row in cursor.fetchall()}


def ensure_columns(cursor, table_name: str, ddl_by_column: Dict[str, str]) -> None:
    existing_columns = load_table_columns(cursor, table_name)
    for column_name, ddl in ddl_by_column.items():
        if column_name in existing_columns:
            continue
        cursor.execute(f"ALTER TABLE {table_name} ADD COLUMN {ddl}")


def ensure_financial_tables(cursor) -> None:
    cursor.execute(
        """
        CREATE TABLE IF NOT EXISTS financial_indicator (
            indicator_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '指标ID',
            symbol_id INT UNSIGNED NOT NULL COMMENT '标的ID',
            report_date DATE NOT NULL COMMENT '报告期',
            report_type ENUM('Q1', 'Q2', 'Q3', 'Q4', 'FY') NOT NULL COMMENT '报告类型',
            effective_disclosure_date DATE DEFAULT NULL COMMENT '实际披露日期',
            eps DECIMAL(10, 4) DEFAULT NULL COMMENT '每股收益',
            bps DECIMAL(10, 4) DEFAULT NULL COMMENT '每股净资产',
            roa DECIMAL(8, 4) DEFAULT NULL COMMENT '总资产收益率',
            roe DECIMAL(8, 4) DEFAULT NULL COMMENT '净资产收益率',
            profit_margin DECIMAL(8, 4) DEFAULT NULL COMMENT '净利率',
            gross_margin DECIMAL(8, 4) DEFAULT NULL COMMENT '毛利率',
            operating_margin DECIMAL(8, 4) DEFAULT NULL COMMENT '营业利润率',
            debt_to_equity DECIMAL(8, 4) DEFAULT NULL COMMENT '资产负债率',
            current_ratio DECIMAL(8, 4) DEFAULT NULL COMMENT '流动比率',
            quick_ratio DECIMAL(8, 4) DEFAULT NULL COMMENT '速动比率',
            operating_cash_flow DECIMAL(20, 4) DEFAULT NULL COMMENT '经营活动现金流',
            investing_cash_flow DECIMAL(20, 4) DEFAULT NULL COMMENT '投资活动现金流',
            financing_cash_flow DECIMAL(20, 4) DEFAULT NULL COMMENT '筹资活动现金流',
            total_revenue DECIMAL(20, 4) DEFAULT NULL COMMENT '营业收入',
            net_profit DECIMAL(20, 4) DEFAULT NULL COMMENT '净利润',
            total_assets DECIMAL(20, 4) DEFAULT NULL COMMENT '总资产',
            total_liabilities DECIMAL(20, 4) DEFAULT NULL COMMENT '总负债',
            equity DECIMAL(20, 4) DEFAULT NULL COMMENT '所有者权益',
            dividend_yield DECIMAL(12, 6) DEFAULT NULL COMMENT '股息率',
            payout_ratio DECIMAL(12, 6) DEFAULT NULL COMMENT '分红支付率',
            dividend_stability DECIMAL(8, 4) DEFAULT NULL COMMENT '分红稳定性',
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
            PRIMARY KEY (indicator_id),
            UNIQUE KEY uk_symbol_report (symbol_id, report_date, report_type),
            KEY idx_symbol_id (symbol_id),
            KEY idx_report_date (report_date),
            CONSTRAINT fk_financial_symbol FOREIGN KEY (symbol_id) REFERENCES symbol_info (symbol_id) ON DELETE CASCADE
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='财务指标表'
        """
    )
    cursor.execute(
        """
        CREATE TABLE IF NOT EXISTS financial_indicator_daily (
            indicator_daily_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '日频指标ID',
            symbol_id INT UNSIGNED NOT NULL COMMENT '标的ID',
            trade_date DATE NOT NULL COMMENT '交易日期',
            report_date DATE NOT NULL COMMENT '原始报告期',
            report_type ENUM('Q1', 'Q2', 'Q3', 'Q4', 'FY') NOT NULL COMMENT '原始报告类型',
            effective_disclosure_date DATE DEFAULT NULL COMMENT '实际披露日期',
            eps DECIMAL(10, 4) DEFAULT NULL COMMENT '每股收益',
            bps DECIMAL(10, 4) DEFAULT NULL COMMENT '每股净资产',
            roa DECIMAL(8, 4) DEFAULT NULL COMMENT '总资产收益率',
            roe DECIMAL(8, 4) DEFAULT NULL COMMENT '净资产收益率',
            profit_margin DECIMAL(8, 4) DEFAULT NULL COMMENT '净利率',
            gross_margin DECIMAL(8, 4) DEFAULT NULL COMMENT '毛利率',
            operating_margin DECIMAL(8, 4) DEFAULT NULL COMMENT '营业利润率',
            debt_to_equity DECIMAL(8, 4) DEFAULT NULL COMMENT '资产负债率',
            current_ratio DECIMAL(8, 4) DEFAULT NULL COMMENT '流动比率',
            quick_ratio DECIMAL(8, 4) DEFAULT NULL COMMENT '速动比率',
            operating_cash_flow DECIMAL(20, 4) DEFAULT NULL COMMENT '经营活动现金流',
            investing_cash_flow DECIMAL(20, 4) DEFAULT NULL COMMENT '投资活动现金流',
            financing_cash_flow DECIMAL(20, 4) DEFAULT NULL COMMENT '筹资活动现金流',
            total_revenue DECIMAL(20, 4) DEFAULT NULL COMMENT '营业收入',
            net_profit DECIMAL(20, 4) DEFAULT NULL COMMENT '净利润',
            total_assets DECIMAL(20, 4) DEFAULT NULL COMMENT '总资产',
            total_liabilities DECIMAL(20, 4) DEFAULT NULL COMMENT '总负债',
            equity DECIMAL(20, 4) DEFAULT NULL COMMENT '所有者权益',
            dividend_yield DECIMAL(12, 6) DEFAULT NULL COMMENT '股息率',
            payout_ratio DECIMAL(12, 6) DEFAULT NULL COMMENT '分红支付率',
            dividend_stability DECIMAL(8, 4) DEFAULT NULL COMMENT '分红稳定性',
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
            PRIMARY KEY (indicator_daily_id),
            UNIQUE KEY uk_symbol_trade_date (symbol_id, trade_date),
            KEY idx_symbol_trade_date (symbol_id, trade_date),
            KEY idx_trade_date (trade_date),
            KEY idx_report_date (report_date),
            CONSTRAINT fk_financial_daily_symbol FOREIGN KEY (symbol_id) REFERENCES symbol_info (symbol_id) ON DELETE CASCADE
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='财务指标日频对齐表'
        """
    )

    ensure_columns(
        cursor,
        "financial_indicator",
        {
            "effective_disclosure_date": "effective_disclosure_date DATE DEFAULT NULL COMMENT '实际披露日期' AFTER report_type",
            "gross_margin": "gross_margin DECIMAL(8, 4) DEFAULT NULL COMMENT '毛利率' AFTER profit_margin",
            "operating_margin": "operating_margin DECIMAL(8, 4) DEFAULT NULL COMMENT '营业利润率' AFTER gross_margin",
            "dividend_yield": "dividend_yield DECIMAL(12, 6) DEFAULT NULL COMMENT '股息率' AFTER equity",
            "payout_ratio": "payout_ratio DECIMAL(12, 6) DEFAULT NULL COMMENT '分红支付率' AFTER dividend_yield",
            "dividend_stability": "dividend_stability DECIMAL(8, 4) DEFAULT NULL COMMENT '分红稳定性' AFTER payout_ratio",
        },
    )
    ensure_columns(
        cursor,
        "financial_indicator_daily",
        {
            "effective_disclosure_date": "effective_disclosure_date DATE DEFAULT NULL COMMENT '实际披露日期' AFTER report_type",
            "gross_margin": "gross_margin DECIMAL(8, 4) DEFAULT NULL COMMENT '毛利率' AFTER profit_margin",
            "operating_margin": "operating_margin DECIMAL(8, 4) DEFAULT NULL COMMENT '营业利润率' AFTER gross_margin",
            "dividend_yield": "dividend_yield DECIMAL(12, 6) DEFAULT NULL COMMENT '股息率' AFTER equity",
            "payout_ratio": "payout_ratio DECIMAL(12, 6) DEFAULT NULL COMMENT '分红支付率' AFTER dividend_yield",
            "dividend_stability": "dividend_stability DECIMAL(8, 4) DEFAULT NULL COMMENT '分红稳定性' AFTER payout_ratio",
        },
    )


def fetch_akshare_financial_frames(symbol: str, start_year: int) -> Tuple[pd.DataFrame, pd.DataFrame, pd.DataFrame, pd.DataFrame, pd.DataFrame]:
    ak_symbol = local_to_akshare_symbol(symbol)
    if ak_symbol is None:
        return pd.DataFrame(), pd.DataFrame(), pd.DataFrame(), pd.DataFrame(), pd.DataFrame()

    code = symbol.split(".", 1)[0]
    balance_df = ak.stock_balance_sheet_by_report_em(symbol=ak_symbol)
    profit_df = ak.stock_profit_sheet_by_report_em(symbol=ak_symbol)
    cash_df = ak.stock_cash_flow_sheet_by_report_em(symbol=ak_symbol)
    analysis_df = ak.stock_financial_analysis_indicator(symbol=code, start_year=str(start_year))
    abstract_df = ak.stock_financial_abstract(symbol=code)
    return balance_df, profit_df, cash_df, analysis_df, abstract_df


def build_akshare_rows(
    symbol_id: int,
    symbol: str,
    balance_df: pd.DataFrame,
    profit_df: pd.DataFrame,
    cash_df: pd.DataFrame,
    analysis_df: pd.DataFrame,
    abstract_df: pd.DataFrame,
    start_year: int,
    end_year: int,
) -> List[dict]:
    rows: List[dict] = []
    if balance_df is None or getattr(balance_df, "empty", True):
        return rows

    profit_by_date = frame_row_by_report_date(profit_df)
    cash_by_date = frame_row_by_report_date(cash_df)
    analysis_by_date = frame_row_by_report_date(analysis_df, date_column="日期")
    abstract_metric_values = frame_metric_values_by_report_date(abstract_df, ["毛利率", "营业利润率"])
    gross_margin_by_date = abstract_metric_values.get("毛利率", {})
    operating_margin_by_date = abstract_metric_values.get("营业利润率", {})

    for _, balance_row in balance_df.iterrows():
        report_date = parse_report_date(balance_row.get("REPORT_DATE"))
        if report_date is None or report_date.year < start_year or report_date.year > end_year:
            continue

        report_type = normalize_report_type_label(balance_row.get("REPORT_TYPE"), report_date)
        profit_row = profit_by_date.get(report_date)
        cash_row = cash_by_date.get(report_date)
        analysis_row = analysis_by_date.get(report_date)

        total_assets = to_float(balance_row.get("TOTAL_ASSETS"))
        total_liabilities = to_float(balance_row.get("TOTAL_LIABILITIES"))
        total_equity = to_float(balance_row.get("TOTAL_EQUITY")) or to_float(balance_row.get("TOTAL_PARENT_EQUITY"))
        current_assets = to_float(balance_row.get("TOTAL_CURRENT_ASSETS"))
        current_liability = to_float(balance_row.get("TOTAL_CURRENT_LIAB"))
        share_capital = to_float(balance_row.get("SHARE_CAPITAL"))

        total_revenue = to_float(profit_row.get("TOTAL_OPERATE_INCOME")) if profit_row is not None else None
        if total_revenue is None and profit_row is not None:
            total_revenue = to_float(profit_row.get("OPERATE_INCOME"))
        net_profit = to_float(profit_row.get("NETPROFIT")) if profit_row is not None else None
        if net_profit is None and profit_row is not None:
            net_profit = to_float(profit_row.get("PARENT_NETPROFIT"))

        operating_cash_flow = to_float(cash_row.get("NETCASH_OPERATE")) if cash_row is not None else None
        investing_cash_flow = to_float(cash_row.get("NETCASH_INVEST")) if cash_row is not None else None
        financing_cash_flow = to_float(cash_row.get("NETCASH_FINANCE")) if cash_row is not None else None

        eps = to_float(analysis_row.get("摊薄每股收益(元)")) if analysis_row is not None else None
        if eps is None and profit_row is not None:
            eps = to_float(profit_row.get("BASIC_EPS"))

        bps = to_float(analysis_row.get("每股净资产_调整后(元)")) if analysis_row is not None else None
        if bps is None:
            bps = safe_ratio(total_equity, share_capital)

        roa = to_float(analysis_row.get("总资产净利润率(%)")) if analysis_row is not None else None
        if roa is None and total_assets not in (None, 0) and net_profit is not None:
            roa = safe_ratio(net_profit, total_assets) * 100.0

        roe = to_float(analysis_row.get("净资产收益率(%)")) if analysis_row is not None else None
        if roe is None and total_equity not in (None, 0) and net_profit is not None:
            roe = safe_ratio(net_profit, total_equity) * 100.0

        profit_margin = to_float(analysis_row.get("销售净利率(%)")) if analysis_row is not None else None
        if profit_margin is None and total_revenue not in (None, 0) and net_profit is not None:
            profit_margin = safe_ratio(net_profit, total_revenue) * 100.0

        gross_margin = gross_margin_by_date.get(report_date)
        if gross_margin is None and profit_row is not None:
            operating_income = to_float(profit_row.get("OPERATE_INCOME"))
            operating_cost = to_float(profit_row.get("OPERATE_COST"))
            if operating_income not in (None, 0) and operating_cost is not None:
                gross_margin = safe_ratio(operating_income - operating_cost, operating_income) * 100.0

        operating_margin = operating_margin_by_date.get(report_date)
        if operating_margin is None and total_revenue not in (None, 0) and profit_row is not None:
            operating_profit = to_float(profit_row.get("OPERATE_PROFIT"))
            if operating_profit is not None:
                operating_margin = safe_ratio(operating_profit, total_revenue) * 100.0

        effective_disclosure_date = resolve_effective_disclosure_date(balance_row, profit_row, cash_row)

        debt_to_equity = None
        if total_liabilities is not None and total_equity not in (None, 0):
            debt_to_equity = safe_ratio(total_liabilities, total_equity) * 100.0

        current_ratio = to_float(analysis_row.get("流动比率")) if analysis_row is not None else None
        if current_ratio is None and current_assets is not None and current_liability not in (None, 0):
            current_ratio = safe_ratio(current_assets, current_liability)

        quick_ratio = to_float(analysis_row.get("速动比率")) if analysis_row is not None else None
        if quick_ratio is None:
            quick_ratio = current_ratio

        rows.append(
            {
                "symbol_id": symbol_id,
                "report_date": report_date,
                "report_type": report_type,
                "effective_disclosure_date": effective_disclosure_date,
                "eps": clamp_decimal(eps, 999999.9999),
                "bps": clamp_decimal(bps, 999999.9999),
                "roa": clamp_decimal(roa, 9999.9999),
                "roe": clamp_decimal(roe, 9999.9999),
                "profit_margin": clamp_decimal(profit_margin, 9999.9999),
                "gross_margin": clamp_decimal(gross_margin, 9999.9999),
                "operating_margin": clamp_decimal(operating_margin, 9999.9999),
                "debt_to_equity": clamp_decimal(debt_to_equity, 9999.9999),
                "current_ratio": clamp_decimal(current_ratio, 9999.9999),
                "quick_ratio": clamp_decimal(quick_ratio, 9999.9999),
                "operating_cash_flow": clamp_decimal(operating_cash_flow, 9999999999999999.9999),
                "investing_cash_flow": clamp_decimal(investing_cash_flow, 9999999999999999.9999),
                "financing_cash_flow": clamp_decimal(financing_cash_flow, 9999999999999999.9999),
                "total_revenue": clamp_decimal(total_revenue, 9999999999999999.9999),
                "net_profit": clamp_decimal(net_profit, 9999999999999999.9999),
                "total_assets": clamp_decimal(total_assets, 9999999999999999.9999),
                "total_liabilities": clamp_decimal(total_liabilities, 9999999999999999.9999),
                "equity": clamp_decimal(total_equity, 9999999999999999.9999),
                "dividend_yield": None,
                "payout_ratio": None,
                "dividend_stability": None,
            }
        )

    return rows


def build_report_period_labels(start_year: int, end_year: int) -> List[str]:
    labels: List[str] = []
    for year in range(start_year, end_year + 1):
        labels.append(f"{year}")
        labels.append(f"{year}q1")
        labels.append(f"{year}q2")
        labels.append(f"{year}q3")
    return labels


def build_expected_report_dates(start_year: int, end_year: int) -> List[dt.date]:
    report_dates: List[dt.date] = []
    if start_year > end_year:
        return report_dates
    for year in range(start_year, end_year + 1):
        report_dates.extend(
            [
                dt.date(year, 3, 31),
                dt.date(year, 6, 30),
                dt.date(year, 9, 30),
                dt.date(year, 12, 31),
            ]
        )
    return report_dates


def load_existing_report_dates(
    cursor, start_date: dt.date, end_date: dt.date
) -> Dict[int, Set[dt.date]]:
    cursor.execute(
        """
        SELECT symbol_id, report_date
        FROM financial_indicator
        WHERE report_date BETWEEN %s AND %s
        """,
        (start_date, end_date),
    )
    existing_dates: Dict[int, Set[dt.date]] = {}
    for symbol_id, report_date in cursor.fetchall():
        if report_date is None:
            continue
        existing_dates.setdefault(int(symbol_id), set()).add(report_date)
    return existing_dates


def fetch_financial_snapshot(anchor_date: dt.date, limit: int):
    query = jq.query(
        jq.indicator.code,
        jq.indicator.statDate,
        jq.indicator.roe,
        jq.indicator.roa,
        jq.indicator.gross_profit_margin,
        jq.indicator.net_profit_margin,
        jq.income.basic_eps,
        jq.income.net_profit,
        jq.income.total_operating_revenue,
        jq.balance.total_assets,
        jq.balance.total_liability,
        jq.balance.total_sheet_owner_equities,
        jq.balance.paidin_capital,
        jq.balance.total_current_assets,
        jq.balance.total_current_liability,
        jq.cash_flow.net_operate_cash_flow,
        jq.cash_flow.net_invest_cash_flow,
        jq.cash_flow.net_finance_cash_flow,
    ).limit(limit)
    return jq.get_fundamentals(query, date=anchor_date.isoformat())


def fetch_financial_report(report_period: str, limit: int):
    query = jq.query(
        jq.indicator.code,
        jq.indicator.statDate,
        jq.indicator.roe,
        jq.indicator.roa,
        jq.indicator.gross_profit_margin,
        jq.indicator.net_profit_margin,
        jq.income.basic_eps,
        jq.income.net_profit,
        jq.income.total_operating_revenue,
        jq.balance.total_assets,
        jq.balance.total_liability,
        jq.balance.total_sheet_owner_equities,
        jq.balance.paidin_capital,
        jq.balance.total_current_assets,
        jq.balance.total_current_liability,
        jq.cash_flow.net_operate_cash_flow,
        jq.cash_flow.net_invest_cash_flow,
        jq.cash_flow.net_finance_cash_flow,
    ).limit(limit)
    return jq.get_fundamentals(query, statDate=report_period)


def normalize_value(value):
    if value != value:
        return None
    return value


def parse_stat_date_value(stat_date_raw):
    if stat_date_raw is None:
        return None
    if isinstance(stat_date_raw, dt.datetime):
        return stat_date_raw.date()
    if isinstance(stat_date_raw, dt.date):
        return stat_date_raw
    if isinstance(stat_date_raw, str):
        text = stat_date_raw.strip().lower()
        if len(text) == 4 and text.isdigit():
            return dt.date(int(text), 12, 31)
        if len(text) == 6 and text[:4].isdigit() and text[4] == "q" and text[5] in {"1", "2", "3", "4"}:
            year = int(text[:4])
            quarter = int(text[5])
            if quarter == 1:
                return dt.date(year, 3, 31)
            if quarter == 2:
                return dt.date(year, 6, 30)
            if quarter == 3:
                return dt.date(year, 9, 30)
            return dt.date(year, 12, 31)
        return dt.date.fromisoformat(text[:10])
    return stat_date_raw


def clamp_decimal(value, max_abs: float) -> Optional[float]:
    value = normalize_value(value)
    if value is None:
        return None
    try:
        numeric = float(value)
    except Exception:
        return None
    if numeric > max_abs:
        return max_abs
    if numeric < -max_abs:
        return -max_abs
    return numeric


def build_rows(df, symbol_map: Dict[str, int]) -> List[dict]:
    rows: List[dict] = []
    for _, item in df.iterrows():
        local_symbol = jq_to_local_symbol(str(item["code"]))
        if not local_symbol or local_symbol not in symbol_map:
            continue

        stat_date = parse_stat_date_value(item["statDate"])
        if stat_date is None:
            continue

        total_liability = normalize_value(item.get("total_liability"))
        total_equity = normalize_value(item.get("total_sheet_owner_equities"))
        paidin_capital = normalize_value(item.get("paidin_capital"))
        current_assets = normalize_value(item.get("total_current_assets"))
        current_liability = normalize_value(item.get("total_current_liability"))

        current_ratio = normalize_value(item.get("current_ratio"))
        if current_ratio is None:
            current_ratio = safe_ratio(current_assets, current_liability)

        quick_ratio = normalize_value(item.get("quick_ratio"))
        if quick_ratio is None:
            quick_ratio = current_ratio

        debt_to_equity = safe_ratio(total_liability, total_equity)
        if debt_to_equity is not None:
            debt_to_equity *= 100.0

        bps = safe_ratio(total_equity, paidin_capital)

        profit_margin = normalize_value(item.get("net_profit_margin"))
        if profit_margin is None:
            profit_margin = normalize_value(item.get("gross_profit_margin"))
        if profit_margin is not None:
            try:
                profit_margin = float(profit_margin)
            except Exception:
                profit_margin = None
        if profit_margin is not None and abs(profit_margin) > 9999.9999:
            gross_margin = normalize_value(item.get("gross_profit_margin"))
            try:
                gross_margin = float(gross_margin) if gross_margin is not None else None
            except Exception:
                gross_margin = None
            profit_margin = gross_margin if gross_margin is not None else profit_margin

        rows.append(
            {
                "symbol_id": symbol_map[local_symbol],
                "report_date": stat_date,
                "report_type": report_type_from_stat_date(stat_date),
                "effective_disclosure_date": None,
                "eps": clamp_decimal(item.get("basic_eps"), 999999.9999),
                "bps": clamp_decimal(bps, 999999.9999),
                "roa": clamp_decimal(item.get("roa"), 9999.9999),
                "roe": clamp_decimal(item.get("roe"), 9999.9999),
                "profit_margin": clamp_decimal(profit_margin, 9999.9999),
                "gross_margin": None,
                "operating_margin": None,
                "debt_to_equity": clamp_decimal(debt_to_equity, 9999.9999),
                "current_ratio": clamp_decimal(current_ratio, 9999.9999),
                "quick_ratio": clamp_decimal(quick_ratio, 9999.9999),
                "operating_cash_flow": clamp_decimal(item.get("net_operate_cash_flow"), 9999999999999999.9999),
                "investing_cash_flow": clamp_decimal(item.get("net_invest_cash_flow"), 9999999999999999.9999),
                "financing_cash_flow": clamp_decimal(item.get("net_finance_cash_flow"), 9999999999999999.9999),
                "total_revenue": clamp_decimal(item.get("total_operating_revenue"), 9999999999999999.9999),
                "net_profit": clamp_decimal(item.get("net_profit"), 9999999999999999.9999),
                "total_assets": clamp_decimal(item.get("total_assets"), 9999999999999999.9999),
                "total_liabilities": clamp_decimal(total_liability, 9999999999999999.9999),
                "equity": clamp_decimal(total_equity, 9999999999999999.9999),
                "dividend_yield": None,
                "payout_ratio": None,
                "dividend_stability": None,
            }
        )
    return rows


def upsert_rows(cursor, rows: Iterable[dict]) -> int:
    payload = list(rows)
    if not payload:
        return 0

    sql = """
    INSERT INTO financial_indicator (
        symbol_id, report_date, report_type, effective_disclosure_date,
        eps, bps, roa, roe, profit_margin, gross_margin, operating_margin,
        debt_to_equity, current_ratio, quick_ratio,
        operating_cash_flow, investing_cash_flow, financing_cash_flow,
        total_revenue, net_profit, total_assets, total_liabilities, equity,
        dividend_yield, payout_ratio, dividend_stability
    ) VALUES (
        %(symbol_id)s, %(report_date)s, %(report_type)s, %(effective_disclosure_date)s,
        %(eps)s, %(bps)s, %(roa)s, %(roe)s, %(profit_margin)s, %(gross_margin)s, %(operating_margin)s,
        %(debt_to_equity)s, %(current_ratio)s, %(quick_ratio)s,
        %(operating_cash_flow)s, %(investing_cash_flow)s, %(financing_cash_flow)s,
        %(total_revenue)s, %(net_profit)s, %(total_assets)s, %(total_liabilities)s, %(equity)s,
        %(dividend_yield)s, %(payout_ratio)s, %(dividend_stability)s
    )
    ON DUPLICATE KEY UPDATE
        effective_disclosure_date = VALUES(effective_disclosure_date),
        eps = VALUES(eps),
        bps = VALUES(bps),
        roa = VALUES(roa),
        roe = VALUES(roe),
        profit_margin = VALUES(profit_margin),
        gross_margin = VALUES(gross_margin),
        operating_margin = VALUES(operating_margin),
        debt_to_equity = VALUES(debt_to_equity),
        current_ratio = VALUES(current_ratio),
        quick_ratio = VALUES(quick_ratio),
        operating_cash_flow = VALUES(operating_cash_flow),
        investing_cash_flow = VALUES(investing_cash_flow),
        financing_cash_flow = VALUES(financing_cash_flow),
        total_revenue = VALUES(total_revenue),
        net_profit = VALUES(net_profit),
        total_assets = VALUES(total_assets),
        total_liabilities = VALUES(total_liabilities),
        equity = VALUES(equity),
        dividend_yield = VALUES(dividend_yield),
        payout_ratio = VALUES(payout_ratio),
        dividend_stability = VALUES(dividend_stability)
    """
    cursor.executemany(sql, payload)
    return len(payload)


def _resolve_worker_count(workers: int, task_count: int) -> int:
    if task_count <= 0:
        return 1
    return max(1, min(workers, task_count))


def _fetch_and_upsert_symbol_financial_history(
    symbol: str,
    symbol_id: int,
    start_year: int,
    end_year: int,
) -> Tuple[str, int, int]:
    balance_df, profit_df, cash_df, analysis_df, abstract_df = fetch_akshare_financial_frames(symbol, start_year)
    rows = build_akshare_rows(
        symbol_id,
        symbol,
        balance_df,
        profit_df,
        cash_df,
        analysis_df,
        abstract_df,
        start_year,
        end_year,
    )
    fetched_rows = len(rows)

    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            affected = upsert_rows(cursor, rows)
        conn.commit()
        return symbol, fetched_rows, affected
    finally:
        conn.close()


def _fetch_and_upsert_report_date(
    report_date: dt.date,
    missing_symbol_ids: Set[int],
    symbol_map: Dict[str, int],
    limit: int,
) -> Tuple[str, int, int]:
    report_df = fetch_financial_report(report_date.isoformat(), limit)
    rows = [
        row
        for row in build_rows(report_df, symbol_map)
        if row["symbol_id"] in missing_symbol_ids and row["report_date"] == report_date
    ]
    fetched_rows = len(rows)

    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            affected = upsert_rows(cursor, rows)
        conn.commit()
        return report_date.isoformat(), fetched_rows, affected
    finally:
        conn.close()


def load_abnormal_roe_repair_targets(
    cursor,
    roe_threshold: float,
) -> Tuple[List[Tuple[str, int, Set[dt.date]]], int, int]:
    cursor.execute(
        """
        SELECT
            fi.symbol_id,
            si.symbol,
            fi.report_date,
            fi.roe
        FROM financial_indicator fi
        JOIN symbol_info si
            ON si.symbol_id = fi.symbol_id
        WHERE fi.roe IS NULL
           OR ABS(fi.roe) > %s
        ORDER BY fi.symbol_id, fi.report_date
        """,
        (roe_threshold,),
    )

    grouped_targets: Dict[int, Dict[str, object]] = {}
    null_rows = 0
    abnormal_rows = 0

    for symbol_id, symbol, report_date, roe in cursor.fetchall():
        if report_date is None:
            continue
        if roe is None:
            null_rows += 1
        else:
            abnormal_rows += 1

        entry = grouped_targets.setdefault(
            int(symbol_id),
            {"symbol": str(symbol), "report_dates": set()},
        )
        report_dates = entry["report_dates"]
        if isinstance(report_dates, set):
            report_dates.add(report_date)

    targets: List[Tuple[str, int, Set[dt.date]]] = []
    for symbol_id, payload in grouped_targets.items():
        symbol = str(payload["symbol"])
        report_dates = payload["report_dates"]
        if not isinstance(report_dates, set) or not report_dates:
            continue
        targets.append((symbol, symbol_id, report_dates))

    targets.sort(key=lambda item: item[0])
    return targets, null_rows, abnormal_rows


def _repair_symbol_roe_rows(
    symbol: str,
    symbol_id: int,
    report_dates: Set[dt.date],
) -> Tuple[str, int, int]:
    if not report_dates:
        return symbol, 0, 0

    start_year = min(report_date.year for report_date in report_dates)
    end_year = max(report_date.year for report_date in report_dates)

    try:
        balance_df, profit_df, cash_df, analysis_df, abstract_df = fetch_akshare_financial_frames(symbol, start_year)
        rows = build_akshare_rows(
            symbol_id,
            symbol,
            balance_df,
            profit_df,
            cash_df,
            analysis_df,
            abstract_df,
            start_year,
            end_year,
        )
    except Exception as exc:
        print(f"[repair][roe-skip] symbol={symbol} error={exc}", flush=True)
        return symbol, len(report_dates), 0

    row_by_report_date = {
        row["report_date"]: row
        for row in rows
        if row["report_date"] in report_dates
    }

    updates = []
    for report_date in sorted(report_dates):
        row = row_by_report_date.get(report_date)
        if row is None:
            continue
        roe = row.get("roe")
        if roe is None:
            continue
        updates.append((roe, symbol_id, report_date))

    if not updates:
        return symbol, len(report_dates), 0

    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            cursor.executemany(
                """
                UPDATE financial_indicator
                SET roe = %s
                WHERE symbol_id = %s
                  AND report_date = %s
                """,
                updates,
            )
        conn.commit()
        return symbol, len(report_dates), len(updates)
    finally:
        conn.close()


def repair_abnormal_roe_fields(roe_threshold: float = 100.0, workers: int = 8) -> Tuple[int, int, int, int, int, int]:
    conn = get_connection()
    target_date: Optional[dt.date] = None
    try:
        with conn.cursor() as cursor:
            ensure_financial_tables(cursor)
            target_date = load_latest_trade_date(cursor)
            targets, null_rows, abnormal_rows = load_abnormal_roe_repair_targets(cursor, roe_threshold)
    finally:
        conn.close()

    if not targets:
        print(f"[repair][roe] no abnormal/null roe rows found threshold={roe_threshold}", flush=True)
        return 0, 0, 0, 0, 0, 0

    total_target_dates = sum(len(report_dates) for _, _, report_dates in targets)
    resolved_workers = _resolve_worker_count(workers, len(targets))
    print(
        f"[repair][roe] threshold={roe_threshold} symbol_count={len(targets)} target_dates={total_target_dates} null_rows={null_rows} abnormal_rows={abnormal_rows} workers={resolved_workers}",
        flush=True,
    )

    total_updated_rows = 0
    with ThreadPoolExecutor(max_workers=resolved_workers) as executor:
        future_to_symbol = {
            executor.submit(_repair_symbol_roe_rows, symbol, symbol_id, report_dates): symbol
            for symbol, symbol_id, report_dates in targets
        }

        for completed_count, future in enumerate(as_completed(future_to_symbol), start=1):
            symbol = future_to_symbol[future]
            try:
                completed_symbol, target_count, updated_rows = future.result()
                total_updated_rows += updated_rows
                if completed_count == 1 or completed_count % 50 == 0:
                    print(
                        f"[repair][roe-progress] completed={completed_count}/{len(future_to_symbol)} total_updated_rows={total_updated_rows}",
                        flush=True,
                    )
                print(
                    f"[repair][roe-upsert] symbol={completed_symbol} target_dates={target_count} updated_rows={updated_rows}",
                    flush=True,
                )
            except Exception as exc:
                print(f"[repair][roe-skip] symbol={symbol}: {exc}", flush=True)

    aligned_rows = 0
    if target_date is not None:
        aligned_rows = backfill_financial_daily_alignment(target_date)
        print(f"[repair][roe-align] aligned_rows={aligned_rows}", flush=True)
    else:
        print("[repair][roe-align] daily_bar 为空，跳过财报日频对齐", flush=True)

    print(
        f"[repair][roe-done] threshold={roe_threshold} target_dates={total_target_dates} updated_rows={total_updated_rows} aligned_rows={aligned_rows}",
        flush=True,
    )
    return len(targets), total_target_dates, null_rows, abnormal_rows, total_updated_rows, aligned_rows


def backfill_financial_daily_alignment(target_date: Optional[dt.date] = None) -> int:
    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            ensure_financial_tables(cursor)
            if target_date is None:
                target_date = load_latest_trade_date(cursor)
            if target_date is None:
                print("[align] daily_bar 为空，跳过财报日频对齐", flush=True)
                return 0

            cursor.execute(
                """
                CREATE TABLE IF NOT EXISTS financial_indicator_daily (
                    indicator_daily_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '日频指标ID',
                    symbol_id INT UNSIGNED NOT NULL COMMENT '标的ID',
                    trade_date DATE NOT NULL COMMENT '交易日期',
                    report_date DATE NOT NULL COMMENT '原始报告期',
                    report_type ENUM('Q1', 'Q2', 'Q3', 'Q4', 'FY') NOT NULL COMMENT '原始报告类型',
                    effective_disclosure_date DATE DEFAULT NULL COMMENT '实际披露日期',
                    eps DECIMAL(10, 4) DEFAULT NULL COMMENT '每股收益',
                    bps DECIMAL(10, 4) DEFAULT NULL COMMENT '每股净资产',
                    roa DECIMAL(8, 4) DEFAULT NULL COMMENT '总资产收益率',
                    roe DECIMAL(8, 4) DEFAULT NULL COMMENT '净资产收益率',
                    profit_margin DECIMAL(8, 4) DEFAULT NULL COMMENT '净利率',
                    gross_margin DECIMAL(8, 4) DEFAULT NULL COMMENT '毛利率',
                    operating_margin DECIMAL(8, 4) DEFAULT NULL COMMENT '营业利润率',
                    debt_to_equity DECIMAL(8, 4) DEFAULT NULL COMMENT '资产负债率',
                    current_ratio DECIMAL(8, 4) DEFAULT NULL COMMENT '流动比率',
                    quick_ratio DECIMAL(8, 4) DEFAULT NULL COMMENT '速动比率',
                    operating_cash_flow DECIMAL(20, 4) DEFAULT NULL COMMENT '经营活动现金流',
                    investing_cash_flow DECIMAL(20, 4) DEFAULT NULL COMMENT '投资活动现金流',
                    financing_cash_flow DECIMAL(20, 4) DEFAULT NULL COMMENT '筹资活动现金流',
                    total_revenue DECIMAL(20, 4) DEFAULT NULL COMMENT '营业收入',
                    net_profit DECIMAL(20, 4) DEFAULT NULL COMMENT '净利润',
                    total_assets DECIMAL(20, 4) DEFAULT NULL COMMENT '总资产',
                    total_liabilities DECIMAL(20, 4) DEFAULT NULL COMMENT '总负债',
                    equity DECIMAL(20, 4) DEFAULT NULL COMMENT '所有者权益',
                    dividend_yield DECIMAL(12, 6) DEFAULT NULL COMMENT '股息率',
                    payout_ratio DECIMAL(12, 6) DEFAULT NULL COMMENT '分红支付率',
                    dividend_stability DECIMAL(8, 4) DEFAULT NULL COMMENT '分红稳定性',
                    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
                    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
                    PRIMARY KEY (indicator_daily_id),
                    UNIQUE KEY uk_symbol_trade_date (symbol_id, trade_date),
                    KEY idx_symbol_trade_date (symbol_id, trade_date),
                    KEY idx_trade_date (trade_date),
                    KEY idx_report_date (report_date),
                    CONSTRAINT fk_financial_daily_symbol FOREIGN KEY (symbol_id) REFERENCES symbol_info (symbol_id) ON DELETE CASCADE
                ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='财务指标日频对齐表'
                """
            )

            cursor.execute(
                """
                SELECT
                    COUNT(*) AS missing_rows,
                    MIN(db.trade_date) AS first_missing_date,
                    MAX(db.trade_date) AS last_missing_date
                FROM daily_bar db
                JOIN symbol_info si
                    ON si.symbol = db.symbol
                LEFT JOIN financial_indicator_daily fid
                    ON fid.symbol_id = si.symbol_id
                   AND fid.trade_date = db.trade_date
                WHERE db.trade_date <= %s
                  AND fid.indicator_daily_id IS NULL
                """,
                (target_date.isoformat(),),
            )
            missing_rows, first_missing_date, last_missing_date = cursor.fetchone()
            missing_rows = int(missing_rows or 0)
            print(
                f"[align] begin target_date={target_date} missing_rows={missing_rows} first_missing_date={first_missing_date} last_missing_date={last_missing_date}",
                flush=True,
            )

            cursor.execute(
                """
                UPDATE financial_indicator_daily fid
                JOIN financial_indicator fi
                  ON fi.symbol_id = fid.symbol_id
                 AND fi.report_date = fid.report_date
                SET fid.report_type = fi.report_type,
                    fid.effective_disclosure_date = fi.effective_disclosure_date,
                    fid.eps = fi.eps,
                    fid.bps = fi.bps,
                    fid.roa = fi.roa,
                    fid.roe = fi.roe,
                    fid.profit_margin = fi.profit_margin,
                    fid.gross_margin = fi.gross_margin,
                    fid.operating_margin = fi.operating_margin,
                    fid.debt_to_equity = fi.debt_to_equity,
                    fid.current_ratio = fi.current_ratio,
                    fid.quick_ratio = fi.quick_ratio,
                    fid.operating_cash_flow = fi.operating_cash_flow,
                    fid.investing_cash_flow = fi.investing_cash_flow,
                    fid.financing_cash_flow = fi.financing_cash_flow,
                    fid.total_revenue = fi.total_revenue,
                    fid.net_profit = fi.net_profit,
                    fid.total_assets = fi.total_assets,
                    fid.total_liabilities = fi.total_liabilities,
                    fid.equity = fi.equity,
                    fid.dividend_yield = fi.dividend_yield,
                    fid.payout_ratio = fi.payout_ratio,
                    fid.dividend_stability = fi.dividend_stability
                WHERE fid.trade_date <= %s
                  AND fi.report_date <= %s
                """,
                (target_date.isoformat(), target_date.isoformat()),
            )
            synced_rows = cursor.rowcount

            if missing_rows == 0:
                backfill_dividend_metrics(cursor, target_date)

                cursor.execute(
                    """
                    UPDATE financial_indicator_daily
                    SET dividend_yield = 0.0,
                        payout_ratio = 0.0,
                        dividend_stability = 0.0
                    WHERE trade_date <= %s
                      AND dividend_yield IS NULL
                    """,
                    (target_date.isoformat(),),
                )
                cleaned_rows = cursor.rowcount
                if cleaned_rows > 0:
                    print(f"[align] cleaned null dividend fields: {cleaned_rows} rows", flush=True)

                conn.commit()
                cursor.execute("SELECT COUNT(*), MAX(trade_date) FROM financial_indicator_daily")
                aligned_total_rows, aligned_max_trade_date = cursor.fetchone()
                print(
                    f"[align] target_date={target_date} missing_rows=0 synced_rows={synced_rows} aligned_total_rows={int(aligned_total_rows or 0)} aligned_max_trade_date={aligned_max_trade_date}",
                    flush=True,
                )
                return int(aligned_total_rows or 0)

            cursor.execute(
                """
                INSERT INTO financial_indicator_daily (
                    symbol_id, trade_date, report_date, report_type, effective_disclosure_date,
                    eps, bps, roa, roe, profit_margin, gross_margin, operating_margin,
                    debt_to_equity, current_ratio, quick_ratio,
                    operating_cash_flow, investing_cash_flow, financing_cash_flow,
                    total_revenue, net_profit, total_assets, total_liabilities, equity,
                    dividend_yield, payout_ratio, dividend_stability
                )
                SELECT
                    latest.symbol_id,
                    latest.trade_date,
                    fi.report_date,
                    fi.report_type,
                    fi.effective_disclosure_date,
                    fi.eps,
                    fi.bps,
                    fi.roa,
                    fi.roe,
                    fi.profit_margin,
                    fi.gross_margin,
                    fi.operating_margin,
                    fi.debt_to_equity,
                    fi.current_ratio,
                    fi.quick_ratio,
                    fi.operating_cash_flow,
                    fi.investing_cash_flow,
                    fi.financing_cash_flow,
                    fi.total_revenue,
                    fi.net_profit,
                    fi.total_assets,
                    fi.total_liabilities,
                    fi.equity,
                    fi.dividend_yield,
                    fi.payout_ratio,
                    fi.dividend_stability
                FROM (
                    SELECT
                        si2.symbol_id,
                        db2.trade_date,
                        MAX(fi2.report_date) AS latest_report_date
                    FROM daily_bar db2
                    JOIN symbol_info si2
                        ON si2.symbol = db2.symbol
                    LEFT JOIN financial_indicator_daily fid2
                        ON fid2.symbol_id = si2.symbol_id
                       AND fid2.trade_date = db2.trade_date
                    JOIN financial_indicator fi2
                        ON fi2.symbol_id = si2.symbol_id
                       AND fi2.report_date <= db2.trade_date
                    WHERE db2.trade_date <= %s
                      AND fid2.indicator_daily_id IS NULL
                    GROUP BY si2.symbol_id, db2.trade_date
                ) latest
                JOIN financial_indicator fi
                    ON fi.symbol_id = latest.symbol_id
                   AND fi.report_date = latest.latest_report_date
                ON DUPLICATE KEY UPDATE
                    report_date = VALUES(report_date),
                    report_type = VALUES(report_type),
                    effective_disclosure_date = VALUES(effective_disclosure_date),
                    eps = VALUES(eps),
                    bps = VALUES(bps),
                    roa = VALUES(roa),
                    roe = VALUES(roe),
                    profit_margin = VALUES(profit_margin),
                    gross_margin = VALUES(gross_margin),
                    operating_margin = VALUES(operating_margin),
                    debt_to_equity = VALUES(debt_to_equity),
                    current_ratio = VALUES(current_ratio),
                    quick_ratio = VALUES(quick_ratio),
                    operating_cash_flow = VALUES(operating_cash_flow),
                    investing_cash_flow = VALUES(investing_cash_flow),
                    financing_cash_flow = VALUES(financing_cash_flow),
                    total_revenue = VALUES(total_revenue),
                    net_profit = VALUES(net_profit),
                    total_assets = VALUES(total_assets),
                    total_liabilities = VALUES(total_liabilities),
                    equity = VALUES(equity),
                    dividend_yield = VALUES(dividend_yield),
                    payout_ratio = VALUES(payout_ratio),
                    dividend_stability = VALUES(dividend_stability)
                """,
                (target_date.isoformat(),),
            )
            affected_rows = cursor.rowcount

            backfill_dividend_metrics(cursor, target_date)

            cursor.execute(
                """
                UPDATE financial_indicator_daily
                SET dividend_yield = 0.0,
                    payout_ratio = 0.0,
                    dividend_stability = 0.0
                WHERE trade_date <= %s
                  AND dividend_yield IS NULL
                """,
                (target_date.isoformat(),),
            )
            cleaned_rows = cursor.rowcount
            if cleaned_rows > 0:
                print(f"[align] cleaned null dividend fields: {cleaned_rows} rows", flush=True)

            conn.commit()

            cursor.execute(
                """
                SELECT COUNT(*)
                FROM daily_bar db
                JOIN symbol_info si
                    ON si.symbol = db.symbol
                LEFT JOIN financial_indicator_daily fid
                    ON fid.symbol_id = si.symbol_id
                   AND fid.trade_date = db.trade_date
                WHERE db.trade_date <= %s
                  AND fid.indicator_daily_id IS NULL
                  AND EXISTS (
                      SELECT 1
                      FROM financial_indicator fi
                      WHERE fi.symbol_id = si.symbol_id
                        AND fi.report_date <= db.trade_date
                  )
                """,
                (target_date.isoformat(),),
            )
            remaining_alignable_rows = int(cursor.fetchone()[0])
            cursor.execute(
                """
                SELECT COUNT(*)
                FROM daily_bar db
                JOIN symbol_info si
                    ON si.symbol = db.symbol
                LEFT JOIN financial_indicator_daily fid
                    ON fid.symbol_id = si.symbol_id
                   AND fid.trade_date = db.trade_date
                WHERE db.trade_date <= %s
                  AND fid.indicator_daily_id IS NULL
                  AND NOT EXISTS (
                      SELECT 1
                      FROM financial_indicator fi
                      WHERE fi.symbol_id = si.symbol_id
                        AND fi.report_date <= db.trade_date
                  )
                """,
                (target_date.isoformat(),),
            )
            remaining_unalignable_rows = int(cursor.fetchone()[0])
            cursor.execute("SELECT COUNT(*), MAX(trade_date) FROM financial_indicator_daily")
            aligned_total_rows, aligned_max_trade_date = cursor.fetchone()
            print(
                f"[align] target_date={target_date} missing_rows={missing_rows} first_missing_date={first_missing_date} last_missing_date={last_missing_date} synced_rows={synced_rows} affected_rows={affected_rows} remaining_alignable_rows={remaining_alignable_rows} remaining_unalignable_rows={remaining_unalignable_rows} aligned_total_rows={int(aligned_total_rows or 0)} aligned_max_trade_date={aligned_max_trade_date}",
                flush=True,
            )
            return int(aligned_total_rows or 0)
    finally:
        conn.close()


def fetch_akshare_dividend_events(symbol: str) -> List[Tuple[dt.date, float]]:
    code = symbol.split(".", 1)[0]
    detail_df = ak.stock_history_dividend_detail(symbol=code, indicator="分红")
    if detail_df is None or getattr(detail_df, "empty", True):
        return []

    events: List[Tuple[dt.date, float]] = []
    for _, row in detail_df.iterrows():
        progress = str(row.get("进度") or "").strip()
        if progress and progress != "实施":
            continue
        effective_date = (
            parse_report_date(row.get("除权除息日"))
            or parse_report_date(row.get("股权登记日"))
            or parse_report_date(row.get("公告日期"))
        )
        cash_dividend = to_float(row.get("派息"))
        if effective_date is None or cash_dividend is None or cash_dividend <= 0:
            continue
        events.append((effective_date, cash_dividend / 10.0))

    events.sort(key=lambda item: item[0])
    return events


def backfill_dividend_metrics(cursor, target_date: dt.date) -> int:
    conn = cursor.connection
    cursor.execute(
        """
        SELECT si.symbol_id, si.symbol
        FROM symbol_info si
        WHERE si.asset_class = 'STOCK'
          AND EXISTS (
              SELECT 1
              FROM financial_indicator_daily fid
              WHERE fid.symbol_id = si.symbol_id
                AND fid.trade_date <= %s
          )
        ORDER BY si.symbol
        """,
        (target_date.isoformat(),),
    )
    symbols = [(int(symbol_id), str(symbol)) for symbol_id, symbol in cursor.fetchall()]
    total_updates = 0

    for index, (symbol_id, symbol) in enumerate(symbols, start=1):
        if index == 1 or index % 100 == 0:
            print(
                f"[dividend-progress] daily completed={index - 1}/{len(symbols)} total_updates={total_updates}",
                flush=True,
            )
        try:
            dividend_events = fetch_akshare_dividend_events(symbol)
        except Exception as exc:
            print(f"[dividend-skip] {symbol}: {exc}, fallback to zero", flush=True)
            dividend_events = []

        cursor.execute(
            """
            SELECT fid.trade_date, db.close
            FROM financial_indicator_daily fid
            JOIN symbol_info si
              ON si.symbol_id = fid.symbol_id
            JOIN daily_bar db
              ON db.symbol = si.symbol
             AND db.trade_date = fid.trade_date
            WHERE fid.symbol_id = %s
              AND fid.trade_date <= %s
            ORDER BY fid.trade_date
            """,
            (symbol_id, target_date.isoformat()),
        )
        trade_rows = [(row[0], to_float(row[1])) for row in cursor.fetchall() if row[0] is not None]
        if not trade_rows:
            continue

        cursor.execute(
            """
            SELECT report_date, eps
            FROM financial_indicator
            WHERE symbol_id = %s
              AND report_type = 'FY'
              AND report_date <= %s
            ORDER BY report_date
            """,
            (symbol_id, target_date.isoformat()),
        )
        annual_eps_rows = [(row[0], to_float(row[1])) for row in cursor.fetchall() if row[0] is not None]

        annual_eps_index = 0
        latest_annual_eps: Optional[float] = None
        updates = []

        for trade_date, close_price in trade_rows:
            while annual_eps_index < len(annual_eps_rows) and annual_eps_rows[annual_eps_index][0] <= trade_date:
                latest_annual_eps = annual_eps_rows[annual_eps_index][1]
                annual_eps_index += 1

            trailing_start = trade_date - dt.timedelta(days=365)
            trailing_cash = sum(
                cash_per_share
                for event_date, cash_per_share in dividend_events
                if trailing_start < event_date <= trade_date
            )
            recent_years = {
                event_date.year
                for event_date, cash_per_share in dividend_events
                if cash_per_share > 0 and (trade_date.year - 4) <= event_date.year <= trade_date.year and event_date <= trade_date
            }

            dividend_yield = 0.0
            if close_price not in (None, 0) and trailing_cash > 0:
                dividend_yield = trailing_cash / close_price

            payout_ratio = 0.0
            if trailing_cash > 0 and latest_annual_eps not in (None, 0) and latest_annual_eps > 0:
                payout_ratio = trailing_cash / latest_annual_eps

            dividend_stability = len(recent_years) / 5.0
            updates.append(
                (
                    clamp_decimal(dividend_yield, 999999.999999),
                    clamp_decimal(payout_ratio, 999999.999999),
                    clamp_decimal(dividend_stability, 9999.9999),
                    symbol_id,
                    trade_date,
                )
            )

        if updates:
            cursor.executemany(
                """
                UPDATE financial_indicator_daily
                SET dividend_yield = %s,
                    payout_ratio = %s,
                    dividend_stability = %s
                WHERE symbol_id = %s
                  AND trade_date = %s
                """,
                updates,
            )
            total_updates += len(updates)
            conn.commit()

    return total_updates


def backfill_report_dividend_metrics(cursor, target_date: dt.date) -> int:
    conn = cursor.connection
    cursor.execute(
        """
        SELECT si.symbol_id, si.symbol
        FROM symbol_info si
        WHERE si.asset_class = 'STOCK'
          AND EXISTS (
              SELECT 1
              FROM financial_indicator fi
              WHERE fi.symbol_id = si.symbol_id
                AND fi.report_date <= %s
          )
        ORDER BY si.symbol
        """,
        (target_date.isoformat(),),
    )
    symbols = [(int(symbol_id), str(symbol)) for symbol_id, symbol in cursor.fetchall()]
    total_updates = 0

    for index, (symbol_id, symbol) in enumerate(symbols, start=1):
        if index == 1 or index % 100 == 0:
            print(
                f"[dividend-progress] report completed={index - 1}/{len(symbols)} total_updates={total_updates}",
                flush=True,
            )
        try:
            dividend_events = fetch_akshare_dividend_events(symbol)
        except Exception as exc:
            print(f"[dividend-report-skip] {symbol}: {exc}, fallback to zero", flush=True)
            dividend_events = []

        cursor.execute(
            """
            SELECT db.trade_date, db.close
            FROM daily_bar db
            JOIN symbol_info si
              ON si.symbol = db.symbol
            WHERE si.symbol_id = %s
              AND db.trade_date <= %s
            ORDER BY db.trade_date
            """,
            (symbol_id, target_date.isoformat()),
        )
        price_rows = [(row[0], to_float(row[1])) for row in cursor.fetchall() if row[0] is not None]
        if not price_rows:
            continue

        cursor.execute(
            """
            SELECT report_date, report_type, eps
            FROM financial_indicator
            WHERE symbol_id = %s
              AND report_date <= %s
            ORDER BY report_date
            """,
            (symbol_id, target_date.isoformat()),
        )
        report_rows = [
            (row[0], str(row[1]), to_float(row[2]))
            for row in cursor.fetchall()
            if row[0] is not None
        ]
        if not report_rows:
            continue

        annual_eps_rows = [(report_date, eps) for report_date, report_type, eps in report_rows if report_type == "FY"]
        annual_eps_index = 0
        latest_annual_eps: Optional[float] = None
        price_index = 0
        latest_close_price: Optional[float] = None
        updates = []

        for report_date, _report_type, _eps in report_rows:
            while price_index < len(price_rows) and price_rows[price_index][0] <= report_date:
                latest_close_price = price_rows[price_index][1]
                price_index += 1

            while annual_eps_index < len(annual_eps_rows) and annual_eps_rows[annual_eps_index][0] <= report_date:
                latest_annual_eps = annual_eps_rows[annual_eps_index][1]
                annual_eps_index += 1

            trailing_start = report_date - dt.timedelta(days=365)
            trailing_cash = sum(
                cash_per_share
                for event_date, cash_per_share in dividend_events
                if trailing_start < event_date <= report_date
            )
            recent_years = {
                event_date.year
                for event_date, cash_per_share in dividend_events
                if cash_per_share > 0 and (report_date.year - 4) <= event_date.year <= report_date.year and event_date <= report_date
            }

            dividend_yield = 0.0
            if latest_close_price not in (None, 0) and trailing_cash > 0:
                dividend_yield = trailing_cash / latest_close_price

            payout_ratio = 0.0
            if trailing_cash > 0 and latest_annual_eps not in (None, 0) and latest_annual_eps > 0:
                payout_ratio = trailing_cash / latest_annual_eps

            dividend_stability = len(recent_years) / 5.0
            updates.append(
                (
                    clamp_decimal(dividend_yield, 999999.999999),
                    clamp_decimal(payout_ratio, 999999.999999),
                    clamp_decimal(dividend_stability, 9999.9999),
                    symbol_id,
                    report_date,
                )
            )

        if updates:
            cursor.executemany(
                """
                UPDATE financial_indicator
                SET dividend_yield = %s,
                    payout_ratio = %s,
                    dividend_stability = %s
                WHERE symbol_id = %s
                  AND report_date = %s
                """,
                updates,
            )
            total_updates += len(updates)
            conn.commit()

    return total_updates


def fetch_financial_history(start_year: int, end_year: int, limit: int, workers: int = 8) -> List[Tuple[str, int, int]]:
    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            ensure_financial_tables(cursor)
            symbols = load_backfill_symbols(cursor)
            expected_report_dates = build_expected_report_dates(start_year, end_year)
            if not expected_report_dates:
                print("[stage] 没有可回补的报告期", flush=True)
                return []
            existing_dates_by_symbol = load_existing_report_dates(
                cursor, expected_report_dates[0], expected_report_dates[-1]
            )
    finally:
        conn.close()

    pending_symbols = []
    for symbol, symbol_id, _latest_report_date in symbols:
        existing_dates = existing_dates_by_symbol.get(symbol_id, set())
        if any(report_date not in existing_dates for report_date in expected_report_dates):
            pending_symbols.append((symbol, symbol_id))

    period_results: List[Tuple[str, int, int]] = []
    total_upserts = 0
    resolved_workers = _resolve_worker_count(workers, len(pending_symbols))
    print(
        f"[stage] financial workers={resolved_workers} symbol_count={len(symbols)} report_date_count={len(expected_report_dates)} pending_symbol_count={len(pending_symbols)}",
        flush=True,
    )

    if not pending_symbols:
        print("[stage] 所有目标报告期都已存在，跳过财务历史回补", flush=True)
        return period_results

    with ThreadPoolExecutor(max_workers=resolved_workers) as executor:
        future_to_meta = {}
        for index, (symbol, symbol_id) in enumerate(pending_symbols, start=1):
            if index == 1 or index % 10 == 0:
                print(
                    f"[fetch] submit symbol={symbol} index={index}/{len(pending_symbols)}",
                    flush=True,
                )
            future = executor.submit(
                _fetch_and_upsert_symbol_financial_history,
                symbol,
                symbol_id,
                start_year,
                end_year,
            )
            future_to_meta[future] = symbol

        pending_futures = set(future_to_meta)
        completed_count = 0
        try:
            while pending_futures:
                completed_batch, pending_futures = wait(
                    pending_futures,
                    timeout=PROGRESS_HEARTBEAT_SECONDS,
                    return_when=FIRST_COMPLETED,
                )
                if not completed_batch:
                    print(
                        f"[heartbeat] financial fetch running completed={completed_count}/{len(future_to_meta)} pending={len(pending_futures)} total_upserts={total_upserts}",
                        flush=True,
                    )
                    continue

                for future in completed_batch:
                    completed_count += 1
                    symbol = future_to_meta[future]
                    try:
                        completed_symbol, fetched_rows, affected = future.result()
                        total_upserts += affected
                        period_results.append((completed_symbol, fetched_rows, affected))
                        if completed_count == 1 or completed_count % 50 == 0:
                            print(
                                f"[progress] completed={completed_count}/{len(future_to_meta)} total_upserts={total_upserts}",
                                flush=True,
                            )
                        print(f"[upsert] symbol={completed_symbol} rows={affected}")
                    except Exception as exc:
                        print(f"[skip] symbol={symbol}: {exc}")
        except KeyboardInterrupt:
            for future in future_to_meta:
                future.cancel()
            print("[interrupt] 停止接收新的财报任务，已提交并已提交入库的数据不会回退", flush=True)
            raise

    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            cursor.execute("SELECT COUNT(*) FROM financial_indicator")
            total_rows = cursor.fetchone()[0]
            print(f"[done] total_upserts={total_upserts} total_rows={total_rows}")
            return period_results
    finally:
        conn.close()


def repair_dividend_fields() -> None:
    conn = get_connection()
    target_date: Optional[dt.date] = None
    try:
        with conn.cursor() as cursor:
            target_date = load_latest_trade_date(cursor)
            if target_date is None:
                print("[repair] daily_bar 为空，跳过分红字段回填", flush=True)
                return

            report_dividend_updates = backfill_report_dividend_metrics(cursor, target_date)
            print(f"[repair][dividend-report] updated_rows={report_dividend_updates}", flush=True)
    finally:
        conn.close()

    aligned_rows = backfill_financial_daily_alignment(target_date)
    print(f"[repair][daily-align] aligned_rows={aligned_rows}", flush=True)


def main() -> None:
    args = parse_args()

    if args.repair_dividend_fields:
        repair_dividend_fields()
        return

    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            daily_start_date, daily_end_date = load_daily_trade_date_bounds(cursor)
            start_year = args.start_year if args.start_year is not None else daily_start_date.year
            end_year = args.end_year if args.end_year is not None else daily_end_date.year
            print(
                f"[stage] align financial history to daily_bar range: {daily_start_date}..{daily_end_date} -> base_years={start_year}..{end_year}",
                flush=True,
            )
    finally:
        conn.close()

    fetch_financial_history(start_year, end_year, args.limit, args.workers)
    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            report_dividend_updates = backfill_report_dividend_metrics(cursor, load_latest_trade_date(cursor) or daily_end_date)
            print(f"[dividend-report] updated_rows={report_dividend_updates}", flush=True)
    finally:
        conn.close()

    backfill_financial_daily_alignment()


if __name__ == "__main__":
    main()