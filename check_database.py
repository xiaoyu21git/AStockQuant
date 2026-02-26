
import mysql.connector
import sys

def check_database():
    try:
        cnx = mysql.connector.connect(
            user='root',
            password='123456a',
            host='localhost',
            database='astock_quant'
        )
        cursor = cnx.cursor()
        
        # 检查表结构
        cursor.execute("DESCRIBE daily_bar")
        print("Table structure:")
        for row in cursor:
            print(row)
        print()
        
        # 检查最近5天的数据
        cursor.execute("SELECT trade_date, COUNT(*) FROM daily_bar GROUP BY trade_date ORDER BY trade_date DESC LIMIT 10")
        print("Data by date (latest 10):")
        for (date, count) in cursor:
            print(f"{date}: {count} records")
        print()
        
        # 检查特定日期是否有数据
        cursor.execute("SELECT COUNT(*) FROM daily_bar WHERE trade_date = '2026-01-30'")
        count = cursor.fetchone()[0]
        print(f"Records for 2026-01-30: {count}")
        print()
        
        # 如果没有2026-01-30的数据，检查最近的数据
        if count == 0:
            cursor.execute("SELECT trade_date, COUNT(*) FROM daily_bar WHERE trade_date <= '2026-01-30' ORDER BY trade_date DESC LIMIT 1")
            last_date = cursor.fetchone()
            if last_date:
                print(f"Closest date before 2026-01-30: {last_date[0]} with {last_date[1]} records")
            else:
                print("No data before 2026-01-30")
        
        cursor.close()
        cnx.close()
        
    except mysql.connector.Error as err:
        
        
            else:
            if last_date:
            if last_date:
            if last_date:
            if last_date:
            if last_date:
