"""
tdx_probe.py — 通达信行情服务器 (pytdx) 5 分钟数据深度探测

前提: tools/venv_gm/Scripts/python.exe -m pip install pytdx
运行: tools/venv_gm/Scripts/python.exe -X utf8 tools/tdx_probe.py

探测三件事:
  1. 已知公开服务器连通性
  2. 5 分钟 K 线保留深度 (从最新往前翻 offset, 看最早到哪年)
  3. 当日 48 根 5 分钟 volume 求和 与 PG 同日 GM 实时数据对比, 确认单位是股还是手
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from pytdx.hq import TdxHq_API  # noqa: E402
import psycopg2  # noqa: E402
from tools.db_config import pg_connect  # noqa: E402

# 公开行情服务器 (pytdx 社区常用)
SERVERS = [
    ("119.147.212.81", 7709),
    ("115.238.90.165", 7709),
    ("218.108.98.244", 7709),
    ("180.153.18.170", 7709),
    ("124.71.187.122", 7709),
    ("115.238.56.198", 7709),
]

# 000001.SZ (symbol_id=1); 2026-08-12 为最近一个完整交易日
CHECK_DATE = "2026-08-12"


def main() -> None:
    api = TdxHq_API()
    conn = None
    for ip, port in SERVERS:
        try:
            if api.connect(ip, port, time_out=5):
                print(f"[conn] {ip}:{port} ok", flush=True)
                conn = ip
                break
        except Exception as e:
            print(f"[conn] {ip}:{port} fail: {e}", flush=True)
    if conn is None:
        print("所有服务器连接失败", flush=True)
        return

    # 1) 最新 800 根 (约 16 个交易日)
    latest = api.get_security_bars(0, 0, "000001", 0, 800)
    print(f"[latest] rows={len(latest) if latest else 0} "
          f"first={latest[0]['datetime'] if latest else None} "
          f"last={latest[-1]['datetime'] if latest else None}", flush=True)
    if latest:
        print(f"[latest] sample={latest[0]}", flush=True)
        times = [b['datetime'][-5:] for b in latest[-48:]]
        print(f"[latest] 最近48根时间序列: {times[:6]} ... {times[-6:]}", flush=True)

    # 2) 深度探测: 每个 offset 往前翻, 看服务器保留到哪
    #    每天约 48 根, 一年约 11600 根
    for offset, label in [(48000, "~2022"), (76000, "~2020"),
                          (100000, "~2018"), (115000, "~2015"),
                          (130000, "~2013"), (145000, "~2011")]:
        bars = api.get_security_bars(0, 0, "000001", offset, 10)
        if bars:
            print(f"[depth] offset={offset} ({label}): "
                  f"{bars[0]['datetime']} ~ {bars[-1]['datetime']}", flush=True)
        else:
            print(f"[depth] offset={offset} ({label}): EMPTY", flush=True)

    # 3) volume 单位验证: 翻到 CHECK_DATE, 与该日 PG 数据求和对比
    pg_sum = None
    try:
        pg = pg_connect()
        cur = pg.cursor()
        cur.execute(
            "SELECT sum(volume), sum(amount) FROM mkt.minute_bar "
            "WHERE symbol_id = 1 AND trade_ts >= %s::date AND trade_ts < %s::date + INTERVAL '1 day'",
            (CHECK_DATE, CHECK_DATE))
        row = cur.fetchone()
        if row and row[0]:
            pg_sum = int(row[0])
        cur.close()
        pg.close()
    except Exception as e:
        print(f"[pg] 查询失败: {e}", flush=True)

    # 从最新往前找 CHECK_DATE 的 48 根: 逐步翻 offset
    tdx_sum = None
    for start in range(0, 4800, 800):
        bars = api.get_security_bars(0, 0, "000001", start, 800)
        if not bars:
            break
        day_bars = [b for b in bars if b["datetime"].startswith(CHECK_DATE)]
        if day_bars:
            tdx_sum = sum(b["vol"] for b in day_bars)
            tdx_amt = sum(b["amount"] for b in day_bars)
            print(f"[unit] {CHECK_DATE} TDX: {len(day_bars)} 根 volume 和={tdx_sum} "
                  f"amount 和={tdx_amt:.0f}", flush=True)
            break
    if tdx_sum is not None and pg_sum:
        ratio = tdx_sum / pg_sum
        unit = "股" if 0.9 < ratio < 1.1 else ("手" if 0.009 < ratio < 0.011 else f"未知(比值{ratio:.4f})")
        print(f"[unit] PG 同日 volume={pg_sum} (GM实时), TDX={tdx_sum}, 比值={ratio:.4f} → TDX 单位={unit}", flush=True)

    api.disconnect()


if __name__ == "__main__":
    main()
