"""
trim_financial_data.py
清理 financial_indicator 和 financial_indicator_daily 中 2015-01-01 之前的数据
"""

import argparse
import pymysql

MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
}

CUTOFF_DATE = "2015-01-01"

def main():
    parser = argparse.ArgumentParser(description="删除 financial_indicator / financial_indicator_daily 中 2015-01-01 前的历史数据")
    parser.add_argument("--dry-run", action="store_true", help="仅统计，不执行删除")
    args = parser.parse_args()

    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cur:
            # 统计
            cur.execute("SELECT COUNT(*) FROM financial_indicator WHERE report_date < %s", (CUTOFF_DATE,))
            fi_pre = cur.fetchone()[0]
            cur.execute("SELECT COUNT(*) FROM financial_indicator WHERE report_date >= %s", (CUTOFF_DATE,))
            fi_post = cur.fetchone()[0]
            cur.execute("SELECT COUNT(*) FROM financial_indicator_daily WHERE trade_date < %s", (CUTOFF_DATE,))
            fid_pre = cur.fetchone()[0]
            cur.execute("SELECT COUNT(*) FROM financial_indicator_daily WHERE trade_date >= %s", (CUTOFF_DATE,))
            fid_post = cur.fetchone()[0]

            print(f"financial_indicator:     pre-2015 = {fi_pre:>10,} rows, post-2015 = {fi_post:>10,} rows")
            print(f"financial_indicator_daily: pre-2015 = {fid_pre:>10,} rows, post-2015 = {fid_post:>10,} rows")

            if args.dry_run:
                print("\n[DRY RUN] 不会执行删除")
                return

            # 删除
            cur.execute("DELETE FROM financial_indicator_daily WHERE trade_date < %s", (CUTOFF_DATE,))
            fid_deleted = cur.rowcount
            cur.execute("DELETE FROM financial_indicator WHERE report_date < %s", (CUTOFF_DATE,))
            fi_deleted = cur.rowcount
            conn.commit()

            print(f"\n已删除: financial_indicator_daily {fid_deleted} rows, financial_indicator {fi_deleted} rows")

    finally:
        conn.close()


if __name__ == "__main__":
    main()