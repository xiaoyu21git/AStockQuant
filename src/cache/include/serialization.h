// serialization.h
// 序列化工具 - 使用Foundation JSON接口

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <type_traits>

// 使用Foundation的JSON接口
#include "../../foundation/include/foundation/json/json_facade.h"

namespace AStockQuantEngine {
namespace Cache {

// 量化数据类型定义
struct KLine {
    std::string symbol;
    std::string trade_date;
    double open{0.0};
    double high{0.0};
    double low{0.0};
    double close{0.0};
    double volume{0.0};
    double change_pct{0.0};
};

struct Tick {
    std::string symbol;
    std::string timestamp;
    double price{0.0};
    double volume{0.0};
    double amount{0.0};
    int direction{0};  // 0:未知, 1:买, 2:卖
};

struct Factor {
    std::string symbol;
    std::string date;
    std::string factor_name;
    double value{0.0};
};

// 序列化工具类 - 使用Foundation JSON接口
class Serializer {
public:
    // 使用Foundation JSON序列化K线数据
    static std::string serializeKLine(const KLine& kline) {
        foundation::json::JsonFacade json = foundation::json::JsonFacade::createObject();
        
        json.set("symbol", foundation::json::JsonFacade::createString(kline.symbol));
        json.set("trade_date", foundation::json::JsonFacade::createString(kline.trade_date));
        json.set("open", foundation::json::JsonFacade::createDouble(kline.open));
        json.set("high", foundation::json::JsonFacade::createDouble(kline.high));
        json.set("low", foundation::json::JsonFacade::createDouble(kline.low));
        json.set("close", foundation::json::JsonFacade::createDouble(kline.close));
        json.set("volume", foundation::json::JsonFacade::createDouble(kline.volume));
        json.set("change_pct", foundation::json::JsonFacade::createDouble(kline.change_pct));
        
        return json.toString();
    }
    
    // 使用Foundation JSON反序列化K线数据
    static KLine deserializeKLine(const std::string& data) {
        try {
            foundation::json::JsonFacade json = foundation::json::JsonFacade::parse(data);
            
            KLine kline;
            kline.symbol = json.get("symbol").asString();
            kline.trade_date = json.get("trade_date").asString();
            kline.open = json.get("open").asDouble();
            kline.high = json.get("high").asDouble();
            kline.low = json.get("low").asDouble();
            kline.close = json.get("close").asDouble();
            kline.volume = json.get("volume").asDouble();
            kline.change_pct = json.get("change_pct").asDouble();
            
            return kline;
        } catch (const std::exception& e) {
            // 返回默认对象
            return KLine();
        }
    }
    
    // 序列化Tick数据
    static std::string serializeTick(const Tick& tick) {
        foundation::json::JsonFacade json = foundation::json::JsonFacade::createObject();
        
        json.set("symbol", foundation::json::JsonFacade::createString(tick.symbol));
        json.set("timestamp", foundation::json::JsonFacade::createString(tick.timestamp));
        json.set("price", foundation::json::JsonFacade::createDouble(tick.price));
        json.set("volume", foundation::json::JsonFacade::createDouble(tick.volume));
        json.set("amount", foundation::json::JsonFacade::createDouble(tick.amount));
        json.set("direction", foundation::json::JsonFacade::createInt(tick.direction));
        
        return json.toString();
    }
    
    // 反序列化Tick数据
    static Tick deserializeTick(const std::string& data) {
        try {
            foundation::json::JsonFacade json = foundation::json::JsonFacade::parse(data);
            
            Tick tick;
            tick.symbol = json.get("symbol").asString();
            tick.timestamp = json.get("timestamp").asString();
            tick.price = json.get("price").asDouble();
            tick.volume = json.get("volume").asDouble();
            tick.amount = json.get("amount").asDouble();
            tick.direction = json.get("direction").asInt();
            
            return tick;
        } catch (const std::exception& e) {
            return Tick();
        }
    }
    
    // 序列化Factor数据
    static std::string serializeFactor(const Factor& factor) {
        foundation::json::JsonFacade json = foundation::json::JsonFacade::createObject();
        
        json.set("symbol", foundation::json::JsonFacade::createString(factor.symbol));
        json.set("date", foundation::json::JsonFacade::createString(factor.date));
        json.set("factor_name", foundation::json::JsonFacade::createString(factor.factor_name));
        json.set("value", foundation::json::JsonFacade::createDouble(factor.value));
        
        return json.toString();
    }
    
    // 反序列化Factor数据
    static Factor deserializeFactor(const std::string& data) {
        try {
            foundation::json::JsonFacade json = foundation::json::JsonFacade::parse(data);
            
            Factor factor;
            factor.symbol = json.get("symbol").asString();
            factor.date = json.get("date").asString();
            factor.factor_name = json.get("factor_name").asString();
            factor.value = json.get("value").asDouble();
            
            return factor;
        } catch (const std::exception& e) {
            return Factor();
        }
    }
    
    // 序列化基本类型 - 字符串
    static std::string serializeString(const std::string& obj) {
        // 对于字符串，直接返回
        return obj;
    }
    
    // 反序列化基本类型 - 字符串
    static std::string deserializeString(const std::string& data) {
        // 对于字符串，直接返回
        return data;
    }
    
    // 序列化基本类型 - 整数
    static std::string serializeInt(int obj) {
        return std::to_string(obj);
    }
    
    // 反序列化基本类型 - 整数
    static int deserializeInt(const std::string& data) {
        try {
            return std::stoi(data);
        } catch (...) {
            return 0;
        }
    }
    
    // 序列化基本类型 - 双精度浮点数
    static std::string serializeDouble(double obj) {
        return std::to_string(obj);
    }
    
    // 反序列化基本类型 - 双精度浮点数
    static double deserializeDouble(const std::string& data) {
        try {
            return std::stod(data);
        } catch (...) {
            return 0.0;
        }
    }
    
    // 通用序列化接口（兼容现有代码）
    template<typename T>
    static std::string serialize(const T& obj) {
        // 根据类型调用相应的序列化函数
        if constexpr (std::is_same_v<T, std::string>) {
            return serializeString(obj); // 调用字符串重载
        } else if constexpr (std::is_same_v<T, int>) {
            return serializeInt(obj);
        } else if constexpr (std::is_same_v<T, double>) {
            return serializeDouble(obj);
        } else if constexpr (std::is_same_v<T, KLine>) {
            return serializeKLine(obj);
        } else if constexpr (std::is_same_v<T, Tick>) {
            return serializeTick(obj);
        } else if constexpr (std::is_same_v<T, Factor>) {
            return serializeFactor(obj);
        } else {
            static_assert(sizeof(T) == 0, "Unsupported type for serialization");
        }
    }
    
    // 通用反序列化接口（兼容现有代码）
    template<typename T>
    static T deserialize(const std::string& data) {
        if constexpr (std::is_same_v<T, std::string>) {
            return deserializeString(data); // 调用字符串重载
        } else if constexpr (std::is_same_v<T, int>) {
            return deserializeInt(data);
        } else if constexpr (std::is_same_v<T, double>) {
            return deserializeDouble(data);
        } else if constexpr (std::is_same_v<T, KLine>) {
            return deserializeKLine(data);
        } else if constexpr (std::is_same_v<T, Tick>) {
            return deserializeTick(data);
        } else if constexpr (std::is_same_v<T, Factor>) {
            return deserializeFactor(data);
        } else {
            static_assert(sizeof(T) == 0, "Unsupported type for deserialization");
        }
    }
};

} // namespace Cache
} // namespace AStockQuantEngine
