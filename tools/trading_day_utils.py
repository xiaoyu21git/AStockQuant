from __future__ import annotations

import datetime as dt
import time
from functools import lru_cache
from typing import Callable, Optional

from data_source_config import get_trade_calendar as _get_calendar, clear_calendar_cache


DEFAULT_MARKET_CLOSE_TIME = "15:30"


def parse_time_text(value: str) -> dt.time:
    text = str(value or DEFAULT_MARKET_CLOSE_TIME).strip()
    try:
        return dt.datetime.strptime(text, "%H:%M").time()
    except ValueError as exc:
        raise ValueError(f"invalid time format: {text}, expected HH:MM") from exc


@lru_cache(maxsize=1)
def get_trade_calendar() -> list[dt.date]:
    return _get_calendar()


def resolve_latest_closed_trade_date(
    now: Optional[dt.datetime] = None,
    market_close_time: Optional[dt.time] = None,
) -> dt.date:
    current_time = now or dt.datetime.now()
    close_time = market_close_time or parse_time_text(DEFAULT_MARKET_CLOSE_TIME)
    today = current_time.date()
    close_cutoff = dt.datetime.combine(today, close_time)

    latest_closed: Optional[dt.date] = None
    for trade_date in get_trade_calendar():
        if trade_date < today:
            latest_closed = trade_date
            continue
        if trade_date == today and current_time >= close_cutoff:
            latest_closed = trade_date
        break

    if latest_closed is None:
        raise RuntimeError("未能解析最近已收盘交易日")
    return latest_closed


def resolve_next_market_close_datetime(
    now: Optional[dt.datetime] = None,
    market_close_time: Optional[dt.time] = None,
) -> dt.datetime:
    current_time = now or dt.datetime.now()
    close_time = market_close_time or parse_time_text(DEFAULT_MARKET_CLOSE_TIME)
    today = current_time.date()
    close_today = dt.datetime.combine(today, close_time)

    for trade_date in get_trade_calendar():
        if trade_date < today:
            continue
        if trade_date == today and current_time < close_today:
            return close_today
        if trade_date > today:
            return dt.datetime.combine(trade_date, close_time)

    raise RuntimeError("未能解析下一次收盘时间")


def wait_until_market_close(
    market_close_time: Optional[dt.time] = None,
    status_callback: Optional[Callable[[str], None]] = None,
) -> dt.datetime:
    target_time = resolve_next_market_close_datetime(dt.datetime.now(), market_close_time)
    while True:
        now = dt.datetime.now()
        remaining_seconds = int((target_time - now).total_seconds())
        if remaining_seconds <= 0:
            return dt.datetime.now()

        if status_callback:
            status_callback(
                f"等待到收盘后执行，目标时间={target_time.strftime('%Y-%m-%d %H:%M:%S')}，剩余={remaining_seconds}s"
            )

        sleep_seconds = 60 if remaining_seconds > 300 else min(remaining_seconds, 5)
        time.sleep(max(1, sleep_seconds))