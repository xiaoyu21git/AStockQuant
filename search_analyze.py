import os
import json
import sys
from datetime import datetime

def analyze_info(path):
    print(f"\n--- Info File Analysis: {path} ---")
    try:
        with open(path, 'r', encoding='utf-8') as f:
            data = json.load(f)
        fields = ['displayName', 'sourceType', 'rowCount', 'isBacktestReady', 'startDate', 'endDate', 'tags']
        for field in fields:
            print(f"{field}: {data.get(field)}")
        
        available_fields = data.get('availableFields', [])
        print(f"availableFields (first 50): {available_fields[:50]}")
        
        stock_codes = data.get('stockCodes', [])
        print(f"stockCodes (first 10): {stock_codes[:10]}")
    except Exception as e:
        print(f"Error reading info file: {e}")

def analyze_data(path):
    print(f"\n--- Data File Analysis: {path} ---")
    try:
        with open(path, 'r', encoding='utf-8') as f:
            data = json.load(f)
        
        if isinstance(data, list):
            row_count = len(data)
            print(f"JSON Array Length (Rows): {row_count}")
            if row_count > 0:
                keys = set()
                dates = set()
                symbols = set()
                for row in data:
                    keys.update(row.keys())
                    if 'trade_date' in row: dates.add(row['trade_date'])
                    if 'symbol' in row: symbols.add(row['symbol'])
                
                print(f"Field names (first 80): {sorted(list(keys))[:80]}")
                print(f"Unique trade_date count: {len(dates)}")
                print(f"Unique symbol count: {len(symbols)}")
        else:
            print("Data is not a JSON array.")
    except Exception as e:
        print(f"Error reading data file: {e}")

search_paths = sys.argv[1:]
targets = ['dataset_48_info.json', 'dataset_48_data.json']
found_files = []

for p in search_paths:
    if not os.path.exists(p): continue
    for root, dirs, files in os.walk(p):
        if 'DataServiceCache' in root:
            for t in targets:
                if t in files:
                    full_path = os.path.join(root, t)
                    mtime = os.path.getmtime(full_path)
                    found_files.append({'path': full_path, 'name': t, 'mtime': mtime})

if not found_files:
    print("No target files found in DataServiceCache folders.")
    sys.exit(0)

# Group by name and sort by mtime
info_files = sorted([f for f in found_files if f['name'] == 'dataset_48_info.json'], key=lambda x: x['mtime'], reverse=True)
data_files = sorted([f for f in found_files if f['name'] == 'dataset_48_data.json'], key=lambda x: x['mtime'], reverse=True)

print("Found Info Files:")
for f in info_files: print(f"{datetime.fromtimestamp(f['mtime'])} - {f['path']}")

print("\nFound Data Files:")
for f in data_files: print(f"{datetime.fromtimestamp(f['mtime'])} - {f['path']}")

if info_files:
    analyze_info(info_files[0]['path'])
if data_files:
    analyze_data(data_files[0]['path'])
