import pymysql
import json
import os

try:
    conn = pymysql.connect(host='localhost', user='root', password='', db='astock_quant', charset='utf8mb4', cursorclass=pymysql.cursors.DictCursor)
    with conn.cursor() as cursor:
        cursor.execute("SELECT full_config FROM factor_instance WHERE instance_id='___________________252_1777305617877_33e3b8ca'")
        result = cursor.fetchone()
        if result:
            config = json.loads(result['full_config'])
            # Extract requested fields
            keys = ['factorType', 'growthMetrics', 'growthWeights', 'lookbackWindow', 'laggedEnabled', 'lagPeriods', 'frequency', 'neutralizationEnabled']
            extracted = {k: config.get(k) for k in keys}
            print("CONFIG_JSON:" + json.dumps(extracted))
        else:
            print("Instance not found")
    conn.close()
except Exception as e:
    print(f"Error: {e}")
