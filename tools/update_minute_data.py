"""
update_minute_data.py — 分析 minute_bar 缺口并回填 (多线程版)
用法: python tools/update_minute_data.py              # 当日
      python tools/update_minute_data.py --backfill    # 自动找缺口
      python tools/update_minute_data.py --workers 16  # 指定线程数
"""

import sys, argparse, datetime as dt, time, threading
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import akshare as ak, pandas as pd, psycopg2, psycopg2.extras
from tools.db_config import PG_CONFIG

UPSERT = """
INSERT INTO mkt.minute_bar(symbol_id,trade_ts,open,high,low,close,volume,amount)
VALUES(%(symbol_id)s,%(trade_ts)s,%(open)s,%(high)s,%(low)s,%(close)s,%(volume)s,%(amount)s)
ON CONFLICT(symbol_id,trade_ts) DO UPDATE SET
 open=EXCLUDED.open,high=EXCLUDED.high,low=EXCLUDED.low,close=EXCLUDED.close,volume=EXCLUDED.volume,amount=EXCLUDED.amount
"""

# ── 全局写锁 (commit 串行化, 避免 PostgreSQL 冲突) ──
_write_lock = threading.Lock()
# ── 全局计数器 ──
_stats_lock = threading.Lock()
_stats = {"total": 0, "skipped": 0, "errors": 0}


def fetch(symbol: str, date: dt.date) -> list:
    """单日单标的分钟K线拉取 (akshare)"""
    try:
        df = ak.stock_zh_a_hist_min_em(
            symbol=symbol, period="1",
            start_date=date.strftime("%Y-%m-%d"),
            end_date=date.strftime("%Y-%m-%d"), adjust="")
    except Exception:
        return []
    if df is None or df.empty:
        return []
    bars = []
    for _, r in df.iterrows():
        try:
            ts = pd.Timestamp(r["时间"]).to_pydatetime()
            o = float(r.get("开盘", 0) or 0)
            c = float(r.get("收盘", 0) or 0)
            if o == 0:
                o = c
            bars.append({
                "trade_ts": ts,
                "open": o,
                "high": float(r.get("最高", 0) or 0),
                "low": float(r.get("最低", 0) or 0),
                "close": c,
                "volume": int(r.get("成交量", 0) or 0),
                "amount": float(r.get("成交额", 0) or 0),
            })
        except Exception:
            continue
    return bars


def fetch_and_store(sid: int, sym: str, date: dt.date, sleep: float) -> int:
    """
    单标的单日: 检查是否存在 → 拉取 → 写入。
    返回写入条数 (0 表示已存在或请求失败)。
    线程安全: 每个 worker 用独立连接。
    """
    # 独立连接 (psycopg2 连接不是线程安全的)
    try:
        conn = psycopg2.connect(**PG_CONFIG)
        conn.autocommit = False
    except Exception as e:
        with _stats_lock:
            _stats["errors"] += 1
        print(f"[WARN] DB连接失败 sid={sid} sym={sym}: {e}")
        return 0

    try:
        cur = conn.cursor()
        # 检查是否已有该日数据
        cur.execute(
            "SELECT 1 FROM mkt.minute_bar WHERE symbol_id=%s AND trade_ts::date=%s LIMIT 1",
            (sid, date))
        if cur.fetchone():
            with _stats_lock:
                _stats["skipped"] += 1
            return 0

        # 拉取
        bars = fetch(sym.zfill(6), date)
        if not bars:
            return 0

        for b in bars:
            b["symbol_id"] = sid

        # 线程安全写入
        with _write_lock:
            psycopg2.extras.execute_values(cur, UPSERT, bars, page_size=500)
            conn.commit()

        if sleep > 0:
            time.sleep(sleep)
        return len(bars)
    except Exception as e:
        with _stats_lock:
            _stats["errors"] += 1
        try:
            conn.rollback()
        except Exception:
            pass
        print(f"[WARN] fetch_and_store 异常 sid={sid} sym={sym} date={date}: {e}")
        return 0
    finally:
        try:
            conn.close()
        except Exception:
            pass


