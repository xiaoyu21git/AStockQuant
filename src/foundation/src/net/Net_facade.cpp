// net_impl.cpp
#include "foundation/net/Net_facade.h"
#include <iostream>
#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include <string>
#include <curl/curl.h>  // 假设使用libcurl

namespace foundation::net {
    
// ============ 工具函数 ============

std::string HttpResponse::getHeader(const std::string& name) const {
    auto range = headers.equal_range(name);
    if (range.first != range.second) {
        return range.first->second;
    }
    return "";
}

std::vector<std::string> HttpResponse::getHeaderValues(const std::string& name) const {
    std::vector<std::string> values;
    auto range = headers.equal_range(name);
    for (auto it = range.first; it != range.second; ++it) {
        values.push_back(it->second);
    }
    return values;
}

HttpRequest HttpRequest::create_get(const std::string& url) {
    HttpRequest req;
    req.url = url;
    req.method = "GET";
    return req;
}

HttpRequest HttpRequest::create_post(const std::string& url, const std::string& body) {
    HttpRequest req;
    req.url = url;
    req.method = "POST";
    req.body = body;
    if (!body.empty()) {
        req.headers.emplace("Content-Type", "application/json");
    }
    return req;
}

HttpRequest HttpRequest::create_put(const std::string& url, const std::string& body) {
    HttpRequest req;
    req.url = url;
    req.method = "PUT";
    req.body = body;
    if (!body.empty()) {
        req.headers.emplace("Content-Type", "application/json");
    }
    return req;
}

HttpRequest HttpRequest::create_delete(const std::string& url) {
    HttpRequest req;
    req.url = url;
    req.method = "DELETE";
    return req;
}

HttpRequest& HttpRequest::set_header(const std::string& name, const std::string& value) {
    headers.emplace(name, value);
    return *this;
}

HttpRequest& HttpRequest::setBody(const std::string& content, 
                                 const std::string& contentType) {
    body = content;
    headers.emplace("Content-Type", contentType);
    return *this;
}

std::string WebSocketMessage::getText() const {
    if (type != Text) return "";
    return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

// ============ HttpClient实现 ============

class HttpClient::Impl {
private:
    CURL* curlHandle_;
    std::multimap<std::string, std::string> defaultHeaders_;
    // 添加代理设置检查方法
    bool has_proxy() const {
        return use_proxy_ && !proxy_host_.empty() && proxy_port_ > 0;
    }
    
    // RAII管理CURL句柄
    class CurlHandleRAII {
    private:
        CURL* handle_;
        
    public:
        CurlHandleRAII() : handle_(curl_easy_init()) {
            if (!handle_) {
                throw std::runtime_error("无法初始化CURL");
            }
        }
        
        ~CurlHandleRAII() {
            if (handle_) {
                curl_easy_cleanup(handle_);
            }
        }
        
        CURL* get() const { return handle_; }
        
        // 禁用拷贝
        CurlHandleRAII(const CurlHandleRAII&) = delete;
        CurlHandleRAII& operator=(const CurlHandleRAII&) = delete;
        
        // 允许移动
        CurlHandleRAII(CurlHandleRAII&& other) noexcept : handle_(other.handle_) {
            other.handle_ = nullptr;
        }
       
    };
    
public:
    std::chrono::milliseconds defaultTimeout_;
    std::string proxy_host_;
    uint16_t proxy_port_ = 0;
    bool use_proxy_ = false;
    bool verify_ssl_ = true;
    Impl() : defaultTimeout_(30000) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    
    ~Impl() {
        curl_global_cleanup();
    }
    
    void setDefaultTimeout(std::chrono::milliseconds timeout) {
        defaultTimeout_ = timeout;
    }
    
    void setDefaultHeaders(const std::multimap<std::string, std::string>& headers) {
        defaultHeaders_ = headers;
    }
    
    void addDefaultHeader(const std::string& name, const std::string& value) {
        defaultHeaders_.emplace(name, value);
    }
    
    HttpResponse send(const HttpRequest& request) {
        CurlHandleRAII curl;
        
        // 设置URL
        curl_easy_setopt(curl.get(), CURLOPT_URL, request.url.c_str());
        
        // 设置方法
        if (request.method == "POST") {
            curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
        } else if (request.method == "PUT") {
            curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, "PUT");
        } else if (request.method == "DELETE") {
            curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, "DELETE");
        }
        
        // 设置超时
        auto timeout = request.timeout.count() > 0 ? 
                      request.timeout : defaultTimeout_;
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, timeout.count());
        
