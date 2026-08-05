import sys; sys.path.insert(0, 'tools')
from db_config import pg_connect
import json

conn = pg_connect()
cur = conn.cursor()

# GrowthMetric enum: REVENUE_GROWTH=0, NET_PROFIT_GROWTH=1, DELTA_ROE=2, SUE=3
# QualityMetric: ROE=0, ROA=1, GROSS_MARGIN=2, OPERATING_MARGIN=3, EARNINGS_QUALITY=4
# ValuationMetric: BP=0, EP=1, DIVIDEND_YIELD=2, CFP=3
# 频率枚举: 低频=2 (对应 DataFrequency enum)
# 标准化: Z-Score=0, Rank=1, None=2

factors = [
    # MOM type=1
    {'match':'MOM_20%','params':{'window':20,'lookbackWindow':252,'skipRecent':5,'standardization':0,'frequency':2,'lagEnabled':True,'neutralizationEnabled':False}},
    {'match':'MOM_60%','params':{'window':60,'lookbackWindow':252,'skipRecent':10,'standardization':1,'frequency':2,'lagEnabled':True,'neutralizationEnabled':False}},
    # VAL type=0
    {'match':'VAL_pe%','params':{'valuationMetrics':[1],'bpWeight':0,'epWeight':100,'cfPWeight':0,'lookbackWindow':252,'skipRecent':90,'standardization':0,'frequency':2,'lagEnabled':True,'neutralizationEnabled':True}},
    {'match':'VAL_pb%','params':{'valuationMetrics':[0],'bpWeight':100,'epWeight':0,'cfPWeight':0,'lookbackWindow':252,'skipRecent':90,'standardization':0,'frequency':2,'lagEnabled':True,'neutralizationEnabled':True}},
    # GR type=4 (growthMetrics + growthWeights are paired arrays)
    {'match':'GR_rev%','params':{'growthMetrics':[0],'growthWeights':[100.0],'lookbackWindow':252,'standardization':0,'frequency':2,'lagEnabled':True,'neutralizationEnabled':True}},
    {'match':'GR_roe%','params':{'growthMetrics':[2],'growthWeights':[100.0],'lookbackWindow':252,'standardization':0,'frequency':2,'lagEnabled':True,'neutralizationEnabled':True}},
    # QL type=3 (metric is single enum int)
    {'match':'QL_roe%','params':{'metric':0,'lookbackPeriod':252,'skipRecent':90,'standardization':0,'frequency':2,'lagEnabled':True,'neutralizationEnabled':True}},
    {'match':'QL_gross%','params':{'metric':2,'lookbackPeriod':252,'skipRecent':90,'standardization':0,'frequency':2,'lagEnabled':True,'neutralizationEnabled':True}},
    # SZ type=2
    {'match':'SZ_float%','params':{'sizeMetric':'circulating_market_cap','logTransform':True,'lookbackPeriod':1,'skipRecent':0,'standardization':2,'frequency':2,'lagEnabled':True,'neutralizationEnabled':False}},
    # LVOL type=12
    {'match':'LV_60%','params':{'window':60,'components':['volatility'],'volatilityWeight':100.0,'betaWeight':0.0,'drawdownWeight':0.0,'lookbackPeriod':252,'skipRecent':90,'standardization':2,'frequency':2,'lagEnabled':True,'neutralizationEnabled':True}},
]

for f in factors:
    cur.execute('SELECT instance_id, full_config FROM alpha.factor_instance WHERE instance_name LIKE %s', (f['match'],))
    r = cur.fetchone()
    if not r:
        print(f'SKIP: {f["match"]}')
        continue
    fid = r[0]
    cfg = r[1] if isinstance(r[1], dict) else json.loads(r[1])
    cfg['parameters'] = f['params']
    cur.execute('UPDATE alpha.factor_instance SET full_config=%s::jsonb WHERE instance_id=%s',
        (json.dumps(cfg, ensure_ascii=False), fid))
    print(f'OK: {f["match"]}')

conn.commit()
print('Done')
conn.close()
