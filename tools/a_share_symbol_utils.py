from __future__ import annotations

import re
from typing import Optional


MAINLAND_STOCK_PATTERN = re.compile(r"^(?:[0-9]{6}|[0-9]{6}\.(?:SH|SZ|BJ))$", re.IGNORECASE)


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


def classify_mainland_stock_symbol(symbol: str) -> Optional[str]:
    normalized = normalize_symbol(symbol)
    if not MAINLAND_STOCK_PATTERN.fullmatch(normalized):
        return None

    code, _, exchange = normalized.partition(".")
    if exchange == "BJ":
        return "BJ"
    if code.startswith(("200", "900")):
        return "B"
    if exchange in {"SH", "SZ"}:
        return "A"
    return None


def is_mainland_a_share_symbol(symbol: str) -> bool:
    return classify_mainland_stock_symbol(symbol) == "A"


def is_mainland_b_share_symbol(symbol: str) -> bool:
    return classify_mainland_stock_symbol(symbol) == "B"


def is_supported_akshare_stock_symbol(symbol: str) -> bool:
    return classify_mainland_stock_symbol(symbol) in {"A", "BJ"}


def to_akshare_symbol(symbol: str) -> Optional[str]:
    normalized = normalize_symbol(symbol)
    if not is_supported_akshare_stock_symbol(normalized):
        return None

    code, _, exchange = normalized.partition(".")
    if exchange in {"SH", "SZ", "BJ"}:
        return f"{exchange.lower()}{code}"
    return None