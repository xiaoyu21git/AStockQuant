#!/usr/bin/env python3

import argparse
import csv
import dataclasses
import datetime as dt
import json
import math
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

try:
    import numpy as np
except Exception:  # pragma: no cover
    np = None

try:
    import talib  # type: ignore
except Exception:  # pragma: no cover
    talib = None


DATES = [(dt.date(2024, 1, 1) + dt.timedelta(days=index)).isoformat() for index in range(10)]
SYMBOLS = ["AAA", "BBB", "CCC"]
BENCHMARK = "000300.SH"


@dataclasses.dataclass
class Scenario:
    dates: List[str]
    symbols: List[str]
    benchmark: str
    price: Dict[str, List[float]]
    high: Dict[str, List[float]]
    low: Dict[str, List[float]]
    volume: Dict[str, List[float]]
    turnover_rate: Dict[str, List[float]]
    amplitude: Dict[str, List[float]]
    adj_factor: Dict[str, List[float]]
    benchmark_price: List[float]
    cross_section: Dict[str, Dict[str, Dict[str, float]]]
    financial_series: Dict[str, Dict[str, List[float]]]
    macro_series: Dict[str, List[float]]
    sentiment_series: Dict[str, List[float]]
    industry_series: Dict[str, List[float]]


def _series(base: float, step: float, wobble: float, count: int = 10) -> List[float]:
    values: List[float] = []
    for index in range(count):
        values.append(base + step * index + wobble * (((index % 3) - 1) * 0.35))
    return values
    """
        price = {
            "AAA": [100.0, 101.50, 101.00, 103.10, 104.20, 104.05, 105.95, 107.60, 107.25, 109.80],
            "BBB": [82.0, 81.10, 80.85, 80.10, 79.65, 79.25, 78.80, 78.05, 77.65, 77.10],
            "CCC": [120.0, 120.65, 121.30, 121.15, 121.80, 122.55, 122.70, 123.20, 123.65, 124.30],
        }
        high = {
            symbol: _series(base, step, 0.0)
            for symbol, base, step in [
                ("AAA", 101.25, 1.5),
                ("BBB", 83.25, -0.9),
                ("CCC", 121.25, 0.65),
            ]
        }
        low = {
            symbol: _series(base, step, 0.0)
            for symbol, base, step in [
                ("AAA", 98.90, 1.5),
                ("BBB", 80.90, -0.9),
                ("CCC", 118.90, 0.65),
            ]
        }
        volume = {
            "AAA": [1000.0, 1090.0, 1180.0, 1270.0, 1360.0, 1450.0, 1540.0, 1630.0, 1720.0, 1810.0],
            "BBB": [1500.0, 1460.0, 1420.0, 1380.0, 1340.0, 1300.0, 1260.0, 1220.0, 1180.0, 1140.0],
            "CCC": [1200.0, 1255.0, 1310.0, 1365.0, 1420.0, 1475.0, 1530.0, 1585.0, 1640.0, 1695.0],
        }
        adj_factor = {
            "AAA": _series(1.000, 0.0015, 0.0),
            "BBB": _series(1.012, -0.0007, 0.0),
            "CCC": _series(0.995, 0.0009, 0.0),
        }
        turnover_rate = {
            symbol: [round(value / 1000.0, 6) for value in values]
            for symbol, values in volume.items()
        }
        amplitude = {
            symbol: [round((high_value - low_value) / max(1e-12, close_value) * 100.0, 6)
                     for high_value, low_value, close_value in zip(high[symbol], low[symbol], price[symbol])]
            for symbol in SYMBOLS
        }

        benchmark_price = _series(3000.0, 2.2, 0.0)

        cross_section = {
            DATES[-1]: {
                "market_cap": {"AAA": 1308.0, "BBB": 990.0, "CCC": 1926.0},
                "circulating_market_cap": {"AAA": 1040.0, "BBB": 901.0, "CCC": 1608.0},
                "total_assets": {"AAA": 2662.0, "BBB": 1944.0, "CCC": 3380.0},
                "pb_ratio": {"AAA": 1.89, "BBB": 1.472, "CCC": 2.308},
                "pe_ratio": {"AAA": 16.54, "BBB": 12.36, "CCC": 20.72},
                "dividend_yield": {"AAA": 0.0316, "BBB": 0.0267, "CCC": 0.0395},
                "operating_cash_flow": {"AAA": 225.0, "BBB": 186.0, "CCC": 274.0},
                "roe": {"AAA": 0.0935, "BBB": 0.1008, "CCC": 0.1162},
                "roa": {"AAA": 0.0481, "BBB": 0.0572, "CCC": 0.0690},
                "profit_margin": {"AAA": 0.1608, "BBB": 0.1699, "CCC": 0.1926},
                "net_profit": {"AAA": 136.0, "BBB": 165.0, "CCC": 194.0},
                "equity": {"AAA": 1008.0, "BBB": 1136.0, "CCC": 1264.0},
                "payout_ratio": {"AAA": 0.35, "BBB": 0.42, "CCC": 0.30},
                "dividend_stability": {"AAA": 0.72, "BBB": 0.68, "CCC": 0.76},
                "industry_metric": {"AAA": 0.60, "BBB": 0.42, "CCC": 0.55},
                "macro_metric": {"AAA": 0.0, "BBB": 0.0, "CCC": 0.0},
                "sentiment_metric": {"AAA": 0.22, "BBB": 0.10, "CCC": 0.18},
                "custom_a": {"AAA": 1.0, "BBB": 2.0, "CCC": 3.0},
                "custom_b": {"AAA": 4.0, "BBB": 5.0, "CCC": 6.0},
            }
        }

        financial_series: Dict[str, Dict[str, List[float]]] = {
            "AAA": {
                "total_revenue": _series(1000.0, 18.0, 2.0),
                "net_profit": _series(100.0, 4.0, 1.2),
                "roe": _series(0.08, 0.0015, 0.0004),
                "roa": _series(0.04, 0.0009, 0.0003),
                "profit_margin": _series(0.15, 0.0012, 0.0002),
                "eps": _series(1.0, 0.05, 0.01),
                "operating_cash_flow": _series(180.0, 5.0, 1.0),
                "equity": _series(900.0, 12.0, 1.0),
                "dividend_yield": _series(0.028, 0.0004, 0.0002),
            },
            "BBB": {
                "total_revenue": _series(1120.0, 21.0, 2.0),
                "net_profit": _series(120.0, 5.0, 1.2),
                "roe": _series(0.09, 0.0019, 0.0004),
                "roa": _series(0.046, 0.0012, 0.0003),
                "profit_margin": _series(0.16, 0.0014, 0.0002),
                "eps": _series(1.12, 0.06, 0.01),
                "operating_cash_flow": _series(205.0, 6.0, 1.0),
                "equity": _series(1010.0, 14.0, 1.0),
                "dividend_yield": _series(0.030, 0.0005, 0.0002),
            },
            "CCC": {
                "total_revenue": _series(1240.0, 24.0, 2.0),
                "net_profit": _series(140.0, 6.0, 1.2),
                "roe": _series(0.10, 0.0023, 0.0004),
                "roa": _series(0.052, 0.0015, 0.0003),
                "profit_margin": _series(0.18, 0.0016, 0.0002),
                "eps": _series(1.24, 0.07, 0.01),
                "operating_cash_flow": _series(230.0, 7.0, 1.0),
                "equity": _series(1120.0, 16.0, 1.0),
                "dividend_yield": _series(0.032, 0.0006, 0.0002),
            },
        }

        macro_series = {
            "industrial_added_value_yoy": _series(5.8, 0.1, 0.05),
            "cpi_yoy": _series(2.3, 0.03, 0.02),
            "m2_yoy": _series(8.0, 0.04, 0.02),
        }
        sentiment_series = {
            "sentiment_score": _series(0.12, 0.01, 0.01),
        }
        industry_series = {
            "industry_metric": _series(0.5, 0.015, 0.01),
        }
                            0.0015 if symbol == "AAA" else (0.0019 if symbol == "BBB" else 0.0023),
                            0.0004),
            "roa": _series(0.04 if symbol == "AAA" else (0.046 if symbol == "BBB" else 0.052),
                            0.0009 if symbol == "AAA" else (0.0012 if symbol == "BBB" else 0.0015),
                            0.0003),
            "profit_margin": _series(0.15 if symbol == "AAA" else (0.16 if symbol == "BBB" else 0.18),
                                      0.0012 if symbol == "AAA" else (0.0014 if symbol == "BBB" else 0.0016),
                                      0.0002),
            "eps": _series(1.0 if symbol == "AAA" else (1.12 if symbol == "BBB" else 1.24),
                            0.05 if symbol == "AAA" else (0.06 if symbol == "BBB" else 0.07),
                            0.01),
            "operating_cash_flow": _series(180.0 if symbol == "AAA" else (205.0 if symbol == "BBB" else 230.0),
                                            5.0 if symbol == "AAA" else (6.0 if symbol == "BBB" else 7.0),
                                            1.0),
            "equity": _series(900.0 if symbol == "AAA" else (1010.0 if symbol == "BBB" else 1120.0),
                               12.0 if symbol == "AAA" else (14.0 if symbol == "BBB" else 16.0),
                               1.0),
            "dividend_yield": _series(0.028 if symbol == "AAA" else (0.030 if symbol == "BBB" else 0.032),
                                       0.0004 if symbol == "AAA" else (0.0005 if symbol == "BBB" else 0.0006),
                                       0.0002),
        }

    macro_series = {
        "industrial_added_value_yoy": _series(5.8, 0.1, 0.05),
        "cpi_yoy": _series(2.3, 0.03, 0.02),
        "m2_yoy": _series(8.0, 0.04, 0.02),
    }
    sentiment_series = {
        "sentiment_score": _series(0.12, 0.01, 0.01),
    }
    industry_series = {
        "industry_metric": _series(0.5, 0.015, 0.01),
    }

    return Scenario(
        dates=DATES,
        symbols=SYMBOLS,
        benchmark=BENCHMARK,
        price=price,
        high=high,
        low=low,
        volume=volume,
        turnover_rate=turnover_rate,
        amplitude=amplitude,
        adj_factor=adj_factor,
        benchmark_price=benchmark_price,
        cross_section=cross_section,
        financial_series=financial_series,
        macro_series=macro_series,
        sentiment_series=sentiment_series,
        industry_series=industry_series,
    )


def _safe_mean(values: Sequence[float]) -> float:
    finite = [value for value in values if math.isfinite(value)]
    return sum(finite) / len(finite) if finite else float("nan")


def _safe_std(values: Sequence[float]) -> float:
    finite = [value for value in values if math.isfinite(value)]
    if len(finite) < 2:
        return float("nan")
    mean = _safe_mean(finite)
    variance = sum((value - mean) ** 2 for value in finite) / len(finite)
    return math.sqrt(max(variance, 0.0))


def _percentile_ranks(values: Dict[str, float]) -> Dict[str, float]:
    ranked = sorted(values.items(), key=lambda item: item[1])
    if not ranked:
        return {}
    if len(ranked) == 1:
        return {ranked[0][0]: 0.0}
    return {symbol: index / len(ranked) for index, (symbol, _) in enumerate(ranked)}


def _zscore(values: Dict[str, float]) -> Dict[str, float]:
    finite = [value for value in values.values() if math.isfinite(value)]
    if not finite:
        return values
    mean = sum(finite) / len(finite)
    variance = sum((value - mean) ** 2 for value in finite) / len(finite)
    stdev = math.sqrt(max(variance, 0.0))
    if stdev <= 1e-12:
        return {symbol: 0.0 for symbol in values}
    return {symbol: (value - mean) / stdev for symbol, value in values.items()}


def _minmax(values: Dict[str, float]) -> Dict[str, float]:
    finite = [value for value in values.values() if math.isfinite(value)]
    if not finite:
        return values
    min_value = min(finite)
    max_value = max(finite)
    span = max_value - min_value
    if span <= 1e-12:
        return {symbol: 0.0 for symbol in values}
    return {symbol: (value - min_value) / span for symbol, value in values.items()}


def _last_finite(values: Sequence[float]) -> float:
    finite = [value for value in values if math.isfinite(value)]
    return finite[-1] if finite else float("nan")


def _close_adjusted_series(scenario: Scenario, symbol: str) -> List[float]:
    return [close * factor for close, factor in zip(scenario.price[symbol], scenario.adj_factor[symbol])]


def _clamp(value: float, lower: float = -1.0, upper: float = 1.0) -> float:
    return max(lower, min(upper, value))


def _as_float_array(values: Sequence[float]):
    if np is None:
        return values
    return np.asarray(values, dtype=float)

        values.append(base + step * index + wobble * ((index % 3) - 1))
    return values


"""
def build_scenario() -> Scenario:
    price = {
        "AAA": _series(100.0, 1.5, 0.0),
        "BBB": _series(82.0, -0.9, 0.0),
        "CCC": _series(120.0, 0.65, 0.0),
    }
    high = {
        "AAA": _series(101.25, 1.5, 0.0),
        "BBB": _series(83.25, -0.9, 0.0),
        "CCC": _series(121.25, 0.65, 0.0),
    }
    low = {
        "AAA": _series(98.90, 1.5, 0.0),
        "BBB": _series(80.90, -0.9, 0.0),
        "CCC": _series(118.90, 0.65, 0.0),
    }
    volume = {
        "AAA": [1000.0, 1090.0, 1180.0, 1270.0, 1360.0, 1450.0, 1540.0, 1630.0, 1720.0, 1810.0],
        "BBB": [1500.0, 1460.0, 1420.0, 1380.0, 1340.0, 1300.0, 1260.0, 1220.0, 1180.0, 1140.0],
        "CCC": [1200.0, 1255.0, 1310.0, 1365.0, 1420.0, 1475.0, 1530.0, 1585.0, 1640.0, 1695.0],
    }
    adj_factor = {
        "AAA": _series(1.000, 0.0015, 0.0),
        "BBB": _series(1.012, -0.0007, 0.0),
        "CCC": _series(0.995, 0.0009, 0.0),
    }
    turnover_rate = {
        symbol: [round(value / 1000.0, 6) for value in values]
        for symbol, values in volume.items()
    }
    amplitude = {
        symbol: [round((high_value - low_value) / max(1e-12, close_value) * 100.0, 6)
                 for high_value, low_value, close_value in zip(high[symbol], low[symbol], price[symbol])]
        for symbol in SYMBOLS
    }

    benchmark_price = _series(3000.0, 2.2, 0.0)

    cross_section = {
        DATES[-1]: {
            "market_cap": {"AAA": _series(1200.0, 12.0, 0.0)[-1], "BBB": _series(900.0, 10.0, 0.0)[-1], "CCC": _series(1800.0, 14.0, 0.0)[-1]},
            "circulating_market_cap": {"AAA": _series(950.0, 10.0, 0.0)[-1], "BBB": _series(820.0, 9.0, 0.0)[-1], "CCC": _series(1500.0, 12.0, 0.0)[-1]},
            "total_assets": {"AAA": _series(2500.0, 18.0, 0.0)[-1], "BBB": _series(1800.0, 16.0, 0.0)[-1], "CCC": _series(3200.0, 20.0, 0.0)[-1]},
            "pb_ratio": {"AAA": _series(1.8, 0.01, 0.0)[-1], "BBB": _series(1.4, 0.008, 0.0)[-1], "CCC": _series(2.2, 0.012, 0.0)[-1]},
            "pe_ratio": {"AAA": _series(16.0, 0.06, 0.0)[-1], "BBB": _series(12.0, 0.04, 0.0)[-1], "CCC": _series(20.0, 0.08, 0.0)[-1]},
            "dividend_yield": {"AAA": _series(0.028, 0.0004, 0.0)[-1], "BBB": _series(0.024, 0.0003, 0.0)[-1], "CCC": _series(0.035, 0.0005, 0.0)[-1]},
            "operating_cash_flow": {"AAA": _series(180.0, 5.0, 0.0)[-1], "BBB": _series(150.0, 4.0, 0.0)[-1], "CCC": _series(220.0, 6.0, 0.0)[-1]},
            "roe": {"AAA": _series(0.08, 0.0015, 0.0)[-1], "BBB": _series(0.09, 0.0012, 0.0)[-1], "CCC": _series(0.10, 0.0018, 0.0)[-1]},
            "roa": {"AAA": _series(0.04, 0.0009, 0.0)[-1], "BBB": _series(0.05, 0.0008, 0.0)[-1], "CCC": _series(0.06, 0.0010, 0.0)[-1]},
            "profit_margin": {"AAA": _series(0.15, 0.0012, 0.0)[-1], "BBB": _series(0.16, 0.0011, 0.0)[-1], "CCC": _series(0.18, 0.0014, 0.0)[-1]},
            "net_profit": {"AAA": _series(100.0, 4.0, 0.0)[-1], "BBB": _series(120.0, 4.5, 0.0)[-1], "CCC": _series(140.0, 5.0, 0.0)[-1]},
            "equity": {"AAA": _series(900.0, 12.0, 0.0)[-1], "BBB": _series(1000.0, 14.0, 0.0)[-1], "CCC": _series(1100.0, 16.0, 0.0)[-1]},
            "payout_ratio": {"AAA": 0.35, "BBB": 0.42, "CCC": 0.30},
            "dividend_stability": {"AAA": 0.72, "BBB": 0.68, "CCC": 0.76},
            "industry_metric": {"AAA": 0.60, "BBB": 0.42, "CCC": 0.55},
            "macro_metric": {"AAA": 0.0, "BBB": 0.0, "CCC": 0.0},
            "sentiment_metric": {"AAA": 0.22, "BBB": 0.10, "CCC": 0.18},
            "custom_a": {"AAA": 1.0, "BBB": 2.0, "CCC": 3.0},
            "custom_b": {"AAA": 4.0, "BBB": 5.0, "CCC": 6.0},
        }
    }

    financial_series: Dict[str, Dict[str, List[float]]] = {
        "AAA": {
            "total_revenue": _series(1000.0, 18.0, 0.0),
            "net_profit": _series(100.0, 4.0, 0.0),
            "roe": _series(0.08, 0.0015, 0.0),
            "roa": _series(0.04, 0.0009, 0.0),
            "profit_margin": _series(0.15, 0.0012, 0.0),
            "eps": _series(1.0, 0.05, 0.0),
            "operating_cash_flow": _series(180.0, 5.0, 0.0),
            "equity": _series(900.0, 12.0, 0.0),
            "dividend_yield": _series(0.028, 0.0004, 0.0),
        },
        "BBB": {
            "total_revenue": _series(1120.0, 21.0, 0.0),
            "net_profit": _series(120.0, 4.5, 0.0),
            "roe": _series(0.09, 0.0012, 0.0),
            "roa": _series(0.05, 0.0008, 0.0),
            "profit_margin": _series(0.16, 0.0011, 0.0),
            "eps": _series(1.12, 0.06, 0.0),
            "operating_cash_flow": _series(150.0, 4.0, 0.0),
            "equity": _series(1000.0, 14.0, 0.0),
            "dividend_yield": _series(0.024, 0.0003, 0.0),
        },
        "CCC": {
            "total_revenue": _series(1240.0, 24.0, 0.0),
            "net_profit": _series(140.0, 5.0, 0.0),
            "roe": _series(0.10, 0.0018, 0.0),
            "roa": _series(0.06, 0.0010, 0.0),
            "profit_margin": _series(0.18, 0.0014, 0.0),
            "eps": _series(1.24, 0.07, 0.0),
            "operating_cash_flow": _series(220.0, 6.0, 0.0),
            "equity": _series(1100.0, 16.0, 0.0),
            "dividend_yield": _series(0.035, 0.0005, 0.0),
        },
    }

    macro_series = {
        "industrial_added_value_yoy": _series(5.8, 0.1, 0.05),
        "cpi_yoy": _series(2.3, 0.03, 0.02),
        "m2_yoy": _series(8.0, 0.04, 0.02),
    }
    sentiment_series = {
        "sentiment_score": _series(0.12, 0.01, 0.01),
    }
    industry_series = {
        "industry_metric": _series(0.5, 0.015, 0.01),
    }

    return Scenario(
        dates=DATES,
        symbols=SYMBOLS,
        benchmark=BENCHMARK,
        price=price,
        high=high,
        low=low,
        volume=volume,
        turnover_rate=turnover_rate,
        amplitude=amplitude,
        adj_factor=adj_factor,
        benchmark_price=benchmark_price,
        cross_section=cross_section,
        financial_series=financial_series,
        macro_series=macro_series,
        sentiment_series=sentiment_series,
        industry_series=industry_series,
    )


    mean = _safe_mean(window)
    return max(-1.0, min(1.0, math.tanh((volumes[-1] - mean) / max(1e-6, abs(mean)))))


