"""
完整数据流集成测试
验证：数据获取 → EventBus → 策略执行 → 信号生成

测试场景：
1. DataProvider获取真实市场数据
2. 通过EventBus分发给策略
3. 策略生成交易信号
4. 验证完整链路的数据流转
"""

import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import time
import pandas as pd
from datetime import datetime, timedelta
from typing import List, Dict, Any

# 导入核心模块
from astock_engine.core.eventbus_simple import EventBus
from astock_engine.data.providers.futures_provider import FuturesDataProvider
from astock_engine.strategies.base_strategy import BaseStrategy


class IntegrationTestStrategy(BaseStrategy):
    """集成测试策略"""
    
    def __init__(self, name: str = "IntegrationTest"):
        super().__init__(name)
        self.signals_generated = []
        self.bars_processed = 0
    
    def generate_signals(self):
        """生成交易信号（BaseStrategy要求的抽象方法）"""
        pass
        
    def on_bar(self, bar):
        """处理Bar数据"""
        # 如果是Event对象，提取data
        if hasattr(bar, 'data'):
            bar_data = bar.data
        else:
            bar_data = bar
            
        self.bars_processed += 1
        
        # 简单策略逻辑：价格突破20日均线
        if self.bars_processed < 20:
            return
            
        close = bar_data.get('close', 0)
        
        # 模拟均线计算
        if close > 4000:  # 简单阈值
            signal = {
                'type': 'buy',
                'symbol': bar_data['symbol'],
                'price': close,
                'quantity': 1,
                'reason': '价格突破阈值',
                'timestamp': time.time(),
                'strategy': self.name
            }
            self.signals_generated.append(signal)
            print(f"  [策略] 生成信号: {signal['type']} {signal['symbol']} @{signal['price']}")
    
    def on_tick(self, tick: Dict[str, Any]):
        """处理Tick数据"""
        pass


