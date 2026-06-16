"""
迁移 strategy_definitions.factor_ids: 整数 → instance_id 字符串
使用项目现有数据库配置
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'astock_engine'))

import pymysql
from data.database.session import DATABASE_CONFIG

conn = pymysql.connect(host='127.0.0.1', port=3306,
                       user='root', password='123456a',
                       database='astock_quant', charset='utf8mb4')
cur = conn.cursor()

# 1. 查看当前数据
print("=== 当前 factor_ids ===")
cur.execute("SELECT strategy_id, factor_ids FROM strategy_definitions")
for sid, fids in cur.fetchall():
    print(f"  {sid}: {fids}")

# 2. 查看映射关系
print("\n=== 映射关系 ===")
cur.execute("""
    SELECT s.strategy_id, s.factor_ids,
           GROUP_CONCAT(fi.instance_id ORDER BY fi.instance_id) AS new_ids
    FROM strategy_definitions s
    CROSS JOIN JSON_TABLE(s.factor_ids, '$[*]' COLUMNS(fid INT PATH '$')) jt
    JOIN factor_instance fi ON fi.factor_id = jt.fid
    WHERE s.factor_ids IS NOT NULL AND s.factor_ids != '[]'
    GROUP BY s.strategy_id, s.factor_ids
""")
mappings = {}
for sid, old, new in cur.fetchall():
    print(f"  {sid}: {old} → {new}")
    mappings[sid] = (old, new)

if not mappings:
    print("  无需迁移 — factor_ids 已是字符串或为空")
    cur.close(); conn.close(); sys.exit(0)

# 3. 确认
resp = input("\n确认更新? (yes/no): ")
if resp.lower() != 'yes':
    print("已取消")
    cur.close(); conn.close(); sys.exit(0)

# 4. 执行更新
for sid, (old, new) in mappings.items():
    instance_ids = new.split(',')
    json_arr = '["' + '","'.join(instance_ids) + '"]'
    cur.execute(
        "UPDATE strategy_definitions SET factor_ids = %s WHERE strategy_id = %s",
        (json_arr, sid))
    print(f"  已更新: {sid}: {old} → {json_arr}")

conn.commit()

# 5. 验证
print("\n=== 验证结果 ===")
cur.execute("SELECT strategy_id, factor_ids FROM strategy_definitions")
for sid, fids in cur.fetchall():
    print(f"  {sid}: {fids}")

cur.close(); conn.close()
print("\n迁移完成！")
