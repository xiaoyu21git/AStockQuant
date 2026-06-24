"""共享数据库配置 — 统一 PostgreSQL 连接"""
import psycopg2

PG_CONFIG = {
    "host": "127.0.0.1",
    "port": 5432,
    "user": "astock",
    "password": "astock123",
    "database": "astock_quant",
    "options": "-c search_path=ref,mkt,fund,alpha,live,port,data,public",
}

def pg_connect():
    return psycopg2.connect(**PG_CONFIG)
