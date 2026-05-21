import json
import os
import sys
import datetime as dt
from concurrent.futures import ThreadPoolExecutor, as_completed
import pymysql

# Set Python path
sys.path.append(os.getcwd())

from tools.update_daily_data import (
    load_symbol_update_targets,
    process_market_update_task
)

# 1) Load stock codes
info_path = r"bin\Debug\cache\datasets\dataset_62_info.json"
with open(info_path, 'r', encoding='utf-8') as f:
    info_data = json.load(f)
stock_codes = set(info_data.get("stockCodes", []))

# 2) Load targets
target_date = dt.date(2023, 1, 12)
print(f"Loading update targets up to {target_date}...")
# load_symbol_update_targets returns (targets_map, symbols_to_add)
targets_map, _ = load_symbol_update_targets(target_date, 'history')

# 3) Filter to dataset_62
# targets_map is dict[symbol, start_date]
tasks = []
for symbol, start_date in targets_map.items():
    if symbol in stock_codes:
        tasks.append({
            "symbol": symbol,
            "category": "stock",
            "start_date": start_date,
            "data_source": "akshare"
        })

print(f"Total tasks in dataset_62: {len(tasks)}")

# 4) Process tasks using process_market_update_task
results = []
if tasks:
    print(f"Processing {len(tasks)} symbols with 6 workers...")
    with ThreadPoolExecutor(max_workers=6) as executor:
        future_to_symbol = {executor.submit(process_market_update_task, task, target_date): task["symbol"] for task in tasks}
        for future in as_completed(future_to_symbol):
            try:
                res = future.result()
                results.append(res)
            except Exception as exc:
                print(f"{future_to_symbol[future]} generated an exception: {exc}")

# 5) Summarize results
# Statuses from source code: "success", "empty", "unavailable", "failure" (implied)
success_count = sum(1 for r in results if r.get("status") == "success")
empty_count = sum(1 for r in results if r.get("status") == "empty")
fail_count = sum(1 for r in results if r.get("status") not in ["success", "empty"])
fetched_rows = sum(r.get("fetched_rows", 0) for r in results)
written_rows = sum(r.get("written_rows", 0) for r in results)

print(f"Success: {success_count}")
print(f"Empty: {empty_count}")
print(f"Fail/Others: {fail_count}")
print(f"Fetched rows: {fetched_rows}")
print(f"Written rows: {written_rows}")

# 6) Query specific stock counts
config = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant"
}
check_symbols = ["600519.SH", "600438.SH", "000333.SZ", "000001.SZ"]
start_date = "2022-01-01"
end_date_str = "2023-01-12"

print("\nVerify results (2022-01-01 to 2023-01-12):")
try:
    conn = pymysql.connect(**config)
    cursor = conn.cursor()
    for sym in check_symbols:
        query = "SELECT COUNT(*) FROM daily_bar WHERE symbol = %s AND trade_date >= %s AND trade_date <= %s AND close > 0"
        cursor.execute(query, (sym, start_date, end_date_str))
        count = cursor.fetchone()[0]
        print(f"{sym}: {count}")
    conn.close()
except Exception as e:
    print(f"DB Error: {e}")
