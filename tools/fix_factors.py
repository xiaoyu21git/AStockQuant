import sys; sys.path.insert(0, 'tools')
from db_config import pg_connect
import json

conn = pg_connect()
cur = conn.cursor()

factors = [
    {'match':'MOM_20%','params':{'method':'残差动量','window':20,'frequency':'低频','skipRecent':5,'laggedEnabled':True,'lookbackPeriod':252,'standardization':'Z-Score','neutralizationEnabled':False}},
    {'match':'MOM_60%','params':{'method':'残差动量','window':60,'frequency':'低频','skipRecent':10,'laggedEnabled':True,'lookbackPeriod':252,'standardization':'Rank','neutralizationEnabled':False}},
    {'match':'VAL_pe%','params':{'valuationMetrics':['pe_ttm'],'frequency':'低频','skipRecent':90,'laggedEnabled':True,'lookbackPeriod':252,'standardization':'Z-Score','neutralizationEnabled':True,'bpWeight':0,'epWeight':100,'cfPWeight':0,'use_percentile':False,'industry_neutral':True}},
    {'match':'VAL_pb%','params':{'valuationMetrics':['pb_lq'],'frequency':'低频','skipRecent':90,'laggedEnabled':True,'lookbackPeriod':252,'standardization':'Z-Score','neutralizationEnabled':True,'bpWeight':100,'epWeight':0,'cfPWeight':0,'use_percentile':False,'industry_neutral':True}},
    {'match':'GR_rev%','params':{'frequency':2,'laggedEnabled':True,'lookbackWindow':252,'standardization':1,'neutralizationEnabled':True,'growthMetrics':'revenue_yoy','sueWeight':0,'revenueGrowthWeight':100,'netProfitGrowthWeight':0,'deltaRoeWeight':0}},
    {'match':'GR_roe%','params':{'frequency':2,'laggedEnabled':True,'lookbackWindow':252,'standardization':1,'neutralizationEnabled':True,'growthMetrics':'roe_yoy','sueWeight':0,'revenueGrowthWeight':0,'netProfitGrowthWeight':0,'deltaRoeWeight':100}},
    {'match':'QL_roe%','params':{'frequency':'低频','laggedEnabled':True,'lookbackPeriod':252,'standardization':'Z-Score','neutralizationEnabled':True,'qualityMetrics':'roe_ttm','skipRecent':90}},
    {'match':'QL_gross%','params':{'frequency':'低频','laggedEnabled':True,'lookbackPeriod':252,'standardization':'Z-Score','neutralizationEnabled':True,'qualityMetrics':'gross_margin','skipRecent':90}},
    {'match':'SZ_float%','params':{'frequency':'低频','laggedEnabled':True,'lookbackPeriod':1,'standardization':'None','neutralizationEnabled':False,'sizeMetric':'circulating_market_cap','skipRecent':0,'logTransform':True}},
    {'match':'LV_60%','params':{'frequency':'低频','laggedEnabled':True,'lookbackPeriod':252,'standardization':'None','neutralizationEnabled':True,'skipRecent':90,'window':60,'components':['volatility'],'volatilityWeight':100,'betaWeight':0,'drawdownWeight':0}},
]

for f in factors:
    cur.execute('SELECT instance_id, full_config FROM alpha.factor_instance WHERE instance_name LIKE %s', (f['match'],))
    r = cur.fetchone()
    if not r:
        print(f'SKIP: {f["match"]} not found')
        continue
    fid = r[0]
    cfg = r[1] if isinstance(r[1], dict) else json.loads(r[1])
    cfg['parameters'] = f['params']
    cur.execute('UPDATE alpha.factor_instance SET full_config=%s::jsonb WHERE instance_id=%s',
        (json.dumps(cfg, ensure_ascii=False), fid))
    print(f'OK: {fid[:40]}...')

conn.commit()
print('Done — 10 factors fixed')
conn.close()
