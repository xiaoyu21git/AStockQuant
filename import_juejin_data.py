#!/usr/bin/env python3
"""
掘金数据导入工具
从掘金API获取数据并保存到MySQL数据库
"""

import sys
import os
import argparse
from datetime import datetime, timedelta

# 添加项目路径
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

def import_juejin_data():
    """导入掘金数据到数据库"""
    print("=" * 60)
    print("📊 掘金数据导入工具")
    print("=" * 60)
    
    try:
        # 解析命令行参数
        parser = argparse.ArgumentParser(description='从掘金API导入数据到MySQL数据库')
        parser.add_argument('--symbols', type=str, default='600000.SH,000001.SZ',
                          help='股票代码列表，用逗号分隔（默认：600000.SH,000001.SZ）')
        parser.add_argument('--start-date', type=str, default='2024-01-01',
                          help='开始日期（格式：YYYY-MM-DD，默认：2024-01-01）')
        parser.add_argument('--end-date', type=str, default='2024-12-31',
                          help='结束日期（格式：YYYY-MM-DD，默认：2024-12-31）')
        parser.add_argument('--data-type', type=str, default='daily',
                          choices=['daily', 'minute'], help='数据类型（默认：daily）')
        parser.add_argument('--save-to-db', action='store_true',
                          help='保存数据到数据库')
        parser.add_argument('--test-only', action='store_true',
                          help='仅测试，不保存数据')
        
        args = parser.parse_args()
        
        # 解析股票代码
        symbols = [s.strip() for s in args.symbols.split(',') if s.strip()]
        
        print(f"📈 导入配置:")
        print(f"  股票代码: {symbols}")
        print(f"  时间范围: {args.start_date} 到 {args.end_date}")
        print(f"  数据类型: {args.data_type}")
        print(f"  保存到数据库: {'是' if args.save_to_db else '否'}")
        print(f"  仅测试模式: {'是' if args.test_only else '否'}")
        
        # 导入掘金数据源
        from astock_engine.data.juejin_data_source import JuejinDataSource
        
        print(f"\n🔧 初始化掘金数据源...")
        data_source = JuejinDataSource()
        
        if not data_source.initialize():
            print("❌ 数据源初始化失败")
            return False
        
        print("✅ 数据源初始化成功")
        
        if not data_source.connect():
            print("❌ 连接掘金平台失败")
            return False
        
        print("✅ 连接到掘金平台")
        
        # 获取历史数据
        all_data = []
        
        for symbol in symbols:
            print(f"\n📊 获取 {symbol} 的历史数据...")
            
            historical_data = data_source.get_historical_data(
                symbol, 
                args.start_date, 
                args.end_date
            )
            
            if historical_data:
                print(f"✅ 获取 {symbol} 数据成功: {len(historical_data)}条")
                all_data.extend(historical_data)
                
                # 显示数据示例
                print(f"📈 数据示例:")
                for i, data in enumerate(historical_data[:3]):
                    print(f"  {i+1}. {data['date']}: 开盘={data['open']}, 收盘={data['close']}, 成交量={data['volume']}")
            else:
                print(f"❌ 获取 {symbol} 数据失败")
        
        if not all_data:
            print("\n❌ 没有获取到任何数据")
            data_source.shutdown()
            return False
        
        print(f"\n📊 总计获取数据: {len(all_data)}条")
        
        # 保存到数据库
        if args.save_to_db and not args.test_only:
            print(f"\n💾 保存数据到数据库...")
            
            try:
                # 导入数据库模块
                from astock_engine.data.database_wrapper import Database, DatabaseConfig
                
                # 创建数据库配置
                db_config = DatabaseConfig(
                    host='127.0.0.1',
                    port=3306,
                    database='astock_quant',
                    username='root',
                    password='123456a',
                    charset='utf8mb4',
                    pool_size=5,
                    max_overflow=10
                )
                
                # 创建数据库实例
                database = Database(db_config)
                
                if not database.initialize():
                    print("❌ 数据库初始化失败")
                    data_source.shutdown()
                    return False
                
                print("✅ 数据库连接成功")
                
                # 转换数据格式
                daily_bars = []
                for data in all_data:
                    daily_bar = {
                        'symbol': data['symbol'],
                        'trade_date': data['date'],
                        'open': data['open'],
                        'high': data['high'],
                        'low': data['low'],
                        'close': data['close'],
                        'volume': data['volume'],
                        'turnover': data['volume'] * data['close']
                    }
                    daily_bars.append(daily_bar)
                
                # 保存数据
                saved_count = database.save_daily_bars(daily_bars)
                
                if saved_count > 0:
                    print(f"✅ 成功保存 {saved_count} 条数据到数据库")
                else:
                    print("❌ 保存数据到数据库失败")
                
                # 关闭数据库连接
                database.close()
                print("✅ 数据库连接已关闭")
                
            except Exception as e:
                print(f"❌ 数据库操作失败: {e}")
                import traceback
                traceback.print_exc()
        
        elif args.test_only:
            print(f"\n🔍 测试模式：不保存数据到数据库")
            print(f"   获取的数据示例:")
            for i, data in enumerate(all_data[:5]):
                print(f"   {i+1}. {data['symbol']} {data['date']}: "
                      f"开盘={data['open']}, 收盘={data['close']}, 成交量={data['volume']}")
        
        # 关闭数据源
        data_source.shutdown()
        print("\n✅ 数据源已关闭")
        
        print("\n" + "=" * 60)
        print("🎉 数据导入完成")
        print("=" * 60)
        
        return True
        
    except Exception as e:
        print(f"❌ 数据导入失败: {e}")
        import traceback
        traceback.print_exc()
        return False

def main():
    """主函数"""
    try:
        success = import_juejin_data()
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        print("\n\n⚠️  用户中断操作")
        sys.exit(1)

if __name__ == "__main__":
    main()