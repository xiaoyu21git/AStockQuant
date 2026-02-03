import jqdatasdk
from datetime import datetime, timedelta
import pandas as pd

# 配置聚宽账号
JQ_USER = "your_jq_user"
JQ_PASS = "your_jq_password"

def jq_auth():
    jqdatasdk.auth(JQ_USER, JQ_PASS)
    info = jqdatasdk.get_query_count()
    print(f"[聚宽] 登录成功，剩余可用条数: {info}")

def fetch_all_a_share_symbols_jq() -> pd.DataFrame:
    """获取全A股列表（聚宽版）"""
    stocks = jqdatasdk.get_all_securities(['stock'], date=None)
    stocks = stocks.reset_index().rename(columns={"code": "symbol"})
    return stocks

def fetch_daily_bars_jq(symbol: str, start: str, end: str) -> pd.DataFrame:
    """获取日线行情（聚宽版）"""
    df = jqdatasdk.get_price(symbol, start_date=start, end_date=end, frequency='daily', panel=False)
    return df

def main():
    jq_auth()
    # 示例：获取全A股列表
    stocks = fetch_all_a_share_symbols_jq()
    print(stocks.head())
    # 示例：获取某只股票日线
    df = fetch_daily_bars_jq('000001.XSHE', start='2024-01-01', end='2024-01-10')
    print(df)

if __name__ == '__main__':
    main()