def _safe_mean(values: Sequence[float]) -> float:
    finite = [value for value in values if math.isfinite(value)]
    return sum(finite) / len(finite) if finite else float("nan")


def _safe_std(values: Sequence[float]) -> float:
    finite = [value for value in values if math.isfinite(value)]
    if len(finite) < 2:
        return float("nan")
    mean = _safe_mean(finite)
    variance = sum((value - mean) ** 2 for value in finite) / len(finite)
    return math.sqrt(max(variance, 0.0))


def _percentile_ranks(values: Dict[str, float]) -> Dict[str, float]:
    ranked = sorted(values.items(), key=lambda item: item[1])
    if not ranked:
        return {}
    if len(ranked) == 1:
        return {ranked[0][0]: 0.0}
    return {symbol: index / len(ranked) for index, (symbol, _) in enumerate(ranked)}


def _zscore(values: Dict[str, float]) -> Dict[str, float]:
    finite = [value for value in values.values() if math.isfinite(value)]
    if not finite:
        return values
    mean = sum(finite) / len(finite)
    variance = sum((value - mean) ** 2 for value in finite) / len(finite)
    stdev = math.sqrt(max(variance, 0.0))
    if stdev <= 1e-12:
        return {symbol: 0.0 for symbol in values}
    return {symbol: (value - mean) / stdev for symbol, value in values.items()}


