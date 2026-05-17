from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass, field
from typing import Any

import pymysql


MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
    "autocommit": False,
}

NON_CONFIGURABLE_FACTOR_TYPES = {0, 1, 2, 3, 12}
CONFIGURABLE_FACTOR_TYPES = {4, 5, 6, 7, 8, 9, 10, 11}
INTEGER_TEXT_PATTERN = re.compile(r"^-?\d+$")

FACTOR_TYPE_MAP = {
    "value": 0,
    "momentum": 1,
    "size": 2,
    "quality": 3,
    "growth": 4,
    "dividend": 5,
    "technical": 6,
    "liquidity": 7,
    "macro": 8,
    "industry": 9,
    "sentiment": 10,
    "custom": 11,
    "lowvolatility": 12,
    "low_volatility": 12,
    "lowvol": 12,
}

FREQUENCY_MAP = {
    "daily": 0,
    "weekly": 1,
    "monthly": 2,
    "quarterly": 3,
    "annual": 4,
    "yearly": 4,
    "日频": 0,
    "周频": 1,
    "月频": 2,
    "季频": 3,
    "年频": 4,
}

COMMON_STANDARDIZATION_MAP = {
    "none": 0,
    "zscore": 1,
    "z_score": 1,
    "minmax": 2,
    "min_max": 2,
    "percentile": 3,
}

CONFIGURABLE_STANDARDIZATION_MAP = {
    "none": 0,
    "zscore": 1,
    "z_score": 1,
    "minmax": 2,
    "min_max": 2,
    "rank": 3,
    "percentile": 4,
}

VALUATION_METRIC_MAP = {
    "bp": 0,
    "ep": 1,
    "dividend_yield": 2,
    "cfp": 3,
    "cf_p": 3,
}

SIZE_METRIC_MAP = {
    "market_cap": 0,
    "marketcap": 0,
    "circulating_market_cap": 1,
    "circulatingmarketcap": 1,
    "total_assets": 2,
    "totalassets": 2,
}

QUALITY_METRIC_MAP = {
    "roe": 0,
    "roa": 1,
    "gross_margin": 2,
    "grossmargin": 2,
    "operating_margin": 3,
    "operatingmargin": 3,
    "earnings_quality": 4,
    "earningsquality": 4,
}

GROWTH_METRIC_MAP = {
    "revenue_growth": 0,
    "revenuegrowth": 0,
    "net_profit_growth": 1,
    "netprofitgrowth": 1,
    "delta_roe": 2,
    "deltaroe": 2,
    "sue": 3,
}

DIVIDEND_METRIC_MAP = {
    "dividend_yield": 0,
    "dividendyield": 0,
    "payout_ratio": 1,
    "payoutratio": 1,
    "dividend_stability": 2,
    "dividendstability": 2,
}

LIQUIDITY_METRIC_MAP = {
    "turnover_rate": 0,
    "turnoverrate": 0,
    "volume": 1,
    "amihud_illiquidity": 2,
    "amihudilliquidity": 2,
    "amplitude": 3,
}

INDUSTRY_METRIC_MAP = {
    "industry_prosperity": 0,
    "industryprosperity": 0,
    "industry_momentum": 1,
    "industrymomentum": 1,
    "industry_concentration": 2,
    "industryconcentration": 2,
}

SENTIMENT_METRIC_MAP = {
    "sentiment_score": 0,
    "sentimentscore": 0,
    "social_sentiment": 1,
    "socialsentiment": 1,
    "investor_sentiment": 2,
    "investorsentiment": 2,
    "market_sentiment": 3,
    "marketsentiment": 3,
}

TECHNICAL_INDICATOR_MAP = {
    "rsi": 0,
    "macd": 1,
    "ma": 2,
    "ema": 3,
    "boll": 4,
    "kdj": 5,
    "atr": 6,
    "obv": 7,
    "vwap": 8,
    "volume_ratio": 9,
    "volumeratio": 9,
    "turnover_stability": 10,
    "turnoverstability": 10,
}

