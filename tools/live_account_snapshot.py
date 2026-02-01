"""定期从掘金 MyQuantBroker 拉取账户资金和持仓快照，写入 JSON 文件，
供 C++/QML 实盘仪表盘读取。

用法（示例）：

    # 激活你的虚拟环境，并确保已安装 gm 和 astock_engine
    # 设置好 GM_TOKEN / GM_ACCOUNT_ID 或在 MyQuantBroker 中写默认值
    python -m tools.live_account_snapshot

默认输出文件：data/live_account_snapshot.json
"""

from __future__ import annotations

import json
import os
from datetime import datetime, timezone, timedelta
from pathlib import Path
from typing import Dict, Any, List

from astock_engine.broker import MyQuantBroker


def _call_with_timeout(func, timeout: float, default):
    """在独立线程中调用 func，超过 timeout 秒则放弃结果，返回 default。

    这样即便 gm.api 内部阻塞，也不会卡死整个快照脚本。
    """

    import threading

    result: Dict[str, Any] = {"ok": False, "value": default}

    def runner():
        try:
            value = func()
        except Exception:
            value = default
        result["ok"] = True
        result["value"] = value

    t = threading.Thread(target=runner, daemon=True)
    t.start()
    t.join(timeout)

    if not result["ok"]:
        print("[live_account_snapshot] 调用超时，使用默认值", flush=True)
        return default
    return result["value"]


def build_snapshot(broker: MyQuantBroker | None = None) -> dict:
    """构建一份账户资金 + 持仓市值快照。

    - 默认内部创建 MyQuantBroker（适合命令行单次调用）；
    - 若传入已有 broker，则复用该实例（适合实时容器中长时间运行）。
    """

    print("[live_account_snapshot] 初始化券商适配层 MyQuantBroker...", flush=True)
    broker = broker or MyQuantBroker()

    print("[live_account_snapshot] 获取账户资金快照...", flush=True)
    snap: Dict[str, Any] = _call_with_timeout(
        broker.get_account_snapshot,
        timeout=5.0,
        default={},
    ) or {}

    print("[live_account_snapshot] 获取规范化持仓列表...", flush=True)
    positions: List[Dict[str, Any]] = _call_with_timeout(
        broker.get_normalized_positions,
        timeout=5.0,
        default=[],
    ) or []

    total_asset = float(snap.get("total_asset") or 0.0)
    cash = float(snap.get("cash") or 0.0)
    available = float(snap.get("available") or cash)

    # 简单用 持仓数量 * 价格 估算市值，同时构建持仓明细
    position_mv = 0.0
    positions_out: List[Dict[str, Any]] = []
    for pos in positions:
        try:
            symbol = str(pos.get("symbol") or "")
            gm_symbol = str(pos.get("gm_symbol") or "")
            qty = float(pos.get("quantity") or 0.0)
            price = float(pos.get("price") or 0.0)
            direction = str(pos.get("direction") or "LONG")
        except Exception:
            continue

        if qty <= 0 or price <= 0 or not symbol:
            continue

        value = max(0.0, qty) * max(0.0, price)
        position_mv += value

        positions_out.append(
            {
                "symbol": symbol,
                "gm_symbol": gm_symbol,
                "quantity": qty,
                "price": price,
                "market_value": value,
                "direction": direction,
            }
        )

    # 尝试从原始返回中抽取当日盈亏字段（若无则为 0）
    raw = snap.get("raw")
    base = None
    if isinstance(raw, list) and raw:
        base = raw[0]
    elif isinstance(raw, dict):
        base = raw

    today_pnl = 0.0
    if isinstance(base, dict):
        for key in ("pnl", "fpnl", "today_pnl", "day_pnl"):
            if key in base:
                try:
                    today_pnl = float(base.get(key) or 0.0)
                    break
                except Exception:
                    continue

    tz = timezone(timedelta(hours=8))  # 北京时间
    now = datetime.now(tz)

    return {
        "ts": now.isoformat(timespec="seconds"),
        "total_asset": total_asset,
        "cash": cash,
        "available": available,
        "position_market_value": position_mv,
        "today_pnl": today_pnl,
        "positions": positions_out,
    }


def main() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    out_dir = repo_root / "data"
    out_dir.mkdir(parents=True, exist_ok=True)
    out_file = out_dir / "live_account_snapshot.json"

    print("[live_account_snapshot] 开始构建账户快照", flush=True)
    snap = build_snapshot()

    tmp_file = out_file.with_suffix(".json.tmp")
    with tmp_file.open("w", encoding="utf-8") as f:
        json.dump(snap, f, ensure_ascii=False, indent=2)
    tmp_file.replace(out_file)

    print(f"[live_account_snapshot] 已写入账户快照到: {out_file}")
    print(json.dumps(snap, ensure_ascii=False, indent=2))


if __name__ == "__main__":  # pragma: no cover
    main()
