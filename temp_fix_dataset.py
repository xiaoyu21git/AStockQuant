import json
import pymysql
import datetime as dt
from concurrent.futures import ThreadPoolExecutor, as_completed
import sys
import os

# Set up project root
PROJECT_ROOT = r"G:\C++\AStockQuantEngine"
if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)

from tools.update_daily_data import process_market_update_task

config = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant"
}

target_date_str = "2023-01-12"
target_date = dt.datetime.strptime(target_date_str, "%Y-%m-%d").date()

def get_stats(cursor, codes):
    if not codes: return {}
    placeholders = ",".join(["%s"] * len(codes))
    query = f"""
        SELECT symbol, COUNT(*) 
        FROM daily_bar 
        WHERE symbol IN ({placeholders}) AND trade_date <= %s AND close > 0 
        GROUP BY symbol
    """
    cursor.execute(query, codes + [target_date_str])
    results = {row[0]: row[1] for row in cursor.fetchall()}
    return results

def main():
    info_path = r"bin\Debug\cache\datasets\dataset_62_info.json"
    with open(info_path, "r", encoding="utf-8") as f:
        info = json.load(f)
    stock_codes = info["stockCodes"]
    
    conn = pymysql.connect(**config)
    cursor = conn.cursor()
    
    # 2) Find candidates with count < 281
    counts = get_stats(cursor, stock_codes)
    candidates = []
    for code in stock_codes:
        cnt = counts.get(code, 0)
        if cnt < 281:
            cursor.execute("SELECT list_date FROM symbol_info WHERE symbol = %s", (code,))
            row = cursor.fetchone()
            if row and row[0]:
                list_date = row[0]
                if isinstance(list_date, str):
                    list_date = dt.datetime.strptime(list_date, "%Y-%m-%d").date()
                candidates.append((code, list_date, cnt))
            else:
                # Default to some old date if not found
                candidates.append((code, dt.date(2010, 1, 1), cnt))

    print(f"Found {len(candidates)} candidates for backfill.")
    
    # 3) Backfill
    tasks = []
    for code, list_date, cnt in candidates:
        tasks.append({
            "task": {
                "symbol": code,
                "category": "stock",
                "start_date": list_date, # Pass as datetime.date object
                "data_source": "akshare"
            },
            "target_date": target_date
        })

    with ThreadPoolExecutor(max_workers=4) as executor:
        futures = {executor.submit(process_market_update_task, t["task"], t["target_date"]): t for t in tasks}
        total = len(tasks)
        for i, future in enumerate(as_completed(futures)):
            try:
                res = future.result()
                if (i+1) % 10 == 0:
                    print(f"Progress: {i+1}/{total} symbols processed")
            except Exception as e:
                # print(f"Error processing {futures[future]['task']['symbol']}: {e}")
                pass

    # 4) Recount
    new_counts = get_stats(cursor, stock_codes)
    final_under_281 = [(code, new_counts.get(code, 0)) for code in stock_codes if new_counts.get(code, 0) < 281]
    
    print(f"\nAfter backfill, {len(final_under_281)} symbols still have count < 281.")
    print("Top 20 symbols with count < 281:")
    for code, cnt in sorted(final_under_281, key=lambda x: x[1])[:20]:
        print(f"  {code}: {cnt}")

    # 5) Specific symbols
    specials = ["600438.SH", "600460.SH", "600519.SH", "000333.SZ"]
    print("\nSpecific symbol counts:")
    for code in specials:
        print(f"  {code}: {new_counts.get(code, 0)}")

    conn.close()

if __name__ == "__main__":
    main()
