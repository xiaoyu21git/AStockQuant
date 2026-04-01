from __future__ import annotations

import re
from typing import Optional


MAINLAND_A_SHARE_PATTERN = re.compile(r"^(?:[0-9]{6}|[0-9]{6}\.(?:SH|SZ|BJ))$", re.IGNORECASE)


def normalize_symbol(symbol: str) -> str:
    code = str(symbol).strip().upper()
    if not code or "." in code:
        return code
    if code.startswith(("6", "9")):
        return f"{code}.SH"
    if code.startswith(("0", "2", "3")):
        return f"{code}.SZ"
    if code.startswith(("4", "8")):
        return f"{code}.BJ"
    return code


def is_mainland_a_share_symbol(symbol: str) -> bool:
    return bool(MAINLAND_A_SHARE_PATTERN.fullmatch(normalize_symbol(symbol)))


def to_akshare_symbol(symbol: str) -> Optional[str]:
    normalized = normalize_symbol(symbol)
    if normalized.endswith(".SH"):
        return f"sh{normalized.split('.')[0]}"
    if normalized.endswith(".SZ"):
        return f"sz{normalized.split('.')[0]}"
    return None