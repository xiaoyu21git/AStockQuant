import json
data_path = r'C:\Users\wang\AppData\Local\astockquantapp-exe\datasets\dataset_48_data.json'
with open(data_path, 'r', encoding='utf-8') as f:
    data = json.load(f)
dates = sorted(list(set(r.get('trade_date') for r in data if r.get('trade_date'))), reverse=True)
print('Recent Dates in Dataset:', dates[:5])
for fld in ['total_revenue', 'net_profit', 'market_cap', 'industry_code']:
    recent_day = next((d for d in dates if any(r.get(fld) is not None and r.get('trade_date') == d for r in data)), None)
    count = sum(1 for r in data if r.get(fld) is not None and r.get('trade_date') == recent_day)
    print(f'Field: {fld} | Latest Date with data: {recent_day} | Count: {count}')
