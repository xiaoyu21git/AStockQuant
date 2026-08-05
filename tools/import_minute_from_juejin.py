"""
从掘金导入1分钟数据到 PG mkt.minute_bar (多线程版)
用法: python tools/import_minute_from_juejin.py --start 2026-06-01 --end 2026-06-30
      python tools/import_minute_from_juejin.py --backfill  # 自动补全缺失日期
"""

from __future__ import annotations

import argparse, datetime as dt, sys, threading, time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Any, Dict, List

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import psycopg2, psycopg2.extras
from tools.db_config import PG_CONFIG

# ── GM SDK 初始化 ──
import import_from_juejin as base
base._ensure_gm_inited()
from gm.api import history

# ═══════════════════════════════════════════════════════════════════
UPSERT = """
INSERT INTO mkt.minute_bar(symbol_id,trade_ts,open,high,low,close,volume,amount)
VALUES %s
ON CONFLICT(symbol_id,trade_ts) DO UPDATE SET
 open=EXCLUDED.open,high=EXCLUDED.high,low=EXCLUDED.low,close=EXCLUDED.close,volume=EXCLUDED.volume,amount=EXCLUDED.amount
"""

_write_lock = threading.Lock()
_stats_lock = threading.Lock()
_stats = {"total": 0, "skipped": 0, "errors": 0}


def fetch_minute(symbol: str, start: dt.datetime, end: dt.datetime) -> list:
    """从掘金拉单标的时间段内的1分钟K线, 返回 dict 列表"""
    try:
        rows = history(
            symbol=symbol, frequency="60s",
            start_time=start.strftime("%Y-%m-%d %H:%M:%S"),
            end_time=end.strftime("%Y-%m-%d %H:%M:%S"),
            fields="symbol,eob,open,high,low,close,volume,amount",
            skip_suspended=True, df=False)
    except Exception as e:
        print(f"[warn] fetch {symbol}: {e}")
        return []

    if not rows:
        return []

    result = []
    for row in rows:
        try:
            eob = row.get("eob") or row.get("bob") or row.get("bar_time")
            bar_time = None
            if isinstance(eob, str):
                bar_time = dt.datetime.fromisoformat(eob[:19])
            elif eob is not None:
                bar_time = eob
            if bar_time is None:
                continue

            result.append({
                "trade_ts": bar_time,
                "open": float(row.get("open", 0) or 0),
                "high": float(row.get("high", 0) or 0),
                "low": float(row.get("low", 0) or 0),
                "close": float(row.get("close", 0) or 0),
                "volume": int(float(row.get("volume", 0) or 0)),
                "amount": float(row.get("amount", 0) or 0),
            })
        except Exception:
            continue
    return result


def fetch_and_store(sid: int, gm_sym: str, date: dt.date) -> int:
    """单标的单日: 检查已有数据 → 拉取 → 写入, 返回写入条数"""
    try:
        conn = psycopg2.connect(**PG_CONFIG)
        conn.autocommit = False
    except Exception:
        with _stats_lock:
            _stats["errors"] += 1
        return 0

    try:
        cur = conn.cursor()
        cur.execute(
            "SELECT 1 FROM mkt.minute_bar WHERE symbol_id=%s AND trade_ts::date=%s LIMIT 1",
            (sid, date))
        if cur.fetchone():
            with _stats_lock:
                _stats["skipped"] += 1
            return 0

        start = dt.datetime.combine(date, dt.time(9, 30))
        end   = dt.datetime.combine(date, dt.time(15, 5))
        bars = fetch_minute(gm_sym, start, end)
        if not bars:
            return 0

        tuples = [(sid, b["trade_ts"], b["open"], b["high"], b["low"], b["close"], b["volume"], b["amount"]) for b in bars]

        with _write_lock:
            psycopg2.extras.execute_values(cur, UPSERT, tuples, page_size=500)
            conn.commit()
        return len(bars)
    except Exception:
        with _stats_lock:
            _stats["errors"] += 1
        try:
            conn.rollback()
        except Exception:
            pass
        return 0
    finally:
        try:
            conn.close()
        except Exception:
            pass