TECHNICAL_COMBINATION_MODE_MAP = {
    "equalweight": 0,
    "equal_weight": 0,
    "normalizedaverage": 1,
    "normalized_average": 1,
}

TECHNICAL_PRICE_TYPE_MAP = {
    "close": 0,
    "open": 1,
    "high": 2,
    "low": 3,
}

SENTIMENT_SOURCE_MAP = {
    "news": 0,
    "news_sentiment": 0,
    "newssentiment": 0,
    "social_media": 1,
    "socialmedia": 1,
    "analyst_rating": 2,
    "analystrating": 2,
    "market": 3,
    "market_sentiment": 3,
    "marketsentiment": 3,
    "policy": 4,
    "alternative": 5,
    "derivatives": 6,
}

SECTOR_TYPE_MAP = {
    "sw_l1": 0,
    "swl1": 0,
    "申万一级": 0,
    "sw_l2": 1,
    "swl2": 1,
    "申万二级": 1,
    "citic_l1": 2,
    "citicl1": 2,
    "中信一级": 2,
    "citic_l2": 3,
    "citicl2": 3,
    "中信二级": 3,
}

MACRO_DIMENSION_MAP = {
    "growth": 0,
    "inflation": 1,
    "credit": 2,
    "rates": 3,
    "policy": 4,
    "risk_appetite": 5,
    "riskappetite": 5,
}

MACRO_INDICATOR_MAP = {
    "industrial_added_value_yoy": 0,
    "industrialaddedvalueyoy": 0,
    "manufacturing_pmi": 1,
    "manufacturingpmi": 1,
    "gdp_yoy": 2,
    "gdpyoy": 2,
    "cpi_yoy": 3,
    "cpiyoy": 3,
    "ppi_yoy": 4,
    "ppiyoy": 4,
    "m2_yoy": 5,
    "m2yoy": 5,
    "social_financing_stock_yoy": 6,
    "socialfinancingstockyoy": 6,
    "m1_m2_spread": 7,
    "m1m2spread": 7,
    "ten_year_bond_yield": 8,
    "tenyearbondyield": 8,
    "shibor_3m": 9,
    "shibor3m": 9,
    "lpr_1y": 10,
    "lpr1y": 10,
    "reserve_requirement_ratio": 11,
    "reserverequirementratio": 11,
    "aa_credit_spread": 12,
    "aacreditspread": 12,
    "vix_proxy": 13,
    "vixproxy": 13,
}

MOMENTUM_TYPE_MAP = {
    "simple": 0,
    "rank": 1,
    "normalized": 2,
    "exponential": 3,
}

ADJUST_PRICE_TYPE_MAP = {
    "pre_adjust_factor": 0,
    "preadjustfactor": 0,
    "前复权": 0,
    "post_adjust_factor": 1,
    "postadjustfactor": 1,
    "后复权": 1,
}

LOW_VOL_COMPONENT_MAP = {
    "volatility": 0,
    "drawdown": 1,
    "beta": 2,
}

NEW_STOCK_HANDLING_MAP = {
    "exclude_if_lt_60d": 0,
    "excludeiflt60d": 0,
    "include": 1,
}

SUSPENDED_HANDLING_MAP = {
    "forward_fill": 0,
    "forwardfill": 0,
    "exclude": 1,
    "set_null": 2,
    "setnull": 2,
}

DELISTED_HANDLING_MAP = {
    "keep_until_delist": 0,
    "keepuntildelist": 0,
    "exclude": 1,
}

OUTLIER_HANDLING_MAP = {
    "winsorize_3sigma": 0,
    "winsorize3sigma": 0,
    "exclude": 1,
    "keep": 2,
}