        // 设置请求体
        if (!request.body.empty()) {
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, request.body.c_str());
        }
        
        // 设置请求头
        struct curl_slist* headers = nullptr;
        
        // 添加默认头
        for (const auto& [name, value] : defaultHeaders_) {
            std::string header = name + ": " + value;
            headers = curl_slist_append(headers, header.c_str());
        }
        
        // 添加请求特定头
        for (const auto& [name, value] : request.headers) {
            std::string header = name + ": " + value;
            headers = curl_slist_append(headers, header.c_str());
        }
        
        if (headers) {
            curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
        }
        
        // 准备响应
        HttpResponse response;
        std::string responseBody;
        std::multimap<std::string, std::string> responseHeaders;
        
        // 设置写回调
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &responseBody);
        
        // 设置头回调
        curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, headerCallback);
        curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &responseHeaders);
        
        // 执行请求
        auto start = std::chrono::steady_clock::now();
        CURLcode res = curl_easy_perform(curl.get());
        auto end = std::chrono::steady_clock::now();
        
        response.elapsedTime = 
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        // 清理头
        if (headers) {
            curl_slist_free_all(headers);
        }
        
        if (res != CURLE_OK) {
            throw std::runtime_error(curl_easy_strerror(res));
        }
        
        // 获取状态码
        long httpCode = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &httpCode);
        response.statusCode = static_cast<int>(httpCode);
        
        // 获取状态文本（简化）
        response.statusText = getStatusText(response.statusCode);
        response.body = responseBody;
        response.headers = responseHeaders;
        
        return response;
    }
    
private:
    static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        std::string* response = static_cast<std::string*>(userdata);
        size_t totalSize = size * nmemb;
        response->append(ptr, totalSize);
        return totalSize;
    }
    
    static size_t headerCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
        std::multimap<std::string, std::string>* headers = 
            static_cast<std::multimap<std::string, std::string>*>(userdata);
        
        std::string header(buffer, size * nitems);
        
        // 移除换行符
        if (!header.empty() && header.back() == '\n') {
            header.pop_back();
        }
        if (!header.empty() && header.back() == '\r') {
            header.pop_back();
        }
        
        // 分割键值
        size_t colonPos = header.find(':');
        if (colonPos != std::string::npos) {
            std::string key = header.substr(0, colonPos);
            std::string value = header.substr(colonPos + 1);
            
            // 去除空格
            key.erase(0, key.find_first_not_of(' '));
            key.erase(key.find_last_not_of(' ') + 1);
            value.erase(0, value.find_first_not_of(' '));
            value.erase(value.find_last_not_of(' ') + 1);
            
            if (!key.empty()) {
                headers->emplace(key, value);
            }
        }
        
        return size * nitems;
    }
    
    static std::string getStatusText(int code) {
        static const std::map<int, std::string> statusTexts = {
            {200, "OK"},
            {201, "Created"},
            {204, "No Content"},
            {400, "Bad Request"},
            {401, "Unauthorized"},
            {403, "Forbidden"},
            {404, "Not Found"},
            {500, "Internal Server Error"},
        };
        
        auto it = statusTexts.find(code);
        return it != statusTexts.end() ? it->second : "Unknown";
    }
};

// HttpClient接口实现
HttpClient::HttpClient() 
    : impl_(std::make_unique<Impl>()) {}

HttpClient::HttpClient(std::unique_ptr<Impl> impl) 
    : impl_(std::move(impl)) {}

HttpClient::~HttpClient() = default;

void HttpClient::setDefaultTimeout(std::chrono::milliseconds timeout) {
    impl_->setDefaultTimeout(timeout);
}

void HttpClient::setDefaultHeaders(const std::multimap<std::string, std::string>& headers) {
    impl_->setDefaultHeaders(headers);
}

