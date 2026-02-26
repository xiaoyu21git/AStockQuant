import pymysql

config = {
    'host': '127.0.0.1',
    'port': 3306,
    'user': 'root',
    'password': '123456a',
    'database': 'astock_quant',
    'charset': 'utf8mb4'
}

try:
    conn = pymysql.connect(**config)
    cur = conn.cursor()
    
    # 查询所有表
    cur.execute("SHOW TABLES")
    tables = cur.fetchall()
    
    print("=== 数据库中的表 ===")
    for table in tables:
        table_name = table[0]
        print(f"\n表: {table_name}")
        
        # 查询表结构
        cur.execute(f"DESCRIBE {table_name}")
        columns = cur.fetchall()
        print(f"  列名:")
        for col in columns:
            print(f"    - {col[0]} ({col[1]})")
        
        # 查询数据量
        cur.execute(f"SELECT COUNT(*) FROM {table_name}")
        count = cur.fetchone()[0]
        print(f"  记录数: {count}")
        
        # 如果是时间序列表，检查时间范围
        if 'trade_date' in [col[0] for col in columns] or 'date' in [col[0] for col in columns]:
            date_col = 'trade_date' if 'trade_date' in [col[0] for col in columns] else 'date'
            cur.execute(f"SELECT MIN({date_col}), MAX({date_col}) FROM {table_name}")
            min_date, max_date = cur.fetchone()
            print(f"  时间范围: {min_date} 到 {max_date}")
            
            # 查询不同symbol的数量
            if 'symbol' in [col[0] for col in columns]:
                cur.execute(f"SELECT COUNT(DISTINCT symbol) FROM {table_name}")
                symbol_count = cur.fetchone()[0]
                print(f"  股票数量: {symbol_count}")
    
    cur.close()
    conn.close()
    
except Exception as e:
    print(f"错误: {e}")
    import traceback
    traceback.print_exc()