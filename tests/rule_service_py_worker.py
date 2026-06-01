#!/usr/bin/env python3
import json
import sys


def _eval_signal(signal, rule_ids):
    score = float(signal.get("score", 0.0))
    target_weight = float(signal.get("target_weight", 0.0))

    RULE_SCORE_NON_NEGATIVE = 1
    RULE_TARGET_WEIGHT_ABS_LIMIT = 2

    passed = True
    reject_reason = 0

    for rule_id in rule_ids:
        if rule_id == RULE_SCORE_NON_NEGATIVE and score < 0.0:
            passed = False
            reject_reason = 3  # RuleTemplateBlocked
            break
        if rule_id == RULE_TARGET_WEIGHT_ABS_LIMIT and abs(target_weight) > 1.0:
            passed = False
            reject_reason = 2  # RiskGuardBlocked
            break

    return {"passed": passed, "reject_reason": reject_reason}


def _handle_request(payload):
    signals = payload.get("signals", [])
    rule_ids = payload.get("rule_ids", [])
    results = [_eval_signal(signal, rule_ids) for signal in signals]
    return results


def main() -> int:
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue

        try:
            payload = json.loads(line)
            results = _handle_request(payload)
            sys.stdout.write(json.dumps(results, ensure_ascii=True) + "\n")
            sys.stdout.flush()
        except Exception:
            # Fail fast with an empty result line so caller can detect mismatch.
            sys.stdout.write("[]\n")
            sys.stdout.flush()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
