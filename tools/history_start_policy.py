from __future__ import annotations

import datetime as dt


UNIFIED_HISTORY_START_DATE = dt.date(2015, 1, 1)


def clamp_history_start_date(date_value: dt.date) -> dt.date:
    return max(date_value, UNIFIED_HISTORY_START_DATE)


def resolve_history_date_bounds(start_date: dt.date, end_date: dt.date, source_name: str) -> tuple[dt.date, dt.date]:
    resolved_start = clamp_history_start_date(start_date)
    if end_date < resolved_start:
        raise RuntimeError(
            f"{source_name} has no data on or after {UNIFIED_HISTORY_START_DATE.isoformat()}"
        )
    return resolved_start, end_date