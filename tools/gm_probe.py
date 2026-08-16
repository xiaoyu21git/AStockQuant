"""
gm_probe.py — 掘金 (GM) SDK 历史分钟数据深度探测

前提: tools/venv_gm 已建 (--system-site-packages + protobuf==3.20.3),
      主环境已装 gm SDK; 用 venv 里的 python 运行本脚本:
        tools/venv_gm/Scripts/python.exe tools/gm_probe.py

token 不硬编码, 运行时从 bin/Release/config/trading_connection.json 读取
(provider=jujin 的 token 字段, 与 C++ JujinMarketConnector 同源)。
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from gm.api import set_token, history, ADJUST_NONE  # noqa: E402


def load_token() -> str:
    cfg = json.loads(
        (Path(__file__).resolve().parents[1] / "bin" / "Release" / "config"
         / "trading_connection.json").read_text(encoding="utf-8"))
    token = cfg.get("token", "")
    if not token:
        print("ERROR: trading_connection.json 无 token", flush=True)
        sys.exit(1)
    return token


def probe(symbol: str, freq: str, start: str, end: str) -> None:
    try:
        df = history(symbol, freq, start, end,
                     fields="eob,open,high,low,close,volume,amount",
                     adjust=ADJUST_NONE, df=True)
        if df is None or len(df) == 0:
            print(f"{symbol} {freq} {start}~{end}: EMPTY", flush=True)
            return
        print(f"{symbol} {freq} {start}~{end}: rows={len(df)} "
              f"first={df['eob'].iloc[0]} last={df['eob'].iloc[-1]}", flush=True)
        print(f"  cols={list(df.columns)}", flush=True)
        print(f"  sample={df.iloc[0].to_dict()}", flush=True)
    except Exception as e:
        print(f"{symbol} {freq} {start}~{end}: ERROR {type(e).__name__}: {e}", flush=True)


def main() -> None:
    token = load_token()
    print(f"set_token(***{token[-4:]})...", flush=True)
    try:
        set_token(token)
        print("token ok", flush=True)
    except Exception as e:
        print(f"set_token ERROR {type(e).__name__}: {e}", flush=True)
        sys.exit(1)

    # 5 分钟深度探测: 从近到远试探保留起点
    probe("SZSE.000001", "300s", "2020-01-02", "2020-01-03")
    probe("SZSE.000001", "300s", "2016-01-04", "2016-01-05")
    probe("SZSE.000001", "300s", "2015-01-05", "2015-01-06")
    probe("SZSE.000001", "300s", "2005-01-04", "2005-01-05")
    # 1 分钟深度探测
    probe("SZSE.000001", "60s", "2020-01-02", "2020-01-02")
    # 日线对照 (账号数据权限正常与否的参照)
    probe("SZSE.000001", "1d", "2005-01-04", "2005-01-10")


if __name__ == "__main__":
    main()
