#!/usr/bin/env python3
"""从 ChainKnowledgeGraph 生成 import_mapping.py 和 commodity_ranker.py 的数据部分"""
import json, re
from collections import defaultdict

DATA = 'ChainKnowledgeGraph/data'

COMMODITY_KEYWORDS = [
    '铁矿石','螺纹钢','热轧','线材','盘条','冷轧','中厚板','型钢','钢管','硅钢',
    '焦煤','焦炭','动力煤','无烟煤','喷吹煤',
    '铜','电解铜','阴极铜','铜箔','铜板','铜带','铜管','铜杆','铜合金','铜材',
    '铝锭','电解铝','铝材','铝箔','铝板','铝带','铝型材',
    '锌锭','铅锭','镍','锡锭',
    '黄金','白银','铂','钯',
    '稀土','氧化镨','氧化钕','氧化镝','氧化铽',
    '钨','钼','钴','锂','碳酸锂','氢氧化锂','锰','硅','工业硅','多晶硅','镁','钛','海绵钛',
    '锆','锑','锗','镓',
    '原油','燃料油','汽油','柴油','沥青','液化气','天然气','LNG',
    'PTA','对苯二甲酸','乙二醇','聚丙烯','聚乙烯','聚氯乙烯','PVC',
    '甲醇','纯碱','烧碱','尿素','苯乙烯','醋酸',
    '丙烯酸','环氧丙烷','环氧乙烷','钛白粉','MDI','TDI',
    '磷酸','硫酸','氢氟酸',
    '水泥','玻璃','浮法','光伏玻璃',
    '豆粕','豆油','大豆','玉米','玉米淀粉','小麦',
    '棕榈油','菜籽油','菜油','棉花','白糖','棉纱',
    '天然橡胶','橡胶','纸浆','木浆',
    '生猪','鸡蛋','苹果','红枣',
    '正极材料','负极材料','电解液','隔膜','六氟磷酸锂','三元材料','磷酸铁锂',
    '锂离子电池','锂电池','锂电',
    '光伏硅片','光伏组件','电池片','太阳能电池',
]

EXCLUDE = ['铜牌','铜奖','金牌','金杯','金奖','银牌','银奖','笔试','面试','笔试题','答案','解答','金矿','铜矿','铝矿']

