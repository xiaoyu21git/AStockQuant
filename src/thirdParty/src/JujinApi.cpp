/**
 * @file JujinApi.cpp
 * @brief 掘金API实现
 */

#include "include/JujinApi.h"
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <vector>
#include <string>
#include <memory>

// 掘金SDK头文件
extern "C" {
#include "../../../thirdparty/gmsdk/strategy.h"
#include "../../../thirdparty/gmsdk/gmapi.h"
#include "../../../thirdparty/gmsdk/gmdef.h"
#include "../../../thirdparty/gmsdk/error.h"
}

namespace thirdparty {

// 构造函数
JujinApi::JujinApi() : running_(false) {
    // 初始化回调函数为空
    market_data_callback_ = nullptr;
    order_callback_ = nullptr;
    position_callback_ = nullptr;
    account_callback_ = nullptr;
    error_callback_ = nullptr;
}

// 析构函数
JujinApi::~JujinApi() {
    disconnect();
}

// 初始化API
bool JujinApi::initialize(const ConfigParams& config) {
    config_ = config;
    
    // 验证配置
    if (config_.token.empty()) {
        process_error(-1, "掘金token不能为空");
        return false;
    }
    
    if (config_.platform != PlatformType::JUJIN) {
        process_error(-1, "平台类型不匹配，应为掘金平台");
        return false;
    }
    
    return true;
}

// 连接平台
bool JujinApi::connect() {
    if (connected_) {
        return true;
    }
    
    try {
        // 创建掘金策略实例
        // 注意：这里需要根据掘金SDK的实际API进行调整
        // 假设掘金SDK的Strategy构造函数需要token、策略ID和模式
        const char* strategy_id = config_.extra_params.count("strategy_id") 
            ? config_.extra_params.at("strategy_id").c_str() 
            : "default_strategy";
        
        int mode = 2; // 默认回测模式
        if (config_.extra_params.count("mode")) {
            mode = std::stoi(config_.extra_params.at("mode"));
        }
        
        strategy_ = new Strategy(config_.token.c_str(), strategy_id, mode);
        
        if (!strategy_) {
            process_error(-1, "创建掘金策略实例失败");
            return false;
        }
        
        // 设置回调函数
        strategy_->set_on_tick_callback(on_tick_callback, this);
        strategy_->set_on_bar_callback(on_bar_callback, this);
        strategy_->set_on_order_status_callback(on_order_status_callback, this);
        strategy_->set_on_position_callback(on_position_callback, this);
        strategy_->set_on_cash_callback(on_cash_callback, this);
        strategy_->set_on_error_callback(on_error_callback, this);
        
        // 启动事件处理线程
        running_ = true;
        event_thread_ = std::thread(&JujinApi::event_loop, this);
        
        connected_ = true;
        return true;
        
    } catch (const std::exception& e) {
        process_error(-1, std::string("连接掘金平台失败: ") + e.what());
        return false;
    }
}

// 断开连接
void JujinApi::disconnect() {
    if (!connected_) {
        return;
    }
    
    // 停止事件处理线程
    running_ = false;
    cv_.notify_all();
    
    if (event_thread_.joinable()) {
        event_thread_.join();
    }
    
    // 清理掘金策略实例
    if (strategy_) {
        delete strategy_;
        strategy_ = nullptr;
    }
    
    connected_ = false;
}

// 获取市场数据
std::vector<MarketData> JujinApi::get_market_data(
    const std::string& symbol,
    MarketDataType data_type,
    std::chrono::system_clock::time_point start_time,
    std::chrono::system_clock::time_point end_time) {
    
    std::vector<MarketData> result;
    
    if (!connected_ || !strategy_) {
        process_error(-1, "未连接到掘金平台");
        return result;
    }
    
    try {
        // 转换代码格式
        std::string gm_symbol = convert_to_gm_symbol(symbol);
        std::string frequency = convert_market_data_type(data_type);
        
        // 转换时间格式
        std::string start_str = timepoint_to_string(start_time);
        std::string end_str = timepoint_to_string(end_time);
        
        if (start_str.empty() || end_str.empty()) {
            process_error(-1, "时间格式错误");
            return result;
        }
        
        // 调用掘金SDK获取历史数据
        // 注意：这里需要根据掘金SDK的实际API进行调整
        // 假设掘金SDK有get_history_data函数
        // auto history_data = strategy_->get_history_data(
        //     gm_symbol.c_str(), 
        //     frequency.c_str(),
        //     start_str.c_str(),
        //     end_str.c_str());
        
        // 这里先返回空数据，实际实现需要调用掘金SDK
        // TODO: 实现掘金SDK调用
        
    } catch (const std::exception& e) {
        process_error(-1, std::string("获取市场数据失败: ") + e.what());
    }
    
    return result;
}

// 获取实时行情
std::vector<MarketData> JujinApi::get_realtime_quotes(
    const std::vector<std::string>& symbols) {
    
    std::vector<MarketData> result;
    
    if (!connected_ || !strategy_) {
        process_error(-1, "未连接到掘金平台");
        return result;
    }
    
    try {
        // 转换代码格式
        std::vector<std::string> gm_symbols;
        for (const auto& symbol : symbols) {
            gm_symbols.push_back(convert_to_gm_symbol(symbol));
        }
        
        // 调用掘金SDK获取实时行情
        // 注意：这里需要根据掘金SDK的实际API进行调整
        // 假设掘金SDK有get_current_quotes函数
        // auto quotes = strategy_->get_current_quotes(gm_symbols);
        
        // 这里先返回空数据，实际实现需要调用掘金SDK
        // TODO: 实现掘金SDK调用
        
    } catch (const std::exception& e) {
        process_error(-1, std::string("获取实时行情失败: ") + e.what());
    }
    
    return result;
}

// 订阅行情
bool JujinApi::subscribe_market_data(
    const std::vector<std::string>& symbols,
    MarketDataType data_type,
    MarketDataCallback callback) {
    
    if (!connected_ || !strategy_) {
        process_error(-1, "未连接到掘金平台");
        return false;
    }
    
    try {
        // 设置回调函数
        market_data_callback_ = callback;
        
        // 转换代码格式和频率
        for (const auto& symbol : symbols) {
            std::string gm_symbol = convert_to_gm_symbol(symbol);
            std::string frequency = convert_market_data_type(data_type);
            
            // 记录订阅
            subscriptions_[gm_symbol].push_back(data_type);
            
            // 调用掘金SDK订阅
            // 注意：这里需要根据掘金SDK的实际API进行调整
            // strategy_->subscribe(gm_symbol.c_str(), frequency.c_str());
        }
        
        return true;
        
    } catch (const std::exception& e) {
        process_error(-1, std::string("订阅行情失败: ") + e.what());
        return false;
    }
}

// 取消订阅
bool JujinApi::unsubscribe_market_data(
    const std::vector<std::string>& symbols,
    MarketDataType data_type) {
    
    if (!connected_ || !strategy_) {
        process_error(-1, "未连接到掘金平台");
        return false;
    }
    
    try {
        for (const auto& symbol : symbols) {
            std::string gm_symbol = convert_to_gm_symbol(symbol);
            
            // 从订阅记录中移除
            auto it = subscriptions_.find(gm_symbol);
            if (it != subscriptions_.end()) {
                auto& types = it->second;
                types.erase(std::remove(types.begin(), types.end(), data_type), types.end());
                if (types.empty()) {
                    subscriptions_.erase(it);
                }
            }
            
            // 调用掘金SDK取消订阅
            // 注意：这里需要根据掘金SDK的实际API进行调整
            // strategy_->unsubscribe(gm_symbol.c_str(), convert_market_data_type(data_type).c_str());
        }
        
        return true;
        
    } catch (const std::exception& e) {
        process_error(-1, std::string("取消订阅失败: ") + e.what());
        return false;
    }
}

// 获取账户信息
AccountInfo JujinApi::get_account_info() {
    AccountInfo info;
    
    if (!connected_ || !strategy_) {
        process_error(-1, "未连接到掘金平台");
        return info;
    }
    
    try {
        // 调用掘金SDK获取账户信息
        // 注意：这里需要根据掘金SDK的实际API进行调整
        // auto cash = strategy_->get_cash();
        
        // 这里先返回空数据，实际实现需要调用掘金SDK
        // TODO: 实现掘金SDK调用
        
        info.account_id = config_.account_id;
        
    } catch (const std::exception& e) {
        process_error(-1, std::string("获取账户信息失败: ") + e.what());
    }
    
    return info;
}

// 获取持仓信息
std::vector<PositionInfo> JujinApi::get_positions() {
    std::vector<PositionInfo> positions;
    
    if (!connected_ || !strategy_) {
        process_error(-1, "未连接到掘金平台");
        return positions;
    }
    
    try {
        // 调用掘金SDK获取持仓信息
        // 注意：这里需要根据掘金SDK的实际API进行调整
        // auto gm_positions = strategy_->get_position();
        
        // 这里先返回空数据，实际实现需要调用掘金SDK
        // TODO: 实现掘金SDK调用
        
    } catch (const std::exception& e) {
        process_error(-1, std::string("获取持仓信息失败: ") + e.what());
    }
    
    return positions;
}

// 获取订单列表
std::vector<OrderInfo> JujinApi::get_orders(
    const std::string& symbol,
    std::chrono::system_clock::time_point start_time,
    std::chrono::system_clock::time_point end_time) {
    
    std::vector<OrderInfo> orders;
    
    if (!connected_ || !strategy_) {
        process_error(-1, "未连接到掘金平台");
        return orders;
    }
    
    try {
        // 调用掘金SDK获取订单列表
        // 注意：这里需要根据掘金SDK的实际API进行调整
        // auto gm_orders = strategy_->get_orders();
        
        // 这里先返回空数据，实际实现需要调用掘金SDK
        // TODO: 实现掘金SDK调用
        
    } catch (const std::exception& e) {
        process_error(-1, std::string("获取订单列表失败: ") + e.what());
    }
    
    return orders;
}

// 下单
std::string JujinApi::place_order(
    const std::string& symbol,
    OrderSide side,
    OrderType type,
    double price,
    double volume) {
    
    if (!connected_ || !strategy_) {
        process_error(-1, "未连接到掘金平台");
        return "";
    }
    
    try {
        // 转换代码格式
        std::string gm_symbol = convert_to_gm_symbol(symbol);
        
        // 转换订单方向
        int gm_side = (side == OrderSide::BUY) ? 1 : 2; // 假设1为买入，2为卖出
        
        // 转换订单类型
        int gm_type = 1; // 默认限价单
        if (type == OrderType::MARKET) {
            gm_type = 2; // 市价单
        } else if (type == OrderType::STOP) {
            gm_type = 3; // 止损单
        } else if (type == OrderType::STOP_LIMIT) {
            gm_type = 4; // 止损限价单
        }
        
        // 调用掘金SDK下单
        // 注意：这里需要根据掘金SDK的实际API进行调整
        // Order order = strategy_->order_volume(
        //     gm_symbol.c_str(),
        //     volume,
        //     gm_side,
        //     gm_type,
        //     1, // 开仓
        //     price);
        
        // 返回订单ID
        // return order.cl_ord_id;
        
        // 这里先返回空字符串，实际实现需要调用掘金SDK
        // TODO: 实现掘金SDK调用
        return "simulated_order_id";
        
    } catch (const std::exception& e) {
        process_error(-1, std::string("下单失败: ") + e.what());
        return "";
    }
}

// 撤单
bool JujinApi::cancel_order(const std::string& order_id) {
    if (!connected_ || !strategy_) {
        process_error(-1, "未连接到掘金平台");
        return false;
    }
    
    try {
        // 调用掘金SDK撤单
        // 注意：这里需要根据掘金SDK的实际API进行调整
        // int result = strategy_->order_cancel(order_id.c_str());
        // return result == 0;
        
        // 这里先返回true，实际实现需要调用掘金SDK
        // TODO: 实现掘金SDK调用
        return true;
        
    } catch (const std::exception& e) {
        process_error(-1, std::string("撤单失败: ") + e.what());
        return false;
    }
}

// 设置回调函数
void JujinApi::set_order_callback(OrderCallback callback) {
    order_callback_ = callback;
}

void JujinApi::set_position_callback(PositionCallback callback) {
    position_callback_ = callback;
}

void JujinApi::set_account_callback(AccountCallback callback) {
    account_callback_ = callback;
}

void JujinApi::set_error_callback(ErrorCallback callback) {
    error_callback_ = callback;
}

// 获取平台信息
PlatformType JujinApi::get_platform_type() const {
    return PlatformType::JUJIN;
}

std::string JujinApi::get_platform_name() const {
    return "掘金量化";
}

bool JujinApi::is_connected() const {
    return connected_;
}

// 内部辅助函数
std::string JujinApi::convert_to_gm_symbol(const std::string& symbol) const {
    return convert_symbol_format(symbol, PlatformType::JUJIN);
}

std::string JujinApi::convert_from_gm_symbol(const std::string& gm_symbol) const {
    return convert_to_internal_format(gm_symbol, PlatformType::JUJIN);
}

std::string JujinApi::convert_market_data_type(MarketDataType data_type) const {
    switch (data_type) {
        case MarketDataType::TICK:
            return "tick";
        case MarketDataType::BAR_1M:
            return "60s";
        case MarketDataType::BAR_5M:
            return "300s";
        case MarketDataType::BAR_15M:
            return "900s";
        case MarketDataType::BAR_30M:
            return "1800s";
        case MarketDataType::BAR_60M:
            return "3600s";
        case MarketDataType::BAR_1D:
            return "1d";
        case MarketDataType::BAR_1W:
            return "1w";
        case MarketDataType::BAR_1MTH:
            return "1month";
        default:
            return "1d";
    }
}

MarketDataType JujinApi::convert_from_gm_frequency(const std::string& frequency) const {
    if (frequency == "tick") return MarketDataType::TICK;
    if (frequency == "60s") return MarketDataType::BAR_1M;
    if (frequency == "300s") return MarketDataType::BAR_5M;
    if (frequency == "900s") return MarketDataType::BAR_15M;
    if (frequency == "1800s") return MarketDataType::BAR_30M;
    if (frequency == "3600s") return MarketDataType::BAR_60M;
    if (frequency == "1d") return MarketDataType::BAR_1D;
    if (frequency == "1w") return MarketDataType::BAR_1W;
    if (frequency == "1month") return MarketDataType::BAR_1MTH;
    return MarketDataType::BAR_1D;
}

// 掘金回调处理（静态函数）
void JujinApi::on_tick_callback(Tick* tick, void* user_data) {
    JujinApi* api = static_cast<JujinApi*>(user_data);
    if (api) {
        api->process_tick(tick);
    }
}

void JujinApi::on_bar_callback(Bar* bar, void* user_data) {
    JujinApi* api = static_cast<JujinApi*>(user_data);
    if (api) {
        api->process_bar(bar);
    }
}

void JujinApi::on_order_status_callback(Order* order, void* user_data) {
    JujinApi* api = static_cast<JujinApi*>(user_data);
    if (api) {
        api->process_order_status(order);
    }
}

void JujinApi::on_position_callback(Position* position, void* user_data) {
    JujinApi* api = static_cast<JujinApi*>(user_data);
    if (api) {
        api->process_position(position);
    }
}

void JujinApi::on_cash_callback(Cash* cash, void* user_data) {
    JujinApi* api = static_cast<JujinApi*>(user_data);
    if (