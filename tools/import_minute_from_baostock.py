"""
import_minute_from_baostock.py — 证券宝 5 分钟线补录 PG mkt.minute_bar (严格单线程版)

背景:
  - 历史分钟数据此前由 AmiBroker CSV 导入 (import_wstock_csv_to_pg.py), 仅 200~450 只标的
    且 amount 硬编码 NULL; 2026 年实时订阅才全市场带成交额。
  - 证券宝 (baostock) 提供 2015-01-05 起全市场 5 分钟线, 48 根/天, 含成交额,
    恰好匹配 HighFreqFactor barsPerDay=48 设计。
  - ⚠️ 证券宝严格限流: 禁止多线程, 必须单线程顺序请求 + 查询间隔。

用法:
  python tools/import_minute_from_baostock.py                  # 单进程全部标的, 2020-01-01~2025-12-31
  python tools/import_minute_from_baostock.py --limit 20       # pilot: 只处理前 N 只标的
  python tools/import_minute_from_baostock.py --symbols 000001.SZ,600000.SH
  python tools/import_minute_from_baostock.py --start 2015-01-01 --end 2025-12-31
  python tools/import_minute_from_baostock.py --shard 0 --shards 6   # 6 进程并行: 每片独立 state
  python tools/import_minute_from_baostock.py --sleep 0.1      # 每月查询间隔秒数 (默认 0.1)
  python tools/import_minute_from_baostock.py --proxy http://127.0.0.1:7890   # 经本机 Clash 走 CONNECT 隧道改出口 IP

查询粒度 (2026-08-13 实测结论): 按月查询 (每查询 2~3 页)。证券宝按"查询成本"限流:
24 页的年查询会被 10002007 拒绝或静默截断, 月查询实测 244 次零失败 (含被限流时段);
单进程全市场约 155h, N 分片并行 ÷ N。

写入策略 (经确认):
  - 按 (symbol, trade_date) 替换: 仅当证券宝当日返回数据时, 先 DELETE 该日旧行再 INSERT;
    证券宝无数据的日期保留旧行 (保底不丢行)。
  - 2026-01-01 起为实时订阅数据, 硬性保护不触碰。
  - 断点续跑: 每标的每月完成后记入 state JSON (分片模式每片独立 state 文件), 重跑自动跳过。
  - 截断守卫: baostock 分页失败不报错会静默丢行, 按月校验该月交易日数×48 根,
    低于 90% 视为截断, 不写入不标记完成。
  - 瞬态错误 (10001001 请求过频/10002007 数据超时) 自动重试 2 次带退避。
"""

from __future__ import annotations

import argparse
import calendar
import datetime as dt
import json
import os
import sys
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import baostock as bs
import psycopg2, psycopg2.extras
from tools.db_config import pg_connect
from tools.baostock_proxy import apply_baostock_proxy, set_data_timeout

STATE_DIR = Path(__file__).resolve().parents[1] / "tools" / "state"
STATE_FILE = STATE_DIR / "baostock_minute_done.json"  # 单进程/旧格式 state (分片模式从它迁移后保留)

# 硬保护: 实时订阅数据从 2026-01-01 起, 补录结束日期不得超过此界
LIVE_DATA_START = dt.date(2026, 1, 1)

# 截断守卫: 5 分钟线每交易日 48 根 (与 HighFreqFactor barsPerDay=48 一致);
# 月查询行数低于该标的该月交易日数 × 48 × 该比例 → 视为服务器静默截断
BARS_PER_DAY = 48
MIN_MONTH_COVERAGE_RATIO = 0.9

FIELDS = "date,time,open,high,low,close,volume,amount"

# 登录重试: 实测 6 并发登录时服务器直接拒绝 (WinError 10057), 长跑无人值守必须自动重试
LOGIN_MAX_ATTEMPTS = 5
LOGIN_RETRY_WAIT_S = 30.0


# ══════════════════════════════════════════════════════
# 状态文件 (断点续跑)
# ══════════════════════════════════════════════════════

def load_state(path: Path) -> dict:
    if path.exists():
        try:
            return json.loads(path.read_text(encoding="utf-8"))
        except Exception:
            return {}
    return {}


def save_state(state: dict, path: Path) -> None:
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(state, ensure_ascii=False, indent=1), encoding="utf-8")


def shard_state_file(shard: int, shards: int) -> Path:
    """分片模式每片独立 state 文件, 避免多进程互相覆盖"""
    if shards > 1:
        return STATE_DIR / f"baostock_minute_done_shard{shard}.json"
    return STATE_FILE