PRODUCT_MAP = {
    # 黑色
    '铁矿石':'iron_ore','螺纹钢':'rebar','螺纹':'rebar',
    '热轧':'hot_rolled_coil','热轧卷板':'hot_rolled_coil','热轧板卷':'hot_rolled_coil',
    '冷轧':'cold_rolled','线材':'wire_rod','盘条':'wire_rod',
    '硅钢':'silicon_steel','中厚板':'plate','型钢':'section_steel','钢管':'steel_pipe',
    '焦煤':'coking_coal','焦炭':'coke','动力煤':'thermal_coal',
    '无烟煤':'anthracite','喷吹煤':'pci_coal',
    # 有色
    '铜':'copper','电解铜':'copper','阴极铜':'copper','铜材':'copper',
    '铝锭':'aluminum','电解铝':'aluminum','铝材':'aluminum',
    '锌锭':'zinc','铅锭':'lead','镍':'nickel','锡锭':'tin',
    '黄金':'gold','白银':'silver','铂':'platinum','钯':'palladium',
    '稀土':'rare_earth','氧化镨':'rare_earth','氧化钕':'rare_earth',
    '钨':'tungsten','钼':'molybdenum','钴':'cobalt',
    '碳酸锂':'lithium_carbonate','氢氧化锂':'lithium_hydroxide',
    '锂':'lithium','锰':'manganese','工业硅':'silicon_metal',
    '多晶硅':'polysilicon','硅':'silicon','硅片':'solar_wafer',
    '镁':'magnesium','钛':'titanium','海绵钛':'titanium',
    '锆':'zirconium','锑':'antimony','锗':'germanium','镓':'gallium',
    # 能源
    '原油':'crude_oil','燃料油':'fuel_oil','汽油':'gasoline',
    '柴油':'diesel','沥青':'asphalt','液化气':'lpg',
    '天然气':'natural_gas','LNG':'natural_gas',
    # 化工
    'PTA':'pta','对苯二甲酸':'pta',
    '乙二醇':'ethylene_glycol','聚丙烯':'polypropylene',
    '聚乙烯':'polyethylene','聚氯乙烯':'pvc','PVC':'pvc',
    '甲醇':'methanol','纯碱':'soda_ash','烧碱':'caustic_soda',
    '尿素':'urea','苯乙烯':'styrene','醋酸':'acetic_acid',
    '丙烯酸':'acrylic_acid','环氧丙烷':'propylene_oxide','环氧乙烷':'ethylene_oxide',
    'MDI':'mdi','TDI':'tdi',
    '磷酸':'phosphoric_acid','硫酸':'sulfuric_acid','氢氟酸':'hydrofluoric_acid',
    '钛白粉':'titanium_dioxide',
    # 建材
    '水泥':'cement','玻璃':'glass','浮法玻璃':'float_glass','光伏玻璃':'pv_glass',
    # 农产品
    '豆粕':'soybean_meal','大豆':'soybean','豆油':'soybean_oil',
    '玉米':'corn','玉米淀粉':'corn_starch','小麦':'wheat',
    '棕榈油':'palm_oil','菜籽油':'rapeseed_oil','菜油':'rapeseed_oil',
    '棉花':'cotton','白糖':'sugar','棉纱':'cotton_yarn',
    '橡胶':'rubber','天然橡胶':'rubber',
    '纸浆':'pulp','木浆':'wood_pulp',
    '生猪':'live_hog','鸡蛋':'egg','苹果':'apple','红枣':'jujube',
    # 新能源材料
    '正极材料':'cathode','负极材料':'anode','电解液':'electrolyte',
    '隔膜':'separator','六氟磷酸锂':'lipf6','六氟':'lipf6',
    '磷酸铁锂':'lfp','三元材料':'ncm',
    '锂离子电池':'electrolyte','锂电池':'electrolyte','锂电':'electrolyte',
    '锂离子电池材料':'electrolyte','锂离子电池电解液':'electrolyte',
    '锂盐':'lithium','钴酸锂':'cathode','锰酸锂':'cathode',
    '太阳能电池':'solar_cell','光伏电池':'solar_cell',
    '光伏硅片':'solar_wafer','光伏组件':'solar_module','电池片':'solar_cell',
}

# 产品对应的 akshare 期货代码 (None = 无国内期货)
AKSHARE_SYMBOLS = {
    'iron_ore':'I0','coke':'J0','coking_coal':'JM0','rebar':'RB0',
    'hot_rolled_coil':'HC0','cold_rolled':None,'wire_rod':None,'silicon_steel':None,
    'plate':None,'section_steel':None,'steel_pipe':None,
    'copper':'CU0','aluminum':'AL0','zinc':'ZN0','lead':'PB0','nickel':'NI0','tin':'SN0',
    'gold':'AU0','silver':'AG0',
    'crude_oil':'SC0','fuel_oil':'FU0','asphalt':'BU0','natural_gas':None,'lpg':'PG0',
    'pta':'TA0','ethylene_glycol':'EG0','polypropylene':'PP0','polyethylene':'PE0',
    'pvc':'V0','methanol':'MA0','soda_ash':'SA0','caustic_soda':None,'urea':'UR0',
    'styrene':'EB0','acetic_acid':None,'acrylic_acid':None,'propylene_oxide':None,
    'ethylene_oxide':None,'mdi':None,'tdi':None,
    'phosphoric_acid':None,'sulfuric_acid':None,'hydrofluoric_acid':None,'titanium_dioxide':None,
    'cement':None,'glass':'FG0','float_glass':'FG0','pv_glass':None,
    'soybean_meal':'M0','soybean':'A0','soybean_oil':'Y0','corn':'C0','corn_starch':'CS0',
    'wheat':None,'palm_oil':'P0','rapeseed_oil':'OI0','cotton':'CF0','sugar':'SR0',
    'cotton_yarn':'CY0','rubber':'RU0','pulp':'SP0','wood_pulp':'SP0',
    'live_hog':'LH0','egg':'JD0','apple':'AP0','jujube':'CJ0',
    'thermal_coal':'ZC0','anthracite':None,'pci_coal':None,
    'manganese':'SM0','silicon_metal':'SI0','polysilicon':None,
    'lithium_carbonate':'LC0','lithium_hydroxide':None,'lithium':None,
    'cobalt':None,'tungsten':None,'molybdenum':None,'magnesium':None,
    'titanium':None,'rare_earth':None,'platinum':None,'palladium':None,
    'zirconium':None,'antimony':None,'germanium':None,'gallium':None,
    'gasoline':None,'diesel':None,
    'cathode':None,'anode':None,'electrolyte':None,
    'separator':None,'lipf6':None,'lfp':None,'ncm':None,
    'solar_wafer':None,'solar_module':None,'solar_cell':None,
    'silicon':None,
}

