import argparse
import datetime as dt
from typing import Dict, List, Sequence, Tuple

import pymysql


MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
    "cursorclass": pymysql.cursors.DictCursor,
}


PRICE_FIELDS = [
    "symbol",
    "trade_date",
    "date",
    "open",
    "high",
    "low",
    "close",
    "pre_close",
    "volume",
    "turnover",
    "change_pct",
    "change_amt",
    "amplitude",
    "turnover_rate",
    "pe_ratio",
    "pb_ratio",
    "market_cap",
    "circulating_market_cap",
    "data_source",
]

MINUTE_FIELDS = [
    "bar_id",
    "symbol_id",
    "symbol",
    "timeframe",
    "bar_time",
    "date",
    "open",
    "high",
    "low",
    "close",
    "volume",
    "turnover",
    "vwap",
    "created_at",
]

FINANCIAL_FIELDS = [
    "indicator_id",
    "symbol_id",
    "symbol",
    "report_date",
    "date",
    "report_type",
    "eps",
    "bps",
    "roa",
    "roe",
    "profit_margin",
    "debt_to_equity",
    "current_ratio",
    "quick_ratio",
    "operating_cash_flow",
    "investing_cash_flow",
    "financing_cash_flow",
    "total_revenue",
    "net_profit",
    "total_assets",
    "total_liabilities",
    "equity",
    "created_at",
    "updated_at",
]


def get_scalar(cursor, sql: str, params: Sequence = ()):
    cursor.execute(sql, params)
    row = cursor.fetchone()
    if not row:
        return None
    return next(iter(row.values()))


def inspect_query(cursor, name: str, query: str, params: Sequence, expected_fields: List[str]) -> Dict:
    count_sql = f"SELECT COUNT(*) AS count FROM ({query}) AS t"
    cursor.execute(count_sql, params)
    count = cursor.fetchone()["count"]

    sample_sql = f"SELECT * FROM ({query}) AS t LIMIT 1"
    cursor.execute(sample_sql, params)
    row = cursor.fetchone()
    actual_fields = list(row.keys()) if row else []
    missing_fields = [field for field in expected_fields if field not in actual_fields]
    extra_fields = [field for field in actual_fields if field not in expected_fields]

    return {
        "name": name,
        "count": count,
        "actual_fields": actual_fields,
        "missing_fields": missing_fields,
        "extra_fields": extra_fields,
        "sample": row,
        "reason": "",
    }


def unavailable_result(name: str, reason: str, expected_fields: List[str]) -> Dict:
    return {
        "name": name,
        "count": 0,
        "actual_fields": [],
        "missing_fields": expected_fields,
        "extra_fields": [],
        "sample": None,
        "reason": reason,
    }


def build_period_query(period: str) -> str:
    period_expr = "DATE_FORMAT(trade_date, '%%Y-%%m')" if period == "monthly" else "YEARWEEK(trade_date, 1)"
    return f"""
SELECT
    agg.symbol,
    agg.period_end AS trade_date,
    agg.period_end AS date,
    first_day.open AS open,
    agg.high AS high,
    agg.low AS low,
    last_day.close AS close,
    first_day.pre_close AS pre_close,
    agg.volume AS volume,
    agg.turnover AS turnover,
    CASE
        WHEN first_day.pre_close IS NOT NULL AND first_day.pre_close <> 0
        THEN ((last_day.close - first_day.pre_close) / first_day.pre_close) * 100
        ELSE NULL
    END AS change_pct,
    CASE
        WHEN first_day.pre_close IS NOT NULL
        THEN last_day.close - first_day.pre_close
        ELSE NULL
    END AS change_amt,
    CASE
        WHEN first_day.pre_close IS NOT NULL AND first_day.pre_close <> 0
        THEN ((agg.high - agg.low) / first_day.pre_close) * 100
        ELSE NULL
    END AS amplitude,
    agg.turnover_rate AS turnover_rate,
    last_day.pe_ratio AS pe_ratio,
    last_day.pb_ratio AS pb_ratio,
    last_day.market_cap AS market_cap,
    last_day.circulating_market_cap AS circulating_market_cap,
    COALESCE(last_day.data_source, 'AGGREGATED_DAILY') AS data_source
FROM (
    SELECT
        symbol,
        {period_expr} AS period_key,
        MIN(trade_date) AS period_start,
        MAX(trade_date) AS period_end,
        MAX(high) AS high,
        MIN(low) AS low,
        SUM(volume) AS volume,
        SUM(turnover) AS turnover,
        SUM(COALESCE(turnover_rate, 0)) AS turnover_rate
    FROM daily_bar
    WHERE trade_date BETWEEN %s AND %s
    GROUP BY symbol, {period_expr}
) agg
JOIN daily_bar first_day
    ON first_day.symbol = agg.symbol AND first_day.trade_date = agg.period_start
JOIN daily_bar last_day
    ON last_day.symbol = agg.symbol AND last_day.trade_date = agg.period_end
ORDER BY agg.symbol, agg.period_end
""".strip()


