#!/usr/bin/env python3
"""
wstock_amibroker_csv_to_pg.py — AmiBroker 5分钟 CSV → PG mkt.minute_bar
========================================================================
CSV 格式:
  文件名: SH600036.csv, SZ000001.csv  (交易所前缀 + 代码)
  内容:
    首行: <ticker>,<date>,<open>,<high>,<low>,<close>,<vol>
    数据: 2015-01-05 09:35:00,16.560,16.720,16.410,16.640,338477
          字段: datetime, open, high, low, close, volume

DB 格式:
  ref.symbol_info.symbol = "600036.SH" (代码.交易所)
  mkt.minute_bar = (symbol_id INT, trade_ts TIMESTAMPTZ, open/high/low/close DOUBLE,
                    volume BIGINT, amount DOUBLE)
  UNIQUE(symbol_id, trade_ts) — ON CONFLICT DO NOTHING

用法:
  python wstock_amibroker_csv_to_pg.py D:/wsWDZ/wstock_amibroker
"""

import csv
import os
import sys
import time
from pathlib import Path

import psycopg2

# ── PG 连接参数 ──
PG_CONFIG = {
    "host": "127.0.0.1",
    "port": 5432,
    "dbname": "astock_quant",
    "user": "astock",
    "password": "astock123",
}

# ── CSV 列索引 (header: <ticker>,<date>,<open>,<high>,<low>,<close>,<vol>) ──
# 数据行实际是 6 列: datetime, open, high, low, close, volume
# <ticker> 在 header 中声明但数据行没有 — 从文件名提取


def csv_filename_to_db_symbol(filename: str) -> str:
    """SH600036.csv → 600036.SH, SZ000001.csv → 000001.SZ"""
    name = Path(filename).stem                     # "SH600036"
    exchange = name[:2]                            # "SH", "SZ"
    code = name[2:]                                # "600036"
    exchange_map = {"SH": "SH", "SZ": "SZ", "BJ": "BJ"}
    ex = exchange_map.get(exchange)
    if not ex:
        raise ValueError(f"未知交易所前缀: {exchange} (文件={filename})")
    return f"{code}.{ex}"


def build_symbol_map(conn) -> dict:
    """从 ref.symbol_info 建立 {db_symbol → id} 映射"""
    print("[symbol] 加载 ref.symbol_info ...")
    with conn.cursor() as cur:
        cur.execute("SELECT id, symbol FROM ref.symbol_info WHERE status='ACTIVE'")
        rows = cur.fetchall()
    mapping = {row[1]: row[0] for row in rows}
    print(f"[symbol] 加载完成: {len(mapping)} 只活跃标的")
    return mapping


def read_csv_file(filepath: str) -> list:
    """
    读取单个 CSV 文件，返回 [(trade_ts, open, high, low, close, volume), ...]
    跳过 header 行，若文件为空或只有 header 则返回空列表
    """
    rows = []
    with open(filepath, "r", encoding="utf-8") as f:
        reader = csv.reader(f)
        header = next(reader, None)
        if header is None:
            return rows
        for row in reader:
            if len(row) < 6:
                continue
            try:
                ts = row[0].strip()
                open_p = float(row[1])
                high_p = float(row[2])
                low_p = float(row[3])
                close_p = float(row[4])
                vol = int(float(row[5]))   # 处理科学计数法
            except (ValueError, IndexError):
                continue
            if close_p <= 0:
                continue
            rows.append((ts, open_p, high_p, low_p, close_p, vol))
    return rows


