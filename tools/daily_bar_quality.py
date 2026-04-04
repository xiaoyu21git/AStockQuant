from __future__ import annotations

from typing import Any, Callable, Mapping, Sequence


PRICE_TOLERANCE = 1e-8
PCT_TOLERANCE = 1e-4


Record = Mapping[str, Any]
InvalidSample = tuple[dict[str, Any], list[str]]


def to_float(value: Any) -> float | None:
    if value is None:
        return None
    try:
        result = float(value)
    except Exception:
        return None
    if result != result:
        return None
    return result


def _non_empty_text(value: Any) -> bool:
    if value is None:
        return False
    return bool(str(value).strip())


def detect_daily_price_anomalies(record: Record) -> list[str]:
    anomalies: list[str] = []

    trade_date = record.get("trade_date") or record.get("date")
    if trade_date is None:
        anomalies.append("missing trade_date")

    open_price = to_float(record.get("open"))
    high_price = to_float(record.get("high"))
    low_price = to_float(record.get("low"))
    close_price = to_float(record.get("close"))
    pre_close = to_float(record.get("pre_close"))
    volume = to_float(record.get("volume"))
    turnover = to_float(record.get("turnover"))
    change_amt = to_float(record.get("change_amt"))
    change_pct = to_float(record.get("change_pct"))
    amplitude = to_float(record.get("amplitude"))
    turnover_rate = to_float(record.get("turnover_rate"))

    for field_name, value in {
        "open": open_price,
        "high": high_price,
        "low": low_price,
        "close": close_price,
    }.items():
        if value is None or value <= PRICE_TOLERANCE:
            anomalies.append(f"invalid {field_name}")

    if pre_close is not None and pre_close <= PRICE_TOLERANCE:
        anomalies.append("invalid pre_close")

    if high_price is not None and low_price is not None and high_price + PRICE_TOLERANCE < low_price:
        anomalies.append("high below low")

    if None not in {open_price, high_price, low_price, close_price}:
        if high_price + PRICE_TOLERANCE < max(open_price, close_price, low_price):
            anomalies.append("high below open/low/close")
        if low_price - PRICE_TOLERANCE > min(open_price, close_price, high_price):
            anomalies.append("low above open/high/close")

    if volume is None or volume < 0:
        anomalies.append("invalid volume")
    if turnover is None or turnover < 0:
        anomalies.append("invalid turnover")

    if close_price is not None and close_price > PRICE_TOLERANCE:
        if volume is not None and abs(volume) <= PRICE_TOLERANCE:
            anomalies.append("zero volume with positive close")
        if turnover is not None and abs(turnover) <= PRICE_TOLERANCE:
            anomalies.append("zero turnover with positive close")

    if pre_close is not None and close_price is not None and abs(close_price - pre_close) > PRICE_TOLERANCE:
        if change_amt is not None and abs(change_amt) <= PRICE_TOLERANCE:
            anomalies.append("zero change_amt with close change")
        if change_pct is not None and abs(change_pct) <= PCT_TOLERANCE:
            anomalies.append("zero change_pct with close change")

    if pre_close is not None and pre_close > PRICE_TOLERANCE and high_price is not None and low_price is not None:
        if abs(high_price - low_price) > PRICE_TOLERANCE and amplitude is not None and abs(amplitude) <= PCT_TOLERANCE:
            anomalies.append("zero amplitude with high/low diff")

    if turnover_rate is not None:
        if turnover_rate < 0:
            anomalies.append("negative turnover_rate")
        if turnover is not None and turnover > PRICE_TOLERANCE and abs(turnover_rate) <= PCT_TOLERANCE:
            anomalies.append("zero turnover_rate with positive turnover")

    if "data_source" in record and not _non_empty_text(record.get("data_source")):
        anomalies.append("empty data_source")

    return anomalies


def sanitize_valuation_record(record: Record) -> tuple[dict[str, Any], list[str]]:
    sanitized = dict(record)
    anomalies: list[str] = []

    pe_ratio = to_float(record.get("pe_ratio"))
    pb_ratio = to_float(record.get("pb_ratio"))
    market_cap = to_float(record.get("market_cap"))
    circulating_market_cap = to_float(record.get("circulating_market_cap"))

    if pe_ratio is None or abs(pe_ratio) <= PCT_TOLERANCE:
        if record.get("pe_ratio") is not None:
            anomalies.append("invalid pe_ratio")
        sanitized["pe_ratio"] = None
    else:
        sanitized["pe_ratio"] = pe_ratio

    if pb_ratio is None or abs(pb_ratio) <= PCT_TOLERANCE:
        if record.get("pb_ratio") is not None:
            anomalies.append("invalid pb_ratio")
        sanitized["pb_ratio"] = None
    else:
        sanitized["pb_ratio"] = pb_ratio

    if market_cap is None or market_cap <= PRICE_TOLERANCE:
        if record.get("market_cap") is not None:
            anomalies.append("invalid market_cap")
        sanitized["market_cap"] = None
    else:
        sanitized["market_cap"] = market_cap

    if circulating_market_cap is None or circulating_market_cap <= PRICE_TOLERANCE:
        if record.get("circulating_market_cap") is not None:
            anomalies.append("invalid circulating_market_cap")
        sanitized["circulating_market_cap"] = None
    else:
        sanitized["circulating_market_cap"] = circulating_market_cap

    if (
        sanitized.get("market_cap") is not None
        and sanitized.get("circulating_market_cap") is not None
        and sanitized["market_cap"] + max(1.0, sanitized["market_cap"] * 1e-6) < sanitized["circulating_market_cap"]
    ):
        anomalies.append("market_cap below circulating_market_cap")
        sanitized["market_cap"] = None

    if not any(
        sanitized.get(field_name) is not None
        for field_name in ("pe_ratio", "pb_ratio", "market_cap", "circulating_market_cap")
    ):
        anomalies.append("no usable valuation fields")

    return sanitized, anomalies


def filter_valid_records(
    records: Sequence[Record],
    detector: Callable[[Record], list[str]],
) -> tuple[list[dict[str, Any]], list[InvalidSample]]:
    valid_records: list[dict[str, Any]] = []
    invalid_samples: list[InvalidSample] = []
    for record in records:
        anomalies = detector(record)
        if anomalies:
            invalid_samples.append((dict(record), anomalies))
            continue
        valid_records.append(dict(record))
    return valid_records, invalid_samples


def format_invalid_samples(invalid_samples: Sequence[InvalidSample], limit: int = 3) -> str:
    if not invalid_samples:
        return "none"

    parts: list[str] = []
    for record, anomalies in invalid_samples[:limit]:
        symbol = record.get("symbol") or "?"
        trade_date = record.get("trade_date") or record.get("date") or "?"
        parts.append(f"{symbol}@{trade_date}: {', '.join(anomalies[:3])}")
    return " | ".join(parts)