# 产品中文名
PRODUCT_NAMES = {
    'iron_ore':'铁矿石','coke':'焦炭','coking_coal':'焦煤','rebar':'螺纹钢',
    'hot_rolled_coil':'热轧卷板','cold_rolled':'冷轧板','wire_rod':'线材',
    'silicon_steel':'硅钢','plate':'中厚板','section_steel':'型钢','steel_pipe':'钢管',
    'copper':'铜','aluminum':'铝','zinc':'锌','lead':'铅','nickel':'镍','tin':'锡',
    'gold':'黄金','silver':'白银','platinum':'铂','palladium':'钯',
    'rare_earth':'稀土','tungsten':'钨','molybdenum':'钼','cobalt':'钴',
    'lithium':'锂','lithium_carbonate':'碳酸锂','lithium_hydroxide':'氢氧化锂',
    'manganese':'锰','silicon_metal':'工业硅','polysilicon':'多晶硅','silicon':'硅',
    'magnesium':'镁','titanium':'钛','zirconium':'锆','antimony':'锑',
    'germanium':'锗','gallium':'镓',
    'crude_oil':'原油','fuel_oil':'燃料油','natural_gas':'天然气','gasoline':'汽油',
    'diesel':'柴油','asphalt':'沥青','lpg':'液化气',
    'pta':'PTA','ethylene_glycol':'乙二醇','polypropylene':'聚丙烯',
    'polyethylene':'聚乙烯','pvc':'PVC','methanol':'甲醇','soda_ash':'纯碱',
    'caustic_soda':'烧碱','urea':'尿素','styrene':'苯乙烯','acetic_acid':'醋酸',
    'acrylic_acid':'丙烯酸','propylene_oxide':'环氧丙烷','ethylene_oxide':'环氧乙烷',
    'mdi':'MDI','tdi':'TDI','phosphoric_acid':'磷酸','sulfuric_acid':'硫酸',
    'hydrofluoric_acid':'氢氟酸','titanium_dioxide':'钛白粉',
    'cement':'水泥','glass':'玻璃','float_glass':'浮法玻璃','pv_glass':'光伏玻璃',
    'soybean_meal':'豆粕','soybean':'大豆','soybean_oil':'豆油',
    'corn':'玉米','corn_starch':'玉米淀粉','wheat':'小麦',
    'palm_oil':'棕榈油','rapeseed_oil':'菜籽油','cotton':'棉花','sugar':'白糖',
    'cotton_yarn':'棉纱','rubber':'天然橡胶','pulp':'纸浆','wood_pulp':'木浆',
    'live_hog':'生猪','egg':'鸡蛋','apple':'苹果','jujube':'红枣',
    'cathode':'正极材料','anode':'负极材料','electrolyte':'电解液',
    'separator':'隔膜','lipf6':'六氟磷酸锂','lfp':'磷酸铁锂','ncm':'三元材料',
    'solar_wafer':'光伏硅片','solar_module':'光伏组件','solar_cell':'电池片',
    'thermal_coal':'动力煤','anthracite':'无烟煤','pci_coal':'喷吹煤',
}

