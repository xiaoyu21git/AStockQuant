import pymysql
import json
import collections

# 1. Database Connection and Config Retrieval
try:
    conn = pymysql.connect(
        host='127.0.0.1', 
        port=3306,
        user='root', 
        password='123456a', 
        db='astock_quant', 
        charset='utf8mb4', 
        cursorclass=pymysql.cursors.DictCursor
    )
    with conn.cursor() as cursor:
        cursor.execute("SELECT full_config FROM factor_instance WHERE instance_id='___________________252_1777305617877_33e3b8ca'")
        result = cursor.fetchone()
        if not result:
            print("Error: Instance not found")
            exit()
        full_config = json.loads(result['full_config'])
        
    conn.close()
except Exception as e:
    print(f"Database Error: {e}")
    exit()

# 2. Extract Config
cfg = {
    'factorType': full_config.get('factorType'),
    'growthMetrics': full_config.get('growthMetrics', []),
    'growthWeights': full_config.get('growthWeights', []),
    'lookbackWindow': int(full_config.get('lookbackWindow', 60)),
    'laggedEnabled': full_config.get('laggedEnabled'),
    'lagPeriods': full_config.get('lagPeriods'),
    'frequency': full_config.get('frequency'),
    'neutralizationEnabled': full_config.get('neutralizationEnabled')
}
print(f"Config: {cfg}")

# mapping metrics to required data fields
metric_to_fields = {
    'Revenue Growth': ['total_revenue'],
    'Net Profit Growth': ['net_profit'],
    'ROE Growth': ['roe'],
    'SUE': ['eps']
}
req_fields = set()
for m in cfg['growthMetrics']:
    for f in metric_to_fields.get(m, []):
        req_fields.add(f)

# 3. Load Dataset
data_path = r'C:\Users\wang\AppData\Local\astockquantapp-exe\datasets\dataset_48_data.json'
with open(data_path, 'r', encoding='utf-8') as f:
    raw_data = json.load(f)

# Organize data by date and symbol
# data_by_date[date][symbol] = {field: value}
data_by_date = collections.defaultdict(dict)
all_dates = set()
for item in raw_data:
    dt = item.get('trade_date')
    sym = item.get('symbol')
    if dt and sym:
        all_dates.add(dt)
        data_by_date[dt][sym] = item

sorted_dates = sorted(list(all_dates))

# 4. Simulation Logic
# Sequence requirement: 2 for most, 5 for SUE (EPS)
def count_history(target_date_idx, symbol, field, min_len):
    count = 0
    # Search backwards from target_date_idx (inclusive)
    for i in range(target_date_idx, -1, -1):
        d = sorted_dates[i]
        if field in data_by_date[d].get(symbol, {}) and data_by_date[d][symbol][field] is not None:
            count += 1
            if count >= min_len:
                return True
    return False

results = []
# Analyze each trade_date
# For performance, we skip very early dates
for i, trade_date in enumerate(sorted_dates):
    # resolveGrowthEffectiveDate logic: 
    # find first date in [trade_date - lookbackWindow, trade_date] that has any required field data
    # (Simplified: check if any symbol has any seq data at this date)
    effective_date = None
    lookback = cfg['lookbackWindow']
    start_idx = max(0, i - lookback)
    
    # In C++, it usually picks the latest date within window that is non-empty.
    for offset in range(0, lookback + 1):
        check_idx = i - offset
        if check_idx < 0: break
        check_date = sorted_dates[check_idx]
        if data_by_date[check_date]:
            effective_date = check_date
            effective_idx = check_idx
            break
    
    if not effective_date:
        continue

    # Count valid symbols at effective_date
    symbols_at_eff = data_by_date[effective_date].keys()
    
    metrics_valid_counts = collections.defaultdict(int)
    combined_valid_symbols = set()
    
    for sym in symbols_at_eff:
        sym_valid_for_any_metric = False
        for m in cfg['growthMetrics']:
            fields = metric_to_fields.get(m, [])
            # Check sequence length for each field in metric
            min_req = 5 if m == 'SUE' else 2
            
            is_metric_valid = True
            for f in fields:
                if not count_history(effective_idx, sym, f, min_req):
                    is_metric_valid = False
                    break
            
            if is_metric_valid:
                metrics_valid_counts[m] += 1
                sym_valid_for_any_metric = True
        
        if sym_valid_for_any_metric:
            combined_valid_symbols.add(sym)
            
    results.append({
        'trade_date': trade_date,
        'effective_date': effective_date,
        'combined_count': len(combined_valid_symbols),
        'metric_counts': dict(metrics_valid_counts)
    })

# 4. Output
results.sort(key=lambda x: x['combined_count'])
print("\nTop 20 dates with lowest combined symbol count:")
for r in results[:20]:
    print(f"Date: {r['trade_date']} | EffDate: {r['effective_date']} | Combined: {r['combined_count']} | Details: {r['metric_counts']}")

# 5. Conclusion
# Analyze min counts to find bottleneck
all_metric_totals = collections.defaultdict(int)
for r in results:
    for m, c in r['metric_counts'].items():
        all_metric_totals[m] += c

print("\nConclusion:")
if not results:
    print("No data processed.")
else:
    # Estimate bottleneck
    avg_counts = {m: all_metric_totals[m]/len(results) for m in all_metric_totals}
    bottleneck = min(avg_counts, key=avg_counts.get) if avg_counts else "Unknown"
    print(f"样本数收缩的主因是 {bottleneck} 指标的历史序列长度不满足要求（尤其是早期日期或高频字段缺失）。")
