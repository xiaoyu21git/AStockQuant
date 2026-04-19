import argparse
import json
from pathlib import Path

import pymysql


def merge_maps(base, overlay):
    merged = dict(base)
    for key, value in overlay.items():
        existing = merged.get(key)
        if isinstance(existing, dict) and isinstance(value, dict):
            merged[key] = merge_maps(existing, value)
        else:
            merged[key] = value
    return merged


def sanitize_logged_json(payload_text):
    sanitized = []
    in_string = False
    escaped = False
    index = 0

    while index < len(payload_text):
        char = payload_text[index]

        if escaped:
            sanitized.append(char)
            escaped = False
            index += 1
            continue

        if char == "\\":
            next_char = payload_text[index + 1] if index + 1 < len(payload_text) else ""
            if not in_string and next_char in {"n", "r", "t"}:
                index += 2
                continue

            sanitized.append(char)
            escaped = True
            index += 1
            continue

        if char == '"':
            in_string = not in_string

        sanitized.append(char)
        index += 1

    return "".join(sanitized)


def extract_payload(log_path, strategy_name):
    marker = "策略数据构建完成: "
    for line in log_path.read_text(encoding="utf-8").splitlines():
        if marker not in line or f'"name": "{strategy_name}"' not in line:
            continue
        payload_text = line.split(marker, 1)[1].rsplit(" (qrc:/", 1)[0]
        payload_text = sanitize_logged_json(payload_text)
        payload = json.loads(payload_text)
        parameters = payload.get("parameters") or {}
        if parameters:
            return parameters
    raise RuntimeError("unable to find strategy payload in log")


def main():
    parser = argparse.ArgumentParser(description="Restore strategy parameters from application log")
    parser.add_argument("--strategy-id", required=True)
    parser.add_argument("--strategy-name", required=True)
    parser.add_argument("--log-path", required=True)
    parser.add_argument("--db-host", default="127.0.0.1")
    parser.add_argument("--db-port", type=int, default=3306)
    parser.add_argument("--db-name", default="astock_quant")
    parser.add_argument("--db-user", default="root")
    parser.add_argument("--db-password", default="123456a")
    args = parser.parse_args()

    restored_parameters = extract_payload(Path(args.log_path), args.strategy_name)

    connection = pymysql.connect(
        host=args.db_host,
        user=args.db_user,
        password=args.db_password,
        database=args.db_name,
        port=args.db_port,
        charset="utf8mb4",
    )

    try:
        with connection.cursor() as cursor:
            cursor.execute(
                "SELECT parameters FROM strategy WHERE strategy_id=%s",
                (args.strategy_id,),
            )
            row = cursor.fetchone()
            if not row:
                raise RuntimeError("strategy row not found")

            current_parameters = json.loads(row[0]) if row[0] else {}
            merged_parameters = merge_maps(restored_parameters, current_parameters)
            cursor.execute(
                "UPDATE strategy SET parameters=%s WHERE strategy_id=%s",
                (json.dumps(merged_parameters, ensure_ascii=False), args.strategy_id),
            )
        connection.commit()
    finally:
        connection.close()

    print("restored_top_keys=", sorted(merged_parameters.keys()))
    print("has_rule_profile=", "rule_profile" in merged_parameters)
    print("has_rule_composer_state=", "rule_composer_state" in merged_parameters)
    print("has_rule_template_bindings=", "rule_template_bindings" in merged_parameters)
    print("binding_count=", len(merged_parameters.get("rule_template_bindings", [])))


if __name__ == "__main__":
    main()