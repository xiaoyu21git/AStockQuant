"""
update_daily_data.py
收盘后自动拉取并更新A股日线行情数据，写入数据库。
"""
import datetime as dt
from astock_engine.data.providers.stock_provider import StockDataProvider
from astock_engine.data.database.repository import DatabaseRepository
from astock_engine.data.providers.base_provider import DataType



def main():
    today = dt.date.today()
    provider = StockDataProvider()
    stock_df = provider.get_stock_list()
    if stock_df.empty:
        print("未获取到股票列表")
        return
    symbols = stock_df['symbol'].tolist()
    from astock_engine.data.providers.base_provider import DataQuery
    query = DataQuery(symbols=symbols, start_date=today, end_date=today)
    resp = provider.get_data(query, DataType.STOCK_DAILY)
    if resp.data is not None and not resp.data.empty:
        DatabaseRepository.save_daily_bars(resp.data)
        print(f"已更新 {len(resp.data)} 条日线数据")
    else:
        print("今日无新数据或全部失败")

if __name__ == "__main__":
    main()
