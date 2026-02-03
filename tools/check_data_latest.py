"""
check_data_latest.py
检测数据库日线数据是否为今日，若不是则返回False。
"""
import datetime as dt
import pymysql

MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
}

def is_data_latest():
    today = dt.date.today()
    conn = pymysql.connect(**MYSQL_CONFIG)
    cur = conn.cursor()
    cur.execute("SELECT MAX(trade_date) FROM daily_bar")
    row = cur.fetchone()
    cur.close()
    conn.close()
    if row and row[0]:
        latest = row[0]
        return str(latest) == str(today)
    return False

if __name__ == "__main__":
    print(is_data_latest())
