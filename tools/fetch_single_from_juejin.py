#!/usr/bin/env python3
"""
从掘金获取单只股票数据的脚本
用于C++ DataService的回退机制
"""
import sys
import os
import json
import argparse
from datetime import datetime, date
from pathlib import Path

# 添加项目根目录到路径 - 处理从不同目录运行的情况
current_file = Path(__file__).resolve()

# 尝试多种可能的项目根目录路径
possible_roots = [
    current_file.parent.parent,  # 从bin/Debug/tools运行时
    current_file.parent.parent.parent,  # 从其他目录运行时
    Path.cwd(),  # 当前工作目录
    Path.cwd().parent,  # 上一级目录
]

for root in possible_roots:
    root_str = str(root)
    if root_str not in sys.path:
        sys.path.insert(0, root_str)
    
    # 检查tools目录是否存在
    tools_dir = root / "tools"
    if tools_dir.exists():
        # 添加tools目录到路径
        if str(tools_dir) not in sys.path:
            sys.path.insert(0, str(tools_dir))
        break

# 尝试导入
try:
    from import_from_juejin import (
        fetch_daily_bars_from_juejin,
        DEFAULT_START_DATE,
        DEFAULT_END_DATE
    )
    print(f"✅ 成功导入掘金模块，项目根目录: {root}")
except ImportError as e:
    print(f"❌ 导入失败: {e}")
    print(f"当前sys.path: {sys.path}")
    print(f"当前工作目录: {os.getcwd()}")
    print(f"脚本路径: {current_file}")
    sys.exit(1)

def fetch_single_stock_data(symbol, start_date_str, end_date_str):
    """
    获取单只股票数据
    
    Args:
        symbol: 股票代码
        start_date_str: 开始日期 YYYY-MM-DD
        end_date_str: 结束日期 YYYY-MM-DD
        
    Returns:
        list: 股票数据列表
    """
    # 转换日期
    start_date = datetime.strptime(start_date_str, "%Y-%m-%d").date()
    end_date = datetime.strptime(end_date_str, "%Y-%m-%d").date()
    
    print(f"从掘金获取数据: {symbol}, {start_date} 到 {end_date}")
    
    # 获取数据
    data = fetch_daily_bars_from_juejin(symbol, start_date, end_date)
    
    # 转换数据格式为C++可用的格式
    result = []
    for item in data:
        result.append({
            "symbol": symbol,
            "trade_date": item["trade_date"].strftime("%Y-%m-%d") if hasattr(item["trade_date"], "strftime") else str(item["trade_date"]),
            "open": float(item.get("open", 0.0)),
            "high": float(item.get("high", 0.0)),
            "low": float(item.get("low", 0.0)),
            "close": float(item.get("close", 0.0)),
            "volume": float(item.get("volume", 0.0)),
            "change_pct": float(item.get("change_pct", 0.0))
        })
    
    return result

def main():
    parser = argparse.ArgumentParser(description='从掘金获取单只股票数据')
    parser.add_argument('--symbol', type=str, required=True, help='股票代码')
    parser.add_argument('--start_date', type=str, required=True, help='开始日期 YYYY-MM-DD')
    parser.add_argument('--end_date', type=str, required=True, help='结束日期 YYYY-MM-DD')
    parser.add_argument('--output', type=str, required=True, help='输出文件路径')
    
    args = parser.parse_args()
    
    try:
        # 获取数据
        data = fetch_single_stock_data(args.symbol, args.start_date, args.end_date)
        
        # 保存到文件
        with open(args.output, 'w', encoding='utf-8') as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
        
        print(f"数据已保存到: {args.output}, 共 {len(data)} 条记录")
        
        # 返回成功
        sys.exit(0)
        
    except Exception as e:
        print(f"错误: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()