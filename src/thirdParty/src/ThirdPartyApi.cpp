/**
 * @file ThirdPartyApi.cpp
 * @brief 第三方API统一接口库实现
 */

#include "include/ThirdPartyApi.h"
#include "include/JujinApi.h"
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace thirdparty {

// 工厂方法实现
std::unique_ptr<IThirdPartyApi> ThirdPartyApiFactory::create_api(PlatformType platform) {
    switch (platform) {
        case PlatformType::JUJIN:
            return create_jujin_api();
        case PlatformType::JUQUAN:
            // TODO: 实现聚宽API
            throw std::runtime_error("聚宽API尚未实现");
        case PlatformType::RICEQUANT:
            // TODO: 实现米筐API
            throw std::runtime_error("米筐API尚未实现");
        case PlatformType::TUSHARE:
            // TODO: 实现TuShare API
            throw std::runtime_error("TuShare API尚未实现");
        case PlatformType::CUSTOM:
            // TODO: 实现自定义API
            throw std::runtime_error("自定义API尚未实现");
        default:
            throw std::runtime_error("未知的平台类型");
    }
}

std::unique_ptr<IThirdPartyApi> ThirdPartyApiFactory::create_jujin_api() {
    return std::make_unique<JujinApi>();
}

std::unique_ptr<IThirdPartyApi> ThirdPartyApiFactory::create_juquan_api() {
    // TODO: 实现聚宽API
    throw std::runtime_error("聚宽API尚未实现");
}

std::unique_ptr<IThirdPartyApi> ThirdPartyApiFactory::create_ricequant_api() {
    // TODO: 实现米筐API
    throw std::runtime_error("米筐API尚未实现");
}

std::unique_ptr<IThirdPartyApi> ThirdPartyApiFactory::create_tushare_api() {
    // TODO: 实现TuShare API
    throw std::runtime_error("TuShare API尚未实现");
}

// 工具函数实现
std::string convert_symbol_format(
    const std::string& internal_symbol,
    PlatformType platform) {
    
    if (internal_symbol.empty()) {
        return "";
    }
    
    switch (platform) {
        case PlatformType::JUJIN: {
            // 内部格式: 000001.SZ, 600000.SH
            // 掘金格式: SZSE.000001, SHSE.600000
            size_t dot_pos = internal_symbol.find('.');
            if (dot_pos == std::string::npos) {
                return internal_symbol;
            }
            
            std::string code = internal_symbol.substr(0, dot_pos);
            std::string exchange = internal_symbol.substr(dot_pos + 1);
            
            if (exchange == "SZ") {
                return "SZSE." + code;
            } else if (exchange == "SH") {
                return "SHSE." + code;
            } else if (exchange == "BJ") {
                return "BSE." + code;
            } else {
                return exchange + "." + code;
            }
        }
        
        case PlatformType::JUQUAN: {
            // 聚宽格式: 000001.XSHE, 600000.XSHG
            size_t dot_pos = internal_symbol.find('.');
            if (dot_pos == std::string::npos) {
                return internal_symbol;
            }
            
            std::string code = internal_symbol.substr(0, dot_pos);
            std::string exchange = internal_symbol.substr(dot_pos + 1);
            
            if (exchange == "SZ") {
                return code + ".XSHE";
            } else if (exchange == "SH") {
                return code + ".XSHG";
            } else {
                return internal_symbol;
            }
        }
        
        case PlatformType::RICEQUANT: {
            // 米筐格式: 000001.SZ, 600000.SH
            return internal_symbol;
        }
        
        case PlatformType::TUSHARE: {
            // TuShare格式: 000001.SZ, 600000.SH
            return internal_symbol;
        }
        
        default:
            return internal_symbol;
    }
}

std::string convert_to_internal_format(
    const std::string& platform_symbol,
    PlatformType platform) {
    
    if (platform_symbol.empty()) {
        return "";
    }
    
    switch (platform) {
        case PlatformType::JUJIN: {
            // 掘金格式: SZSE.000001, SHSE.600000
            // 内部格式: 000001.SZ, 600000.SH
            size_t dot_pos = platform_symbol.find('.');
            if (dot_pos == std::string::npos) {
                return platform_symbol;
            }
            
            std::string exchange = platform_symbol.substr(0, dot_pos);
            std::string code = platform_symbol.substr(dot_pos + 1);
            
            if (exchange == "SZSE") {
                return code + ".SZ";
            } else if (exchange == "SHSE") {
                return code + ".SH";
            } else if (exchange == "BSE") {
                return code + ".BJ";
            } else {
                return code + "." + exchange;
            }
        }
        
        case PlatformType::JUQUAN: {
            // 聚宽格式: 000001.XSHE, 600000.XSHG
            // 内部格式: 000001.SZ, 600000.SH
            size_t dot_pos = platform_symbol.find('.');
            if (dot_pos == std::string::npos) {
                return platform_symbol;
            }
            
            std::string code = platform_symbol.substr(0, dot_pos);
            std::string exchange = platform_symbol.substr(dot_pos + 1);
            
            if (exchange == "XSHE") {
                return code + ".SZ";
            } else if (exchange == "XSHG") {
                return code + ".SH";
            } else {
                return platform_symbol;
            }
        }
        
        case PlatformType::RICEQUANT: {
            // 米筐格式: 000001.SZ, 600000.SH
            return platform_symbol;
        }
        
        case PlatformType::TUSHARE: {
            // TuShare格式: 000001.SZ, 600000.SH
            return platform_symbol;
        }
        
        default:
            return platform_symbol;
    }
}

// 时间转换辅助函数
static std::chrono::system_clock::time_point string_to_timepoint(const std::string& time_str) {
    std::tm tm = {};
    std::istringstream ss(time_str);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        ss.clear();
        ss.str(time_str);
        ss >> std::get_time(&tm, "%Y-%m-%d");
    }
    
    if (ss.fail()) {
        return std::chrono::system_clock::time_point();
    }
    
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

static std::string timepoint_to_string(std::chrono::system_clock::time_point tp) {
    if (tp == std::chrono::system_clock::time_point()) {
        return "";
    }
    
    std::time_t time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = *std::localtime(&time);
    
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

} // namespace thirdparty