def is_a_share(code):
    return bool(re.match(r'^[0-9]{6}\.(SH|SZ)$', code or ''))

def norm_pid(name):
    n = name.strip()
    if n in PRODUCT_MAP: return PRODUCT_MAP[n]
    for kw, pid in sorted(PRODUCT_MAP.items(), key=lambda x: -len(x[0])):
        if kw in n: return pid
    return None  # 返回 None 表示无法识别

# ── 加载 ──
print('加载知识图谱...')
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
print(f'  A股公司产品映射: {len(cps)}')

# ── 筛选商品 ──
EXCLUDE_SET = set(EXCLUDE)
commod = []
for cp in cps:
    pn = cp.get('product_name','')
    skip = False
    for ex in EXCLUDE_SET:
        if ex in pn: skip = True; break
    if skip: continue
    for kw in COMMODITY_KEYWORDS:
        if kw in pn:
            commod.append(cp)
            break
print(f'  商品相关: {len(commod)}')

# ── 标准化聚合并去重乱码 ──
ps = defaultdict(lambda: defaultdict(float))
skipped = 0
for cp in commod:
    pid = norm_pid(cp['product_name'])
    if pid is None:
        skipped += 1
        continue
    sym = cp['company_code']
    try: w = float(cp.get('rel_weight', 1.0))
    except: w = 1.0
    if w > ps[pid][sym]:
        ps[pid][sym] = w

print(f'  有效商品品种: {len(ps)} (跳过{skipped}个无法识别的产品名)')

# ── 统计 ──
all_stocks = set(s for pid in ps for s in ps[pid])
total = sum(len(v) for v in ps.values())
print(f'  股票数: {len(all_stocks)}, 总映射: {total}')

# ── 输出排名 ──
print(f'\n=== Top 40 商品 (按股票数) ===')
for pid in sorted(ps, key=lambda p: -len(ps[p]))[:40]:
    name = PRODUCT_NAMES.get(pid, pid)
    aks = AKSHARE_SYMBOLS.get(pid)
    aks_str = f' ({aks})' if aks else ''
    print(f'  {pid:28s} {len(ps[pid]):4d}只  {name}{aks_str}')

# ── 生成 commodity_ranker COMMODITY_REGISTRY ──
print(f'\n\n# === COMMODITY_REGISTRY for commodity_ranker.py ===')
print('COMMODITY_REGISTRY: Dict[str, dict] = {')
for pid in sorted(ps.keys()):
    name = PRODUCT_NAMES.get(pid, pid)
    aks = AKSHARE_SYMBOLS.get(pid)
    chain = 'general'
    note = '' if aks else ', "_note": "无国内期货"'
    print(f'    "{pid}": {{"akshare_symbol": {repr(aks)}, "name": "{name}", "chain": "{chain}"{note}}},')
print('}')

# ── 生成 import_mapping MAPPINGS ──
print(f'\n\n# === MAPPINGS for import_mapping.py ===')
print('MAPPINGS: Dict[str, List[Tuple[str, float, str, str]]] = {')
for pid in sorted(ps.keys()):
    stocks = ps[pid]
    name = PRODUCT_NAMES.get(pid, pid)
    print(f'    # {name} ({len(stocks)}只)')
    print(f'    "{pid}": [')
    for sym, w in sorted(stocks.items(), key=lambda x: -x[1])[:30]:
        print(f'        ("{sym}", {w:+.1f}, "2000-01-01", "2099-12-31"),')
    print(f'    ],')
print('}')

# ── 保存完整 JSON ──
output = {pid: [(s,w) for s,w in sorted(stocks.items(), key=lambda x: -x[1])]
          for pid, stocks in ps.items()}
with open('tools/commodity_stock_mapping_from_kg.json', 'w', encoding='utf-8') as f:
    json.dump(output, f, ensure_ascii=False, indent=2)
print(f'\n\n完整数据已保存: tools/commodity_stock_mapping_from_kg.json')
