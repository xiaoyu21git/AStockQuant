/**
 * @file JujinApi_EventBus.cpp
 * @brief 掘金API与EventBus集成实现
 * 
 * 这部分代码需要添加到JujinApi.cpp文件的末尾
 */

#include "include/JujinApi.h"
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace thirdparty {

// ===== EventBus集成实现 =====

// 设置EventBus
void JujinApi::set_event_bus(std::shared_ptr<engine::EventBus> bus) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_bus_ = bus;
    
    if (event_bus_) {
        std::cout << "[JujinApi] EventBus已设置" << std::endl;
    }
}

// 获取EventBus
std::shared_ptr<engine::EventBus> JujinApi::get_event_bus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return event_bus_;
}

// 订阅掘金事件
foundation::Uuid JujinApi::subscribe_jujin_event(
    const std::string& event_type,
    engine::EventFormatHandler handler) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!event_bus_) {
        std::cerr << "[JujinApi] 错误: EventBus未设置" << std::endl;
        return foundation::Uuid();
    }
    
    return event_bus_->subscribe(event_type, handler);
}

// ===== 事件处理函数实现 =====

// 处理Tick数据
void JujinApi::process_tick(Tick* tick) {
    if (!tick) return;
    
    try {
        // 1. 转换为内部MarketData格式
        MarketData md;
        md.symbol = convert_from_gm_symbol(tick->symbol);
        md.open = tick->open;
        md.high = tick->high;
        md.low = tick->low;
        md.close = tick->last_price;
        md.volume = tick->volume;
        md.timestamp = std::chrono::system_clock::from_time_t(tick->created_at);
        
        // 2. 调用用户回调（如果设置）
        if (market_data_callback_) {
            market_data_callback_(md);
        }
        
        // 3. 发布到EventBus（如果设置）
        if (event_bus_) {
            auto event = engine::EventFormat::create_from_strings(
                "market.tick",
                "JUJIN_MARKET_DATA",
                std::chrono::duration_cast<std::chrono::microseconds>(
                    md.timestamp.time_since_epoch()
                ).count()
            );
            
            event.metadata["symbol"] = md.symbol;
            event.metadata["price"] = md.close;
            event.metadata["volume"] = md.volume;
            event.metadata["bid"] = tick->bid_price1;
            event.metadata["ask"] = tick->ask_price1;
            event.metadata["bid_volume"] = tick->bid_volume1;
            event.metadata["ask_volume"] = tick->ask_volume1;
            
            auto result = event_bus_->publish(event, engine::EventPriority::HIGH);
            if (!result.is_ok()) {
                std::cerr << "[JujinApi] 发布Tick事件失败: " << result.message << std::endl;
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[JujinApi] 处理Tick数据错误: " << e.what() << std::endl;
        process_error(-1, std::string("处理Tick数据错误: ") + e.what());
    }
}

// 处理Bar数据
void JujinApi::process_bar(Bar* bar) {
    if (!bar) return;
    
    try {
        // 1. 转换为内部MarketData格式
        MarketData md;
        md.symbol = convert_from_gm_symbol(bar->symbol);
        md.open = bar->open;
        md.high = bar->high;
        md.low = bar->low;
        md.close = bar->close;
        md.volume = bar->volume;
        md.timestamp = std::chrono::system_clock::from_time_t(bar->created_at);
        
        // 2. 调用用户回调（如果设置）
        if (market_data_callback_) {
            market_data_callback_(md);
        }
        
        // 3. 发布到EventBus（如果设置）
        if (event_bus_) {
            auto event = engine::EventFormat::create_from_strings(
                "market.bar",
                "JUJIN_MARKET_DATA",
                std::chrono::duration_cast<std::chrono::microseconds>(
                    md.timestamp.time_since_epoch()
                ).count()
            );
            
            event.metadata["symbol"] = md.symbol;
            event.metadata["open"] = md.open;
            event.metadata["high"] = md.high;
            event.metadata["low"] = md.low;
            event.metadata["close"] = md.close;
            event.metadata["volume"] = md.volume;
            event.metadata["frequency"] = bar->frequency;
            
            auto result = event_bus_->publish(event, engine::EventPriority::NORMAL);
            if (!result.is_ok()) {
                std::cerr << "[JujinApi] 发布Bar事件失败: " << result.message << std::endl;
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[JujinApi] 处理Bar数据错误: " << e.what() << std::endl;
        process_error(-1, std::string("处理Bar数据错误: ") + e.what());
    }
}

// 处理订单状态
void JujinApi::process_order_status(Order* order) {
    if (!order) return;
    
    try {
        // 1. 转换为内部OrderInfo格式
        OrderInfo oi;
        oi.order_id = order->cl_ord_id;
        oi.symbol = convert_from_gm_symbol(order->symbol);
        oi.side = (order->side == 1) ? OrderSide::BUY : OrderSide::SELL;
        oi.type = OrderType::LIMIT; // 根据掘金类型转换
        oi.price = order->price;
        oi.volume = order->volume;
        oi.filled_volume = order->filled_volume;
        oi.status = convert_order_status(order->status);
        oi.timestamp = std::chrono::system_clock::from_time_t(order->created_at);
        
        // 2. 调用用户回调（如果设置）
        if (order_callback_) {
            order_callback_(oi);
        }
        
        // 3. 发布到EventBus（如果设置）
        if (event_bus_) {
            auto event = engine::EventFormat::create_from_strings(
                "order.status",
                "JUJIN_TRADING",
                std::chrono::duration_cast<std::chrono::microseconds>(
                    oi.timestamp.time_since_epoch()
                ).count()
            );
            
            event.metadata["order_id"] = oi.order_id;
            event.metadata["symbol"] = oi.symbol;
            event.metadata["side"] = (oi.side == OrderSide::BUY) ? "BUY" : "SELL";
            event.metadata["price"] = oi.price;
            event.metadata["volume"] = oi.volume;
            event.metadata["filled_volume"] = oi.filled_volume;
            event.metadata["status"] = order_status_to_string(oi.status);
            
            auto result = event_bus_->publish(event, engine::EventPriority::HIGH);
            if (!result.is_ok()) {
                std::cerr << "[JujinApi] 发布订单状态事件失败: " << result.message << std::endl;
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[JujinApi] 处理订单状态错误: " << e.what() << std::endl;
        process_error(-1, std::string("处理订单状态错误: ") + e.what());
    }
}

// 处理持仓信息
void JujinApi::process_position(Position* position) {
    if (!position) return;
    
    try {
        // 1. 转换为内部PositionInfo格式
        PositionInfo pi;
        pi.symbol = convert_from_gm_symbol(position->symbol);
        pi.volume = position->volume;
        pi.cost_price = position->cost_price;
        pi.market_value = position->market_value;
        pi.float_profit = position->float_profit;
        
        // 2. 调用用户回调（如果设置）
        if (position_callback_) {
            position_callback_(pi);
        }
        
        // 3. 发布到EventBus（如果设置）
        if (event_bus_) {
            auto event = engine::EventFormat::create_from_strings(
                "position.update",
                "JUJIN_TRADING",
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count()
            );
            
            event.metadata["symbol"] = pi.symbol;
            event.metadata["volume"] = pi.volume;
            event.metadata["cost_price"] = pi.cost_price;
            event.metadata["market_value"] = pi.market_value;
            event.metadata["float_profit"] = pi.float_profit;
            
            auto result = event_bus_->publish(event, engine::EventPriority::NORMAL);
            if (!result.is_ok()) {
                std::cerr << "[JujinApi] 发布持仓事件失败: " << result.message << std::endl;
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[JujinApi] 处理持仓信息错误: " << e.what() << std::endl;
        process_error(-1, std::string("处理持仓信息错误: ") + e.what());
    }
}

// 处理现金信息
void JujinApi::process_cash(Cash* cash) {
    if (!cash) return;
    
    try {
        // 1. 转换为内部AccountInfo格式
        AccountInfo ai;
        ai.account_id = config_.account_id;
        ai.total_asset = cash->total_asset;
        ai.available_cash = cash->available;
        ai.market_value = cash->market_value;
        ai.float_profit = cash->float_profit;
        
        // 2. 调用用户回调（如果设置）
        if (account_callback_) {
            account_callback_(ai);
        }
        
        // 3. 发布到EventBus（如果设置）
        if (event_bus_) {
            auto event = engine::EventFormat::create_from_strings(
                "account.update",
                "JUJIN_TRADING",
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count()
            );
            
            event.metadata["account_id"] = ai.account_id;
            event.metadata["total_asset"] = ai.total_asset;
            event.metadata["available_cash"] = ai.available_cash;
            event.metadata["market_value"] = ai.market_value;
            event.metadata["float_profit"] = ai.float_profit;
            
            auto result = event_bus_->publish(event, engine::EventPriority::NORMAL);
            if (!result.is_ok()) {
                std::cerr << "[JujinApi] 发布账户事件失败: " << result.message << std::endl;
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[JujinApi] 处理现金信息错误: " << e.what() << std::endl;
        process_error(-1, std::string("处理现金信息错误: ") + e.what());
    }
}

// 处理错误
void JujinApi::process_error(int error_code, const std::string& error_msg) {
    try {
        // 1. 调用用户回调（如果设置）
        if (error_callback_) {
            error_callback_(error_code, error_msg);
        }
        
        // 2. 发布到EventBus（如果设置）
        if (event_bus_) {
            auto event = engine::EventFormat::create_from_strings(
                "jujin.error",
                "JUJIN_SYSTEM",
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count()
            );
            
            event.metadata["error_code"] = error_code;
            event.metadata["error_msg"] = error_msg;
            
            auto result = event_bus_->publish(event, engine::EventPriority::CRITICAL);
            if (!result.is_ok()) {
                std::cerr << "[JujinApi] 发布错误事件失败: " << result.message << std::endl;
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[JujinApi] 处理错误信息错误: " << e.what() << std::endl;
    }
}

// ===== 辅助函数 =====

// 转换订单状态
OrderStatus JujinApi::convert_order_status(int gm_status) {
    switch (gm_status) {
        case 0: return OrderStatus::PENDING;
        case 1: return OrderStatus::SUBMITTED;
        case 2: return OrderStatus::PARTIAL_FILLED;
        case 3: return OrderStatus::FILLED;
        case 4: return OrderStatus::CANCELLED;
        case 5: return OrderStatus::REJECTED;
        default: return OrderStatus::PENDING;
    }
}

// 订单状态转字符串
std::string JujinApi::order_status_to_string(OrderStatus status) {
    switch (status) {
        case OrderStatus::PENDING: return "PENDING";
        case OrderStatus::SUBMITTED: return "SUBMITTED";
        case OrderStatus::PARTIAL_FILLED: return "PARTIAL_FILLED";
        case OrderStatus::FILLED: return "FILLED";
        case OrderStatus::CANCELLED: return "CANCELLED";
        case OrderStatus::REJECTED: return "REJECTED";
        default: return "UNKNOWN";
    }
}

// 时间点转字符串
std::string JujinApi::timepoint_to_string(std::chrono::system_clock::time_point tp) {
    if (tp == std::chrono::system_clock::time_point()) {
        return "";
    }
    
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm;
    localtime_s(&tm, &time_t);
    
    std::stringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// 事件循环
void JujinApi::event_loop() {
    while (running_) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        if (event_queue_.empty()) {
            cv_.wait_for(lock, std::chrono::milliseconds(100));
            continue;
        }
        
        Event event = std::move(event_queue_.front());
        event_queue_.pop();
        lock.unlock();
        
        try {
            switch (event.type) {
                case Event::TICK:
                    process_tick(event.data.tick);
                    break;
                case Event::BAR:
                    process_bar(event.data.bar);
                    break;
                case Event::ORDER:
                    process_order_status(event.data.order);
                    break;
                case Event::POSITION:
                    process_position(event.data.position);
                    break;
                case Event::CASH:
                    process_cash(event.data.cash);
                    break;
                case Event::ERROR:
                    process_error(event.data.error.code, event.data.error.msg);
                    break;
                default:
                    std::cerr << "[JujinApi] 未知事件类型: " << static_cast<int>(event.type) << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[JujinApi] 事件处理错误: " << e.what() << std::endl;
        }
    }
}

} // namespace thirdparty