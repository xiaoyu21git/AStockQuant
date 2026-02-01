"""简单的实盘动作 JSONL 日志记录器。

每次调用 log_action，会向 data/live_actions.jsonl 追加一行 JSON：

    {"ts": "2026-02-02T09:30:00+08:00", "type": "ORDER_NEW", "symbol": "000001.SZ", ...}
"""

from __future__ import annotations

import json
from datetime import datetime, timezone, timedelta
from pathlib import Path
from typing import Any, Dict, Optional


def _now_ts() -> str:
    tz = timezone(timedelta(hours=8))
    return datetime.now(tz).isoformat(timespec="seconds")


def log_action(
    action_type: str,
    symbol: str = "",
    side: str = "",
    quantity: float = 0.0,
    price: float = 0.0,
    status: str = "",
    extra: Optional[Dict[str, Any]] = None,
) -> None:
    """将一条动作记录以 JSONL 形式追加到 data/live_actions.jsonl。

    设计为尽量无副作用：任何异常都被静默吞掉，不影响主流程。
    """

    try:
        repo_root = Path(__file__).resolve().parents[1]
        log_dir = repo_root / "data"
        log_dir.mkdir(parents=True, exist_ok=True)
        log_file = log_dir / "live_actions.jsonl"

        record: Dict[str, Any] = {
            "ts": _now_ts(),
            "type": action_type,
            "symbol": symbol,
            "side": side,
            "quantity": float(quantity or 0.0),
            "price": float(price or 0.0),
            "status": status,
        }
        if extra:
            record.update(extra)

        with log_file.open("a", encoding="utf-8") as f:
            json.dump(record, f, ensure_ascii=False)
            f.write("\n")
    except Exception:
        # 日志记录失败不应影响任何实盘逻辑
        return