def _minmax(values: Dict[str, float]) -> Dict[str, float]:
    finite = [value for value in values.values() if math.isfinite(value)]
    if not finite:
        return values
    min_value = min(finite)
    max_value = max(finite)
    span = max_value - min_value
    if span <= 1e-12:
        return {symbol: 0.0 for symbol in values}
    return {symbol: (value - min_value) / span for symbol, value in values.items()}


def _last_finite(values: Sequence[float]) -> float:
    finite = [value for value in values if math.isfinite(value)]
    return finite[-1] if finite else float("nan")


def _close_adjusted_series(scenario: Scenario, symbol: str) -> List[float]:
    return [close * factor for close, factor in zip(scenario.price[symbol], scenario.adj_factor[symbol])]


def _ema_series(values: Sequence[float], period: int) -> List[float]:
    if not values:
        return []
    resolved = max(2, period)
    alpha = 2.0 / (resolved + 1.0)
    beta = 1.0 - alpha
    ema = [float(values[0])]
    for value in values[1:]:
        ema.append(alpha * value + beta * ema[-1])
    return ema


def _simple_momentum_series(scenario: Scenario, symbol: str, window: int, skip_recent: int) -> float:
    adjusted = _close_adjusted_series(scenario, symbol)
    if len(adjusted) < window + skip_recent + 1:
        return float("nan")
    anchor_index = len(adjusted) - skip_recent - 1
    previous_index = anchor_index - window
    if previous_index < 0:
        return float("nan")
    base = adjusted[previous_index]
    if base <= 0.0 or not math.isfinite(base):
        return float("nan")
    return (adjusted[anchor_index] - base) / base


