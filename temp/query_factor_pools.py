import json
import pymysql

conn = pymysql.connect(host='127.0.0.1', port=3306, user='root', password='123456a', database='astock_quant', charset='utf8mb4')
try:
    with conn.cursor() as cur:
        cur.execute("SELECT factor_id, COALESCE(NULLIF(display_name,''), factor_name) AS factor_name, backtest_symbol_pool, actual_start_date, effective_end_date FROM factors WHERE backtest_symbol_pool IS NOT NULL AND TRIM(backtest_symbol_pool) <> '' ORDER BY factor_id")
        rows = cur.fetchall()
    result = []
    for factor_id, factor_name, raw_pool, actual_start_date, effective_end_date in rows:
        symbols = []
        try:
            parsed = json.loads(raw_pool) if raw_pool else []
            if isinstance(parsed, list):
                symbols = [str(item).strip().upper() for item in parsed if str(item).strip()]
            elif raw_pool:
                symbols = [str(raw_pool).strip().upper()]
        except Exception:
            if raw_pool:
                symbols = [str(raw_pool).strip().upper()]
        deduped = []
        seen = set()
        for symbol in symbols:
            if symbol and symbol not in seen:
                seen.add(symbol)
                deduped.append(symbol)
        result.append({
            'factorId': factor_id,
            'factorName': factor_name,
            'stockPoolCount': len(deduped),
            'stockPool': deduped,
            'actualStartDate': '' if actual_start_date is None else str(actual_start_date),
            'effectiveEndDate': '' if effective_end_date is None else str(effective_end_date),
        })
    print(json.dumps(result, ensure_ascii=False, indent=2))
finally:
    conn.close()