def import_file(conn, symbol_id: int, filepath: str) -> int:
    """
    单文件高效导入:
    1. COPY → 临时表（秒级）
    2. INSERT INTO mkt.minute_bar ... ON CONFLICT DO NOTHING（秒级）
    返回写入的新行数
    """
    if not os.path.getsize(filepath):
        return 0

    with conn.cursor() as cur:
        # 确保临时表存在
        cur.execute("""
            CREATE TEMP TABLE IF NOT EXISTS _imp_min (
                trade_ts   TIMESTAMPTZ,
                open       DOUBLE PRECISION,
                high       DOUBLE PRECISION,
                low        DOUBLE PRECISION,
                close      DOUBLE PRECISION,
                volume     BIGINT
            ) ON COMMIT DELETE ROWS
        """)
        cur.execute("TRUNCATE _imp_min")

        # COPY CSV → temp table (skip header)
        # CSV 列: datetime, open, high, low, close, volume
        with open(filepath, "r", encoding="utf-8") as f:
            next(f)  # skip header
            cur.copy_expert(
                "COPY _imp_min(trade_ts, open, high, low, close, volume) "
                "FROM STDIN WITH (FORMAT csv)",
                f,
            )

        rows_copied = cur.rowcount

        # INSERT ... ON CONFLICT
        cur.execute("""
            INSERT INTO mkt.minute_bar
                (symbol_id, trade_ts, open, high, low, close, volume, amount)
            SELECT %s, trade_ts, open, high, low, close, volume, NULL
            FROM _imp_min
            WHERE close > 0
            ON CONFLICT(symbol_id, trade_ts) DO NOTHING
        """, (symbol_id,))

        inserted = cur.rowcount
        conn.commit()
        return inserted


def main():
    if len(sys.argv) < 2:
        print(f"用法: {sys.argv[0]} <CSV目录>")
        print(f"示例: {sys.argv[0]} D:/wsWDZ/wstock_amibroker")
        sys.exit(1)

    root_dir = Path(sys.argv[1])
    if not root_dir.is_dir():
        print(f"错误: 目录不存在 — {root_dir}")
        sys.exit(1)

    print(f"[开始] 扫描 CSV 文件: {root_dir}")
    conn = psycopg2.connect(**PG_CONFIG)
    conn.set_session(autocommit=False)

    try:
        # 1. 建立符号映射
        sym_to_id = build_symbol_map(conn)

        # 2. 扫描所有 CSV 文件
        csv_files = list(root_dir.rglob("*.csv"))
        # 过滤文件名格式: SHxxxxxx.csv 或 SZxxxxxx.csv
        csv_files = [f for f in csv_files if len(f.stem) >= 8 and f.stem[:2] in ("SH", "SZ")]
        print(f"[扫描] 找到 {len(csv_files)} 个 CSV 文件")

        # 3. 逐个导入
        total_files = len(csv_files)
        total_imported = 0
        skipped_no_symbol = 0
        skipped_empty = 0
        start_time = time.time()

        for idx, fpath in enumerate(csv_files, 1):
            # 解析符号
            try:
                db_sym = csv_filename_to_db_symbol(fpath.name)
            except ValueError as e:
                skipped_no_symbol += 1
                if skipped_no_symbol <= 3:
                    print(f"  [{idx}/{total_files}] 跳过: {fpath.name} ({e})")
                continue

            sym_id = sym_to_id.get(db_sym)
            if sym_id is None:
                skipped_no_symbol += 1
                if skipped_no_symbol <= 3:
                    print(f"  [{idx}/{total_files}] 跳过: {db_sym} 不在 ref.symbol_info")
                continue

            # 导入
            n = import_file(conn, sym_id, str(fpath))
            if n < 0:
                skipped_empty += 1
                continue
            total_imported += n

            if idx % 200 == 0 or idx == total_files:
                elapsed = time.time() - start_time
                rate = total_imported / elapsed if elapsed > 0 else 0
                print(
                    f"  [{idx}/{total_files}] {fpath.name}: {n} 行写入"
                    f" | 累计 {total_imported:,} 行 ({rate:,.0f} 行/秒)"
                )

        elapsed = time.time() - start_time
        print()
        print(f"═══════════════════════════════════════")
        print(f"  文件数:         {total_files}")
        print(f"  跳过(无符号):   {skipped_no_symbol}")
        print(f"  跳过(空文件):   {skipped_empty}")
        print(f"  CSV 总行数:     {total_rows:,}")
        print(f"  写入 DB 行数:   {total_imported:,}")
        print(f"  重复跳过:       {total_rows - total_imported:,}")
        print(f"  耗时:           {elapsed:.1f} 秒")
        print(f"  速率:           {total_imported / elapsed:,.0f} 行/秒" if elapsed > 0 else "")
        print(f"═══════════════════════════════════════")

    finally:
        conn.close()


if __name__ == "__main__":
    main()
