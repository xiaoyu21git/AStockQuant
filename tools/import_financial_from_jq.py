from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import datetime as dt
import os
from typing import Dict, Iterable, List, Optional

import akshare as ak
import jqdatasdk as jq
import pymysql
import pandas as pd


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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="按 AkShare 历史报表回填全量财务数据到 financial_indicator")
    parser.add_argument("--start-year", type=int, default=None, help="回填起始年份，默认与 daily_bar 最早日期对齐")
    parser.add_argument("--end-year", type=int, default=None, help="回填结束年份，默认与 daily_bar 最晚日期对齐")
    parser.add_argument("--limit", type=int, default=10000, help="单次聚宽查询返回上限")
    parser.add_argument("--workers", type=int, default=8, help="财报抓取并发线程数，默认 8")
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


def load_latest_trade_date(cursor) -> Optional[dt.date]:
    cursor.execute("SELECT MAX(trade_date) FROM daily_bar")
    row = cursor.fetchone()
    return row[0] if row and row[0] else None


def load_daily_trade_date_bounds(cursor) -> tuple[dt.date, dt.date]:
    cursor.execute("SELECT MIN(trade_date), MAX(trade_date) FROM daily_bar")
    row = cursor.fetchone()
    if not row or row[0] is None or row[1] is None:
        raise RuntimeError("daily_bar 为空，无法对齐财报起止日期")
    return row[0], row[1]


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


def fetch_akshare_financial_frames(symbol: str, start_year: int) -> tuple[pd.DataFrame, pd.DataFrame, pd.DataFrame, pd.DataFrame]:
    ak_symbol = local_to_akshare_symbol(symbol)
    if ak_symbol is None:
        return pd.DataFrame(), pd.DataFrame(), pd.DataFrame(), pd.DataFrame()

    code = symbol.split(".", 1)[0]
    balance_df = ak.stock_balance_sheet_by_report_em(symbol=ak_symbol)
    profit_df = ak.stock_profit_sheet_by_report_em(symbol=ak_symbol)
    cash_df = ak.stock_cash_flow_sheet_by_report_em(symbol=ak_symbol)
    analysis_df = ak.stock_financial_analysis_indicator(symbol=code, start_year=str(start_year))
    return balance_df, profit_df, cash_df, analysis_df


def build_akshare_rows(
    symbol_id: int,
    symbol: str,
    balance_df: pd.DataFrame,
    profit_df: pd.DataFrame,
    cash_df: pd.DataFrame,
    analysis_df: pd.DataFrame,
    start_year: int,
    end_year: int,
) -> List[dict]:
    rows: List[dict] = []
    if balance_df is None or getattr(balance_df, "empty", True):
        return rows

    profit_by_date = frame_row_by_report_date(profit_df)
    cash_by_date = frame_row_by_report_date(cash_df)
    analysis_by_date = frame_row_by_report_date(analysis_df, date_column="日期")

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
                "eps": clamp_decimal(eps, 999999.9999),
                "bps": clamp_decimal(bps, 999999.9999),
                "roa": clamp_decimal(roa, 9999.9999),
                "roe": clamp_decimal(roe, 9999.9999),
                "profit_margin": clamp_decimal(profit_margin, 9999.9999),
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
                "eps": clamp_decimal(item.get("basic_eps"), 999999.9999),
                "bps": clamp_decimal(bps, 999999.9999),
                "roa": clamp_decimal(item.get("roa"), 9999.9999),
                "roe": clamp_decimal(item.get("roe"), 9999.9999),
                "profit_margin": clamp_decimal(profit_margin, 9999.9999),
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
            }
        )
    return rows


