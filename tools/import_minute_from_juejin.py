"""
从掘金导入分钟线数据到 astock_quant.minute_bar。

用法示例（在项目根目录）：

    .venv/Scripts/python.exe tools/import_minute_from_juejin.py \
        --symbols 600519.SH,600000.SH \
        --start 2024-01-01 --end 2024-01-31 \
        --timeframes 1min,5min

说明：
- 依赖已有的 tools/import_from_juejin.py 中的 GM token 配置与 MYSQL_CONFIG。
- minute_bar 结构见 astock_init.sql：按 symbol_id + timeframe + bar_time 唯一。
- 目前仅拉取 1 分钟、5 分钟 K 线，可按需扩展。
"""

from __future__ import annotations

import argparse
import datetime as dt
from typing import Any, Dict, Iterable, List

import pymysql

# 复用已有的掘金与 MySQL 配置
import import_from_juejin as base
from astock_engine.broker.myquant_broker import MyQuantBroker  # 仅用于代码转换


def _ensure_gm_inited() -> None:
    """复用 daily 导入脚本里的 gm 初始化逻辑。"""

    # import_from_juejin 内部已经实现该逻辑
    if hasattr(base, "_ensure_gm_inited"):
        base._ensure_gm_inited()  # type: ignore[attr-defined]
        return
    raise RuntimeError("import_from_juejin._ensure_gm_inited 不存在，请检查脚本版本")


def get_connection():
    cfg = base.MYSQL_CONFIG
    return pymysql.connect(
        host=cfg["host"],
        port=cfg["port"],
        user=cfg["user"],
        password=cfg["password"],
        database=cfg["database"],
        charset=cfg["charset"],
        autocommit=False,
    )


def get_symbol_id(cursor, symbol: str) -> int | None:
    cursor.execute("SELECT symbol_id FROM symbol_info WHERE symbol=%s", (symbol,))
    row = cursor.fetchone()
    return int(row[0]) if row else None


def fetch_minute_bars_from_juejin(
    symbol: str,
    start: dt.datetime,
    end: dt.datetime,
    timeframe: str,
) -> List[Dict[str, Any]]:
    """从掘金获取某标的在 [start, end] 区间的分钟线数据。

    timeframe: '1min' / '5min' / '15min' / '30min' / '60min'
    对应掘金 frequency：60s / 300s / 900s / 1800s / 3600s。
    """

    from gm.api import history  # type: ignore[import]

    _ensure_gm_inited()

    gm_symbol = MyQuantBroker._to_gm_symbol(symbol)  # type: ignore[attr-defined]
    if not gm_symbol:
        return []

    tf_map = {
        "1min": "60s",
        "5min": "300s",
        "15min": "900s",
        "30min": "1800s",
        "60min": "3600s",
    }
    gm_freq = tf_map.get(timeframe)
    if gm_freq is None:
        raise ValueError(f"不支持的 timeframe: {timeframe}")

    start_str = start.strftime("%Y-%m-%d %H:%M:%S")
    end_str = end.strftime("%Y-%m-%d %H:%M:%S")

    try:
        rows = history(
            symbol=gm_symbol,
            frequency=gm_freq,
            start_time=start_str,
            end_time=end_str,
            fields=None,
            skip_suspended=True,
            df=False,
        )
    except Exception as exc:  # pragma: no cover - 运行期错误
        print(f"[warn] minute history 拉取失败 {symbol} {timeframe}: {exc}")
        return []

    result: List[Dict[str, Any]] = []
    for row in rows or []:
        eob = row.get("eob") or row.get("bob") or row.get("bar_time")
        bar_time: dt.datetime | None = None
        if eob is not None:
            try:
                if isinstance(eob, str):
                    # 掘金通常返回 ISO 格式
                    bar_time = dt.datetime.fromisoformat(eob[:19])
                else:
                    bar_time = eob
            except Exception:
                bar_time = None
        if bar_time is None:
            continue

        def _f(name: str, *alts: str) -> float:
            for key in (name, *alts):
                try:
                    v = row.get(key)
                except Exception:
                    v = None
                if v is not None:
                    try:
                        return float(v)
                    except Exception:
                        continue
            return 0.0

        open_ = _f("open")
        high = _f("high")
        low = _f("low")
        close = _f("close")
        volume = _f("volume")
        turnover = _f("amount", "turnover")
        vwap = turnover / volume if volume > 0 else None

        result.append(
            {
                "bar_time": bar_time,
                "open": open_,
                "high": high,
                "low": low,
                "close": close,
                "volume": volume,
                "turnover": turnover,
                "vwap": vwap,
            }
        )

    return result