def _ta_or_formula_rsi(closes: Sequence[float], period: int, use_talib: bool = True) -> float:
    resolved = max(2, period)
    if use_talib and talib is not None and np is not None:
        rsi_values = talib.RSI(_as_float_array(closes), timeperiod=resolved)
        rsi_value = _last_finite(rsi_values)
        return _clamp((rsi_value - 50.0) / 50.0) if math.isfinite(rsi_value) else float("nan")
    if len(closes) < 2:
        return float("nan")
    window = min(resolved, len(closes) - 1)
    gains: List[float] = []
    losses: List[float] = []
    start = len(closes) - window
    for index in range(start, len(closes)):
        delta = closes[index] - closes[index - 1]
        if delta > 0.0:
            gains.append(delta)
            losses.append(0.0)
        else:
            gains.append(0.0)
            losses.append(-delta)
    avg_gain = _safe_mean(gains)
    avg_loss = _safe_mean(losses)
    if not math.isfinite(avg_gain) or not math.isfinite(avg_loss):
        return float("nan")
    rsi = 100.0 if avg_loss <= 1e-12 else 100.0 - (100.0 / (1.0 + avg_gain / avg_loss))
    return max(-1.0, min(1.0, (rsi - 50.0) / 50.0))


def _ta_or_formula_macd(closes: Sequence[float], fast: int, slow: int, signal: int, use_talib: bool = True) -> float:
    resolved_fast = max(2, fast)
    resolved_slow = max(resolved_fast + 1, slow)
    resolved_signal = max(2, signal)
    if use_talib and talib is not None and np is not None:
        _, _, hist_values = talib.MACD(
            _as_float_array(closes),
            fastperiod=resolved_fast,
            slowperiod=resolved_slow,
            signalperiod=resolved_signal,
        )
        histogram = _last_finite(hist_values)
        if math.isfinite(histogram):
            scale = max(1e-6, abs(closes[-1]))
            return _clamp(math.tanh(histogram / scale))
        return float("nan")
    if len(closes) < 2:
        return float("nan")
    fast_ema = _ema_series(closes, resolved_fast)
    slow_ema = _ema_series(closes, resolved_slow)
    macd_line = [fast_value - slow_value for fast_value, slow_value in zip(fast_ema, slow_ema)]
    signal_line = _ema_series(macd_line, resolved_signal)
    histogram = macd_line[-1] - signal_line[-1]
    scale = max(1e-6, abs(closes[-1]))
    return max(-1.0, min(1.0, math.tanh(histogram / scale)))


