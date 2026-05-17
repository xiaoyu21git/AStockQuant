import json
import math

filepath = r'C:\Users\wang\AppData\Local\astockquantapp-exe\datasets\dataset_48_data.json'

with open(filepath, 'r', encoding='utf-8') as f:
    data = json.load(f)

stats = {}

for row in data:
    d = row.get('trade_date')
    if not d:
        continue
    
    if d not in stats:
        stats[d] = {'total': 0, 'ind': 0, 'cap': 0, 'overlap': 0}
    
    stats[d]['total'] += 1
    
    ind_ok = bool(row.get('industry_code'))
    
    mcap = row.get('market_cap')
    cap_ok = False
    if mcap is not None:
        try:
            val = float(mcap)
            if math.isfinite(val) and val > 0:
                cap_ok = True
        except (ValueError, TypeError):
            pass
            
    if ind_ok:
        stats[d]['ind'] += 1
    if cap_ok:
        stats[d]['cap'] += 1
    if ind_ok and cap_ok:
        stats[d]['overlap'] += 1

sorted_dates = sorted(stats.keys())

print(f"Total dates: {len(sorted_dates)}")
print("\nEarliest 10 days:")
for d in sorted_dates[:10]:
    s = stats[d]
    print(f"{d}: total={s['total']}, ind={s['ind']}, cap={s['cap']}, overlap={s['overlap']}")

print("\nLatest 10 days:")
for d in sorted_dates[-10:]:
    s = stats[d]
    print(f"{d}: total={s['total']}, ind={s['ind']}, cap={s['cap']}, overlap={s['overlap']}")

low_overlap_dates = [d for d in sorted_dates if stats[d]['overlap'] < 3]
print(f"\nDates with overlap < 3: Count={len(low_overlap_dates)}")
if low_overlap_dates:
    print(f"Sample low overlap dates: {low_overlap_dates[:20]}")

range_dates = [d for d in sorted_dates if '2023-01-03' <= d <= '2026-05-13']
if range_dates:
    overlaps = [stats[d]['overlap'] for d in range_dates]
    overlaps.sort()
    min_o = overlaps[0]
    max_o = overlaps[-1]
    med_o = overlaps[len(overlaps)//2]
    print(f"\nRange 2023-01-03 to 2026-05-13 ({len(range_dates)} days):")
    print(f"Overlap Stats: Min={min_o}, Median={med_o}, Max={max_o}")
else:
    print("\nNo dates found in range 2023-01-03 to 2026-05-13")
