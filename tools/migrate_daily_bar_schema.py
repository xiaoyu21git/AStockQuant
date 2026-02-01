"""一次性迁移脚本：重建 daily_bar 表和 v_daily_bar 视图，使其与 C++ / Python 模型以及导入脚本对齐。

注意：
- 当前环境下 daily_bar 为空（我们已经检查过），所以直接 DROP + CREATE 不会丢失有效数据。
- 若你之后在其他环境使用本脚本，请先确认 daily_bar 是否有需要保留的数据。
"""

import pymysql

MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
}

DDL = r"""
DROP VIEW IF EXISTS `v_daily_bar`;
DROP TABLE IF EXISTS `daily_bar`;

CREATE TABLE IF NOT EXISTS `daily_bar` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '行情ID',
    `symbol` VARCHAR(20) NOT NULL COMMENT '标的代码，如: 600000.SH',
    `trade_date` DATE NOT NULL COMMENT '交易日期',
    `open` DECIMAL(12, 4) NOT NULL COMMENT '开盘价',
    `high` DECIMAL(12, 4) NOT NULL COMMENT '最高价',
    `low` DECIMAL(12, 4) NOT NULL COMMENT '最低价',
    `close` DECIMAL(12, 4) NOT NULL COMMENT '收盘价',
    `pre_close` DECIMAL(12, 4) DEFAULT NULL COMMENT '前收盘价',
    `volume` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '成交量(股/手)',
    `turnover` DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '成交额(元)',
    `change_pct` DECIMAL(8, 4) DEFAULT NULL COMMENT '涨跌幅(%)',
    `change_amt` DECIMAL(12, 4) DEFAULT NULL COMMENT '涨跌额',
    `amplitude` DECIMAL(8, 4) DEFAULT NULL COMMENT '振幅(%)',
    `turnover_rate` DECIMAL(8, 4) DEFAULT NULL COMMENT '换手率(%)',
    `pe_ratio` DECIMAL(10, 4) DEFAULT NULL COMMENT '市盈率',
    `pb_ratio` DECIMAL(10, 4) DEFAULT NULL COMMENT '市净率',
    `market_cap` DECIMAL(20, 4) DEFAULT NULL COMMENT '总市值',
    `circulating_market_cap` DECIMAL(20, 4) DEFAULT NULL COMMENT '流通市值',
    `data_source` VARCHAR(50) DEFAULT 'UNKNOWN' COMMENT '数据源',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_symbol_date` (`symbol`, `trade_date`),
    KEY `idx_symbol` (`symbol`),
    KEY `idx_trade_date` (`trade_date`),
    CONSTRAINT `fk_daily_bar_symbol` FOREIGN KEY (`symbol`) REFERENCES `symbol_info` (`symbol`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='日线行情数据表';

CREATE OR REPLACE VIEW `v_daily_bar` AS
SELECT 
    db.`id` AS `bar_id`,
    si.`symbol`,
    si.`name`,
    si.`exchange`,
    db.`trade_date`,
    db.`open`,
    db.`high`,
    db.`low`,
    db.`close`,
    db.`volume`,
    db.`turnover`,
    db.`change_pct`,
    db.`turnover_rate`
FROM `daily_bar` db
JOIN `symbol_info` si ON db.`symbol` = si.`symbol`
WHERE si.`status` = 'active';
"""


def main() -> None:
    conn = pymysql.connect(
        host=MYSQL_CONFIG["host"],
        port=MYSQL_CONFIG["port"],
        user=MYSQL_CONFIG["user"],
        password=MYSQL_CONFIG["password"],
        database=MYSQL_CONFIG["database"],
        charset=MYSQL_CONFIG["charset"],
        autocommit=True,
    )
    cur = conn.cursor()
    print("[migrate] 重建 daily_bar 表和 v_daily_bar 视图...")
    for statement in DDL.split(";\n"):
        stmt = statement.strip()
        if not stmt:
            continue
        cur.execute(stmt + ";")
    cur.close()
    conn.close()
    print("[migrate] 完成")


if __name__ == "__main__":
    main()