def run_date(date: dt.date, sid_map: dict, workers: int) -> int:
    items = list(sid_map.items())
    day_total = 0
    print(f"  [{date}] {len(items)} 只标的, {workers} 线程...")

    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {
            pool.submit(fetch_and_store, sid, gm_sym, date): (sid, gm_sym)
            for sid, gm_sym in items
        }
        done = 0
        for fut in as_completed(futures):
            done += 1
            try:
                day_total += fut.result()
            except Exception:
                with _stats_lock:
                    _stats["errors"] += 1
            if done % 200 == 0 or done == len(items):
                with _stats_lock:
                    t = _stats["total"] + day_total
                    s = _stats["skipped"]
                    e = _stats["errors"]
                print(f"    [{done}/{len(items)}] +{day_total}条 (累计+{t} 跳过{s} 错误{e})")
    return day_total


def main():
    p = argparse.ArgumentParser(description="掘金1分钟数据导入 PG mkt.minute_bar")
    p.add_argument("--start", default="", help="起始日期 YYYY-MM-DD")
    p.add_argument("--end", default="", help="结束日期 YYYY-MM-DD")
    p.add_argument("--backfill", action="store_true", help="自动补缺失日期")
    p.add_argument("--workers", type=int, default=12, help="并行线程数 (默认12)")
    a = p.parse_args()

    conn = psycopg2.connect(**PG_CONFIG)
    cur = conn.cursor()

    # 标的映射: symbol_id → GM symbol (如 SHSE.600519)
    cur.execute("SELECT id, symbol FROM ref.symbol_info WHERE status='ACTIVE' ORDER BY id")
    sid_map = {}
    for sid, sym in cur.fetchall():
        gm_sym = base.MyQuantBroker._to_gm_symbol(sym)
        if gm_sym:
            sid_map[sid] = gm_sym

    print(f"[minute] 活跃标的: {len(sid_map)} 只, 线程: {a.workers}")

    if a.backfill:
        cur.execute("""
            SELECT DISTINCT d.trade_date
            FROM mkt.daily_bar d
            WHERE NOT EXISTS (
                SELECT 1 FROM mkt.minute_bar m
                WHERE m.symbol_id=d.symbol_id AND m.trade_ts::date=d.trade_date
            ) AND d.trade_date <= CURRENT_DATE
            ORDER BY d.trade_date
        """)
        dates = [r[0] for r in cur.fetchall()]
        conn.close()
        if not dates:
            print("[minute] 无缺口, 数据完整")
            return
        print(f"[minute] 发现 {len(dates)} 天缺口 ({dates[0]}→{dates[-1]})")
    else:
        start_date = dt.date.fromisoformat(a.start) if a.start else dt.date.today()
        end_date   = dt.date.fromisoformat(a.end) if a.end else dt.date.today()
        dates = []
        d = start_date
        while d <= end_date:
            dates.append(d)
            d += dt.timedelta(days=1)
        conn.close()
        print(f"[minute] {a.start or start_date} → {a.end or end_date}, {len(dates)} 天")

    workers = max(1, min(a.workers, 16))
    t0 = time.time()
    grand_total = 0
    for di, d in enumerate(dates):
        day_total = run_date(d, sid_map, workers)
        grand_total += day_total
        elapsed = time.time() - t0
        rate = grand_total / elapsed if elapsed > 0 else 0
        eta = (len(dates) - di - 1) * (elapsed / (di + 1)) if di > 0 and grand_total > 0 else 0
        print(f"  [{di+1}/{len(dates)}] {d}: +{day_total}条 {rate:.0f}条/s ETA {eta:.0f}s")

    elapsed = time.time() - t0
    print(f"[minute] 完成: +{grand_total}条 耗时{elapsed:.0f}s")


if __name__ == "__main__":
    main()