def _ta_or_formula_bbands(closes: Sequence[float], period: int, std_multiplier: float, use_talib: bool = True) -> float:
    resolved = max(2, period)
    if use_talib and talib is not None and np is not None:
        upper_values, middle_values, _ = talib.BBANDS(
            _as_float_array(closes),
            timeperiod=resolved,
            nbdevup=std_multiplier,
            nbdevdn=std_multiplier,
            matype=0,
        )
        middle = _last_finite(middle_values)
        upper = _last_finite(upper_values)
        return _clamp(math.tanh((closes[-1] - middle) / max(1e-6, abs(upper - middle)))) if math.isfinite(middle) and math.isfinite(upper) else float("nan")
    if len(closes) < 2:
        return float("nan")
    window = list(closes[-resolved:])
    mean = _safe_mean(window)
    stddev = _safe_std(window)
    scale = max(1e-6, stddev * max(1.0, std_multiplier))
    return max(-1.0, min(1.0, math.tanh((closes[-1] - mean) / scale)))


def _ta_or_formula_atr(highs: Sequence[float], lows: Sequence[float], closes: Sequence[float], period: int, use_talib: bool = True) -> float:
    resolved = max(2, period)
    if use_talib and talib is not None and np is not None:
        atr_values = talib.ATR(
            _as_float_array(highs),
            _as_float_array(lows),
            _as_float_array(closes),
            timeperiod=resolved,
        )
        atr_value = _last_finite(atr_values)
        return _clamp(-atr_value / max(1e-6, abs(closes[-1]))) if math.isfinite(atr_value) else float("nan")
    if len(closes) < 2:
        return float("nan")
    window = min(resolved + 1, len(closes))
    tail_highs = list(highs[-window:])
    tail_lows = list(lows[-window:])
    tail_closes = list(closes[-window:])
    true_ranges: List[float] = []
    for index in range(1, len(tail_closes)):
        true_ranges.append(
            max(
                tail_highs[index] - tail_lows[index],
                abs(tail_highs[index] - tail_closes[index - 1]),
                abs(tail_lows[index] - tail_closes[index - 1]),
            )
        )
    atr = _safe_mean(true_ranges)
    return max(-1.0, min(1.0, -atr / max(1e-6, abs(tail_closes[-1]))))


def _ta_or_formula_obv(closes: Sequence[float], volumes: Sequence[float], period: int, use_talib: bool = True) -> float:
    resolved = max(2, period)
    if use_talib and talib is not None and np is not None:
        obv_values = talib.OBV(_as_float_array(closes), _as_float_array(volumes))
        obv_value = _last_finite(obv_values)
        if math.isfinite(obv_value):
            tail_length = min(resolved + 1, len(closes), len(volumes))
            average_volume = _safe_mean(volumes[-tail_length:])
            return _clamp(math.tanh(obv_value / max(1e-6, average_volume * tail_length)))
        return float("nan")
    if len(closes) < 2 or len(volumes) < 2:
        return float("nan")
    tail_length = min(resolved + 1, len(closes), len(volumes))
    tail_closes = list(closes[-tail_length:])
    tail_volumes = list(volumes[-tail_length:])
    obv = 0.0
    for index in range(1, len(tail_closes)):
        if tail_closes[index] > tail_closes[index - 1]:
            obv += tail_volumes[index]
        elif tail_closes[index] < tail_closes[index - 1]:
            obv -= tail_volumes[index]
    average_volume = _safe_mean(tail_volumes)
    return max(-1.0, min(1.0, math.tanh(obv / max(1e-6, average_volume * len(tail_volumes)))))


def _vwap_value(closes: Sequence[float], volumes: Sequence[float]) -> float:
    weighted_sum = 0.0
    volume_sum = 0.0
    for close, volume in zip(closes, volumes):
        if volume <= 0.0:
            continue
        weighted_sum += close * volume
        volume_sum += volume
    if volume_sum <= 1e-12:
        return float("nan")
    return weighted_sum / volume_sum


def _volume_ratio_value(volumes: Sequence[float], period: int) -> float:
    resolved = max(2, period)
    window = list(volumes[-resolved:])
    mean = _safe_mean(window)
    return max(-1.0, min(1.0, math.tanh((volumes[-1] - mean) / max(1e-6, abs(mean)))))


def _turnover_stability_value(values: Sequence[float]) -> float:
    mean = _safe_mean(values)
    stdev = _safe_std(values)
    if not math.isfinite(mean) or not math.isfinite(stdev):
        return float("nan")
    cv = stdev / max(1e-6, abs(mean))
    normalized = 1.0 - min(max(cv, 0.0), 2.0) / 2.0
    return max(-1.0, min(1.0, normalized * 2.0 - 1.0))


def _momentum_values(scenario: Scenario, window: int, skip_recent: int) -> Dict[str, float]:
    return {symbol: _simple_momentum_series(scenario, symbol, window, skip_recent) for symbol in scenario.symbols}


def _rank_values(values: Dict[str, float]) -> Dict[str, float]:
    return _percentile_ranks(values)


def _normalized_values(values: Dict[str, float]) -> Dict[str, float]:
    return _zscore(values)


def _exponential_values(values: Dict[str, float], window: int) -> Dict[str, float]:
    scaled = {symbol: value * (1.0 + 1.0 / max(1, window)) for symbol, value in values.items()}
    return _zscore(scaled)


def _cross_section_values(scenario: Scenario, field: str) -> Dict[str, float]:
    return dict(scenario.cross_section[DATES[-1]][field])


def _single_value_from_series(series_map: Dict[str, List[float]], transform) -> Dict[str, float]:
    return {symbol: transform(values) for symbol, values in series_map.items()}


def _use_talib_for_technical(mode: str) -> bool:
    if mode == "formula":
        return False
    return talib is not None and np is not None

def _clamp(value: float, lower: float = -1.0, upper: float = 1.0) -> float:
    return max(lower, min(upper, value))

def _as_float_array(values: Sequence[float]):
    if np is None:
        return values
    return np.asarray(values, dtype=float)


