"""从快照恢复策略88575b08到年化10.24%基线配置."""
import psycopg2
import json
import sys
from pathlib import Path

SNAPSHOT_PATH = Path(__file__).parent.parent / "config" / "strategy_10_24_snapshot.json"
STRATEGY_ID = "88575b08-386a-41ff-8cbd-91cabfabf70a"


def main():
    if not SNAPSHOT_PATH.exists():
        print(f"快照文件不存在: {SNAPSHOT_PATH}")
        sys.exit(1)

    with open(SNAPSHOT_PATH, encoding="utf-8") as f:
        snapshot = json.load(f)

    print(f"快照: {snapshot['saved_at']}  性能: {snapshot['performance']}")
    print(f"策略: {snapshot['strategy_name']}")

    conn = psycopg2.connect(
        host="127.0.0.1", port=5432, dbname="astock_quant",
        user="astock", password="astock123",
    )
    cur = conn.cursor()

    # 读取当前参数
    cur.execute(
        "SELECT parameters FROM live.strategy WHERE strategy_id = %s", (STRATEGY_ID,)
    )
    row = cur.fetchone()
    if not row:
        print(f"策略 {STRATEGY_ID} 不存在!")
        conn.close()
        sys.exit(1)

    params = row[0] if isinstance(row[0], dict) else json.loads(row[0])

    # 只替换规则和因子配置，保留其他参数
    params["rule_composer_state"] = snapshot["rule_composer_state"]
    params["factor_overlay"] = snapshot["factor_overlay"]
    for key in ["maxPositions", "stopLossPercent", "takeProfitPercent", "maxDrawdownLimit"]:
        if key in snapshot and snapshot[key] is not None:
            params[key] = snapshot[key]

    params_json = json.dumps(params, ensure_ascii=False)
    cur.execute(
        "UPDATE live.strategy SET parameters = %s::jsonb, updated_at = NOW() WHERE strategy_id = %s",
        (params_json, STRATEGY_ID),
    )
    conn.commit()

    tpl_count = sum(
        1
        for s in snapshot["rule_composer_state"]["stages"]
        for g in s["groups"]
        for _r in g["rules"]
    )
    print(f"已恢复: {tpl_count} 个模板绑定, 因子配置已还原")
    conn.close()


if __name__ == "__main__":
    main()
