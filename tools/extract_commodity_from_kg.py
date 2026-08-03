#!/usr/bin/env python3
"""从 ChainKnowledgeGraph 提取大宗商品→A股映射"""
import json, re
from collections import defaultdict

DATA = 'ChainKnowledgeGraph/data'

COMMODITY_KEYWORDS = [
    '铁矿石','螺纹钢','热轧','线材','盘条','冷轧','中厚板','型钢','钢管','硅钢',
    '焦煤','焦炭','动力煤','无烟煤','喷吹煤',
    '铜','电解铜','阴极铜','铝锭','电解铝','锌锭','铅锭','镍','锡锭',
    '黄金','白银','铂','钯',
    '稀土','氧化镨','氧化钕','氧化镝','氧化铽',
    '钨','钼','钴','锂','碳酸锂','氢氧化锂','锰','硅','工业硅','多晶硅','镁','钛','海绵钛',
    '锆','锑','锗','镓',
    '原油','燃料油','汽油','柴油','沥青','液化气','天然气','LNG',
    'PTA','对苯二甲酸','乙二醇','聚丙烯','聚乙烯','聚氯乙烯','PVC',
    '甲醇','纯碱','烧碱','尿素','苯乙烯','苯','甲苯','二甲苯',
    '醋酸','丙烯酸','环氧丙烷','环氧乙烷','钛白粉','MDI','TDI',
    '磷酸','硫酸','氢氟酸',
    '水泥','玻璃','浮法','光伏玻璃',
    '豆粕','豆油','大豆','玉米','玉米淀粉','小麦','稻谷',
    '棕榈油','菜籽油','菜油','花生','棉花','白糖','白砂糖','棉纱',
    '天然橡胶','橡胶','纸浆','木浆',
    '生猪','鸡蛋','苹果','红枣',
    '正极材料','负极材料','电解液','隔膜','六氟磷酸锂','三元材料','磷酸铁锂',
    '光伏硅片','光伏组件','电池片',
    '阴极铜','电解铜','电解镍',
]

EXCLUDE = ['铜牌','铜奖','金牌','金杯','金奖','银牌','银奖','笔试','面试','笔试题','答案','解答']

def is_commodity(name):
    for ex in EXCLUDE:
        if ex in name: return False
    for kw in COMMODITY_KEYWORDS:
        if kw in name: return True
    return False

def is_a_share(code):
    return bool(re.match(r'^[0-9]{6}\.(SH|SZ)$', code or ''))