def calc_python_indicator(scenario: Scenario, factor_type: str, indicator: str, technical_mode: str) -> Dict[str, float]:
    if factor_type == "technical":
        use_talib = _use_talib_for_technical(technical_mode)
        if indicator == "rsi":
            return {symbol: _ta_or_formula_rsi(scenario.price[symbol], 5, use_talib=use_talib) for symbol in scenario.symbols}
        if indicator == "macd":
            return {symbol: _ta_or_formula_macd(scenario.price[symbol], 3, 5, 2, use_talib=use_talib) for symbol in scenario.symbols}
        if indicator == "boll":
            return {symbol: _ta_or_formula_bbands(scenario.price[symbol], 5, 2.0, use_talib=use_talib) for symbol in scenario.symbols}
        if indicator == "atr":
            return {symbol: _ta_or_formula_atr(scenario.high[symbol], scenario.low[symbol], scenario.price[symbol], 5, use_talib=use_talib) for symbol in scenario.symbols}
        if indicator == "obv":
            return {symbol: _ta_or_formula_obv(scenario.price[symbol], scenario.volume[symbol], 5, use_talib=use_talib) for symbol in scenario.symbols}
        if indicator == "vwap":
            return {
                symbol: max(-1.0, min(1.0, math.tanh((scenario.price[symbol][-1] - _vwap_value(scenario.price[symbol], scenario.volume[symbol])) / max(1e-6, abs(_vwap_value(scenario.price[symbol], scenario.volume[symbol]))))))
                for symbol in scenario.symbols
            }
        if indicator == "volume_ratio":
            return {symbol: _volume_ratio_value(scenario.volume[symbol], 5) for symbol in scenario.symbols}
        if indicator == "turnover_stability":
            return {symbol: _turnover_stability_value(scenario.turnover_rate[symbol][-5:]) for symbol in scenario.symbols}
        if indicator == "ma":
            results: Dict[str, float] = {}
            for symbol in scenario.symbols:
                closes = scenario.price[symbol]
                resolved = 5
                if use_talib and talib is not None and np is not None:
                    ma_values = talib.MA(_as_float_array(closes), timeperiod=resolved, matype=0)
                    ma_value = _last_finite(ma_values)
                    results[symbol] = _clamp(math.tanh((closes[-1] - ma_value) / max(1e-6, abs(ma_value)))) if math.isfinite(ma_value) else float("nan")
                    continue
                ma_value = _safe_mean(closes[-resolved:])
                results[symbol] = _clamp(math.tanh((closes[-1] - ma_value) / max(1e-6, abs(ma_value))))
            return results
        if indicator == "ema":
            results: Dict[str, float] = {}
            for symbol in scenario.symbols:
                closes = scenario.price[symbol]
                resolved = 5
                if use_talib and talib is not None and np is not None:
                    ema_values = talib.EMA(_as_float_array(closes), timeperiod=resolved)
                    ema_value = _last_finite(ema_values)
                    results[symbol] = _clamp(math.tanh((closes[-1] - ema_value) / max(1e-6, abs(ema_value)))) if math.isfinite(ema_value) else float("nan")
                    continue
                ema_value = _ema_series(closes, resolved)[-1]
                results[symbol] = _clamp(math.tanh((closes[-1] - ema_value) / max(1e-6, abs(ema_value))))
            return results
        if indicator == "kdj":
            results: Dict[str, float] = {}
            for symbol in scenario.symbols:
                highs = scenario.high[symbol]
                lows = scenario.low[symbol]
                closes = scenario.price[symbol]
                if use_talib and talib is not None and np is not None:
                    slowk_values, slowd_values = talib.STOCH(
                        _as_float_array(highs),
                        _as_float_array(lows),
                        _as_float_array(closes),
                        fastk_period=5,
                        slowk_period=3,
                        slowk_matype=0,
                        slowd_period=3,
                        slowd_matype=0,
                    )
                    slowk = _last_finite(slowk_values)
                    slowd = _last_finite(slowd_values)
                    if math.isfinite(slowk) and math.isfinite(slowd):
                        j_value = 3.0 * slowk - 2.0 * slowd
                        results[symbol] = _clamp((j_value - 50.0) / 50.0)
                        continue
                    results[symbol] = float("nan")
                    continue
                window_high = max(highs[-5:])
                window_low = min(lows[-5:])
                rsv = 100.0 * (closes[-1] - window_low) / max(1e-12, window_high - window_low)
                k_value = 50.0 + (rsv - 50.0) / 3.0
                d_value = 50.0 + (k_value - 50.0) / 3.0
                j_value = 3.0 * k_value - 2.0 * d_value
                results[symbol] = max(-1.0, min(1.0, (j_value - 50.0) / 50.0))
            return results
        return {}

    if factor_type == "momentum":
        raw = _momentum_values(scenario, 3, 1)
        if indicator == "simple":
            return raw
        if indicator == "rank":
            return _rank_values(raw)
        if indicator == "normalized":
            return _normalized_values(raw)
        if indicator == "exponential":
            return _exponential_values(raw, 3)
        return {}

    if factor_type == "value":
        cross = _cross_section_values(scenario, "market_cap")
        if indicator == "bp":
            return {symbol: 1.0 / scenario.cross_section[DATES[-1]]["pb_ratio"][symbol] for symbol in scenario.symbols}
        if indicator == "ep":
            return {symbol: 1.0 / scenario.cross_section[DATES[-1]]["pe_ratio"][symbol] for symbol in scenario.symbols}
        if indicator == "dividend_yield":
            return dict(scenario.cross_section[DATES[-1]]["dividend_yield"])
        if indicator == "cf_p":
            return {
                symbol: scenario.cross_section[DATES[-1]]["operating_cash_flow"][symbol] / cross[symbol]
                for symbol in scenario.symbols
            }
        return {}

    if factor_type == "quality":
        if indicator == "roe":
            return dict(scenario.cross_section[DATES[-1]]["roe"])
        if indicator == "roa":
            return dict(scenario.cross_section[DATES[-1]]["roa"])
        if indicator in {"gross_margin", "operating_margin"}:
            return dict(scenario.cross_section[DATES[-1]]["profit_margin"])
        if indicator == "earnings_quality":
            return {
                symbol: scenario.cross_section[DATES[-1]]["net_profit"][symbol] / scenario.cross_section[DATES[-1]]["equity"][symbol]
                for symbol in scenario.symbols
            }
        return {}

    if factor_type == "size":
        field = {
            "market_cap": "market_cap",
            "circulating_market_cap": "circulating_market_cap",
            "total_assets": "total_assets",
        }.get(indicator, indicator)
        raw = dict(scenario.cross_section[DATES[-1]][field])
        return {symbol: -math.log(max(1e-12, value)) for symbol, value in raw.items()}

    if factor_type == "lowvol":
        if indicator == "volatility":
            raw = {
                symbol: _safe_std([
                    (scenario.price[symbol][index] - scenario.price[symbol][index - 1]) / scenario.price[symbol][index - 1]
                    for index in range(len(scenario.price[symbol]) - 4, len(scenario.price[symbol]))
                ])
                for symbol in scenario.symbols
            }
        elif indicator == "drawdown":
            raw = {}
            for symbol in scenario.symbols:
                closes = scenario.price[symbol][-5:]
                peak = 0.0
                max_drawdown = 0.0
                for value in closes:
                    peak = max(peak, value)
                    max_drawdown = max(max_drawdown, (peak - value) / max(1e-12, peak))
                raw[symbol] = max_drawdown
        elif indicator == "beta":
            benchmark_tail = scenario.benchmark_price[-5:]
            benchmark_returns = [
                (benchmark_tail[index] - benchmark_tail[index - 1]) / benchmark_tail[index - 1]
                for index in range(1, len(benchmark_tail))
            ]
            raw = {}
            for symbol in scenario.symbols:
                close_tail = scenario.price[symbol][-5:]
                returns = [
                    (close_tail[index] - close_tail[index - 1]) / close_tail[index - 1]
                    for index in range(1, len(close_tail))
                ]
                mean_x = _safe_mean(returns)
                mean_y = _safe_mean(benchmark_returns)
                covariance = sum((x - mean_x) * (y - mean_y) for x, y in zip(returns, benchmark_returns)) / len(benchmark_returns)
                variance = sum((value - mean_y) ** 2 for value in benchmark_returns) / len(benchmark_returns)
                raw[symbol] = covariance / variance if variance > 0.0 else float("nan")
        else:
            return {}

        finite = [value for value in raw.values() if math.isfinite(value)]
        if not finite:
            return raw
        min_value = min(finite)
        max_value = max(finite)
        if max_value <= min_value + 1e-12:
            return {symbol: 1.0 for symbol in raw}
        return {symbol: (max_value - value) / (max_value - min_value) if math.isfinite(value) else float("nan") for symbol, value in raw.items()}

    if factor_type == "growth":
        if indicator == "revenue_growth":
            return {
                symbol: (series[0] - series[1]) / max(1e-12, abs(series[1]))
                for symbol, series in ((symbol, scenario.financial_series[symbol]["total_revenue"]) for symbol in scenario.symbols)
            }
        if indicator == "net_profit_growth":
            return {
                symbol: (series[0] - series[1]) / max(1e-12, abs(series[1]))
                for symbol, series in ((symbol, scenario.financial_series[symbol]["net_profit"]) for symbol in scenario.symbols)
            }
        if indicator == "delta_roe":
            return {
                symbol: series[0] - series[1]
                for symbol, series in ((symbol, scenario.financial_series[symbol]["roe"]) for symbol in scenario.symbols)
            }
        if indicator == "sue":
            results: Dict[str, float] = {}
            for symbol in scenario.symbols:
                eps = scenario.financial_series[symbol]["eps"][-5:]
                changes = [eps[index] - eps[index + 1] for index in range(len(eps) - 1)]
                current = changes[0]
                if len(changes) < 2:
                    results[symbol] = current
                    continue
                history = changes[1:]
                mean = _safe_mean(history)
                stdev = _safe_std(history)
                results[symbol] = (current - mean) / stdev if stdev > 1e-12 else current
            return results
        return {}

    if factor_type == "liquidity":
        if indicator == "turnover_rate":
            return {symbol: _safe_mean(scenario.turnover_rate[symbol]) for symbol in scenario.symbols}
        if indicator == "volume":
            return {symbol: _safe_mean(scenario.volume[symbol]) for symbol in scenario.symbols}
        if indicator == "amplitude":
            return {symbol: -_safe_mean(scenario.amplitude[symbol]) for symbol in scenario.symbols}
        if indicator == "amihud_illiquidity":
            results: Dict[str, float] = {}
            for symbol in scenario.symbols:
                closes = scenario.price[symbol]
                volumes = scenario.volume[symbol]
                ratios = []
                for index in range(1, len(closes)):
                    ratio = abs(closes[index] - closes[index - 1]) / max(1e-12, abs(closes[index - 1])) / max(1e-12, abs(volumes[index]))
                    ratios.append(ratio)
                results[symbol] = -_safe_mean(ratios)
            return results
        return {}

    if factor_type == "dividend":
        if indicator == "dividend_yield":
            return dict(scenario.cross_section[DATES[-1]]["dividend_yield"])
        if indicator == "payout_ratio":
            return dict(scenario.cross_section[DATES[-1]]["payout_ratio"])
        if indicator == "dividend_stability":
            return dict(scenario.cross_section[DATES[-1]]["dividend_stability"])
        return {}

    return {}


