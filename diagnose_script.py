import pymysql
import json
import os

db_config = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
    "cursorclass": pymysql.cursors.DictCursor
}

def diagnose():
    try:
        connection = pymysql.connect(**db_config)
        with connection.cursor() as cursor:
            # 2) 查询 factors 总数
            cursor.execute("SELECT COUNT(*) as count FROM factors")
            factors_count = cursor.fetchone()["count"]
            print(f"Factors Total Count: {factors_count}")

            # 3) 查询 factor_instance 中 ACTIVE 总数
            cursor.execute("SELECT COUNT(*) as count FROM factor_instance WHERE status = 'ACTIVE'")
            active_instances_count = cursor.fetchone()["count"]
            print(f"Active Instances Total Count: {active_instances_count}")

            # 4) 查询前 30 个 factor_id 对应 active instance 数
            cursor.execute("""
                SELECT f.factor_id, COUNT(fi.instance_id) as active_count
                FROM factors f
                LEFT JOIN factor_instance fi ON f.factor_id = fi.factor_id AND fi.status = 'ACTIVE'
                GROUP BY f.factor_id
                ORDER BY f.factor_id ASC
                LIMIT 30
            """)
            print("\nTop 30 Factor IDs and Active Instance Counts:")
            for row in cursor.fetchall():
                print(f"ID: {row['factor_id']}, Active Count: {row['active_count']}")

            # 5) 查询最近 20 条 ACTIVE 的 factor_id, instance_id, status
            cursor.execute("SELECT factor_id, instance_id, status FROM factor_instance WHERE status = 'ACTIVE' ORDER BY instance_id DESC LIMIT 20")
            print("\nLatest 20 ACTIVE Instances:")
            for row in cursor.fetchall():
                print(row)

            # 6) 抽样统计 active instances 的 full_config.factorType
            cursor.execute("SELECT instance_id, full_config FROM factor_instance WHERE status = 'ACTIVE'")
            instances = cursor.fetchall()
            type_stats = {"number": 0, "string": 0, "missing": 0}
            raw_samples = []
            for inst in instances:
                config = inst.get("full_config")
                f_type = None
                if config:
                    try:
                        cfg_json = json.loads(config)
                        f_type = cfg_json.get("factorType")
                    except:
                        pass
                
                if f_type is None:
                    type_stats["missing"] += 1
                elif isinstance(f_type, (int, float)):
                    type_stats["number"] += 1
                elif isinstance(f_type, str):
                    type_stats["string"] += 1
                else:
                    type_stats["missing"] += 1
                
                if len(raw_samples) < 10:
                    raw_samples.append((inst["instance_id"], f_type))
            
            print(f"\nActive Instance FactorType Stats: {type_stats}")
            print("First 10 Samples (InstanceID, factorType):")
            for s in raw_samples:
                print(s)

            # 7) & 8) 统计没有映射的 factor_id
            cursor.execute("""
                SELECT factor_id FROM factors 
                WHERE factor_id NOT IN (SELECT DISTINCT factor_id FROM factor_instance WHERE status = 'ACTIVE')
            """)
            missing_mapping = [row["factor_id"] for row in cursor.fetchall()]
            print(f"\nFactors without ACTIVE instances: Total {len(missing_mapping)}")
            print(f"First 20 missing: {missing_mapping[:20]}")

    except Exception as e:
        print(f"\nDatabase Connection Failed: {e}")
    finally:
        if 'connection' in locals() and connection.open:
            connection.close()

    # 9) DataServiceCache dataset 48
    dataset_base = "G:\\C++\\AStockQuantEngine\\DataServiceCache\\PersistentDatasets"
    info_path = os.path.join(dataset_base, "dataset_48_info.json")
    data_path = os.path.join(dataset_base, "dataset_48_data.json")

    print(f"\nDataset 48 Diagnostics (Path: {dataset_base}):")
    if os.path.exists(info_path):
        with open(info_path, 'r', encoding='utf-8') as f:
            info = json.load(f)
            fields = ["displayName", "sourceType", "rowCount", "isBacktestReady", "startDate", "endDate"]
            for field in fields:
                print(f"{field}: {info.get(field)}")
            print(f"availableFields (Top 40): {info.get('availableFields', [])[:40]}")
            print(f"stockCodes (Top 10): {info.get('stockCodes', [])[:10]}")
    else:
        print(f"Info file not found: {info_path}")

    if os.path.exists(data_path):
        with open(data_path, 'r', encoding='utf-8') as f:
            data = json.load(f)
            if isinstance(data, list):
                print(f"Data Real Row Count: {len(data)}")
                if len(data) > 0:
                    keys = set()
                    trade_dates = set()
                    symbols = set()
                    for entry in data:
                        keys.update(entry.keys())
                        if 'trade_date' in entry: trade_dates.add(entry['trade_date'])
                        if 'symbol' in entry: symbols.add(entry['symbol'])
                    print(f"Real Fields (Top 60): {list(keys)[:60]}")
                    print(f"Unique trade_date count: {len(trade_dates)}")
                    print(f"Unique symbol count: {len(symbols)}")
            else:
                print("Data file format unexpected (expected list).")
    else:
        print(f"Data file not found: {data_path}")

if __name__ == "__main__":
    diagnose()