@dataclass
class RepairReport:
    instance_id: str
    factor_id: str
    status: str
    supported_changes: list[str] = field(default_factory=list)
    unsupported_issues: list[str] = field(default_factory=list)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="审计并修复 factor_instance.full_config 的历史字符串枚举和别名键")
    parser.add_argument("--dry-run", action="store_true", help="仅输出审计和候选修复，不写入数据库")
    parser.add_argument("--only-active", action="store_true", help="仅扫描 status='ACTIVE' 的因子实例")
    parser.add_argument("--instance-id", action="append", default=[], help="仅处理指定 instance_id，可重复传入")
    parser.add_argument("--sample-limit", type=int, default=20, help="最多打印多少条样本，默认 20")
    parser.add_argument(
        "--rewrite-legacy-keys",
        action="store_true",
        help="显式将历史别名键改写为当前 canonical 键；默认仅报错不迁移",
    )
    parser.add_argument(
        "--fail-on-unsupported",
        action="store_true",
        help="若仍存在未支持的字符串枚举或结构问题，则返回非零退出码",
    )
    return parser.parse_args()


def get_connection():
    return pymysql.connect(**MYSQL_CONFIG)


def normalize_token(value: str) -> str:
    return str(value).strip().lower().replace("-", "_").replace(" ", "")


def coerce_integer(value: Any) -> int | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, float) and value.is_integer():
        return int(value)
    if isinstance(value, str):
        raw = value.strip()
        if INTEGER_TEXT_PATTERN.match(raw):
            return int(raw)
    return None


def coerce_numeric(value: Any) -> int | float | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        return value
    if isinstance(value, str):
        raw = value.strip()
        if not raw:
            return None
        try:
            parsed = float(raw)
        except ValueError:
            return None
        if parsed.is_integer():
            return int(parsed)
        return parsed
    return None


def resolve_enum_value(value: Any, mapping: dict[str, int]) -> int | None:
    valid_values = set(mapping.values())
    coerced_integer = coerce_integer(value)
    if coerced_integer is not None and coerced_integer in valid_values:
        return coerced_integer
    if isinstance(value, str):
        return mapping.get(normalize_token(value))
    return None


def repair_scalar_enum(
    calculation: dict[str, Any],
    field_name: str,
    mapping: dict[str, int],
    report: RepairReport,
) -> bool:
    if field_name not in calculation:
        return False
    current_value = calculation[field_name]
    resolved_value = resolve_enum_value(current_value, mapping)
    if resolved_value is None:
        if isinstance(current_value, str):
            report.unsupported_issues.append(f"calculation.{field_name}=unsupported-string:{current_value}")
        elif not isinstance(current_value, int):
            report.unsupported_issues.append(f"calculation.{field_name}=unsupported-type:{type(current_value).__name__}")
        return False
    if current_value != resolved_value:
        calculation[field_name] = resolved_value
        report.supported_changes.append(f"calculation.{field_name}:{current_value}->{resolved_value}")
        return True
    return False


def repair_array_enum(
    calculation: dict[str, Any],
    field_name: str,
    mapping: dict[str, int],
    report: RepairReport,
) -> bool:
    if field_name not in calculation:
        return False
    current_value = calculation[field_name]
    if not isinstance(current_value, list):
        report.unsupported_issues.append(f"calculation.{field_name}=unsupported-type:{type(current_value).__name__}")
        return False
    normalized_items: list[int] = []
    changed = False
    for index, item in enumerate(current_value):
        resolved_value = resolve_enum_value(item, mapping)
        if resolved_value is None:
            report.unsupported_issues.append(f"calculation.{field_name}[{index}]=unsupported:{item}")
            return False
        normalized_items.append(resolved_value)
        changed = changed or item != resolved_value
    if changed:
        calculation[field_name] = normalized_items
        report.supported_changes.append(f"calculation.{field_name}:normalized-array")
        return True
    return False


