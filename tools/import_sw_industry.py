"""
从 AKShare 导入申万行业分类到 ref.industry_classification。
只取当前有效分类 (end_date IS NULL)。
"""
import psycopg2
import sys

try:
    import akshare as ak
except ImportError:
    print("请先安装 akshare: pip install akshare")
    sys.exit(1)

DB = {
    "host": "127.0.0.1",
    "dbname": "astock_quant",
    "user": "astock",
    "password": "astock123",
}

def get_sw_industry():
    """拉取申万 2021 三级行业分类"""
    print("正在从 AKShare 拉取申万行业分类...")
    df = ak.stock_board_industry_name_em()
    # df columns: ['板块名称', '板块代码', ...]
    return df

def parse_industry_code(code_str):
    """申万行业代码: 如 'BK0477' → 前缀 BK + 数字"""
    return code_str

def build_symbol_map(cursor):
    """构建 symbol → id 映射"""
    cursor.execute("SELECT id, symbol, name FROM ref.symbol_info")
    rows = cursor.fetchall()
    return {r[1]: r[0] for r in rows}, {r[2]: r[0] for r in rows}

def main():
    conn = psycopg2.connect(**DB)
    cur = conn.cursor()

    # 清除旧数据
    cur.execute("DELETE FROM ref.industry_classification")
    print("已清除旧数据")

    sym_by_code, sym_by_name = build_symbol_map(cur)
    print(f"ref.symbol_info: {len(sym_by_code)} 条")

    df = get_sw_industry()
    print(f"AKShare 返回 {len(df)} 个行业板块")

    inserted = 0
    not_found = 0

    for _, row in df.iterrows():
        industry_name = row.get("板块名称", "")
        industry_code = row.get("板块代码", "")

        if not industry_code:
            continue

        # 获取该行业下的成分股
        try:
            members = ak.stock_board_industry_cons_em(symbol=industry_name)
        except Exception as e:
            print(f"  获取 {industry_name}({industry_code}) 成分股失败: {e}")
            continue

        for _, m in members.iterrows():
            stock_code = m.get("代码", "")
            # AKShare 返回代码如 "000001"，需补后缀
            if not stock_code:
                continue

            stock_code = str(stock_code).zfill(6)
            if stock_code.startswith(("6", "9")):
                symbol = stock_code + ".SH"
            elif stock_code.startswith(("0", "3")):
                symbol = stock_code + ".SZ"
            elif stock_code.startswith("8"):
                symbol = stock_code + ".BJ"
            else:
                continue

            symbol_id = sym_by_code.get(symbol)
            if symbol_id is None:
                not_found += 1
                continue

            # 解析行业代码数字部分
            # 申万板块代码格式: BKxxxx
            sw_code = industry_code.replace("BK", "") if industry_code.startswith("BK") else industry_code

            try:
                cur.execute("""
                    INSERT INTO ref.industry_classification
                        (symbol_id, industry_code, industry_name, standard, effective_date)
                    VALUES (%s, %s, %s, 'SW', '2014-01-01')
                    ON CONFLICT DO NOTHING
                """, (symbol_id, sw_code, industry_name))
                inserted += 1
            except Exception as e:
                conn.rollback()
                print(f"  插入失败: {e}")
                cur = conn.cursor()

        if inserted % 500 == 0:
            conn.commit()
            print(f"  已插入 {inserted} 条...")

    conn.commit()
    cur.execute("SELECT COUNT(*) FROM ref.industry_classification")
    total = cur.fetchone()[0]
    cur.execute("SELECT COUNT(DISTINCT industry_code) FROM ref.industry_classification")
    industries = cur.fetchone()[0]
    cur.execute("SELECT COUNT(DISTINCT symbol_id) FROM ref.industry_classification")
    symbols = cur.fetchone()[0]

    print(f"\n导入完成: {total} 条记录, {industries} 个行业, {symbols} 只标的")
    print(f"未找到映射: {not_found}")

    cur.close()
    conn.close()

if __name__ == "__main__":
    main()
