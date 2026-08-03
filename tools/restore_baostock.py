#!/usr/bin/env python3
"""恢复全量baostock映射 + 方向修正 (不删除任何行业)"""
import sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
sys.path.insert(0, '.')
from tools.db_config import pg_connect
import baostock as bs
from collections import defaultdict

conn = pg_connect()
cur = conn.cursor()

# 全量CSRC→商品
CSRC_MAP = {
    'B06': ['thermal_coal','coke','coking_coal'],
    'B07': ['crude_oil','natural_gas'],
    'B08': ['iron_ore','manganese'],
    'B09': ['copper','aluminum','zinc','lead','nickel','tin','gold','silver',
            'rare_earth','tungsten','molybdenum','cobalt','lithium','magnesium',
            'titanium','zirconium','germanium','gallium','antimony'],
    'B10': ['phosphoric_acid'], 'B11': ['silver'],
    'A01': ['soybean','corn','wheat','cotton','sugar','rubber','soybean_meal','apple','jujube'],
    'A02': ['wood_pulp','pulp'], 'A03': ['live_hog','egg'], 'A04': ['soybean_meal'],
    'C13': ['soybean_meal','corn','sugar','palm_oil','rapeseed_oil','soybean_oil','corn_starch','wheat','cotton','live_hog'],
    'C17': ['cotton','cotton_yarn'], 'C18': ['cotton','cotton_yarn'],
    'C15': ['corn','sugar','wheat','soybean','soybean_meal'],  # 白酒/啤酒/饮料: 粮食+糖消费者
    'C19': ['rubber'], 'C22': ['pulp','wood_pulp'],
    'C25': ['crude_oil','fuel_oil','asphalt','lpg','coke'],
    'C26': ['methanol','urea','soda_ash','caustic_soda','styrene','acetic_acid','pta',
            'mdi','tdi','acrylic_acid','propylene_oxide','ethylene_oxide','titanium_dioxide',
            'phosphoric_acid','sulfuric_acid','hydrofluoric_acid','silicon','polysilicon',
            'lithium_carbonate','electrolyte','cathode','anode','lipf6','lfp','ncm'],
    'C27': ['urea','phosphoric_acid','acetic_acid','methanol'],
    'C28': ['ethylene_glycol','polyester','acrylic_acid'],
    'C29': ['rubber','polypropylene','polyethylene','pvc'],
    'C30': ['cement','glass','float_glass','pv_glass','soda_ash','titanium_dioxide','polysilicon'],
    'C31': ['rebar','hot_rolled_coil','iron_ore','silicon_steel','steel_pipe','wire_rod','plate','cold_rolled','section_steel','coke'],
    'C32': ['copper','aluminum','zinc','lead','nickel','tin','gold','silver','rare_earth',
            'tungsten','molybdenum','cobalt','lithium','magnesium','titanium','silicon_metal','manganese'],
    'C33': ['copper','aluminum','steel_pipe','tin'],
    'C34': ['solar_wafer','solar_cell','solar_module','silicon'],
    'C35': ['solar_wafer','silicon','polysilicon'],
    'C36': ['lithium','rubber','aluminum','copper','solar_module'],
    'C37': ['aluminum','steel_pipe'],
    'C38': ['lithium','electrolyte','separator','cathode','anode','solar_cell','polysilicon',
            'solar_wafer','pv_glass','silicon','copper'],
    'C39': ['cathode','anode','electrolyte','separator','solar_module','solar_cell','copper',
            'silicon','polysilicon','gallium','germanium','rare_earth','gold','silver'],
    'C40': ['silicon'],
    'D44': ['thermal_coal'], 'D45': ['natural_gas'],
}

# 方向: 哪些CSRC是消费者(下游) -> 负权重
CONSUMER_CSRC = {
    'iron_ore': {'C31'},
    'copper': {'C33','C39'},
    'aluminum': {'C33','C36'},
    'crude_oil': {'C25','C28','C29'},
    'thermal_coal': {'D44'},
    'coke': {'C31'},
    'coking_coal': {'C25'},
    'soybean': {'C13','C15'},
    'corn': {'C13','A03','C15'},
    'soybean_meal': {'A03','A04','C15'},
    'cotton': {'C17','C18'},
    'live_hog': {'C13'},
    'rubber': {'C29'},
    'sugar': {'C14','C15'},
    'wheat': {'C13','C15'},
    'rebar': {'E47','E48','E50','C34','C35'},
    'hot_rolled_coil': {'C33','C34','C36'},
    'cement': {'E47','E48'},
    'glass': {'E47','C36'},
    'pulp': {'C23'},
    'natural_gas': {'D45'},
}

print('拉取baostock行业分类...')
bs.login()
rs = bs.query_stock_industry()
sym_csrc = {}
while (rs.error_code == '0') and rs.next():
    row = rs.get_row_data()
    s = row[1].replace('sh.','').replace('sz.','')
    ex = 'SH' if 'sh.' in row[1] else 'SZ'
    sym_csrc[f'{s}.{ex}'] = row[3][:3]
bs.logout()
print(f'  {len(sym_csrc)} 只股票')

# 聚合
product_stocks = defaultdict(set)
for sym, csrc in sym_csrc.items():
    for pid in CSRC_MAP.get(csrc, []):
        product_stocks[pid].add(sym)

print(f'  聚合: {len(product_stocks)} 品种, {sum(len(v) for v in product_stocks.values())} 条')

# 清空旧baostock + 重新导入
cur.execute("DELETE FROM ref.product_stock_mapping WHERE version LIKE 'baostock%'")
inserted = 0
for pid in sorted(product_stocks):
    for sym in product_stocks[pid]:
        csrc = sym_csrc.get(sym, '')
        sign = -1 if csrc in CONSUMER_CSRC.get(pid, set()) else 1
        cur.execute(
            """INSERT INTO ref.product_stock_mapping (product_id,symbol,weight,effective_date,expired_date,version)
               VALUES (%s,%s,%s,'2000-01-01','2099-12-31','baostock_v3')
               ON CONFLICT (product_id,symbol,effective_date) DO NOTHING""",
            (pid, sym, 0.5 * sign))
        inserted += 1

conn.commit()
print(f'  写入: {inserted} 条')

# 统计
cur.execute('SELECT COUNT(*), COUNT(DISTINCT symbol) FROM ref.product_stock_mapping')
t, s = cur.fetchone()
cur.execute("SELECT COUNT(*) FROM ref.product_stock_mapping WHERE weight < 0")
neg = cur.fetchone()[0]
print(f'\n最终: {t}条, {s}股票, {neg}条负权重(消费者)')

# 验证半导体/医药
for sym in ['002371.SZ','688981.SH','600196.SH','002001.SZ']:
    cur.execute('SELECT product_id, weight FROM ref.product_stock_mapping WHERE symbol=%s ORDER BY ABS(weight) DESC LIMIT 5', (sym,))
    rows = cur.fetchall()
    show = ', '.join(f'{p}({float(w):+.1f})' for p,w in rows) if rows else '无映射'
    csrc = sym_csrc.get(sym, '?')
    print(f'  {sym} [{csrc}]: {show}')

conn.close()