def handle_legacy_key(
    calculation: dict[str, Any],
    legacy_key: str,
    canonical_key: str,
    report: RepairReport,
    rewrite_legacy_keys: bool,
    *,
    numeric: bool = False,
) -> bool:
    if legacy_key not in calculation:
        return False
    legacy_value = calculation[legacy_key]
    if not rewrite_legacy_keys:
        report.unsupported_issues.append(f"calculation.{legacy_key}=legacy-key-use-canonical:{canonical_key}")
        return False

    resolved_value = coerce_numeric(legacy_value) if numeric else legacy_value
    if numeric and resolved_value is None:
        report.unsupported_issues.append(f"calculation.{legacy_key}=invalid-legacy-value:{legacy_value}")
        return False

    if canonical_key in calculation:
        if calculation[canonical_key] != resolved_value:
            report.unsupported_issues.append(f"calculation.{legacy_key}=legacy-key-conflicts-with:{canonical_key}")
            return False
        del calculation[legacy_key]
        report.supported_changes.append(f"calculation.{legacy_key}:removed-legacy-duplicate")
        return True

    calculation[canonical_key] = resolved_value
    del calculation[legacy_key]
    report.supported_changes.append(f"calculation.{legacy_key}->{canonical_key}:rewritten")
    return True


def repair_factor_type(config: dict[str, Any], report: RepairReport) -> int | None:
    if "factorType" not in config:
        report.unsupported_issues.append("factorType=missing")
        return None
    raw_value = config["factorType"]
    resolved_value = resolve_enum_value(raw_value, FACTOR_TYPE_MAP)
    if resolved_value is None:
        report.unsupported_issues.append(f"factorType=unsupported:{raw_value}")
        return None
    if raw_value != resolved_value:
        config["factorType"] = resolved_value
        report.supported_changes.append(f"factorType:{raw_value}->{resolved_value}")
    return resolved_value


def repair_common_fields(
    calculation: dict[str, Any],
    factor_type: int,
    report: RepairReport,
    rewrite_legacy_keys: bool,
) -> bool:
    changed = False
    standardization_map = (
        CONFIGURABLE_STANDARDIZATION_MAP if factor_type in CONFIGURABLE_FACTOR_TYPES else COMMON_STANDARDIZATION_MAP
    )
    changed = handle_legacy_key(calculation, "lookback_period", "lookbackWindow", report, rewrite_legacy_keys, numeric=True) or changed
    changed = handle_legacy_key(calculation, "lookbackPeriod", "lookbackWindow", report, rewrite_legacy_keys, numeric=True) or changed
    changed = handle_legacy_key(calculation, "lagEnabled", "laggedEnabled", report, rewrite_legacy_keys) or changed
    changed = repair_scalar_enum(calculation, "frequency", FREQUENCY_MAP, report) or changed
    changed = repair_scalar_enum(calculation, "standardization", standardization_map, report) or changed
    return changed


