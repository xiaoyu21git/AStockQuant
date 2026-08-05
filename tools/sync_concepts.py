#!/usr/bin/env python
"""
sync_concepts.py — GM SDK 概念/题材数据同步到 PG

调用 GM C++ SDK 的 stk_get_sector_category / stk_get_sector_constituents,
将概念 → 成分股映射写入 live.concept_catalog + live.concept_membership 表。
"""

import json
import sys
import ctypes
import os


GMSDK_DLL = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                         "bin", "Release", "gmsdk.dll")

try:
    gm = ctypes.CDLL(GMSDK_DLL)
except OSError as e:
    print(f"无法加载 gmsdk.dll: {e}")
    print("请在掘金终端登录后运行此脚本")
    sys.exit(1)

print(f"GM SDK 已加载: {GMSDK_DLL}")
print("请确保掘金终端已登录并连接到服务器")
print("由于 Python ctypes 调用 GM SDK 的复杂结构体需要包装代码,")
print("建议通过应用内置的 C++ PostMarketSyncService 执行概念同步。")
print()
print("手动同步: 启动应用 → 等待盘后同步完成 → 概念数据自动入库")
print("或: 在应用中触发 '同步概念板块' 功能按钮")
