// http_request.h
#pragma once

#include <string>
#include <map>
#include <chrono>
#include <optional>
#include <vector>

namespace net {

struct HttpRequest {
    std::string url;
    std::string method = "GET";
    std::string body;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> query_params;
    
    // 超时设置
    std::chrono::milliseconds timeout = std::chrono::seconds(30);
    std::optional<std::chrono::milliseconds> connect_timeout;
    std::optional<std::chrono::milliseconds> read_timeout;
    std::optional<std::chrono::milliseconds> write_timeout;
    
    // 代理设置
    std::optional<std::string> proxy_host;
    std::optional<uint16_t> proxy_port;
    std::optional<std::string> proxy_username;
    std::optional<std::string> proxy_password;
    
    // SSL设置
    bool verify_ssl = true;
    std::optional<std::string> ssl_cert_path;
    std::optional<std::string> ssl_key_path;
    std::optional<std::string> ssl_ca_path;
    
    // 重定向设置
    bool follow_redirects = true;
    int max_redirects = 10;
    
    // 其他
    bool keep_alive = true;
    bool compress = false;
    bool chunked = false;
};

} // namespace net