def repair_factor_specific_fields(
    calculation: dict[str, Any],
    factor_type: int,
    report: RepairReport,
    rewrite_legacy_keys: bool,
) -> bool:
    changed = False
    if factor_type == 0:
        changed = repair_array_enum(calculation, "valuationMetrics", VALUATION_METRIC_MAP, report) or changed
    elif factor_type == 1:
        changed = handle_legacy_key(calculation, "lookback_window", "window", report, rewrite_legacy_keys, numeric=True) or changed
        changed = repair_scalar_enum(calculation, "type", MOMENTUM_TYPE_MAP, report) or changed
        changed = repair_scalar_enum(calculation, "adjustPriceType", ADJUST_PRICE_TYPE_MAP, report) or changed
    elif factor_type == 2:
        changed = repair_scalar_enum(calculation, "sizeMetric", SIZE_METRIC_MAP, report) or changed
    elif factor_type == 3:
        changed = handle_legacy_key(calculation, "quality_threshold", "qualityThreshold", report, rewrite_legacy_keys, numeric=True) or changed
        changed = repair_scalar_enum(calculation, "metric", QUALITY_METRIC_MAP, report) or changed
    elif factor_type == 4:
        changed = repair_array_enum(calculation, "growthMetrics", GROWTH_METRIC_MAP, report) or changed
    elif factor_type == 5:
        changed = repair_scalar_enum(calculation, "metric", DIVIDEND_METRIC_MAP, report) or changed
        changed = repair_array_enum(calculation, "dividendMetrics", DIVIDEND_METRIC_MAP, report) or changed
    elif factor_type == 6:
        changed = repair_array_enum(calculation, "technicalIndicators", TECHNICAL_INDICATOR_MAP, report) or changed
        changed = repair_scalar_enum(calculation, "technicalCombinationMode", TECHNICAL_COMBINATION_MODE_MAP, report) or changed
        changed = repair_scalar_enum(calculation, "turnoverStabilityMetric", LIQUIDITY_METRIC_MAP, report) or changed
        changed = repair_scalar_enum(calculation, "technicalPriceType", TECHNICAL_PRICE_TYPE_MAP, report) or changed
    elif factor_type == 7:
        changed = repair_scalar_enum(calculation, "metric", LIQUIDITY_METRIC_MAP, report) or changed
    elif factor_type == 8:
        changed = repair_array_enum(calculation, "macroDimensions", MACRO_DIMENSION_MAP, report) or changed
        changed = repair_array_enum(calculation, "macroIndicators", MACRO_INDICATOR_MAP, report) or changed
        changed = repair_scalar_enum(calculation, "priceType", TECHNICAL_PRICE_TYPE_MAP, report) or changed
        changed = repair_scalar_enum(calculation, "macroFrequency", FREQUENCY_MAP, report) or changed
    elif factor_type == 9:
        changed = repair_scalar_enum(calculation, "industryMetric", INDUSTRY_METRIC_MAP, report) or changed
        changed = repair_scalar_enum(calculation, "sectorType", SECTOR_TYPE_MAP, report) or changed
    elif factor_type == 10:
        changed = repair_scalar_enum(calculation, "metric", SENTIMENT_METRIC_MAP, report) or changed
        changed = repair_scalar_enum(calculation, "sentimentSource", SENTIMENT_SOURCE_MAP, report) or changed
    elif factor_type == 12:
        changed = repair_array_enum(calculation, "components", LOW_VOL_COMPONENT_MAP, report) or changed
    return changed


def repair_boundary_rules(config: dict[str, Any], report: RepairReport) -> bool:
    boundary_rules = config.get("boundaryRules")
    if boundary_rules is None:
        return False
    if not isinstance(boundary_rules, dict):
        report.unsupported_issues.append(f"boundaryRules=unsupported-type:{type(boundary_rules).__name__}")
        return False

    changed = False
    changed = repair_scalar_enum(boundary_rules, "handleNewStock", NEW_STOCK_HANDLING_MAP, report) or changed
    changed = repair_scalar_enum(boundary_rules, "handleSuspended", SUSPENDED_HANDLING_MAP, report) or changed
    changed = repair_scalar_enum(boundary_rules, "handleDelisted", DELISTED_HANDLING_MAP, report) or changed
    changed = repair_scalar_enum(boundary_rules, "handleOutliers", OUTLIER_HANDLING_MAP, report) or changed
    if changed:
        config["boundaryRules"] = boundary_rules
    return changed