def shard_index(symbol: str, shards: int) -> int:
    """crc32 稳定散列: 同一标的每次运行落同一分片 (进程重启不漂移)"""
    return zlib.crc32(symbol.encode("utf-8")) % shards


# ══════════════════════════════════════════════════════
# 符号转换
# ══════════════════════════════════════════════════════

def to_bs_symbol(symbol: str) -> str:
    """'000001.SZ' → 'sz.000001' / '600000.SH' → 'sh.600000' / '83xxxx.BJ' → 'bj.83xxxx'"""
    code, _, exch = symbol.rpartition(".")
    return f"{exch.lower()}.{code}"


# ══════════════════════════════════════════════════════
# 数据拉取与写入
# ══════════════════════════════════════════════════════

def query_month(bs_symbol: str, year: int, month: int) -> list[list[str]] | None:
    """拉取单标的单月 5 分钟线 (2~3 页小查询, 规避服务器按查询成本的限流);
    瞬态错误重试 2 次带退避, 仍失败返回 None (本次停止, 下次续跑重试)"""
    last_day = calendar.monthrange(year, month)[1]
    start = f"{year:04d}-{month:02d}-01"
    end = f"{year:04d}-{month:02d}-{last_day:02d}"
    for attempt in range(3):
        try:
            rs = bs.query_history_k_data_plus(
                bs_symbol, FIELDS,
                start_date=start, end_date=end,
                frequency="5", adjustflag="3")
            if rs.error_code != "0":
                raise RuntimeError(f"error_code={rs.error_code} msg={rs.error_msg}")
            rows: list[list[str]] = []
            while rs.next():
                rows.append(rs.get_row_data())
            return rows
        except Exception as e:
            backoff = 3.0 * (attempt + 1)
            print(f"  [warn] {bs_symbol} {year}-{month:02d} 第{attempt + 1}次失败: {e} "
                  f"(sleep {backoff:.0f}s)", flush=True)
            time.sleep(backoff)
    return None


def replace_date_rows(cur, symbol_id: int, date_str: str, bars: list[list[str]]) -> None:
    """按日替换: 先删旧行再插新行 (仅当有数据时调用)"""
    cur.execute(
        "DELETE FROM mkt.minute_bar WHERE symbol_id = %s AND trade_ts >= %s::date AND trade_ts < %s::date + INTERVAL '1 day'",
        (symbol_id, date_str, date_str))
    payload = []
    for b in bars:
        # b: [date, time(YYYYMMDDHHMMSSmmm), open, high, low, close, volume, amount]
        t = b[1]
        bar_ts = f"{t[0:4]}-{t[4:6]}-{t[6:8]} {t[8:10]}:{t[10:12]}:{t[12:14]}"
        payload.append((
            symbol_id, bar_ts,
            float(b[2]), float(b[3]), float(b[4]), float(b[5]),
            int(float(b[6])),            # volume: 股, bigint
            float(b[7]) if b[7] else None,  # amount: 成交额(元)
        ))
    psycopg2.extras.execute_values(
        cur,
        "INSERT INTO mkt.minute_bar(symbol_id,trade_ts,open,high,low,close,volume,amount) "
        "VALUES %s ON CONFLICT(symbol_id,trade_ts) DO NOTHING",
        payload)


def iter_months(start: dt.date, end: dt.date):
    """闭区间按月枚举 [(年, 月), ...]"""
    y, m = start.year, start.month
    while (y, m) <= (end.year, end.month):
        yield y, m
        m += 1
        if m > 12:
            m = 1
            y += 1


