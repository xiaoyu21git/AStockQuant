import json
import collections

# 1. Config (Adjusted to match raw JSON)
# Raw JSON uses "growthMetrics": [0, 1] in 'calculation' or ['revenue_growth', 'net_profit_growth'] in 'parameters'
# 0 -> Revenue Growth, 1 -> Net Profit Growth
cfg = {
    'factorType': 4,
    'growthMetrics': ['Revenue Growth', 'Net Profit Growth'], # Based on raw ["revenue_growth", "net_profit_growth"]
    'lookbackWindow': 252,
}

# mapping metrics to required data fields
metric_to_fields = {
    'Revenue Growth': ['total_revenue'],
    'Net Profit Growth': ['net_profit'],
    'ROE Growth': ['roe'],
    'SUE': ['eps']
}

# 2. Load Dataset
data_path = r'C:\Users\wang\AppData\Local\astockquantapp-exe\datasets\dataset_48_data.json'
with open(data_path, 'r', encoding='utf-8') as f:
    raw_data = json.load(f)

data_by_date = collections.defaultdict(dict)
all_dates = set()
for item in raw_data:
    dt = item.get('trade_date')
    sym = item.get('symbol')
    if dt and sym:
        all_dates.add(dt)
        data_by_date[dt][sym] = item

sorted_dates = sorted(list(all_dates))

# 3. Simulation Logic
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
for i, trade_date in enumerate(sorted_dates):
    effective_date = None
    lookback = cfg['lookbackWindow']
    
    # resolveGrowthEffectiveDate logic: find latest date in window [i-lookback, i] that is non-empty
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

    symbols_at_eff = data_by_date[effective_date].keys()
    metrics_valid_counts = collections.defaultdict(int)
    field_valid_counts = collections.defaultdict(int)
    combined_valid_symbols = set()
    
    for sym in symbols_at_eff:
        sym_valid_for_any_metric = False
        for m in cfg['growthMetrics']:
            fields = metric_to_fields.get(m, [])
            min_req = 5 if m == 'SUE' else 2
            
            is_metric_valid = True
            for f in fields:
                if count_history(effective_idx, sym, f, min_req):
                    field_valid_counts[f] += 1
                else:
                    is_metric_valid = False
            
            if is_metric_valid:
                metrics_valid_counts[m] += 1
                sym_valid_for_any_metric = True
        
        if sym_valid_for_any_metric:
            combined_valid_symbols.add(sym)
            
    results.append({
        'trade_date': trade_date,
        'effective_date': effective_date,
        'combined_count': len(combined_valid_symbols),
        'metrics': dict(metrics_valid_counts),
        'fields': dict(field_valid_counts)
    })

# 4. Output
results.sort(key=lambda x: x['combined_count'])
print(f"Config: {cfg}")
print("\nTop 20 dates with lowest combined symbol count:")
for r in results[:20]:
    print(f"Date: {r['trade_date']} | EffDate: {r['effective_date']} | Combined: {r['combined_count']} | Metric Detail: {r['metrics']} | Field Detail: {r['fields']}")

# 5. Conclusion
growth_metrics = cfg['growthMetrics']
metric_totals = {m: sum(r['metrics'].get(m, 0) for r in results) for m in growth_metrics}
print("\nConclusion:")
if not results:
    print("No data processed.")
else:
    avg_counts = {m: metric_totals[m]/len(results) for m in metric_totals}
    # Check if a specific metric is the bottleneck
    bottleneck = min(avg_counts, key=avg_counts.get) if avg_counts else "Unknown"
    
    # Check if general field availability is low
    all_fields = set()
    for m in growth_metrics: 
        for f in metric_to_fields[m]: all_fields.add(f)
    field_totals = {f: sum(r['fields'].get(f, 0) for r in results) for f in all_fields}
    field_bottleneck = min(field_totals, key=field_totals.get) if field_totals else "Unknown"
    
    print(f"样本数收缩的主因是 {field_bottleneck} 字段的历史序列长度不满足要求（需要至少 2 个历史点），导致 {bottleneck} 指标在大量日期下计算结果为空。")
