#!/usr/bin/env python
"""
掘金事件监听器 - 通过EventBus接收C++数据获取请求

功能：
1. 订阅EventBus中的"data_fetch_request"事件
2. 调用掘金API获取数据
3. 发布"data_fetch_response"事件返回结果

使用方法：
python tools/juejin_event_listener.py

注意：需要eventbus_native模块可用
"""

import sys
import os
import json
import logging
from datetime import datetime
from typing import Dict, Any, Optional

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# 添加项目根目录到sys.path
project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, project_root)

try:
    # 导入现有的EventBus系统
    from astock_engine.core.eventbus_simple import EventBus, get_global_bus, Event, EventType
    from astock_engine.core.eventbus_bridge_cpp_python import CppToPythonEventBridge
    
    EVENTBUS_AVAILABLE = True
    logger.info("EventBus导入成功")
except ImportError as e:
    EVENTBUS_AVAILABLE = False
    logger.error(f"无法导入EventBus: {e}")
    sys.exit(1)

try:
    # 导入掘金API
    from tools.import_from_juejin import fetch_daily_bars_from_juejin
    JUJIN_AVAILABLE = True
    logger.info("掘金API导入成功")
except ImportError as e:
    JUJIN_AVAILABLE = False
    logger.error(f"无法导入掘金API: {e}")
    sys.exit(1)


