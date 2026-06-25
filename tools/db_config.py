"""共享数据库配置 — 统一 PostgreSQL 连接"""
import psycopg2

PG_CONFIG = {
    "host": "127.0.0.1",
    "port": 5432,
    "user": "astock",
    "password": "astock123",
    "database": "astock_quant",
    "options": "-c search_path=ref,mkt,fund,alpha,live,port,data,public -c timescaledb.max_tuples_decompressed_per_dml_transaction=0",
}

def pg_connect():
    return psycopg2.connect(**PG_CONFIG)
