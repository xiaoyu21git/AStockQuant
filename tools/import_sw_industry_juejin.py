"""
从掘金 SDK 获取标的 SW 行业分类，导入 ref.industry_classification。
"""
import os, sys, datetime as dt

# 掘金 SDK 路径
sys.path.insert(0, r"D:\Program Files\gmsdk_win32\bin")
try:
    from gm.api import set_token, get_symbol_infos
except ImportError:
    print("掘金 SDK 未安装或路径不对")
    sys.exit(1)

set_token("")  # 本地模式无需 token

DB_HOST = "127.0.0.1"
DB_DB = "astock_quant"
DB_USER = "astock"
DB_PASS = "astock123"

def get_industry_from_gm():
    """通过掘金 SDK 获取全市场股票 SW 行业代码"""
    symbols = []
    for suffix in [".SH", ".SZ", ".BJ"]:
        syms = get_symbol_infos(sec_type1=1, sec_type2=1, trade_date=dt.date.today().isoformat())
        # 过滤后缀
        pass

    # 简化: 遍历所有标的，逐个取行业信息
    import psycopg2
    conn = psycopg2.connect(host=DB_HOST, dbname=DB_DB, user=DB_USER, password=DB_PASS)
    cur = conn.cursor()
    cur.execute("SELECT id, symbol FROM ref.symbol_info WHERE status='ACTIVE'")
    stocks = cur.fetchall()

    inserted = 0
    for sid, sym in stocks:
        try:
            info = get_symbol_infos(symbol=sym, trade_date=dt.date.today().isoformat())
            if info and len(info) > 0:
                row = info[0]
                # GM SDK 字段: industry_code (SW 代码), industry_name
                sw_code = getattr(row, 'industry_code', None) or getattr(row, 'industry', None)
                sw_name = getattr(row, 'industry_name', None)
                if sw_code and sw_name:
                    cur.execute("""
                        INSERT INTO ref.industry_classification
                            (symbol_id, industry_code, industry_name, standard, effective_date)
                        VALUES (%s, %s, %s, 'SW', '2014-01-01')
                        ON CONFLICT (symbol_id, industry_code, effective_date) DO NOTHING
                    """, (sid, str(sw_code), sw_name))
                    inserted += 1
        except Exception as e:
            continue

        if inserted % 500 == 0:
            conn.commit()
            print(f"  已导入 {inserted} 只标的...")

    conn.commit()
    print(f"导入完成: {inserted} 只标的")
    cur.close()
    conn.close()

if __name__ == "__main__":
    get_industry_from_gm()