def print_result(result: Dict) -> None:
    print(f"\n[{result['name']}]")
    print(f"rows={result['count']}")
    if result.get("reason"):
        print(f"reason={result['reason']}")
    print(f"missing_fields={result['missing_fields']}")
    print(f"extra_fields={result['extra_fields']}")
    print(f"actual_fields={result['actual_fields']}")
    if result["sample"]:
        sample_preview = {key: result["sample"][key] for key in list(result["sample"].keys())[:8]}
        print(f"sample={sample_preview}")
    else:
        print("sample=None")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="验证回测所需数据字段完整性")
    parser.add_argument("--daily-window", type=int, default=180, help="日/周/月验证窗口天数")
    parser.add_argument("--financial-window", type=int, default=3650, help="财务验证窗口天数")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    conn = pymysql.connect(**MYSQL_CONFIG)

    try:
        with conn.cursor() as cursor:
            latest_daily = get_scalar(cursor, "SELECT MAX(trade_date) AS latest_date FROM daily_bar")
            latest_minute = get_scalar(cursor, "SELECT MAX(bar_time) AS latest_time FROM minute_bar")
            latest_financial = get_scalar(cursor, "SELECT MAX(report_date) AS latest_date FROM financial_indicator")

            if latest_daily is None:
                raise RuntimeError("daily_bar 无数据，无法验证")

            daily_end = latest_daily
            daily_start = latest_daily - dt.timedelta(days=args.daily_window)

            minute_end = latest_minute if latest_minute is not None else None
            minute_start = None
            if minute_end is not None:
                minute_start = minute_end - dt.timedelta(days=1)

            financial_end = latest_financial
            financial_start = None
            if latest_financial is not None:
                financial_start = latest_financial - dt.timedelta(days=args.financial_window)

            print("Validation window:")
            print(f"daily={daily_start} -> {daily_end}")
            print(f"minute={minute_start} -> {minute_end}")
            print(f"financial={financial_start} -> {financial_end}")

            results = []

            daily_query = """
SELECT
    symbol,
    trade_date,
    trade_date AS date,
    open,
    high,
    low,
    close,
    pre_close,
    volume,
    turnover,
    change_pct,
    change_amt,
    amplitude,
    turnover_rate,
    pe_ratio,
    pb_ratio,
    market_cap,
    circulating_market_cap,
    data_source
FROM daily_bar
WHERE trade_date BETWEEN %s AND %s
ORDER BY symbol, trade_date
""".strip()
            results.append(inspect_query(cursor, "daily", daily_query, (daily_start, daily_end), PRICE_FIELDS))

            weekly_query = build_period_query("weekly")
            results.append(inspect_query(cursor, "weekly_aggregated", weekly_query, (daily_start, daily_end), PRICE_FIELDS))

            monthly_query = build_period_query("monthly")
            results.append(inspect_query(cursor, "monthly_aggregated", monthly_query, (daily_start, daily_end), PRICE_FIELDS))

            if minute_start is not None and minute_end is not None:
                minute_query = """
SELECT
    mb.bar_id,
    mb.symbol_id,
    si.symbol,
    mb.timeframe,
    mb.bar_time,
    mb.bar_time AS date,
    mb.open,
    mb.high,
    mb.low,
    mb.close,
    mb.volume,
    mb.turnover,
    mb.vwap,
    mb.created_at
FROM minute_bar mb
JOIN symbol_info si ON si.symbol_id = mb.symbol_id
WHERE mb.bar_time BETWEEN %s AND %s
ORDER BY si.symbol, mb.bar_time
""".strip()
                results.append(inspect_query(cursor, "minute", minute_query, (minute_start, minute_end), MINUTE_FIELDS))
            else:
                results.append(unavailable_result("minute", "minute_bar 无数据，未执行分钟字段验证", MINUTE_FIELDS))

            if financial_start is not None and financial_end is not None:
                financial_query = """
SELECT
    fi.indicator_id,
    fi.symbol_id,
    si.symbol,
    fi.report_date,
    fi.report_date AS date,
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
    fi.equity,
    fi.created_at,
    fi.updated_at
FROM financial_indicator fi
JOIN symbol_info si ON si.symbol_id = fi.symbol_id
WHERE fi.report_date BETWEEN %s AND %s
ORDER BY si.symbol, fi.report_date, fi.report_type
""".strip()
                results.append(inspect_query(cursor, "financial", financial_query, (financial_start, financial_end), FINANCIAL_FIELDS))
            else:
                results.append(unavailable_result("financial", "financial_indicator 当前无数据，未执行财务字段验证", FINANCIAL_FIELDS))

            for result in results:
                print_result(result)

            failed = [result["name"] for result in results if result["missing_fields"] or result["count"] == 0]
            print("\nSUMMARY:")
            if failed:
                print(f"FAILED={failed}")
            else:
                print("FAILED=[]")
    finally:
        conn.close()


if __name__ == "__main__":
    main()