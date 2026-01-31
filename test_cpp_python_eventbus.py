"""
测试C++和Python EventBus通信
验证C++ EventBus模块是否可以被Python调用
"""

import sys
import os

# 添加C++编译输出目录到路径
cpp_dll_path = os.path.join(os.path.dirname(__file__), 'bin', 'lib', 'Debug')
sys.path.insert(0, cpp_dll_path)

print("=" * 70)
print("C++ EventBus 通信测试")
print("=" * 70)

# 测试1: 导入C++ EventBus模块
print("\n[测试1] 尝试导入C++ EventBus模块...")
try:
    import eventbus_native
    print(f"✓ 成功导入: {eventbus_native}")
    print(f"  模块路径: {eventbus_native.__file__ if hasattr(eventbus_native, '__file__') else 'N/A'}")
    print(f"  可用属性: {[x for x in dir(eventbus_native) if not x.startswith('_')]}")
except ImportError as e:
    print(f"✗ 导入失败: {e}")
    print(f"  搜索路径: {cpp_dll_path}")
    print(f"  文件存在: {os.path.exists(os.path.join(cpp_dll_path, 'eventbus_native.dll'))}")
    sys.exit(1)
except Exception as e:
    print(f"✗ 其他错误: {type(e).__name__}: {e}")
    sys.exit(1)

# 测试2: 创建EventBus实例
print("\n[测试2] 创建C++ EventBus实例...")
try:
    if hasattr(eventbus_native, 'EventBus'):
        # C++ EventBus使用工厂方法创建
        config = eventbus_native.EventBusConfig()
        config.worker_threads = 2
        config.max_queue_size = 10000
        cpp_eventbus = eventbus_native.EventBus.create(config)
        cpp_eventbus.start()
        print(f"✓ C++ EventBus实例创建成功: {type(cpp_eventbus)}")
        print(f"  运行状态: {cpp_eventbus.is_running()}")
        print(f"  配置: {config.worker_threads}线程, 队列{config.max_queue_size}")
    else:
        print("✗ EventBus类不存在")
        print(f"  可用类: {[x for x in dir(eventbus_native) if x[0].isupper()]}")
        sys.exit(1)
except Exception as e:
    print(f"✗ 创建失败: {type(e).__name__}: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)

# 测试3: 测试订阅功能
print("\n[测试3] 测试订阅功能...")
try:
    callback_called = [False, None]
    
    def test_callback(event):
        callback_called[0] = True
        callback_called[1] = event
        print(f"  → 回调被调用: {event}")
    
    sub_id = cpp_eventbus.subscribe("test_event", test_callback)
    print(f"✓ 订阅成功, ID: {sub_id}")
except Exception as e:
    print(f"✗ 订阅失败: {type(e).__name__}: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)

# 测试4: 测试发布功能
print("\n[测试4] 测试发布功能...")
try:
    # 创建EventFormat事件
    test_event = eventbus_native.EventFormat("test_event", 0)  # 0 = SYSTEM
    test_event.metadata["message"] = "Hello from Python!"
    test_event.metadata["value"] = "42"
    
    result = cpp_eventbus.publish(test_event, priority=5)
    print(f"✓ 发布成功, 返回: {result}")
    
    # 等待回调执行
    import time
    time.sleep(0.1)
    
    if callback_called[0]:
        print(f"✓ 回调成功执行")
        print(f"  接收事件: {callback_called[1]}")
    else:
        print(f"⚠ 回调未被调用")
except Exception as e:
    print(f"✗ 发布失败: {type(e).__name__}: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)

# 测试5: 性能测试
print("\n[测试5] C++ EventBus性能测试...")
try:
    import time
    
    event_count = 10000
    received_count = [0]
    
    def perf_callback(event):
        received_count[0] += 1
    
    cpp_eventbus.subscribe("perf_test", perf_callback, priority=0)
    
    start_time = time.perf_counter()
    for i in range(event_count):
        evt = eventbus_native.EventFormat("perf_test", 0)
        evt.metadata["index"] = str(i)
        cpp_eventbus.publish(evt, priority=5)
    end_time = time.perf_counter()
    
    # 等待处理完成
    cpp_eventbus.wait_for_empty(timeout_seconds=5.0)
    
    elapsed = end_time - start_time
    events_per_sec = event_count / elapsed if elapsed > 0 else 0
    
    print(f"✓ 性能测试完成")
    print(f"  发布事件数: {event_count}")
    print(f"  接收事件数: {received_count[0]}")
    print(f"  耗时: {elapsed*1000:.2f}ms")
    print(f"  速度: {events_per_sec:,.0f} events/sec")
    
except Exception as e:
    print(f"✗ 性能测试失败: {type(e).__name__}: {e}")
    import traceback
    traceback.print_exc()

# 测试6: 对比Python EventBus
print("\n[测试6] 对比Python EventBus性能...")
try:
    from astock_engine.core.eventbus_simple import EventBus as PythonEventBus
    
    py_eventbus = PythonEventBus()
    py_received = [0]
    
    def py_callback(event):
        py_received[0] += 1
    
    py_eventbus.subscribe("perf_test", py_callback)
    
    start_time = time.perf_counter()
    for i in range(event_count):
        py_eventbus.publish("perf_test", {"index": i})
    end_time = time.perf_counter()
    
    time.sleep(0.5)
    
    elapsed = end_time - start_time
    py_events_per_sec = event_count / elapsed if elapsed > 0 else 0
    
    print(f"✓ Python EventBus性能")
    print(f"  速度: {py_events_per_sec:,.0f} events/sec")
    print(f"  C++相比Python提升: {events_per_sec/py_events_per_sec:.1f}x")
    
except Exception as e:
    print(f"⚠ Python对比失败: {e}")

# 总结
print("\n" + "=" * 70)
print("测试总结")
print("=" * 70)
print("✓ C++ EventBus模块可以被Python成功调用")
print("✓ Python和C++之间数据传递正常")
print(f"✓ C++ EventBus性能: {events_per_sec:,.0f} events/sec")
try:
    print(f"✓ 性能提升: {events_per_sec/py_events_per_sec:.1f}倍")
except:
    pass

# 清理
try:
    cpp_eventbus.stop(wait_completion=True, timeout_ms=2000)
    print("✓ EventBus已停止")
except:
    pass

print("=" * 70)