class JuejinEventListener:
    """掘金事件监听器"""
    
    def __init__(self):
        self.event_bus = get_global_bus()
        self.bridge = None
        self.running = False
        
        # 订阅事件
        self._subscribe_events()
        
        logger.info("掘金事件监听器初始化完成")
    
    def _subscribe_events(self):
        """订阅相关事件"""
        # 订阅Python EventBus事件
        self.event_bus.subscribe("data_fetch_request", self._on_data_fetch_request)
        
        # 如果需要从C++ EventBus接收事件，可以使用桥接
        try:
            self.bridge = CppToPythonEventBridge()
            # C++事件会通过桥接转发到Python EventBus，所以不需要额外订阅
            logger.info("C++→Python EventBus桥接初始化成功")
        except Exception as e:
            logger.warning(f"C++→Python EventBus桥接初始化失败: {e}")
    
    def _on_data_fetch_request(self, event: Event):
        """处理数据获取请求事件"""
        try:
            logger.info(f"收到数据获取请求: {event.type}")
            
            # 解析事件数据
            data = event.data
            request_id = data.get('request_id', '')
            symbol = data.get('symbol', '')
            start_date = data.get('start_date', '')
            end_date = data.get('end_date', '')
            
            if not all([request_id, symbol, start_date, end_date]):
                logger.error("请求数据不完整")
                self._send_error_response(request_id, "请求数据不完整")
                return
            
            logger.info(f"处理请求 {request_id}: {symbol} {start_date} 到 {end_date}")
            
            # 发送进度更新
            self._send_progress(request_id, 10, "开始从掘金获取数据...")
            
            # 调用掘金API获取数据
            try:
                # 解析日期
                start = datetime.strptime(start_date, "%Y-%m-%d").date()
                end = datetime.strptime(end_date, "%Y-%m-%d").date()
                
                self._send_progress(request_id, 30, f"正在获取{symbol}的数据...")
                
                # 获取数据
                raw_data = fetch_daily_bars_from_juejin(symbol, start, end)
                
                self._send_progress(request_id, 70, "处理获取到的数据...")
                
                # 转换数据格式
                result_data = []
                for item in raw_data:
                    result_data.append({
                        'symbol': symbol,
                        'date': item['trade_date'].strftime('%Y-%m-%d') if hasattr(item['trade_date'], 'strftime') else str(item['trade_date']),
                        'open': float(item.get('open', 0.0)),
                        'high': float(item.get('high', 0.0)),
                        'low': float(item.get('low', 0.0)),
                        'close': float(item.get('close', 0.0)),
                        'volume': float(item.get('volume', 0.0)),
                        'change': float(item.get('change_pct', 0.0))
                    })
                
                self._send_progress(request_id, 90, "数据转换完成...")
                
                # 发送成功响应
                self._send_success_response(request_id, result_data, f"成功获取{len(result_data)}条数据")
                
                logger.info(f"请求 {request_id} 处理完成: 获取到 {len(result_data)} 条数据")
                
            except Exception as e:
                logger.error(f"获取掘金数据失败: {e}", exc_info=True)
                self._send_error_response(request_id, f"获取掘金数据失败: {str(e)}")
                
        except Exception as e:
            logger.error(f"处理数据获取请求失败: {e}", exc_info=True)
            self._send_error_response('unknown', f"处理请求失败: {str(e)}")
    
    def _send_progress(self, request_id: str, progress: int, message: str):
        """发送进度更新事件"""
        try:
            progress_event = Event(
                type="data_fetch_progress",
                data={
                    'request_id': request_id,
                    'progress': progress,
                    'message': message,
                    'timestamp': datetime.now().isoformat()
                }
            )
            self.event_bus.publish(progress_event)
        except Exception as e:
            logger.error(f"发送进度更新失败: {e}")
    
    def _send_success_response(self, request_id: str, data: list, message: str = ""):
        """发送成功响应事件"""
        try:
            response_event = Event(
                type="data_fetch_response",
                data={
                    'request_id': request_id,
                    'status': 'success',
                    'data': data,
                    'message': message,
                    'count': len(data),
                    'timestamp': datetime.now().isoformat()
                }
            )
            self.event_bus.publish(response_event)
        except Exception as e:
            logger.error(f"发送成功响应失败: {e}")
    
    def _send_error_response(self, request_id: str, error_message: str):
        """发送错误响应事件"""
        try:
            error_event = Event(
                type="data_fetch_error",
                data={
                    'request_id': request_id,
                    'error_message': error_message,
                    'timestamp': datetime.now().isoformat()
                }
            )
            self.event_bus.publish(error_event)
        except Exception as e:
            logger.error(f"发送错误响应失败: {e}")
    
    def start(self):
        """启动监听器"""
        if self.running:
            logger.warning("监听器已经在运行")
            return
        
        self.running = True
        
        # 启动C++→Python桥接（如果可用）
        if self.bridge:
            try:
                # 订阅C++事件类型（这些事件会被桥接转发到Python EventBus）
                self.bridge.start(["data_fetch_request"])
                logger.info("C++→Python EventBus桥接已启动")
            except Exception as e:
                logger.warning(f"启动C++→Python桥接失败: {e}")
        
        logger.info("掘金事件监听器已启动，等待事件...")
        
        # 保持运行
        try:
            import time
            while self.running:
                time.sleep(1)
        except KeyboardInterrupt:
            logger.info("接收到中断信号，正在停止...")
            self.stop()
    
    def stop(self):
        """停止监听器"""
        self.running = False
        if self.bridge:
            try:
                self.bridge.stop()
            except:
                pass
        logger.info("掘金事件监听器已停止")


def main():
    """主函数"""
    print("=" * 60)
    print("掘金事件监听器 - Python端")
    print("=" * 60)
    print(f"EventBus: {'可用' if EVENTBUS_AVAILABLE else '不可用'}")
    print(f"掘金API: {'可用' if JUJIN_AVAILABLE else '不可用'}")
    print("=" * 60)
    
    if not EVENTBUS_AVAILABLE or not JUJIN_AVAILABLE:
        print("错误: 必要的依赖不可用")
        return 1
    
    # 创建并启动监听器
    listener = JuejinEventListener()
    
    try:
        listener.start()
    except KeyboardInterrupt:
        print("\n监听器已停止")
    except Exception as e:
        logger.error(f"监听器运行错误: {e}", exc_info=True)
        return 1
    
    return 0


if __name__ == '__main__':
    sys.exit(main())