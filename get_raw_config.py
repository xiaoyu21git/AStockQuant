import pymysql
import json

try:
    conn = pymysql.connect(host='127.0.0.1', port=3306, user='root', password='123456a', db='astock_quant', charset='utf8mb4', cursorclass=pymysql.cursors.DictCursor)
    with conn.cursor() as cursor:
        cursor.execute("SELECT full_config FROM factor_instance WHERE instance_id='___________________252_1777305617877_33e3b8ca'")
        result = cursor.fetchone()
        if result:
            print("FULL_CONFIG_RAW:" + result['full_config'])
    conn.close()
except Exception as e:
    print(f"Error: {e}")