def repair_full_config(full_config_text: str, report: RepairReport, rewrite_legacy_keys: bool) -> tuple[str | None, bool]:
    try:
        config = json.loads(full_config_text)
    except json.JSONDecodeError as exc:
        report.unsupported_issues.append(f"full_config=invalid-json:{exc}")
        return None, False

    if not isinstance(config, dict):
        report.unsupported_issues.append(f"full_config=unsupported-root:{type(config).__name__}")
        return None, False

    factor_type = repair_factor_type(config, report)
    calculation = config.get("calculation")
    changed = bool(report.supported_changes)

    if calculation is None:
        report.unsupported_issues.append("calculation=missing")
    elif not isinstance(calculation, dict):
        report.unsupported_issues.append(f"calculation=unsupported-type:{type(calculation).__name__}")
    elif factor_type is not None:
        changed = repair_common_fields(calculation, factor_type, report, rewrite_legacy_keys) or changed
        changed = repair_factor_specific_fields(calculation, factor_type, report, rewrite_legacy_keys) or changed
        config["calculation"] = calculation

    changed = repair_boundary_rules(config, report) or changed

    if not changed:
        return None, False
    return json.dumps(config, ensure_ascii=False), True


def build_query(args: argparse.Namespace) -> tuple[str, list[Any]]:
    sql = [
        "SELECT instance_id, factor_id, status, CAST(full_config AS CHAR) AS full_config",
        "FROM factor_instance",
    ]
    conditions: list[str] = []
    params: list[Any] = []

    if args.only_active:
        conditions.append("status = 'ACTIVE'")
    if args.instance_id:
        placeholders = ", ".join(["%s"] * len(args.instance_id))
        conditions.append(f"instance_id IN ({placeholders})")
        params.extend(args.instance_id)
    if conditions:
        sql.append("WHERE " + " AND ".join(conditions))
    sql.append("ORDER BY updated_at DESC, instance_id DESC")
    return "\n".join(sql), params


def print_samples(title: str, reports: list[RepairReport], sample_limit: int) -> None:
    print(title)
    for report in reports[:sample_limit]:
        fix_text = "; ".join(report.supported_changes) if report.supported_changes else "none"
        issue_text = "; ".join(report.unsupported_issues) if report.unsupported_issues else "none"
        print(
            f"- instance_id={report.instance_id} factor_id={report.factor_id} status={report.status} "
            f"fixes=[{fix_text}] issues=[{issue_text}]"
        )


def main() -> None:
    args = parse_args()
    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            sql, params = build_query(args)
            cursor.execute(sql, params)
            rows = cursor.fetchall()

            changed_reports: list[RepairReport] = []
            unsupported_reports: list[RepairReport] = []
            update_payloads: list[tuple[str, str]] = []

            for instance_id, factor_id, status, full_config_text in rows:
                report = RepairReport(
                    instance_id=str(instance_id),
                    factor_id=str(factor_id or ""),
                    status=str(status or ""),
                )
                repaired_text, changed = repair_full_config(
                    str(full_config_text or ""),
                    report,
                    args.rewrite_legacy_keys,
                )
                if changed and repaired_text is not None:
                    changed_reports.append(report)
                    update_payloads.append((repaired_text, report.instance_id))
                if report.unsupported_issues:
                    unsupported_reports.append(report)

            print(
                "repair_factor_instance_configs: "
                f"scanned_rows={len(rows)} "
                f"candidate_updates={len(update_payloads)} "
                f"rows_with_unsupported_issues={len(unsupported_reports)} "
                f"dry_run={args.dry_run}"
            )

            if changed_reports:
                print_samples("candidate update samples:", changed_reports, args.sample_limit)
            if unsupported_reports:
                print_samples("unsupported issue samples:", unsupported_reports, args.sample_limit)

            if not args.dry_run and update_payloads:
                cursor.executemany(
                    """
                    UPDATE factor_instance
                    SET full_config = %s,
                        updated_at = CURRENT_TIMESTAMP
                    WHERE instance_id = %s
                    """,
                    update_payloads,
                )

        if args.dry_run:
            conn.rollback()
            print("repair_factor_instance_configs: DRY RUN")
        else:
            conn.commit()
            print("repair_factor_instance_configs: OK")

        if unsupported_reports and args.fail_on_unsupported:
            raise SystemExit(2)
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


if __name__ == "__main__":
    main()