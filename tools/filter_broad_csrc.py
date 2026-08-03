#!/usr/bin/env python3
"""宽泛CSRC行业二次过滤 — 用知识图谱产品名交叉验证"""
import sys, io, json, re
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
sys.path.insert(0, '.')
from tools.db_config import pg_connect
import baostock as bs
from collections import defaultdict

conn = pg_connect()
cur = conn.cursor()

# ── 1. 加载知识图谱公司产品 ──
print('加载知识图谱...')
sym_products = defaultdict(set)  # symbol -> {product_name, ...}
kg_file = 'ChainKnowledgeGraph/data/company_product.json'
with open(kg_file, 'r', encoding='utf-8') as f:
    for line in f:
        line = line.strip()
        if not line: continue
        try:
            obj = json.loads(line)
            code = obj.get('company_code', '')
            if re.match(r'^\d{6}\.(SH|SZ)$', code):
                sym_products[code].add(obj.get('product_name', ''))
        except: pass
print(f'  {len(sym_products)} 家公司, {sum(len(v) for v in sym_products.values())} 条产品')

# ── 2. 商品→产品名关键词映射 ──
COMMODITY_KEYWORDS = {
    'polysilicon': ['多晶硅','硅料','硅片','单晶硅','太阳能级硅','光伏硅'],
    'silicon': ['硅','有机硅','硅基','硅胶','硅油','硅橡胶','工业硅','金属硅'],
    'silicon_metal': ['工业硅','金属硅','硅铁','硅锰'],
    'solar_wafer': ['硅片','单晶硅片','多晶硅片','光伏硅片','太阳能硅片','半导体硅片','切割','切片'],
    'solar_cell': ['电池片','太阳能电池','光伏电池','PERC','TOPCon','HJT','异质结','钙钛矿'],
    'solar_module': ['光伏组件','太阳能组件','组件','光伏板'],
    'pv_glass': ['光伏玻璃','太阳能玻璃','超白玻璃','压延玻璃'],
    'cathode': ['正极材料','三元','磷酸铁锂','钴酸锂','锰酸锂','NCM','NCA','LFP','前驱体'],
    'anode': ['负极材料','石墨','人造石墨','天然石墨','硅碳','负极'],
    'electrolyte': ['电解液','电解质','锂盐','六氟磷酸锂','LiPF6','LiFSI'],
    'separator': ['隔膜','锂电隔膜','电池隔膜','涂覆'],
    'lipf6': ['六氟磷酸锂','LiPF6','六氟'],
    'lfp': ['磷酸铁锂','LFP','磷酸铁'],
    'ncm': ['三元材料','NCM','NCA','镍钴锰','三元前驱体'],
    'rare_earth': ['稀土','钕铁硼','永磁','磁性材料','氧化镨','氧化钕','镨钕'],
    'copper': ['铜','铜箔','铜板','铜带','铜管','铜线','铜合金','电解铜','阴极铜'],
    'aluminum': ['铝','铝箔','铝板','铝带','铝型材','铝合金','电解铝'],
    'lithium': ['锂','锂矿','锂盐','碳酸锂','氢氧化锂','锂云母','盐湖','锂辉石'],
    'lithium_carbonate': ['碳酸锂','锂盐'],
    'cobalt': ['钴','钴酸锂','钴盐','钴矿','四氧化三钴'],
    'nickel': ['镍','镍矿','镍盐','硫酸镍','电解镍','镍粉'],
    'titanium': ['钛','海绵钛','钛合金','钛材','钛白粉'],
    'titanium_dioxide': ['钛白粉','二氧化钛'],
    'tungsten': ['钨','钨矿','钨粉','碳化钨','硬质合金'],
    'molybdenum': ['钼','钼矿','钼精矿','钼铁'],
    'magnesium': ['镁','镁合金','镁矿','镁锭'],
    'zirconium': ['锆','锆英砂','氧化锆','锆材'],
    'germanium': ['锗','锗单晶','锗晶片'],
    'gallium': ['镓','砷化镓','氮化镓'],
    'antimony': ['锑','锑矿','锑锭'],
    'gold': ['黄金','金矿','黄金矿','合质金'],
    'silver': ['白银','银矿','银粉','银浆'],
    'phosphoric_acid': ['磷酸','磷化工','磷矿','磷肥','磷酸铁'],
    'sulfuric_acid': ['硫酸','硫磺','硫铁矿'],
    'acetic_acid': ['醋酸','乙酸','冰醋酸'],
    'methanol': ['甲醇','煤制甲醇'],
    'urea': ['尿素','化肥','氮肥','复合肥'],
    'soda_ash': ['纯碱','碳酸钠','重碱'],
    'caustic_soda': ['烧碱','氢氧化钠','液碱'],
    'styrene': ['苯乙烯'],
    'ethylene_glycol': ['乙二醇','MEG'],
    'polypropylene': ['聚丙烯','PP','丙纶'],
    'polyethylene': ['聚乙烯','PE'],
    'pvc': ['PVC','聚氯乙烯'],
    'rubber': ['橡胶','天然橡胶','合成橡胶','轮胎'],
    'cement': ['水泥','熟料','混凝土'],
    'glass': ['玻璃','浮法玻璃','平板玻璃','钢化玻璃'],
    'crude_oil': ['原油','石油','油气','油田'],
    'natural_gas': ['天然气','LNG','液化天然气','页岩气','煤层气'],
    'thermal_coal': ['煤炭','动力煤','煤矿'],
    'coke': ['焦炭','焦化','炼焦'],
    'iron_ore': ['铁矿石','铁矿','采矿'],
    'rebar': ['螺纹钢','钢筋','棒材','线材','盘条'],
    'hot_rolled_coil': ['热轧','卷板','热轧卷'],
    'steel_pipe': ['钢管','无缝管','焊管','油管','套管'],
    'soybean': ['大豆','黄豆','非转基因大豆'],
    'soybean_meal': ['豆粕','饲料','饲料原料'],
    'corn': ['玉米','玉米种子','玉米深加工'],
    'cotton': ['棉花','棉','皮棉','籽棉'],
    'sugar': ['糖','白糖','蔗糖','甜菜糖','制糖'],
    'live_hog': ['生猪','猪','养猪','种猪','仔猪','猪肉'],
    'egg': ['鸡蛋','蛋鸡','蛋品'],
    'pulp': ['纸浆','木浆','浆','造纸'],
    'polyester': ['涤纶','聚酯','PET','涤纶长丝','涤纶短纤'],
    'manganese': ['锰','电解锰','锰矿','硅锰'],
    'gasoline': ['汽油'], 'diesel': ['柴油'], 'asphalt': ['沥青'], 'lpg': ['液化气','LPG'],
    'fuel_oil': ['燃料油'], 'cotton_yarn': ['棉纱','纱线','棉纺'],
    'plate': ['中厚板','厚板','板材'], 'cold_rolled': ['冷轧','冷轧板'],
    'wire_rod': ['线材','盘条'], 'section_steel': ['型钢','H型钢','工字钢'],
    'silicon_steel': ['硅钢','取向硅钢','无取向硅钢'],
    'pta': ['PTA','对苯二甲酸','精对苯二甲酸'],
    'corn_starch': ['玉米淀粉','淀粉'], 'soybean_oil': ['豆油'],
    'palm_oil': ['棕榈油'], 'rapeseed_oil': ['菜籽油','菜油'],
    'wheat': ['小麦'], 'apple': ['苹果'], 'jujube': ['红枣'],
    'coking_coal': ['焦煤','主焦煤','炼焦煤'],
    'acetic_acid': ['醋酸','乙酸'], 'acrylic_acid': ['丙烯酸'],
    'propylene_oxide': ['环氧丙烷'], 'ethylene_oxide': ['环氧乙烷'],
    'mdi': ['MDI','二苯基甲烷','聚氨酯'], 'tdi': ['TDI','甲苯二异氰酸酯'],
    'hydrofluoric_acid': ['氢氟酸','氟化氢','氟化工'],
    'wood_pulp': ['木浆','阔叶浆','针叶浆'],
    'platinum': ['铂','铂金','铂族'], 'palladium': ['钯','钯金'],
    'lithium_hydroxide': ['氢氧化锂'],
    'anthracite': ['无烟煤'], 'pci_coal': ['喷吹煤'],
}