def run_date(date: dt.date, sid_map: dict, workers: int, sleep: float) -> int:
    """多线程并行拉取单日全量标的"""
    items = list(sid_map.items())  # [(sid, sym), ...]
    day_total = 0

    print(f"  [{date}] 开始, {len(items)} 只标的, {workers} 线程...")

    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {
            pool.submit(fetch_and_store, sid, sym, date, sleep): (sid, sym)
            for sid, sym in items
        }
        done_count = 0
        for fut in as_completed(futures):
            sid, sym = futures[fut]
            done_count += 1
            try:
                n = fut.result()
                day_total += n
            except Exception as e:
                with _stats_lock:
                    _stats["errors"] += 1
                print(f"[WARN] 线程异常 {sym}: {e}")

            # 每 200 只输出进度
            if done_count % 200 == 0 or done_count == len(items):
                with _stats_lock:
                    t = _stats["total"] + day_total
                    s = _stats["skipped"]
                    e = _stats["errors"]
                print(f"    [{done_count}/{len(items)}] +{day_total}条 "
                      f"(累计+{t} 跳过{s} 错误{e})")

    return day_total


def main():
    p = argparse.ArgumentParser(
        description="分钟K线数据更新 (多线程)")
    p.add_argument("--target-date", default="",
                   help="目标日期 (默认当日)")
    p.add_argument("--backfill", action="store_true",
                   help="自动补缺历史缺口")
    p.add_argument("--workers", type=int, default=12,
                   help="并行线程数 (默认12)")
    p.add_argument("--sleep", type=float, default=0.02,
                   help="单标的请求间隔秒 (默认0.02)")
    a = p.parse_args()

    target = dt.date.fromisoformat(a.target_date) if a.target_date else dt.date.today()
    workers = max(1, min(a.workers, 32))

    conn = psycopg2.connect(**PG_CONFIG)
    cur = conn.cursor()
    cur.execute("SELECT id, symbol FROM ref.symbol_info WHERE status='ACTIVE' ORDER BY id")
    sid_map = {r[0]: r[1] for r in cur.fetchall()}
    conn.close()

    print(f"[minute] 活跃标的: {len(sid_map)}只, 线程: {workers}")

    if a.backfill:
        conn2 = psycopg2.connect(**PG_CONFIG)
        cur2 = conn2.cursor()
        cur2.execute("""
            SELECT DISTINCT d.trade_date
            FROM mkt.daily_bar d
            WHERE NOT EXISTS (
                SELECT 1 FROM mkt.minute_bar m
                WHERE m.symbol_id=d.symbol_id AND m.trade_ts::date=d.trade_date
            ) AND d.trade_date <= %s
            ORDER BY d.trade_date
        """, (target,))
        dates = [r[0] for r in cur2.fetchall()]
        conn2.close()

        if not dates:
            print("[minute] 无缺口, 数据完整")
            return
        print(f"[minute] 发现 {len(dates)} 天缺口 ({dates[0]}→{dates[-1]})")
    else:
        dates = [target]
        print(f"[minute] 当日 {target}")

    t0 = time.time()
    grand_total = 0
    for di, d in enumerate(dates):
        day_total = run_date(d, sid_map, workers, a.sleep)
        grand_total += day_total
        with _stats_lock:
            ts = _stats["total"] + grand_total
            sk = _stats["skipped"]
            er = _stats["errors"]
        elapsed = time.time() - t0
        rate = grand_total / elapsed if elapsed > 0 else 0
        eta = (len(dates) - di - 1) * (elapsed / (di + 1)) if di > 0 and grand_total > 0 else 0
        print(f"  [{di+1}/{len(dates)}] {d}: +{day_total}条 "
              f"(累计+{ts} 跳过{sk} 错误{er}) "
              f"{rate:.0f}条/s ETA {eta:.0f}s")

    elapsed = time.time() - t0
    with _stats_lock:
        print(f"[minute] 完成: +{_stats['total'] + grand_total}条 "
              f"跳过{_stats['skipped']} 错误{_stats['errors']} "
              f"耗时{elapsed:.1f}s")


if __name__ == "__main__":
    main()