def process_symbol(cur, state: dict, state_path: Path, symbol_id: int, symbol: str,
                   start: dt.date, end: dt.date, sleep_s: float,
                   expected_by_month: dict[str, int]) -> dict:
    """单标的: 逐月查询 → 截断守卫 → 按日替换写入; 全部月份完成(含空月)才标记 done"""
    entry = state.get(symbol, {})
    months_done = set(entry.get("months_done", []))
    # 旧格式迁移: 年粒度时代的 years_done 展开为 12 个月, 已入库年份不重拉
    for y in entry.get("years_done", []):
        for m in range(1, 13):
            months_done.add(f"{y:04d}-{m:02d}")
    bs_sym = to_bs_symbol(symbol)
    t0 = time.time()
    total_rows = 0

    for y, m in iter_months(start, end):
        key = f"{y:04d}-{m:02d}"
        if key in months_done:
            continue
        rows = query_month(bs_sym, y, m)
        if rows is None:
            # 重试后仍失败: 不标记, 下次续跑重试
            return {"done": False, "rows": total_rows, "elapsed": time.time() - t0}
        # 截断守卫: 分页失败不报错, 静默截断; 行数低于该月交易日数×48×0.9
        # 时不写入不标记, 保住旧行不丢
        expected_days = expected_by_month.get(key, 0)
        if expected_days > 0:
            min_rows = int(expected_days * BARS_PER_DAY * MIN_MONTH_COVERAGE_RATIO)
            if len(rows) < min_rows:
                print(f"  [warn] {symbol} {key} 行数 {len(rows)} < 期望下限 {min_rows} "
                      f"({expected_days} 交易日×48×0.9), 疑似截断, 不写入不标记", flush=True)
                return {"done": False, "rows": total_rows, "elapsed": time.time() - t0}
        by_date: dict[str, list[list[str]]] = {}
        for r in rows:
            by_date.setdefault(r[0], []).append(r)
        for date_str, bars in by_date.items():
            replace_date_rows(cur, symbol_id, date_str, bars)
            total_rows += len(bars)
        cur.connection.commit()
        months_done.add(key)
        # 逐月落盘: 被限流中断时保住已完成月份, 续跑不再重拉
        state[symbol] = {"months_done": sorted(months_done), "rows": total_rows}
        save_state(state, state_path)
        time.sleep(sleep_s)

    state[symbol] = {"months_done": sorted(months_done), "rows": total_rows,
                     "done_at": dt.datetime.now().isoformat(timespec="seconds")}
    save_state(state, state_path)
    print(f"[ok] {symbol} 完成: {total_rows} 行 ({time.time() - t0:.0f}s)", flush=True)
    return {"done": True, "rows": total_rows, "elapsed": time.time() - t0}


# ══════════════════════════════════════════════════════
# 主流程
# ══════════════════════════════════════════════════════

