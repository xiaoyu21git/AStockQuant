#!/usr/bin/env python3
"""PG 迁移脚本：逐表从 MySQL 迁到 PostgreSQL"""
import pymysql, psycopg2

my = pymysql.connect(host='127.0.0.1', port=3306, user='root', password='123456a', database='astock_quant', charset='utf8mb4')
pg = psycopg2.connect(host='127.0.0.1', port=5432, user='astock', password='astock123', database='astock_quant')
pg.autocommit = True

def migrate(pg_table, my_table, my_cols, pg_cols, batch_size=5000):
    mc = my.cursor()
    pc = pg.cursor()
    mc.execute(f"SELECT {','.join(my_cols)} FROM {my_table} ORDER BY 1")
    rows = mc.fetchall()
    if not rows:
        mc.close(); pc.close()
        return 0
    ph = ','.join(['%s'] * len(pg_cols))
    sql = f"INSERT INTO {pg_table} ({','.join(pg_cols)}) VALUES ({ph}) ON CONFLICT DO NOTHING"
    for i in range(0, len(rows), batch_size):
        pc.executemany(sql, rows[i:i+batch_size])
    mc.close(); pc.close()
    return len(rows)

def seq_reset(seq_name, table_name, col='id'):
    pg.cursor().execute(f"SELECT setval('{seq_name}', (SELECT COALESCE(MAX({col}),0) FROM {table_name}))")

# ── 参考数据 ──
n = migrate('ref.symbol_info','symbol_info',
    ['symbol_id','symbol','name','exchange','asset_class','list_date','delist_date','status','industry','created_at','updated_at','industry_code'],
    ['id','symbol','name','exchange','asset_class','list_date','delist_date','status','industry','created_at','updated_at','industry_code'])
print(f'ref.symbol_info: {n}')
seq_reset('ref.symbol_info_id_seq', 'ref.symbol_info')

n = migrate('ref.index_info','index_info', ['symbol','name','created_at'], ['symbol','name','created_at'])
print(f'ref.index_info: {n}')

n = migrate('ref.index_constituents','index_constituents',
    ['id','index_symbol','constituent_symbol','weight','announcement_date','start_date','end_date','status','created_at','updated_at'],
    ['id','index_symbol','constituent_symbol','weight','announcement_date','start_date','end_date','status','created_at','updated_at'], batch_size=10000)
print(f'ref.index_constituents: {n}')

# ── 策略 ──
n = migrate('live.strategy','strategy',
    ['strategy_id','strategy_code','metadata_json','strategy_identity_json','version','author','language','status','parameters','performance_metrics','runtime_json','created_at','updated_at'],
    ['strategy_id','strategy_code','metadata_json','strategy_identity_json','version','author','language','status','parameters','performance_metrics','runtime_json','created_at','updated_at'])
print(f'live.strategy: {n}')

# ── 因子 ── (先查 MySQL 实际列名)
# factors: factor_id, factor_name
# factor_instance: instance_id, factor_id, instance_name, description, full_config, status, creator, created_at, updated_at
n = migrate('alpha.factor_instance','factor_instance',
    ['instance_id','factor_id','instance_name','description','full_config','status','creator','created_at','updated_at'],
    ['instance_id','factor_id','instance_name','description','full_config','status','creator','created_at','updated_at'])
print(f'alpha.factor_instance: {n}')
n = migrate('alpha.factors','factors', ['factor_id','factor_name'], ['factor_id','factor_name'])
print(f'alpha.factors: {n}')

# factor_template, factor_parameter, factor_category, factor_tags — 查实际列后再迁
# 先用 SELECT * LIMIT 0 探测
mc = my.cursor()
for tbl in ['factor_template','factor_parameter','factor_category','factor_tags']:
    mc.execute(f"SELECT * FROM {tbl} LIMIT 0")
    cols = [d[0] for d in mc.description]
    n = migrate(f'alpha.{tbl}', tbl, cols, cols)
    print(f'alpha.{tbl}: {n}')
mc.close()

# ── 衍生/另类/政策 ──
n = migrate('fund.derivatives_data','derivatives_data',
    ['id','symbol','underlying_symbol','contract_code','trade_date','futures_open','futures_high','futures_low','futures_close','futures_volume','open_interest','basis','basis_rate','data_source','created_at','updated_at'],
    ['id','symbol','underlying_symbol','contract_code','trade_date','futures_open','futures_high','futures_low','futures_close','futures_volume','open_interest','basis','basis_rate','data_source','created_at','updated_at'])
print(f'fund.derivatives_data: {n}')
n = migrate('fund.alternative_data','alternative_data',
    ['id','symbol','trade_date','metric_type','hot_rank','popularity_score','comment_count','comment_sentiment','extra_payload_json','data_source','created_at','updated_at'],
    ['id','symbol','trade_date','metric_type','hot_rank','popularity_score','comment_count','comment_sentiment','extra_payload_json','data_source','created_at','updated_at'])
print(f'fund.alternative_data: {n}')
n = migrate('fund.policy_data','policy_data',
    ['id','symbol','trade_date','publish_time','title','content','policy_type','policy_score','policy_strength','policy_count','doc_hash','data_source','created_at','updated_at'],
    ['id','symbol','trade_date','publish_time','title','content','policy_type','policy_score','policy_strength','policy_count','doc_hash','data_source','created_at','updated_at'])
print(f'fund.policy_data: {n}')

# ── 数据源/日志 ──
n = migrate('data.data_source_type','data_source_type',
    ['type_id','type_name','category','description','main_table','time_field','symbol_field','created_at'],
    ['type_id','type_name','category','description','main_table','time_field','symbol_field','created_at'])
print(f'data.data_source_type: {n}')
n = migrate('data.data_update_log','data_update_log',
    ['log_id','data_type','update_date','start_time','end_time','total_records','success_records','failed_records','status','error_message','created_at'],
    ['log_id','data_type','update_date','start_time','end_time','total_records','success_records','failed_records','status','error_message','created_at'])
print(f'data.data_update_log: {n}')
n = migrate('data.cleaning_tasks','cleaning_tasks',
    ['task_id','task_uuid','symbol','start_date','end_date','original_record_count','cleaned_record_count','removed_record_count','data_quality_score','status','start_time','end_time','duration_seconds','error_message','created_at','updated_at'],
    ['task_id','task_uuid','symbol','start_date','end_date','original_record_count','cleaned_record_count','removed_record_count','data_quality_score','status','start_time','end_time','duration_seconds','error_message','created_at','updated_at'])
print(f'data.cleaning_tasks: {n}')
n = migrate('data.news_sentiment','news_sentiment',
    ['id','symbol','trade_date','publish_time','title','content','source','url','sentiment_score','market_sentiment','investor_sentiment','sector_sentiment','theme_sentiment','social_sentiment','news_count','title_hash','data_source','created_at','updated_at'],
    ['id','symbol','trade_date','publish_time','title','content','source','url','sentiment_score','market_sentiment','investor_sentiment','sector_sentiment','theme_sentiment','social_sentiment','news_count','title_hash','data_source','created_at','updated_at'])
print(f'data.news_sentiment: {n}')

# ── cleaned_dataset (depends on data_source_type) ──
n = migrate('data.cleaned_dataset','cleaned_dataset',
    ['dataset_id','dataset_name','description','data_type_id','status','created_by','created_at','updated_at'],
    ['dataset_id','dataset_name','description','data_type_id','status','created_by','created_at','updated_at'])
print(f'data.cleaned_dataset: {n}')

pg.close(); my.close()
print('\n=== 小表全部迁移完成 ===')