def build_jobs() -> List[Dict[str, object]]:
    jobs: List[Dict[str, object]] = []
    for indicator in ["rsi", "macd", "boll", "atr", "obv", "vwap", "volume_ratio", "turnover_stability", "ma", "ema", "kdj"]:
        jobs.append({"factor_type": "technical", "indicator": indicator, "params": {"window": 5}})
    for indicator in ["simple", "rank", "normalized", "exponential"]:
        jobs.append({"factor_type": "momentum", "indicator": indicator, "params": {"window": 3, "skipRecent": 1, "type": indicator, "adjustPriceType": "post_adjust_factor"}})
    for indicator in ["bp", "ep", "dividend_yield", "cf_p"]:
        jobs.append({"factor_type": "value", "indicator": indicator, "params": {"valuationMetrics": [indicator], "standardization": "none"}})
    for indicator in ["roe", "roa", "gross_margin", "operating_margin", "earnings_quality"]:
        jobs.append({"factor_type": "quality", "indicator": indicator, "params": {"metric": indicator, "qualityThreshold": 0.1}})
    for indicator in ["market_cap", "circulating_market_cap", "total_assets"]:
        jobs.append({"factor_type": "size", "indicator": indicator, "params": {"sizeMetric": indicator, "logTransform": True}})
    for indicator in ["volatility", "drawdown", "beta"]:
        jobs.append({"factor_type": "lowvol", "indicator": indicator, "params": {"components": [indicator], "window": 5}})
    for indicator in ["revenue_growth", "net_profit_growth", "delta_roe", "sue"]:
        jobs.append({"factor_type": "growth", "indicator": indicator, "params": {"growthMetrics": [indicator], "growthWeights": [100], "standardization": "none"}})
    for indicator in ["turnover_rate", "volume", "amplitude", "amihud_illiquidity"]:
        jobs.append({"factor_type": "liquidity", "indicator": indicator, "params": {"metric": indicator, "window": 5, "standardization": "none"}})
    for indicator in ["dividend_yield", "payout_ratio", "dividend_stability"]:
        jobs.append({"factor_type": "dividend", "indicator": indicator, "params": {"dividendMetrics": [indicator], "minDividendYield": 0.02}})
    return jobs