def upsert_minute_bars(
    cursor,
    symbol_id: int,
    timeframe: str,
    bars: Iterable[Dict[str, Any]],
) -> None:
    """批量写入 minute_bar，按 (symbol_id, timeframe, bar_time) 唯一键 upsert。"""

    sql = """
    INSERT INTO minute_bar (
        symbol_id, timeframe, bar_time,
        open, high, low, close,
        volume, turnover, vwap
    ) VALUES (
        %(symbol_id)s, %(timeframe)s, %(bar_time)s,
        %(open)s, %(high)s, %(low)s, %(close)s,
        %(volume)s, %(turnover)s, %(vwap)s
    )
    ON DUPLICATE KEY UPDATE
        open = VALUES(open),
        high = VALUES(high),
        low = VALUES(low),
        close = VALUES(close),
        volume = VALUES(volume),
        turnover = VALUES(turnover),
        vwap = VALUES(vwap)
    """

    data = []
    for b in bars:
        data.append(
            {
                "symbol_id": symbol_id,
                "timeframe": timeframe,
                "bar_time": b["bar_time"],
                "open": b.get("open", 0.0),
                "high": b.get("high", 0.0),
                "low": b.get("low", 0.0),
                "close": b.get("close", 0.0),
                "volume": b.get("volume", 0.0),
                "turnover": b.get("turnover", 0.0),
                "vwap": b.get("vwap"),
            }
        )

    if not data:
        return

    cursor.executemany(sql, data)


def main() -> None:
    parser = argparse.ArgumentParser(description="从掘金导入分钟线到 astock_quant.minute_bar")
    parser.add_argument(
        "--symbols",
        type=str,
        required=True,
        help="逗号分隔的标的代码列表，例如 600519.SH,600000.SH",
    )
    parser.add_argument(
        "--start",
        type=str,
        default="2023-01-01",
        help="开始日期 YYYY-MM-DD，默认 2023-01-01",
    )
    parser.add_argument(
        "--end",
        type=str,
        default=dt.date.today().strftime("%Y-%m-%d"),
        help="结束日期 YYYY-MM-DD，默认今天",
    )
    parser.add_argument(
        "--timeframes",
        type=str,
        default="1min,5min",
        help="逗号分隔的周期列表，例如 1min,5min",
    )

    args = parser.parse_args()

    symbols = [s.strip() for s in args.symbols.split(",") if s.strip()]
    timeframes = [tf.strip() for tf in args.timeframes.split(",") if tf.strip()]

    start_date = dt.datetime.strptime(args.start, "%Y-%m-%d")
    end_date = dt.datetime.strptime(args.end, "%Y-%m-%d")
    # 结束时间设为当日 23:59:59
    end_date = end_date.replace(hour=23, minute=59, second=59)

    conn = get_connection()
    try:
        cur = conn.cursor()
        for symbol in symbols:
            symbol_id = get_symbol_id(cur, symbol)
            if symbol_id is None:
                print(f"[warn] symbol_info 中找不到 {symbol}，跳过")
                continue

            for tf in timeframes:
                print(f"[import] {symbol} {tf} {args.start}~{args.end}...")
                bars = fetch_minute_bars_from_juejin(symbol, start_date, end_date, tf)
                print(f"[import] fetched {len(bars)} bars for {symbol} {tf}")
                upsert_minute_bars(cur, symbol_id, tf, bars)

        conn.commit()
        print("[import] minute_bar 导入完成并已提交")
    except Exception as exc:  # pragma: no cover - 运行期错误
        conn.rollback()
        print(f"[error] 导入失败，已回滚: {exc}")
        raise
    finally:
        conn.close()


if __name__ == "__main__":  # pragma: no cover - 脚本入口
    main()
