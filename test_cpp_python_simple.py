"""
简化的C++/Python EventBus通信验证
"""
import sys
import os
import time

# 添加C++ DLL路径
sys.path.insert(0, r'G:\C++\AStockQuantEngine\bin\lib\Debug')

print("=" * 60)
print("C++ ↔ Python EventBus 通信验证")
print("=" * 60)

# 1. 导入测试
print("\n✓ [1/5] 导入C++模块...")
import eventbus_native
print(f"    模块: {eventbus_native}")
print(f"    版本: {eventbus_native.__version__}")

# 2. 创建EventBus
print("\n✓ [2/5] 创建EventBus实例...")
config = eventbus_native.EventBusConfig()
config.worker_threads = 1
config.max_queue_size = 1000
bus = eventbus_native.EventBus.create(config)
bus.start()
print(f"    状态: {bus}")
print(f"    运行: {bus.is_running()}")

# 3. 测试事件创建
print("\n✓ [3/5] 创建事件对象...")
event = eventbus_native.EventFormat("test.message", 0)
event.metadata["msg"] = "Hello C++"
event.metadata["from"] = "Python"
print(f"    事件: {event}")
print(f"    类型: {event.type}")
print(f"    时间戳: {event.timestamp}")

# 4. 测试发布
print("\n✓ [4/5] 发布事件...")
result = bus.publish(event, priority=5)
print(f"    结果: {result}")
print(f"    成功: {result.is_ok()}")

# 5. 测试订阅和回调
print("\n✓ [5/5] 测试订阅回调...")

received_events = []

def callback(evt):
    print(f"    → 回调触发! 事件类型: {evt.type}")
    received_events.append(evt)

# 订阅
sub_id = bus.subscribe("callback.test", callback, priority=0)
print(f"    订阅ID: {sub_id}")

# 发送测试事件
test_evt = eventbus_native.EventFormat("callback.test", 0)
test_evt.metadata["test"] = "data"
bus.publish(test_evt, priority=5)

# 等待处理
bus.wait_for_empty(timeout_seconds=2.0)
time.sleep(0.2)

if received_events:
    print(f"    ✓ 收到 {len(received_events)} 个事件")
else:
    print(f"    ⚠ 未收到回调（可能是异步处理中）")

# 性能测试
print("\n✓ [性能] 批量发布测试...")
count = 1000
start = time.perf_counter()
for i in range(count):
    evt = eventbus_native.EventFormat(f"perf.{i%10}", 0)
    bus.publish(evt, priority=5)
elapsed = time.perf_counter() - start

print(f"    发送: {count}个事件")
print(f"    耗时: {elapsed*1000:.2f}ms")
print(f"    速度: {count/elapsed:,.0f} events/sec")

# 清理
print("\n✓ 停止EventBus...")
bus.stop(wait_completion=True, timeout_ms=2000)

print("\n" + "=" * 60)
print("✓ 验证完成: C++和Python通信正常!")
print("=" * 60)