def completion_requirement_for_job(factor_type: str, indicator: str) -> str:
    if factor_type == "technical":
        return f"技术因子 {indicator} 必须在 TA-Lib 模式下与 C++ 结果一致，且不允许回退到旧公式后端。"
    if factor_type == "momentum":
        return f"动量因子 {indicator} 必须使用回测引擎指定的 adjustPriceType，结果需与 C++ 一致。"
    if factor_type == "value":
        return f"估值因子 {indicator} 的原始值与组合结果都必须可复现，并与 C++ 一致。"
    if factor_type == "quality":
        return f"质量因子 metric={indicator} 必须真实进入计算，阈值过滤和排序结果要生效。"
    if factor_type == "size":
        return f"规模因子 sizeMetric={indicator} 必须真实切换字段并改变结果。"
    if factor_type == "lowvol":
        return f"低波因子组件 {indicator} 必须真实进入计算，权重切换后结果要变化。"
    if factor_type == "growth":
        return f"成长因子 {indicator} 必须真实进入计算，growthWeights 变化要改变结果。"
    if factor_type == "liquidity":
        return f"流动性因子 metric={indicator} 与 window 必须真实进入计算，并与 C++ 一致。"
    if factor_type == "dividend":
        return f"红利因子 {indicator} 必须真实进入计算，minDividendYield 过滤要生效。"
    return "该项结果必须可复现，并与 C++ 一致。"


def completion_requirements_summary() -> Dict[str, str]:
    return {
        "technical": "TA-Lib 模式输出必须与 C++ 在容差内一致。",
        "momentum": "动量因子必须使用回测引擎指定的 pre_adjust_factor / post_adjust_factor，不能回退 close。",
        "value": "估值因子权重与指标选择必须真实改变结果。",
        "quality": "质量指标选择与阈值过滤必须真实进入计算。",
        "size": "规模指标选择必须真实切换字段。",
        "lowvol": "低波组件与权重必须真实进入计算。",
        "growth": "成长指标与权重必须真实进入计算。",
        "liquidity": "流动性指标与窗口必须真实进入计算。",
        "dividend": "红利指标与最低股息率过滤必须真实生效。",
    }


def rows_to_csv(path: Path, rows: List[Dict[str, object]]) -> None:
    fieldnames = ["factor_type", "indicator", "symbol", "date", "params", "completion_requirement", "cpp_value", "python_value", "abs_error", "rel_error", "pass", "note"]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def load_cpp_rows(path: Path) -> List[Dict[str, object]]:
    if not path.exists():
        return []
    payload = json.loads(path.read_text(encoding="utf-8"))
    return list(payload.get("rows", []))


def compare_rows(python_rows: List[Dict[str, object]], cpp_rows: List[Dict[str, object]], tolerance: float) -> List[Dict[str, object]]:
    cpp_index = {
        (row["factor_type"], row["indicator"], row["symbol"], row["date"]): row
        for row in cpp_rows
    }
    output: List[Dict[str, object]] = []
    for python_row in python_rows:
        key = (python_row["factor_type"], python_row["indicator"], python_row["symbol"], python_row["date"])
        cpp_row = cpp_index.get(key)
        if cpp_row is None:
            continue

        cpp_value = cpp_row.get("value")
        python_value = float(python_row["python_value"])
        cpp_float = float(cpp_value)
        abs_error = abs(cpp_float - python_value)
        rel_error = abs_error / max(1e-12, abs(python_value))
        passed = abs_error <= tolerance or rel_error <= tolerance
        note = ""
        output.append({
            **python_row,
            "cpp_value": cpp_value,
            "abs_error": abs_error,
            "rel_error": rel_error,
            "pass": passed,
            "note": note,
        })
    return output


def build_python_rows(scenario: Scenario, technical_mode: str) -> List[Dict[str, object]]:
    rows: List[Dict[str, object]] = []
    jobs = build_jobs()
    for job in jobs:
        factor_type = str(job["factor_type"])
        indicator = str(job["indicator"])
        params = job["params"]
        values = calc_python_indicator(scenario, factor_type, indicator, technical_mode)
        requirement = completion_requirement_for_job(factor_type, indicator)
        for symbol, value in values.items():
            rows.append({
                "factor_type": factor_type,
                "indicator": indicator,
                "symbol": symbol,
                "date": scenario.dates[-1],
                "params": json.dumps(params, ensure_ascii=False, sort_keys=True),
            "completion_requirement": requirement,
                "python_value": value,
            })
    return rows


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Cross-check factor outputs between C++ and Python reference implementations")
    parser.add_argument("--cpp-json", type=Path, default=None, help="Path to the JSON exported by the C++ test runner")
    parser.add_argument("--csv", type=Path, default=Path("factor_crosscheck_results.csv"), help="CSV output path")
    parser.add_argument("--json", type=Path, default=Path("factor_crosscheck_results.json"), help="JSON output path")
    parser.add_argument("--tolerance", type=float, default=1e-6, help="Absolute/relative tolerance")
    parser.add_argument("--technical-mode", choices=["talib", "formula", "auto"], default="talib", help="Technical indicator backend for Python reference")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    scenario = build_scenario()
    python_rows = build_python_rows(scenario, args.technical_mode)
    cpp_rows = load_cpp_rows(args.cpp_json) if args.cpp_json else []
    final_rows = compare_rows(python_rows, cpp_rows, args.tolerance) if cpp_rows else [
        {**row, "cpp_value": None, "abs_error": None, "rel_error": None, "pass": False, "note": "no_cpp_json"}
        for row in python_rows
    ]

    payload = {
        "scenario": "synthetic_v1",
        "dates": scenario.dates,
        "symbols": scenario.symbols,
        "completion_requirements": completion_requirements_summary(),
        "rows": final_rows,
        "talib_available": talib is not None,
        "technical_mode": args.technical_mode,
    }
    args.json.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
    rows_to_csv(args.csv, final_rows)

    print(f"wrote {args.csv}")
    print(f"wrote {args.json}")
    print(f"rows={len(final_rows)}")
    print(f"talib_available={talib is not None}")
    print(f"technical_mode={args.technical_mode}")


if __name__ == "__main__":
    main()