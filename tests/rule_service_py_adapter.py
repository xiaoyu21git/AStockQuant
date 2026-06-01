#!/usr/bin/env python3
import json
import sys


def _load_request(path: str):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def _eval_signal(signal, rule_ids):
    # Keep consistent with LocalRuleEvaluationService semantics.
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


def main() -> int:
    if len(sys.argv) != 2:
        print("[]")
        return 1

    request = _load_request(sys.argv[1])
    signals = request.get("signals", [])
    rule_ids = request.get("rule_ids", [])

    results = [_eval_signal(signal, rule_ids) for signal in signals]
    print(json.dumps(results, ensure_ascii=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
