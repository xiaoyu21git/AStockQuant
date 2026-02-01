"""简单检查：通过 Python 封装调用 C++ MarketDataRepository 读取 daily_bar

用法：在虚拟环境中运行
    python tools/check_cpp_dailybars.py
"""

import sys
from pathlib import Path

# 确保项目根目录在 sys.path 中，便于导入 astock_engine
PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from astock_engine.data.database_wrapper import get_database, close_database


def main() -> None:
    db = get_database()

    symbol = "600371.SH"  # 在 daily_bar_latest 中确认存在的标的
    print("== C++ MarketDataRepository daily_bar 检查 ==")
    print(f"symbol = {symbol}")

    latest = db.get_latest_bar(symbol)
    print("latest_bar:", latest)

    bars = db.get_daily_bars(symbol, "2024-01-01", "2026-01-30")
    print("bars_len:", len(bars))
    if bars:
        print("first_bar:", bars[0])
        print("last_bar:", bars[-1])

    close_database()


if __name__ == "__main__":
    main()