def test_complete_dataflow():
    """测试完整数据流"""
    
    print("=" * 70)
    print("完整数据流集成测试")
    print("=" * 70)
    
    # ========== 1. 初始化组件 ==========
    print("\n[1/5] 初始化组件...")
    
    # EventBus
    eventbus = EventBus()
    print("  ✓ EventBus初始化完成")
    
    # DataProvider
    provider = FuturesDataProvider()
    print("  ✓ FuturesDataProvider初始化完成")
    
    # Strategy
    strategy = IntegrationTestStrategy()
    print("  ✓ Strategy初始化完成")
    
    # ========== 2. 订阅事件 ==========
    print("\n[2/5] 配置事件订阅...")
    
    # 策略订阅市场数据事件
    sub_id = eventbus.subscribe('market.bar.daily', strategy.on_bar)
    print(f"  ✓ 策略订阅 'market.bar.daily' (ID: {sub_id[:8]}...)")
    
    # ========== 3. 获取市场数据 ==========
    print("\n[3/5] 获取市场数据...")
    
    try:
        # 获取螺纹钢主力合约数据
        symbol = 'RB'
        end_date = datetime.now().strftime('%Y%m%d')
        start_date = (datetime.now() - timedelta(days=30)).strftime('%Y%m%d')
        
        print(f"  正在获取 {symbol} 数据 ({start_date} ~ {end_date})...")
        df = provider.get_price_data(
            symbol=symbol,
            start_date=start_date,
            end_date=end_date,
            frequency='daily'
        )
        
        if df.empty:
            print(f"  ⚠ 未获取到数据，使用模拟数据")
            # 创建模拟数据
            dates = pd.date_range(start='2024-01-01', periods=30, freq='D')
            df = pd.DataFrame({
                'symbol': [f'{symbol}2405'] * 30,
                'date': dates,
                'open': [4000 + i * 10 for i in range(30)],
                'high': [4050 + i * 10 for i in range(30)],
                'low': [3950 + i * 10 for i in range(30)],
                'close': [4020 + i * 10 for i in range(30)],
                'volume': [100000] * 30
            })
        
        print(f"  ✓ 获取到 {len(df)} 条数据")
        print(f"    数据范围: {df.iloc[0]['date']} ~ {df.iloc[-1]['date']}")
        print(f"    价格范围: {df['close'].min():.2f} ~ {df['close'].max():.2f}")
        
    except Exception as e:
        print(f"  ⚠ 数据获取异常: {e}")
        print(f"    使用模拟数据继续测试")
        
        # 模拟数据
        dates = pd.date_range(start='2024-01-01', periods=30, freq='D')
        df = pd.DataFrame({
            'symbol': ['RB2405'] * 30,
            'date': dates,
            'open': [4000 + i * 10 for i in range(30)],
            'high': [4050 + i * 10 for i in range(30)],
            'low': [3950 + i * 10 for i in range(30)],
            'close': [4020 + i * 10 for i in range(30)],
            'volume': [100000] * 30
        })
        print(f"  ✓ 生成 {len(df)} 条模拟数据")
    
    # ========== 4. 数据流转测试 ==========
    print("\n[4/5] 数据流转测试...")
    
    events_published = 0
    start_time = time.time()
    
    # 将数据逐条发布到EventBus
    for idx, row in df.iterrows():
        bar_data = {
            'symbol': row['symbol'],
            'date': row['date'],
            'open': float(row['open']),
            'high': float(row['high']),
            'low': float(row['low']),
            'close': float(row['close']),
            'volume': float(row['volume'])
        }
        
        # 发布事件
        handlers_called = eventbus.publish('market.bar.daily', bar_data)
        events_published += 1
        
        if idx < 3:  # 只打印前3条
            print(f"  事件 #{events_published}: {bar_data['symbol']} "
                  f"收盘价={bar_data['close']:.2f} → {handlers_called}个处理器")
    
    elapsed_time = time.time() - start_time
    
    print(f"\n  ✓ 发布 {events_published} 个事件")
    print(f"    耗时: {elapsed_time*1000:.2f}ms")
    print(f"    速度: {events_published/elapsed_time:.0f} events/sec")
    
    # ========== 5. 结果验证 ==========
    print("\n[5/5] 结果验证...")
    
    print(f"\n  策略执行统计:")
    print(f"    处理的Bar数: {strategy.bars_processed}")
    print(f"    生成的信号数: {len(strategy.signals_generated)}")
    
    if strategy.signals_generated:
        print(f"\n  信号详情:")
        for i, signal in enumerate(strategy.signals_generated, 1):
            print(f"    [{i}] {signal['type'].upper()} {signal['symbol']} "
                  f"@{signal['price']:.2f} x{signal['quantity']} - {signal.get('reason', '')}")
    
    # EventBus统计
    stats = eventbus.get_statistics()
    print(f"\n  EventBus统计:")
    print(f"    总事件数: {stats['total_events']}")
    print(f"    总订阅数: {stats['total_subscribers']}")
    
    # ========== 验证断言 ==========
    print("\n" + "=" * 70)
    print("验证结果")
    print("=" * 70)
    
    assertions = [
        (strategy.bars_processed > 0, "策略至少处理了一个Bar"),
        (events_published == len(df), f"发布事件数 ({events_published}) == 数据行数 ({len(df)})"),
        (stats['total_events'] >= events_published, "EventBus统计正确"),
        (elapsed_time < 10, f"性能可接受 ({elapsed_time:.2f}s < 10s)"),
    ]
    
    all_passed = True
    for passed, description in assertions:
        status = "✓" if passed else "✗"
        print(f"  [{status}] {description}")
        if not passed:
            all_passed = False
    
    print("\n" + "=" * 70)
    if all_passed:
        print("✓ 完整数据流集成测试通过")
    else:
        print("✗ 部分测试失败")
    print("=" * 70)
    
    # ========== 数据流总结 ==========
    print("\n数据流验证:")
    print(f"""
    ┌─────────────────┐
    │  DataProvider   │  ← 获取了 {len(df)} 条数据
    └────────┬────────┘
             │ DataFrame
             ↓
    ┌─────────────────┐
    │    EventBus     │  ← 发布了 {events_published} 个事件
    └────────┬────────┘       速度: {events_published/elapsed_time:.0f} events/sec
             │ Event
             ↓
    ┌─────────────────┐
    │    Strategy     │  ← 处理了 {strategy.bars_processed} 个Bar
    └────────┬────────┘       生成了 {len(strategy.signals_generated)} 个信号
             │ Signal
             ↓
    ┌─────────────────┐
    │  OrderManager   │  (未来实现)
    └─────────────────┘
    """)
    
    return all_passed


if __name__ == "__main__":
    try:
        success = test_complete_dataflow()
        sys.exit(0 if success else 1)
    except Exception as e:
        print(f"\n✗ 测试异常: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
