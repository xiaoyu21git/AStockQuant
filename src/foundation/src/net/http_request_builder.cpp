#include "foundation/net/http_request_builder.h"
#include <sstream>
#include <iomanip>
#include <cctype>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace net {

// ============ 静态常量定义 ============

const std::unordered_set<std::string> HttpRequestBuilder::valid_methods_ = {
    "GET", "POST", "PUT", "DELETE", "PATCH", 
    "HEAD", "OPTIONS", "TRACE", "CONNECT"
};

const std::unordered_set<std::string> HttpRequestBuilder::content_type_to_method_ = {
    "POST", "PUT", "PATCH"
};

// ============ 构造函数 ============

HttpRequestBuilder::HttpRequestBuilder(std::string url) 
    : request_{} {
    request_.url = std::move(url);
    request_.method = "GET";
    request_.timeout = std::chrono::seconds(30);
    request_.follow_redirects = true;
    request_.max_redirects = 10;
    request_.verify_ssl = true;
    request_.keep_alive = true;
}

HttpRequestBuilder HttpRequestBuilder::create(std::string url) {
    return HttpRequestBuilder(std::move(url));
}

// ============ HTTP方法设置 ============

HttpRequestBuilder& HttpRequestBuilder::method(std::string_view method) {
    std::string upper_method;
    upper_method.reserve(method.size());
    std::transform(method.begin(), method.end(), 
                   std::back_inserter(upper_method), 
                   [](unsigned char c) { return std::toupper(c); });
    
    if (!is_valid_method(upper_method)) {
        errors_.push_back("Invalid HTTP method: " + std::string(method));
        return *this;
    }
    
    request_.method = upper_method;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::get() {
    return method("GET");
}

HttpRequestBuilder& HttpRequestBuilder::post() {
    return method("POST");
}

HttpRequestBuilder& HttpRequestBuilder::put() {
    return method("PUT");
}

HttpRequestBuilder& HttpRequestBuilder::delete_() {
    return method("DELETE");
}

HttpRequestBuilder& HttpRequestBuilder::patch() {
    return method("PATCH");
}

HttpRequestBuilder& HttpRequestBuilder::head() {
    return method("HEAD");
}

HttpRequestBuilder& HttpRequestBuilder::options() {
    return method("OPTIONS");
}

// ============ 请求体设置 ============

HttpRequestBuilder& HttpRequestBuilder::body(std::string body) {
    request_.body = std::move(body);
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::body(std::string_view body) {
    request_.body = std::string(body);
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::json(std::string json) {
    request_.body = std::move(json);
    request_.headers["Content-Type"] = "application/json";
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::json(std::string_view json) {
    request_.body = std::string(json);
    request_.headers["Content-Type"] = "application/json";
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::form(const std::map<std::string, std::string>& form_data) {
    request_.body = form_url_encode(form_data);
    request_.headers["Content-Type"] = "application/x-www-form-urlencoded";
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::form(const std::unordered_map<std::string, std::string>& form_data) {
    std::map<std::string, std::string> ordered_map(form_data.begin(), form_data.end());
    return form(ordered_map);
}

HttpRequestBuilder& HttpRequestBuilder::form_field(std::string name, std::string value) {
    // 如果当前不是表单数据，初始化
    if (request_.headers["Content-Type"] != "application/x-www-form-urlencoded") {
        request_.body.clear();
        request_.headers["Content-Type"] = "application/x-www-form-urlencoded";
    }
    
    if (!request_.body.empty()) {
        request_.body += "&";
    }
    
    request_.body += url_encode(name) + "=" + url_encode(value);
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::multipart() {
    request_.headers["Content-Type"] = "multipart/form-data";
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::add_file(std::string field_name, 
                                                 std::string filename, 
                                                 std::string content_type,
                                                 const std::vector<uint8_t>& data) {
    // 简化实现，实际应该生成multipart边界
    request_.headers["Content-Type"] = "multipart/form-data";
    // 这里应该实现multipart格式
    return *this;
}

// ============ 请求头设置 ============

HttpRequestBuilder& HttpRequestBuilder::header(std::string name, std::string value) {
    request_.headers[std::move(name)] = std::move(value);
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::header(std::string_view name, std::string_view value) {
    request_.headers[std::string(name)] = std::string(value);
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::content_type(std::string_view type) {
    request_.headers["Content-Type"] = std::string(type);
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::accept(std::string_view type) {
    request_.headers["Accept"] = std::string(type);
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::accept_json() {
    return accept("application/json");
}

HttpRequestBuilder& HttpRequestBuilder::accept_xml() {
    return accept("application/xml");
}

HttpRequestBuilder& HttpRequestBuilder::user_agent(std::string_view agent) {
    request_.headers["User-Agent"] = std::string(agent);
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::referer(std::string_view referer) {
    request_.headers["Referer"] = std::string(referer);
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::origin(std::string_view origin) {
    request_.headers["Origin"] = std::string(origin);
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::authorization(std::string_view scheme, 
                                                      std::string_view credentials) {
    std::string auth = std::string(scheme) + " " + std::string(credentials);
    request_.headers["Authorization"] = auth;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::bearer_token(std::string_view token) {
    return authorization("Bearer", token);
}

HttpRequestBuilder& HttpRequestBuilder::basic_auth(std::string_view username, 
                                                   std::string_view password) {
    std::string credentials = std::string(username) + ":" + std::string(password);
    // 实际应该进行base64编码
    return authorization("Basic", credentials);
}

// ============ Cookie管理 ============

HttpRequestBuilder& HttpRequestBuilder::cookie(std::string name, std::string value) {
    std::string cookie_str = url_encode(name) + "=" + url_encode(value);
    
    if (request_.headers.find("Cookie") != request_.headers.end()) {
        request_.headers["Cookie"] += "; " + cookie_str;
    } else {
        request_.headers["Cookie"] = cookie_str;
    }
    
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::cookies(const std::map<std::string, std::string>& cookies) {
    for (const auto& [name, value] : cookies) {
        cookie(name, value);
    }
    return *this;
}

// ============ 查询参数 ============

HttpRequestBuilder& HttpRequestBuilder::query_param(std::string name, std::string value) {
    request_.query_params[std::move(name)] = std::move(value);
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::query_params(const std::map<std::string, std::string>& params) {
    for (const auto& [name, value] : params) {
        request_.query_params[name] = value;
    }
    return *this;
}

// ============ 超时设置 ============

HttpRequestBuilder& HttpRequestBuilder::timeout(std::chrono::milliseconds timeout) {
    request_.timeout = timeout;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::connect_timeout(std::chrono::milliseconds timeout) {
    request_.connect_timeout = timeout;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::read_timeout(std::chrono::milliseconds timeout) {
    request_.read_timeout = timeout;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::write_timeout(std::chrono::milliseconds timeout) {
    request_.write_timeout = timeout;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::timeout_seconds(int seconds) {
    return timeout(std::chrono::seconds(seconds));
}

HttpRequestBuilder& HttpRequestBuilder::timeout_milliseconds(int ms) {
    return timeout(std::chrono::milliseconds(ms));
}

// ============ 代理设置 ============

HttpRequestBuilder& HttpRequestBuilder::proxy(std::string host, uint16_t port) {
    request_.proxy_host = std::move(host);
    request_.proxy_port = port;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::proxy(std::string_view host, uint16_t port) {
    request_.proxy_host = std::string(host);
    request_.proxy_port = port;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::proxy_auth(std::string username, std::string password) {
    request_.proxy_username = std::move(username);
    request_.proxy_password = std::move(password);
    return *this;
}

// ============ SSL/安全设置 ============

HttpRequestBuilder& HttpRequestBuilder::verify_ssl(bool verify) {
    request_.verify_ssl = verify;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::ssl_cert_path(std::string cert_path) {
    request_.ssl_cert_path = std::move(cert_path);
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::ssl_key_path(std::string key_path) {
    request_.ssl_key_path = std::move(key_path);
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::ssl_ca_path(std::string ca_path) {
    request_.ssl_ca_path = std::move(ca_path);
    return *this;
}

// ============ 重定向设置 ============

HttpRequestBuilder& HttpRequestBuilder::follow_redirects(bool follow) {
    request_.follow_redirects = follow;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::max_redirects(int max_redirects) {
    request_.max_redirects = max_redirects;
    return *this;
}

// ============ 其他设置 ============

HttpRequestBuilder& HttpRequestBuilder::keep_alive(bool keep_alive) {
    request_.keep_alive = keep_alive;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::compress(bool compress) {
    if (compress) {
        request_.headers["Accept-Encoding"] = "gzip, deflate";
    } else {
        request_.headers.erase("Accept-Encoding");
    }
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::chunked(bool chunked) {
    request_.chunked = chunked;
    return *this;
}

// ============ 构建和执行 ============

HttpRequest HttpRequestBuilder::build() const {
    // 验证
    validate();
    
    if (!errors_.empty()) {
        std::string error_msg = "HTTP request validation failed:\n";
        for (const auto& error : errors_) {
            error_msg += "  - " + error + "\n";
        }
        throw std::runtime_error(error_msg);
    }
    
    // 准备请求
    HttpRequest request = request_;
    prepare_headers();
    prepare_body();
    prepare_query_string();
    
    // 设置默认User-Agent
    if (request.headers.find("User-Agent") == request.headers.end()) {
        request.headers["User-Agent"] = "HttpRequestBuilder/1.0";
    }
    
    // 如果是POST/PUT/PATCH但没有Content-Type，设置默认值
    if (content_type_to_method_.count(request.method) > 0 &&
        request.headers.find("Content-Type") == request.headers.end() &&
        !request.body.empty()) {
        request.headers["Content-Type"] = "text/plain";
    }
    
    return request;
}

bool HttpRequestBuilder::validate() const {
    errors_.clear();
    
    validate_method();
    validate_url();
    
    // 检查是否有请求体但没有设置Content-Type
    if (!request_.body.empty() && 
        content_type_to_method_.count(request_.method) > 0 &&
        request_.headers.find("Content-Type") == request_.headers.end()) {
        errors_.push_back("Content-Type header is required for request with body");
    }
    
    // 检查URL是否有效
    if (request_.url.find("://") == std::string::npos) {
        errors_.push_back("Invalid URL: missing protocol");
    }
    
    return errors_.empty();
}

std::vector<std::string> HttpRequestBuilder::get_errors() const {
    return errors_;
}

HttpResponse HttpRequestBuilder::execute() const {
    HttpRequest request = build();
    // 假设有全局的HttpClient实例
    // return foundation::app().http_client().request(request);
    // 这里应该调用实际的HTTP客户端
    return HttpResponse{};
}

std::future<HttpResponse> HttpRequestBuilder::execute_async() const {
    return std::async(std::launch::async, [this]() {
        return execute();
    });
}

void HttpRequestBuilder::execute_async(std::function<void(HttpResponse)> callback) const {
    std::thread([this, callback = std::move(callback)]() {
        callback(execute());
    }).detach();
}

// ============ 工具方法 ============

HttpRequestBuilder& HttpRequestBuilder::reset() {
    std::string url = std::move(request_.url);
    request_ = HttpRequest{};
    request_.url = std::move(url);
    request_.method = "GET";
    request_.timeout = std::chrono::seconds(30);
    errors_.clear();
    return *this;
}

HttpRequestBuilder HttpRequestBuilder::clone() const {
    HttpRequestBuilder builder(request_.url);
    builder.request_ = request_;
    return builder;
}

// ============ 私有辅助方法 ============

void HttpRequestBuilder::validate_method() const {
    if (!is_valid_method(request_.method)) {
        errors_.push_back("Invalid HTTP method: " + request_.method);
    }
}

void HttpRequestBuilder::validate_url() const {
    if (request_.url.empty()) {
        errors_.push_back("URL cannot be empty");
    }
}

void HttpRequestBuilder::prepare_headers()const {
    // 自动添加必要的头部
    if (request_.keep_alive) {
        request_.headers["Connection"] = "keep-alive";
    } else {
        request_.headers["Connection"] = "close";
    }
}

void HttpRequestBuilder::prepare_body()const {
    // 如果有查询参数但没有请求体，将参数添加到URL
    if (request_.body.empty() && !request_.query_params.empty()) {
        prepare_query_string();
    }
}

void HttpRequestBuilder::prepare_query_string()const {
    if (request_.query_params.empty()) {
        return;
    }
    
    std::string query_string;
    for (const auto& [name, value] : request_.query_params) {
        if (!query_string.empty()) {
            query_string += "&";
        }
        query_string += url_encode(name) + "=" + url_encode(value);
    }
    
    if (request_.url.find('?') == std::string::npos) {
        request_.url += "?" + query_string;
    } else {
        request_.url += "&" + query_string;
    }
}

std::string HttpRequestBuilder::url_encode(std::string_view str) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    
    for (char c : str) {
        // 保留字母数字和某些特殊字符
        if (std::isalnum(static_cast<unsigned char>(c)) || 
            c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }
    
    return escaped.str();
}

std::string HttpRequestBuilder::form_url_encode(const std::map<std::string, std::string>& data) {
    std::string result;
    
    for (const auto& [name, value] : data) {
        if (!result.empty()) {
            result += "&";
        }
        result += url_encode(name) + "=" + url_encode(value);
    }
    
    return result;
}

bool HttpRequestBuilder::is_valid_method(std::string_view method) const {
    return valid_methods_.count(std::string(method)) > 0;
}

bool HttpRequestBuilder::is_valid_url(std::string_view url) const {
    // 简单的URL验证
    return url.find("://") != std::string::npos;
}

} // namespace net