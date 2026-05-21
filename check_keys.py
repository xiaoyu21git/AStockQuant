import pandas as pd
import json

file_path = r"bin\Debug\cache\datasets\dataset_62_data.json"
df = pd.read_json(file_path)
df['trade_date_dt'] = pd.to_datetime(df['trade_date'])

# Filter for 600519.SH between 2023-01-03 and 2023-01-11
sample = df[(df['symbol'] == '600519.SH') & (df['trade_date_dt'] >= '2023-01-03') & (df['trade_date_dt'] <= '2023-01-11')].head(1)

if not sample.empty:
    keys = sample.columns.tolist()
    print(f"Sample Keys: {keys}")
    check_keys = ['post_adjust_factor', 'pre_adjust_factor', 'adj_factor']
    for k in check_keys:
        print(f"Key '{k}' exists: {k in keys}")
else:
    print("No records found for 600519.SH in the specified date range.")
