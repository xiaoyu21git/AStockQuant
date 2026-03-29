from __future__ import annotations

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


CREATE_PERIOD_TABLE_SQL = {
    "weekly_bar": """
    CREATE TABLE IF NOT EXISTS weekly_bar (
        id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '行情ID',
        symbol VARCHAR(20) NOT NULL COMMENT '标的代码，如: 600000.SH',
        trade_date DATE NOT NULL COMMENT '周期结束交易日期',
        open DECIMAL(12, 4) NOT NULL COMMENT '开盘价',
        high DECIMAL(12, 4) NOT NULL COMMENT '最高价',
        low DECIMAL(12, 4) NOT NULL COMMENT '最低价',
        close DECIMAL(12, 4) NOT NULL COMMENT '收盘价',
        pre_close DECIMAL(12, 4) DEFAULT NULL COMMENT '前收盘价',
        volume BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '成交量',
        turnover DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '成交额',
        change_pct DECIMAL(8, 4) DEFAULT NULL COMMENT '涨跌幅(%)',
        change_amt DECIMAL(12, 4) DEFAULT NULL COMMENT '涨跌额',
        amplitude DECIMAL(8, 4) DEFAULT NULL COMMENT '振幅(%)',
        turnover_rate DECIMAL(8, 4) DEFAULT NULL COMMENT '换手率(%)',
        pe_ratio DECIMAL(10, 4) DEFAULT NULL COMMENT '市盈率',
        pb_ratio DECIMAL(10, 4) DEFAULT NULL COMMENT '市净率',
        market_cap DECIMAL(20, 4) DEFAULT NULL COMMENT '总市值',
        circulating_market_cap DECIMAL(20, 4) DEFAULT NULL COMMENT '流通市值',
        data_source VARCHAR(50) DEFAULT 'AGGREGATED_DAILY' COMMENT '数据源',
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
        updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
        PRIMARY KEY (id),
        UNIQUE KEY uk_symbol_date (symbol, trade_date),
        KEY idx_symbol (symbol),
        KEY idx_trade_date (trade_date)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='周线行情数据表';
    """,
    "monthly_bar": """
    CREATE TABLE IF NOT EXISTS monthly_bar (
        id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '行情ID',
        symbol VARCHAR(20) NOT NULL COMMENT '标的代码，如: 600000.SH',
        trade_date DATE NOT NULL COMMENT '周期结束交易日期',
        open DECIMAL(12, 4) NOT NULL COMMENT '开盘价',
        high DECIMAL(12, 4) NOT NULL COMMENT '最高价',
        low DECIMAL(12, 4) NOT NULL COMMENT '最低价',
        close DECIMAL(12, 4) NOT NULL COMMENT '收盘价',
        pre_close DECIMAL(12, 4) DEFAULT NULL COMMENT '前收盘价',
        volume BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '成交量',
        turnover DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '成交额',
        change_pct DECIMAL(8, 4) DEFAULT NULL COMMENT '涨跌幅(%)',
        change_amt DECIMAL(12, 4) DEFAULT NULL COMMENT '涨跌额',
        amplitude DECIMAL(8, 4) DEFAULT NULL COMMENT '振幅(%)',
        turnover_rate DECIMAL(8, 4) DEFAULT NULL COMMENT '换手率(%)',
        pe_ratio DECIMAL(10, 4) DEFAULT NULL COMMENT '市盈率',
        pb_ratio DECIMAL(10, 4) DEFAULT NULL COMMENT '市净率',
        market_cap DECIMAL(20, 4) DEFAULT NULL COMMENT '总市值',
        circulating_market_cap DECIMAL(20, 4) DEFAULT NULL COMMENT '流通市值',
        data_source VARCHAR(50) DEFAULT 'AGGREGATED_DAILY' COMMENT '数据源',
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
        updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
        PRIMARY KEY (id),
        UNIQUE KEY uk_symbol_date (symbol, trade_date),
        KEY idx_symbol (symbol),
        KEY idx_trade_date (trade_date)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='月线行情数据表';
    """,
}


def get_connection():
    return pymysql.connect(**MYSQL_CONFIG)


def rebuild_period_table(cursor, table_name: str, period: str) -> None:
    period_expr = "DATE_FORMAT(trade_date, '%Y-%m')" if period == "monthly" else "YEARWEEK(trade_date, 1)"
    cursor.execute(f"TRUNCATE TABLE {table_name}")
    sql = f"""
    INSERT INTO {table_name} (
        symbol, trade_date, open, high, low, close, pre_close, volume, turnover,
        change_pct, change_amt, amplitude, turnover_rate, pe_ratio, pb_ratio,
        market_cap, circulating_market_cap, data_source
    )
    SELECT
        agg.symbol,
        agg.period_end AS trade_date,
        first_day.open AS open,
        agg.high AS high,
        agg.low AS low,
        last_day.close AS close,
        first_day.pre_close AS pre_close,
        agg.volume AS volume,
        agg.turnover AS turnover,
        CASE WHEN first_day.pre_close IS NOT NULL AND first_day.pre_close <> 0
            THEN ((last_day.close - first_day.pre_close) / first_day.pre_close) * 100
            ELSE NULL END AS change_pct,
        CASE WHEN first_day.pre_close IS NOT NULL
            THEN last_day.close - first_day.pre_close
            ELSE NULL END AS change_amt,
        CASE WHEN first_day.pre_close IS NOT NULL AND first_day.pre_close <> 0
            THEN ((agg.high - agg.low) / first_day.pre_close) * 100
            ELSE NULL END AS amplitude,
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
        GROUP BY symbol, {period_expr}
    ) agg
    JOIN daily_bar first_day
        ON first_day.symbol = agg.symbol AND first_day.trade_date = agg.period_start
    JOIN daily_bar last_day
        ON last_day.symbol = agg.symbol AND last_day.trade_date = agg.period_end
    ORDER BY agg.symbol, agg.period_end
    """
    cursor.execute(sql)


def rebuild_cleaned_daily_bar_view(cursor) -> None:
    cursor.execute("DROP VIEW IF EXISTS cleaned_daily_bar")
    cursor.execute(
        """
        CREATE VIEW cleaned_daily_bar AS
        SELECT
            d.*,
            'INCLUDED' AS data_status,
            '默认全量兼容视图' AS cleaning_notes,
            'default_daily_full' AS dataset_id
        FROM daily_bar d
        """
    )


def validate_counts(cursor) -> None:
    for table_name in ("weekly_bar", "monthly_bar", "cleaned_daily_bar"):
        cursor.execute(f"SELECT COUNT(*) FROM {table_name}")
        count = cursor.fetchone()[0]
        print(f"{table_name}: {count}")


def main() -> None:
    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            for table_name, create_sql in CREATE_PERIOD_TABLE_SQL.items():
                cursor.execute(create_sql)
            rebuild_period_table(cursor, "weekly_bar", "weekly")
            rebuild_period_table(cursor, "monthly_bar", "monthly")
            rebuild_cleaned_daily_bar_view(cursor)
        conn.commit()
        with conn.cursor() as cursor:
            validate_counts(cursor)
        print("repair_market_data_tables: OK")
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


if __name__ == "__main__":
    main()