# ── 3. 宽泛CSRC代码 ──
BROAD_CSRC = {'C26','C27','C34','C35','C36','C38','C39','C40'}

# ── 4. 获取所有股票的CSRC ──
print('获取CSRC分类...')
bs.login()
rs = bs.query_stock_industry()
sym_csrc = {}
while (rs.error_code == '0') and rs.next():
    row = rs.get_row_data()
    s = row[1].replace('sh.','').replace('sz.','')
    ex = 'SH' if 'sh.' in row[1] else 'SZ'
    sym_csrc[f'{s}.{ex}'] = row[3][:3]
bs.logout()

# ── 5. 对baostock_v3中宽泛行业条目进行二次验证 ──
print('二次验证宽泛行业...')
cur.execute("SELECT product_id, symbol, weight FROM ref.product_stock_mapping WHERE version='baostock_v3'")
rows = cur.fetchall()

downgraded = 0
kept = 0
for pid, sym, w in rows:
    csrc = sym_csrc.get(sym, '')
    if csrc not in BROAD_CSRC:
        kept += 1
        continue

    # 检查知识图谱是否确认
    kg_products = sym_products.get(sym, set())
    keywords = COMMODITY_KEYWORDS.get(pid, [])
    confirmed = any(
        any(kw in pn for kw in keywords)
        for pn in kg_products
    )

    if not confirmed:
        # 降权到0.2 (宽泛行业+无产品确认)
        cur.execute(
            "UPDATE ref.product_stock_mapping SET weight=%s, version='baostock_v3_low' WHERE product_id=%s AND symbol=%s AND version='baostock_v3'",
            (0.2 * (1 if float(w) > 0 else -1), pid, sym))
        downgraded += 1
    else:
        kept += 1

conn.commit()
print(f'  确认保留: {kept} 条')
print(f'  降权(0.5→0.2): {downgraded} 条')

# ── 6. 验证关键股票 ──
print('\n=== 验证 ===')
for sym, name in [('002371.SZ','北方华创'),('688981.SH','中芯国际'),
                   ('600196.SH','复星医药'),('002001.SZ','新和成'),
                   ('688012.SH','中微公司')]:
    cur.execute('SELECT product_id, weight, version FROM ref.product_stock_mapping WHERE symbol=%s ORDER BY ABS(weight) DESC LIMIT 5', (sym,))
    rows = cur.fetchall()
    csrc = sym_csrc.get(sym, '?')
    show = ', '.join(f'{p}({float(w):+.1f})' for p,w,v in rows) if rows else '无'
    print(f'  {name} [{csrc}]: {show}')

# 统计
cur.execute('SELECT version, COUNT(*) FROM ref.product_stock_mapping GROUP BY version ORDER BY COUNT(*) DESC')
print('\n版本分布:')
for v, n in cur.fetchall():
    print(f'  {v:25s} {n:6d}')

conn.close()