def main() -> None:
    ap = argparse.ArgumentParser(description="证券宝 5 分钟线补录 mkt.minute_bar (月粒度, 可多进程分片)")
    ap.add_argument("--start", default="2020-01-01", help="开始日期 (默认 2020-01-01)")
    ap.add_argument("--end", default="2025-12-31", help="结束日期 (默认 2025-12-31, 不得晚于 2025-12-31)")
    ap.add_argument("--limit", type=int, default=0, help="只处理前 N 只标的 (pilot 用, 0=全部)")
    ap.add_argument("--symbols", default="", help="逗号分隔指定标的 (如 000001.SZ,600000.SH)")
    ap.add_argument("--sleep", type=float, default=0.1,
                    help="每月查询间隔秒数 (默认 0.1, 对齐日线流水线 import_from_baostock 批间间隔)")
    ap.add_argument("--shard", type=int, default=0, help="分片编号 0..shards-1 (与 --shards 配合多进程并行)")
    ap.add_argument("--shards", type=int, default=1, help="总分片数 (1=单进程, 每片独立 state 文件)")
    ap.add_argument("--proxy", default=os.environ.get("BAOSTOCK_PROXY", ""),
                    help="HTTP CONNECT 代理 (如 http://127.0.0.1:7890 本机 Clash; "
                         "默认取环境变量 BAOSTOCK_PROXY, 为空则直连)")
    args = ap.parse_args()

    if args.shards < 1 or not (0 <= args.shard < args.shards):
        print("[error] --shard 须满足 0 <= shard < shards", flush=True)
        sys.exit(1)
    state_path = shard_state_file(args.shard, args.shards)

    start = dt.date.fromisoformat(args.start)
    end = dt.date.fromisoformat(args.end)
    if end >= LIVE_DATA_START:
        print(f"[error] --end 不得晚于 {LIVE_DATA_START - dt.timedelta(days=1)} (2026 起为实时订阅数据)", flush=True)
        sys.exit(1)

    conn = pg_connect()
    cur = conn.cursor()
    try:
        if args.symbols:
            sym_list = [s.strip() for s in args.symbols.split(",") if s.strip()]
            cur.execute(
                "SELECT id, symbol FROM ref.symbol_info WHERE symbol = ANY(%s) ORDER BY id",
                (sym_list,))
        else:
            # 仅 A 股: 证券宝只覆盖 A 股, symbol_info 混有 AAPL./MSFT. 等非 A 股行会报 10004006
            cur.execute(
                "SELECT id, symbol FROM ref.symbol_info "
                "WHERE asset_class = 'STOCK' AND symbol ~ '^[0-9]{6}\\.(SH|SZ|BJ)$' ORDER BY id")
        targets = cur.fetchall()
        # 分片: crc32 稳定散列, 同一标的每次运行落同一分片
        if args.shards > 1:
            targets = [(sid, sym) for sid, sym in targets
                       if shard_index(sym, args.shards) == args.shard]
        if args.limit > 0:
            targets = targets[: args.limit]
        # 截断守卫基准: 每标的每月的实际交易日数 (daily_bar 按标的统计, 停牌日天然无行,
        # 该数×48 即当月 5 分钟线应有根数)
        cur.execute(
            "SELECT symbol_id, EXTRACT(YEAR FROM trade_date)::int AS yr,"
            " EXTRACT(MONTH FROM trade_date)::int AS mo, COUNT(DISTINCT trade_date) AS days"
            " FROM mkt.daily_bar"
            " WHERE trade_date BETWEEN %s AND %s"
            " GROUP BY symbol_id, EXTRACT(YEAR FROM trade_date), EXTRACT(MONTH FROM trade_date)",
            (start, end))
        expected_by_month = {}
        for sid, yr, mo, days in cur.fetchall():
            expected_by_month.setdefault(sid, {})[f"{yr:04d}-{mo:02d}"] = days
    finally:
        cur.close()

    state = load_state(state_path)
    # 旧格式迁移: 单进程时代 state 里的标的按分片归属播种到各片 state (已入库进度不丢)
    if args.shards > 1:
        legacy = load_state(STATE_FILE)
        migrated = 0
        for sym, entry in legacy.items():
            if shard_index(sym, args.shards) == args.shard and sym not in state:
                state[sym] = entry
                migrated += 1
        if migrated:
            save_state(state, state_path)
            print(f"[state] 从旧 state 迁移 {migrated} 只标的进度到本分片", flush=True)
    # 部分完成的标的 (无 done_at) 也要续跑, 按 months_done 跳过已做月份
    pending = [(sid, sym) for sid, sym in targets if not state.get(sym, {}).get("done_at")]
    done_count = len(targets) - len(pending)
    shard_tag = f" 分片 {args.shard}/{args.shards}" if args.shards > 1 else ""
    print(f"[start] 范围 {start}~{end} 标的 {len(targets)} 只{shard_tag} "
          f"(已完成 {done_count}, 待补 {len(pending)}, sleep {args.sleep}s)", flush=True)

    # 证券宝登录 (每进程独立会话); --proxy 经 HTTP CONNECT 隧道改出口 IP 防限流
    if args.proxy:
        apply_baostock_proxy(args.proxy)
    lg = None
    for attempt in range(1, LOGIN_MAX_ATTEMPTS + 1):
        try:
            lg = bs.login()
        except Exception as e:
            print(f"[warn] 证券宝登录第 {attempt} 次异常: {e}", flush=True)
            lg = None
        if lg is not None and lg.error_code == "0":
            break
        if lg is not None:
            print(f"[warn] 证券宝登录第 {attempt} 次失败: {lg.error_msg}", flush=True)
        if attempt == LOGIN_MAX_ATTEMPTS:
            print("[error] 证券宝登录重试耗尽, 退出 (进度已落 state, 重跑自动续)", flush=True)
            sys.exit(1)
        time.sleep(LOGIN_RETRY_WAIT_S)
    print("[login] 证券宝登录成功", flush=True)
    # 直连模式补读超时 (代理隧道已自带): 服务器中途掐断会在 120s 内报错停止,
    # 不会永久阻塞; 被掐断的月不标记完成, 状态文件保住进度
    set_data_timeout()

    t_start = time.time()
    ok_rows = 0
    done_cnt = 0
    skipped = 0
    cur = conn.cursor()
    try:
        for i, (sid, sym) in enumerate(pending, 1):
            res = process_symbol(cur, state, state_path, sid, sym, start, end,
                                 args.sleep, expected_by_month.get(sid, {}))
            if res["done"]:
                ok_rows += res["rows"]
                done_cnt += 1
            else:
                # 疑似限流: 立即停手, 续跑会从该标的的已完成月份接着做 (进度已逐月落盘)
                skipped += 1
                print(f"[stop] {sym} 月份拉取失败, 疑似限流, 本轮停止 "
                      f"(进度已落 state, 冷却后重跑自动续)", flush=True)
                break
            if i % 50 == 0:
                el = time.time() - t_start
                per = el / i
                print(f"[progress] {i}/{len(pending)} 用时 {el:.0f}s 均 {per:.1f}s/只 "
                      f"预计剩余 {(len(pending) - i) * per / 3600:.1f}h", flush=True)
    finally:
        cur.close()
        bs.logout()
        conn.close()

    print(f"[finish] 完成 {done_cnt} 只 跳过 {skipped} 只 "
          f"写入 {ok_rows} 行 总用时 {(time.time() - t_start) / 60:.1f} 分钟", flush=True)


if __name__ == "__main__":
    main()
