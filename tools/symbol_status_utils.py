from __future__ import annotations

import datetime as dt
from typing import Optional


TRACKED_SYMBOL_STATUSES = ("ACTIVE", "ST", "*ST", "SUSPENDED", "DELISTED")
SPECIAL_LAGGING_STATES = {"ST", "SUSPENDED", "DELISTED"}


def normalize_symbol_status(status: object) -> str:
    return str(status or "ACTIVE").strip().upper() or "ACTIVE"


def is_st_name(name: object) -> bool:
    text = str(name or "").strip().upper()
    if not text:
        return False
    normalized = text.lstrip("*")
    return normalized.startswith("ST")


def is_delist_name(name: object) -> bool:
    text = str(name or "").strip()
    return "退" in text


def infer_special_symbol_state(
    name: object,
    status: object,
    delist_date: Optional[dt.date],
    target_date: Optional[dt.date] = None,
) -> Optional[str]:
    normalized_status = normalize_symbol_status(status)
    effective_target = target_date or dt.date.today()

    if normalized_status == "DELISTED":
        return "DELISTED"
    if delist_date is not None and delist_date <= effective_target:
        return "DELISTED"
    if is_delist_name(name):
        return "DELISTED"
    if normalized_status == "SUSPENDED":
        return "SUSPENDED"
    if normalized_status in {"ST", "*ST"} or is_st_name(name):
        return "ST"
    return None


def resolve_effective_target_date(
    target_date: dt.date,
    name: object,
    status: object,
    delist_date: Optional[dt.date],
    latest_trade_date: Optional[dt.date] = None,
) -> dt.date:
    special_state = infer_special_symbol_state(name, status, delist_date, target_date)
    if special_state == "DELISTED" and delist_date is not None and delist_date < target_date:
        return delist_date
    if special_state == "DELISTED" and delist_date is None and latest_trade_date is not None:
        return latest_trade_date
    return target_date


def is_special_lagging_state(state: Optional[str]) -> bool:
    return state in SPECIAL_LAGGING_STATES