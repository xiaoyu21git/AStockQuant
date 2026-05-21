import json
import pandas as pd
import numpy as np

data_path = r"G:\C++\AStockQuantEngine\bin\Debug\cache\datasets\dataset_62_data.json"
with open(data_path, "r", encoding="utf-8") as f:
    data = json.load(f)
df = pd.DataFrame(data)

df['is_valid'] = (df['close'] > 0) & (df['post_adjust_factor'] > 0)
df_valid = df[df['is_valid']].copy()
df_valid = df_valid.sort_values(['symbol', 'trade_date'])
df_valid['cum_count'] = df_valid.groupby('symbol').cumcount() + 1

# For each symbol, find the date where it first reaches 281 valid points
reach_281 = df_valid[df_valid['cum_count'] == 281][['symbol', 'trade_date']].rename(columns={'trade_date': 'reach_date'})

if not reach_281.empty:
    first_reach_date = reach_281['reach_date'].min()
    symbols_at_first_date = reach_281[reach_281['reach_date'] == first_reach_date]
    print(f"Earliest date any symbol reaches 281 points: {first_reach_date}")
    print(f"Symbols reaching 281 points on that specific day: {len(symbols_at_first_date)}")
    print(f"Top 5 earliest reach dates:")
    print(reach_281.groupby('reach_date').size().sort_index().head(5))
else:
    print("No symbols reached 281 points.")