void HttpClient::addDefaultHeader(const std::string& name, const std::string& value) {
    impl_->addDefaultHeader(name, value);
}

HttpResponse HttpClient::get(const std::string& url) {
    return send(HttpRequest::create_get(url));
}

HttpResponse HttpClient::post(const std::string& url, const std::string& body) {
    return send(HttpRequest::create_post(url, body));
}

HttpResponse HttpClient::put(const std::string& url, const std::string& body) {
    return send(HttpRequest::create_put(url, body));
}

HttpResponse HttpClient::delete_(const std::string& url) {
    return send(HttpRequest::create_delete(url));
}

HttpResponse HttpClient::send(const HttpRequest& request) {
    return impl_->send(request);
}

// 异步和批量方法（简化实现）
void HttpClient::getAsync(const std::string& url, Callback callback) {
    std::thread([this, url, callback]() {
        try {
            auto response = this->get(url);
            callback(response);
        } catch (const std::exception& e) {
            // 创建错误响应
            HttpResponse errorResponse;
            errorResponse.statusCode = 0;
            errorResponse.statusText = e.what();
            callback(errorResponse);
        }
    }).detach();
}

void HttpClient::postAsync(const std::string& url, const std::string& body, Callback callback) {
    std::thread([this, url, body, callback]() {
        try {
            auto response = this->post(url, body);
            callback(response);
        } catch (const std::exception& e) {
            HttpResponse errorResponse;
            errorResponse.statusCode = 0;
            errorResponse.statusText = e.what();
            callback(errorResponse);
        }
    }).detach();
}

void HttpClient::sendAsync(const HttpRequest& request, Callback callback) {
    std::thread([this, request, callback]() {
        try {
            auto response = this->send(request);
            callback(response);
        } catch (const std::exception& e) {
            HttpResponse errorResponse;
            errorResponse.statusCode = 0;
            errorResponse.statusText = e.what();
            callback(errorResponse);
        }
    }).detach();
}

void HttpClient::set_timeout(std::chrono::milliseconds timeout) {
    impl_->defaultTimeout_ =std::chrono::duration_cast<std::chrono::milliseconds>(timeout) ;
    
}

void HttpClient::set_proxy(const std::string& host, uint16_t port) {
    impl_->proxy_host_ = host;
    impl_->proxy_port_ = port;
    impl_->use_proxy_ = true;
}

std::vector<HttpResponse> HttpClient::batchSend(const std::vector<HttpRequest>& requests) {
    std::vector<HttpResponse> responses;
    responses.reserve(requests.size());
    
    for (const auto& request : requests) {
        responses.push_back(send(request));
    }
    
    return responses;
}

// ============ NetFacade工厂 ============

std::unique_ptr<HttpClient> NetFacade::createHttpClient() {
    return std::make_unique<HttpClient>();
}

std::unique_ptr<WebSocketConnection> NetFacade::createWebSocket() {
    return std::make_unique<WebSocketConnection>();
}

// 工具函数实现
std::string NetFacade::urlEncode(const std::string& str) {
    // 简化实现
    return str; // 实际应该实现URL编码
}

std::string NetFacade::urlDecode(const std::string& str) {
    // 简化实现
    return str; // 实际应该实现URL解码
}

std::map<std::string, std::string> NetFacade::parseQueryString(const std::string& query) {
    std::map<std::string, std::string> params;
    std::istringstream iss(query);
    std::string pair;
    
    while (std::getline(iss, pair, '&')) {
        size_t eqPos = pair.find('=');
        if (eqPos != std::string::npos) {
            std::string key = pair.substr(0, eqPos);
            std::string value = pair.substr(eqPos + 1);
            params[urlDecode(key)] = urlDecode(value);
        }
    }
    
    return params;
}

std::string NetFacade::buildQueryString(const std::map<std::string, std::string>& params) {
    std::string query;
    bool first = true;
    
    for (const auto& [key, value] : params) {
        if (!first) query += '&';
        query += urlEncode(key) + '=' + urlEncode(value);
        first = false;
    }
    
    return query;
}

} // namespace foundation::net