def upsert_rows(cursor, rows: Iterable[dict]) -> int:
    payload = list(rows)
    if not payload:
        return 0

    sql = """
    INSERT INTO financial_indicator (
        symbol_id, report_date, report_type,
        eps, bps, roa, roe, profit_margin,
        debt_to_equity, current_ratio, quick_ratio,
        operating_cash_flow, investing_cash_flow, financing_cash_flow,
        total_revenue, net_profit, total_assets, total_liabilities, equity
    ) VALUES (
        %(symbol_id)s, %(report_date)s, %(report_type)s,
        %(eps)s, %(bps)s, %(roa)s, %(roe)s, %(profit_margin)s,
        %(debt_to_equity)s, %(current_ratio)s, %(quick_ratio)s,
        %(operating_cash_flow)s, %(investing_cash_flow)s, %(financing_cash_flow)s,
        %(total_revenue)s, %(net_profit)s, %(total_assets)s, %(total_liabilities)s, %(equity)s
    )
    ON DUPLICATE KEY UPDATE
        eps = VALUES(eps),
        bps = VALUES(bps),
        roa = VALUES(roa),
        roe = VALUES(roe),
        profit_margin = VALUES(profit_margin),
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
        equity = VALUES(equity)
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
) -> tuple[str, int, int]:
    balance_df, profit_df, cash_df, analysis_df = fetch_akshare_financial_frames(symbol, start_year)
    rows = build_akshare_rows(
        symbol_id,
        symbol,
        balance_df,
        profit_df,
        cash_df,
        analysis_df,
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


def backfill_financial_daily_alignment(target_date: Optional[dt.date] = None) -> int:
    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            if target_date is None:
                target_date = load_latest_trade_date(cursor)
            if target_date is None:
                print("[align] daily_bar 为空，跳过财报日频对齐")
                return 0

            cursor.execute(
                """
                CREATE TABLE IF NOT EXISTS financial_indicator_daily (
                    indicator_daily_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '日频指标ID',
                    symbol_id INT UNSIGNED NOT NULL COMMENT '标的ID',
                    trade_date DATE NOT NULL COMMENT '交易日期',
                    report_date DATE NOT NULL COMMENT '原始报告期',
                    report_type ENUM('Q1', 'Q2', 'Q3', 'Q4', 'FY') NOT NULL COMMENT '原始报告类型',
                    eps DECIMAL(10, 4) DEFAULT NULL COMMENT '每股收益',
                    bps DECIMAL(10, 4) DEFAULT NULL COMMENT '每股净资产',
                    roa DECIMAL(8, 4) DEFAULT NULL COMMENT '总资产收益率',
                    roe DECIMAL(8, 4) DEFAULT NULL COMMENT '净资产收益率',
                    profit_margin DECIMAL(8, 4) DEFAULT NULL COMMENT '净利率',
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
            if missing_rows == 0:
                cursor.execute("SELECT COUNT(*), MAX(trade_date) FROM financial_indicator_daily")
                aligned_total_rows, aligned_max_trade_date = cursor.fetchone()
                print(
                    f"[align] target_date={target_date} missing_rows=0 aligned_total_rows={int(aligned_total_rows or 0)} aligned_max_trade_date={aligned_max_trade_date}",
                    flush=True,
                )
                return int(aligned_total_rows or 0)

            sql = """
            INSERT INTO financial_indicator_daily (
                symbol_id, trade_date, report_date, report_type,
                eps, bps, roa, roe, profit_margin,
                debt_to_equity, current_ratio, quick_ratio,
                operating_cash_flow, investing_cash_flow, financing_cash_flow,
                total_revenue, net_profit, total_assets, total_liabilities, equity
            )
            SELECT
                latest.symbol_id,
                latest.trade_date,
                fi.report_date,
                fi.report_type,
                fi.eps,
                fi.bps,
                fi.roa,
                fi.roe,
                fi.profit_margin,
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
                fi.equity
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
                eps = VALUES(eps),
                bps = VALUES(bps),
                roa = VALUES(roa),
                roe = VALUES(roe),
                profit_margin = VALUES(profit_margin),
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
                equity = VALUES(equity)
            """
            cursor.execute(sql, (target_date.isoformat(),))
            affected_rows = cursor.rowcount
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
                f"[align] target_date={target_date} missing_rows={missing_rows} first_missing_date={first_missing_date} last_missing_date={last_missing_date} affected_rows={affected_rows} remaining_alignable_rows={remaining_alignable_rows} remaining_unalignable_rows={remaining_unalignable_rows} aligned_total_rows={int(aligned_total_rows or 0)} aligned_max_trade_date={aligned_max_trade_date}",
                flush=True,
            )
            return int(aligned_total_rows or 0)
    finally:
        conn.close()


def fetch_financial_history(start_year: int, end_year: int, limit: int, workers: int = 8) -> List[tuple[str, int, int]]:
    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            cursor.execute(
                """
                SELECT symbol, symbol_id
                FROM symbol_info
                WHERE asset_class = 'STOCK'
                ORDER BY symbol
                """
            )
            symbols = [
                (str(row[0]), int(row[1]))
                for row in cursor.fetchall()
                if local_to_akshare_symbol(str(row[0])) is not None
            ]
    finally:
        conn.close()

    period_results: List[tuple[str, int, int]] = []
    total_upserts = 0
    resolved_workers = _resolve_worker_count(workers, len(symbols))
    print(f"[stage] financial workers={resolved_workers} symbol_count={len(symbols)}", flush=True)

    with ThreadPoolExecutor(max_workers=resolved_workers) as executor:
        future_to_meta = {}
        for index, (symbol, symbol_id) in enumerate(symbols, start=1):
            if index == 1 or index % 50 == 0:
                print(f"[fetch] submit symbol={symbol} index={index}/{len(symbols)}", flush=True)
            future = executor.submit(
                _fetch_and_upsert_symbol_financial_history,
                symbol,
                symbol_id,
                start_year,
                end_year,
            )
            future_to_meta[future] = symbol

        try:
            for completed_count, future in enumerate(as_completed(future_to_meta), start=1):
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


def main() -> None:
    args = parse_args()
    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            daily_start_date, daily_end_date = load_daily_trade_date_bounds(cursor)
            start_year = args.start_year if args.start_year is not None else daily_start_date.year
            end_year = args.end_year if args.end_year is not None else daily_end_date.year
            print(
                f"[stage] align financial history to daily_bar range: {daily_start_date}..{daily_end_date} -> {start_year}..{end_year}",
                flush=True,
            )
    finally:
        conn.close()

    fetch_financial_history(start_year, end_year, args.limit, args.workers)
    backfill_financial_daily_alignment()


if __name__ == "__main__":
    main()