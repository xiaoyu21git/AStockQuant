// http_response.h
#pragma once

#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include <algorithm>  // 包含 std::transform
namespace net {

struct HttpResponse {
    int status_code = 0;
    std::string status_text;
    std::map<std::string, std::string> headers;
    std::vector<uint8_t> body;
    std::string error;
    
    // 性能信息
    double total_time = 0.0;
    double name_lookup_time = 0.0;
    double connect_time = 0.0;
    
    bool is_success() const {
        return status_code >= 200 && status_code < 300;
    }
    
    bool is_redirect() const {
        return status_code >= 300 && status_code < 400;
    }
    
    bool is_client_error() const {
        return status_code >= 400 && status_code < 500;
    }
    
    bool is_server_error() const {
        return status_code >= 500;
    }
    
    std::string get_header(const std::string& name) const {
        auto it = headers.find(name);
        if (it != headers.end()) {
            return it->second;
        }
        
        // 不区分大小写查找
        std::string lower_name;
        lower_name.resize(name.size());
        std::transform(name.begin(), name.end(), lower_name.begin(), ::tolower);
        
        for (const auto& [key, value] : headers) {
            std::string lower_key;
            lower_key.resize(key.size());
            std::transform(key.begin(), key.end(), lower_key.begin(), ::tolower);
            
            if (lower_key == lower_name) {
                return value;
            }
        }
        
        return "";
    }
    
    std::string get_body_string() const {
        return std::string(reinterpret_cast<const char*>(body.data()), body.size());
    }
};

} // namespace net