def norm_pid(name):
    n = name.strip()
    m = {
        '铁矿石':'iron_ore','螺纹钢':'rebar','螺纹':'rebar',
        '热轧':'hot_rolled_coil','热轧卷板':'hot_rolled_coil','热轧板卷':'hot_rolled_coil',
        '冷轧':'cold_rolled','线材':'wire_rod','盘条':'wire_rod',
        '硅钢':'silicon_steel','中厚板':'plate','型钢':'section_steel','钢管':'steel_pipe',
        '焦煤':'coking_coal','焦炭':'coke','动力煤':'thermal_coal',
        '无烟煤':'anthracite','喷吹煤':'pci_coal',
        '铜':'copper','电解铜':'copper','阴极铜':'copper',
        '铝':'aluminum','铝锭':'aluminum','电解铝':'aluminum',
        '锌':'zinc','锌锭':'zinc','铅':'lead','铅锭':'lead',
        '镍':'nickel','锡':'tin','锡锭':'tin',
        '黄金':'gold','白银':'silver','铂':'platinum','钯':'palladium',
        '稀土':'rare_earth','氧化镨':'praseodymium','氧化钕':'neodymium',
        '钨':'tungsten','钼':'molybdenum','钴':'cobalt',
        '锂':'lithium','碳酸锂':'lithium_carbonate','氢氧化锂':'lithium_hydroxide',
        '锰':'manganese','硅':'silicon','工业硅':'silicon_metal','多晶硅':'polysilicon',
        '镁':'magnesium','钛':'titanium','海绵钛':'titanium',
        '锆':'zirconium','锑':'antimony','锗':'germanium','镓':'gallium',
        '原油':'crude_oil','燃料油':'fuel_oil','汽油':'gasoline',
        '柴油':'diesel','沥青':'asphalt','液化气':'lpg','天然气':'natural_gas',
        'pta':'pta','PTA':'pta','对苯二甲酸':'pta',
        '乙二醇':'ethylene_glycol','聚丙烯':'polypropylene',
        '聚乙烯':'polyethylene','聚氯乙烯':'pvc','PVC':'pvc',
        '甲醇':'methanol','纯碱':'soda_ash','烧碱':'caustic_soda',
        '尿素':'urea','苯乙烯':'styrene','醋酸':'acetic_acid',
        '丙烯酸':'acrylic_acid','环氧丙烷':'propylene_oxide','环氧乙烷':'ethylene_oxide',
        'MDI':'mdi','TDI':'tdi',
        '磷酸':'phosphoric_acid','硫酸':'sulfuric_acid','氢氟酸':'hydrofluoric_acid',
        '钛白粉':'titanium_dioxide',
        '水泥':'cement','玻璃':'glass','浮法玻璃':'float_glass','光伏玻璃':'pv_glass',
        '豆粕':'soybean_meal','大豆':'soybean','豆油':'soybean_oil',
        '玉米':'corn','玉米淀粉':'corn_starch','小麦':'wheat',
        '棕榈油':'palm_oil','菜籽油':'rapeseed_oil','菜油':'rapeseed_oil',
        '棉花':'cotton','白糖':'sugar','棉纱':'cotton_yarn',
        '橡胶':'rubber','天然橡胶':'rubber',
        '纸浆':'pulp','木浆':'wood_pulp',
        '生猪':'live_hog','鸡蛋':'egg','苹果':'apple','红枣':'jujube',
        '正极材料':'cathode','负极材料':'anode','电解液':'electrolyte',
        '隔膜':'separator','六氟磷酸锂':'lipf6',
        '磷酸铁锂':'lfp','三元材料':'ncm',
        '光伏硅片':'solar_wafer','光伏组件':'solar_module','电池片':'solar_cell',
    }
    if n in m: return m[n]
    for kw, pid in sorted(m.items(), key=lambda x: -len(x[0])):
        if kw in n: return pid
    slug = re.sub(r'[^a-z0-9_]', '_', n.lower().replace(' ','_'))
    return slug[:40] if len(slug) < 40 else slug[:40]

# ── 主流程 ──
print('加载 company_product.json ...')
cps = []
with open(f'{DATA}/company_product.json', 'r', encoding='utf-8') as f:
    for line in f:
        line = line.strip()
        if not line: continue
        try:
            obj = json.loads(line)
            if is_a_share(obj.get('company_code','')):
                cps.append(obj)
        except: pass

print(f'  A股公司-产品映射: {len(cps)}')

commod = [cp for cp in cps if is_commodity(cp['product_name'])]
print(f'  商品相关映射: {len(commod)}')

# 聚合: pid -> {symbol -> max_weight}
ps = defaultdict(lambda: defaultdict(float))
for cp in commod:
    pid = norm_pid(cp['product_name'])
    sym = cp['company_code']
    w = cp.get('rel_weight', 1.0)
    try: w = float(w)
    except: w = 1.0
    if w > ps[pid][sym]:
        ps[pid][sym] = w

n_products = len(ps)
n_stocks = len(set(s for pid in ps for s in ps[pid]))
n_total = sum(len(v) for v in ps.values())
print(f'  标准化后: {n_products} 商品品种, {n_stocks} 只股票, {n_total} 条映射')

# 输出
print(f'\n=== 按股票数降序 ===')
for pid in sorted(ps, key=lambda p: -len(ps[p])):
    print(f'  {pid:30s} {len(ps[pid]):4d}只')

# 保存为 JSON 供后续使用
output = {pid: list(stocks.items()) for pid, stocks in ps.items()}
with open('tools/commodity_stock_mapping_from_kg.json', 'w', encoding='utf-8') as f:
    json.dump(output, f, ensure_ascii=False, indent=2)
print(f'\n已保存: tools/commodity_stock_mapping_from_kg.json')
