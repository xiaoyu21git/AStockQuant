import datetime
from tools.update_daily_data import load_symbol_update_targets

target_date = datetime.date(2023, 1, 12)
mode = "history"
targets = load_symbol_update_targets(target_date, mode)

symbol = "600438.SH"
target = None
for t in targets:
    if isinstance(t, dict):
        if t.get('symbol') == symbol:
            target = t
            break
    else:
        if getattr(t, 'symbol', None) == symbol:
            target = t
            break

if target:
    print(f"Target found for {symbol}")
    if isinstance(target, dict):
        for k, v in target.items():
            print(f"  {k}: {v}")
    else:
        print(f"  {target}")
else:
    print(f"600438.SH not found in targets")
