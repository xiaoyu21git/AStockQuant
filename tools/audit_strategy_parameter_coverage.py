from __future__ import annotations

import json
from collections import Counter

import pymysql


MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
}


AUDIT_KEYS = [
    "asset_type",
    "time_frame",
    "risk_level",
    "tags",
    "backtest_settings",
    "advanced_options",
    "performance_metrics",
]


def load_strategies(cursor) -> list[dict[str, object]]:
    cursor.execute(
        """
        SELECT strategy_id, strategy_name, strategy_type, status, updated_at, CAST(parameters AS CHAR)
        FROM strategy
        ORDER BY updated_at DESC, strategy_id DESC
        """
    )

    rows = []
    for strategy_id, strategy_name, strategy_type, status, updated_at, parameters_text in cursor.fetchall():
        try:
            parameters = json.loads(parameters_text) if parameters_text else {}
        except json.JSONDecodeError:
            parameters = {}

        rows.append(
            {
                "strategy_id": strategy_id,
                "strategy_name": strategy_name,
                "strategy_type": strategy_type,
                "status": status,
                "updated_at": str(updated_at),
                "parameters": parameters,
            }
        )
    return rows


def has_value(parameters: dict[str, object], key: str) -> bool:
    if key not in parameters:
        return False

    value = parameters[key]
    if value is None:
        return False
    if isinstance(value, str):
        return value.strip() != ""
    if isinstance(value, (list, dict, tuple, set)):
        return len(value) > 0
    return True


def summarize(rows: list[dict[str, object]]) -> None:
    total = len(rows)
    print(f"TOTAL\t{total}")

    print("\nKEY_COVERAGE")
    for key in AUDIT_KEYS:
        present_count = sum(1 for row in rows if has_value(row["parameters"], key))
        ratio = 0.0 if total == 0 else present_count / total
        print(f"{key}\t{present_count}\t{total}\t{ratio:.2%}")

    key_counter = Counter()
    for row in rows:
        parameters = row["parameters"]
        for key in parameters.keys():
            key_counter[key] += 1

    print("\nTOP_PARAMETER_KEYS")
    for key, count in key_counter.most_common(20):
        ratio = 0.0 if total == 0 else count / total
        print(f"{key}\t{count}\t{ratio:.2%}")

    print("\nMISSING_ALL_EXTENDED_FIELDS_SAMPLES")
    sample_count = 0
    for row in rows:
        parameters = row["parameters"]
        if any(has_value(parameters, key) for key in AUDIT_KEYS):
            continue

        print(
            json.dumps(
                {
                    "strategy_id": row["strategy_id"],
                    "strategy_name": row["strategy_name"],
                    "strategy_type": row["strategy_type"],
                    "status": row["status"],
                    "updated_at": row["updated_at"],
                    "parameter_keys": sorted(parameters.keys()),
                },
                ensure_ascii=False,
            )
        )
        sample_count += 1
        if sample_count >= 10:
            break


def main() -> None:
    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            rows = load_strategies(cursor)
        summarize(rows)
    finally:
        conn.close()


if __name__ == "__main__":
    main()