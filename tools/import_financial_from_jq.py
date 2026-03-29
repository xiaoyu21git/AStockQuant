from __future__ import annotations

import argparse
import datetime as dt
import os
from typing import Dict, Iterable, List, Optional

import jqdatasdk as jq
import pymysql


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
    parser = argparse.ArgumentParser(description="从聚宽导入财务快照到 financial_indicator")
    parser.add_argument("--anchor-dates", type=str, default="", help="逗号分隔的查询锚点日期，例如 2024-05-01,2024-09-01")
    parser.add_argument("--limit", type=int, default=10000, help="单次聚宽查询返回上限")
    return parser.parse_args()


def build_default_anchor_dates(today: dt.date) -> List[dt.date]:
    candidates: List[dt.date] = []
    for year in range(today.year - 2, today.year + 1):
        for month, day in ((5, 1), (9, 1), (11, 15)):
            candidate = dt.date(year, month, day)
            if candidate <= today:
                candidates.append(candidate)
    candidates.append(today)
    return sorted(set(candidates))


def resolve_anchor_dates(raw_value: str) -> List[dt.date]:
    if not raw_value.strip():
        return build_default_anchor_dates(dt.date.today())
    dates: List[dt.date] = []
    for item in raw_value.split(","):
        text = item.strip()
        if text:
            dates.append(dt.date.fromisoformat(text))
    return sorted(set(dates))


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
        jq.balance.total_current_assets,
        jq.balance.total_current_liability,
        jq.cash_flow.net_operate_cash_flow,
        jq.cash_flow.net_invest_cash_flow,
        jq.cash_flow.net_finance_cash_flow,
    ).limit(limit)
    return jq.get_fundamentals(query, date=anchor_date.isoformat())


def normalize_value(value):
    if value != value:
        return None
    return value


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

        stat_date_raw = item["statDate"]
        if hasattr(stat_date_raw, "date"):
            stat_date = stat_date_raw.date()
        elif isinstance(stat_date_raw, str):
            stat_date = dt.date.fromisoformat(stat_date_raw[:10])
        else:
            stat_date = stat_date_raw

        total_liability = normalize_value(item.get("total_liability"))
        total_equity = normalize_value(item.get("total_sheet_owner_equities"))
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
                "bps": None,
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


def main() -> None:
    args = parse_args()
    anchor_dates = resolve_anchor_dates(args.anchor_dates)
    auth_jq()

    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            symbol_map = load_symbol_map(cursor)
            total_upserts = 0
            for anchor_date in anchor_dates:
                print(f"[fetch] anchor_date={anchor_date}")
                df = fetch_financial_snapshot(anchor_date, args.limit)
                print(f"[fetch] rows={len(df)}")
                rows = build_rows(df, symbol_map)
                affected = upsert_rows(cursor, rows)
                total_upserts += affected
                conn.commit()
                print(f"[upsert] anchor_date={anchor_date} rows={affected}")

            cursor.execute("SELECT COUNT(*) FROM financial_indicator")
            total_rows = cursor.fetchone()[0]
            print(f"[done] total_upserts={total_upserts} total_rows={total_rows}")
    finally:
        conn.close()


if __name__ == "__main__":
    main()