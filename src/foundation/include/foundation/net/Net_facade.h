// net_facade.h
#pragma once
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <chrono>
#include <map>
#include <optional>
namespace foundation::net {
    
// HTTP响应
struct HttpResponse {
    int statusCode;
    std::string statusText;
    std::string body;
    std::multimap<std::string, std::string> headers;
    std::chrono::milliseconds elapsedTime;
    
    bool isSuccess() const { return statusCode >= 200 && statusCode < 300; }
    std::string getHeader(const std::string& name) const;
    std::vector<std::string> getHeaderValues(const std::string& name) const;
};

// HTTP请求
struct HttpRequest {
    // 基本请求信息
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
    
    // 重定向设置 - 添加缺失的成员
    bool follow_redirects = true;
    int max_redirects = 10;
    
    // 连接设置
    bool keep_alive = true;
    bool compress = false;
    bool chunked = false;
    
    // 性能跟踪
    bool enable_timing = false;
    
    // 构造函数
    HttpRequest() = default;
    
    explicit HttpRequest(std::string url)
        : url(std::move(url)) {}
    
    // 便捷方法
    bool has_body() const { return !body.empty(); }
    
    bool is_secure() const {
        return url.find("https://") == 0 || url.find("HTTPS://") == 0;
    }
    
    std::string get_header(const std::string& name) const {
        auto it = headers.find(name);
        return it != headers.end() ? it->second : "";
    }
    
    HttpRequest& set_header(const std::string& name, const std::string& value);
    HttpRequest& setBody(const std::string& content, const std::string& contentType);
    void add_query_param(const std::string& name, const std::string& value) {
        query_params[name] = value;
    }
    static HttpRequest create_get(const std::string& url);
    static HttpRequest create_post(const std::string& url, const std::string& body = "");
    static HttpRequest create_put(const std::string& url, const std::string& body);
    static HttpRequest create_delete(const std::string& url) ;
};

// WebSocket消息
struct WebSocketMessage {
    enum Type { Text, Binary, Close, Ping, Pong } type;
    std::vector<uint8_t> data;
    
    std::string getText() const;
};

// Facade：HTTP客户端
class HttpClient {
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    
public:
    HttpClient();
    ~HttpClient();
    
    // 配置
    void setDefaultTimeout(std::chrono::milliseconds timeout);
    void setDefaultHeaders(const std::multimap<std::string, std::string>& headers);
    void addDefaultHeader(const std::string& name, const std::string& value);
    
    // 简单请求方法
    HttpResponse get(const std::string& url);
    HttpResponse post(const std::string& url, const std::string& body = "");
    HttpResponse put(const std::string& url, const std::string& body = "");
    HttpResponse delete_(const std::string& url);
    
    // 高级请求
    HttpResponse send(const HttpRequest& request);
    
    // 异步请求
    using Callback = std::function<void(HttpResponse)>;
    void getAsync(const std::string& url, Callback callback);
    void postAsync(const std::string& url, const std::string& body, Callback callback);
    void sendAsync(const HttpRequest& request, Callback callback);
    
    // 批量请求
    std::vector<HttpResponse> batchSend(const std::vector<HttpRequest>& requests);
    void set_timeout(std::chrono::milliseconds timeout);
    void set_proxy(const std::string& host, uint16_t port);
private:
    explicit HttpClient(std::unique_ptr<Impl> impl);
};

// RAII：WebSocket连接
class WebSocketConnection {
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    
public:
    WebSocketConnection();
    ~WebSocketConnection();
    
    // 连接管理
    bool connect(const std::string& url);
    bool isConnected() const;
    void disconnect();
    
    // 消息发送
    bool sendText(const std::string& text);
    bool sendBinary(const std::vector<uint8_t>& data);
    
    // 消息接收（阻塞）
    bool receive(WebSocketMessage& message, 
                std::chrono::milliseconds timeout = std::chrono::seconds(5));
    
    // 消息接收（非阻塞）
    bool tryReceive(WebSocketMessage& message);
    
    // 事件回调
    using MessageHandler = std::function<void(const WebSocketMessage&)>;
    using ErrorHandler = std::function<void(const std::string&)>;
    
    void setMessageHandler(MessageHandler handler);
    void setErrorHandler(ErrorHandler handler);
    
    // 开始事件循环（需要在单独的线程中运行）
    void runEventLoop();
    void stopEventLoop();
    
private:
    explicit WebSocketConnection(std::unique_ptr<Impl> impl);
};

// Facade工厂
class NetFacade {
public:
    static std::unique_ptr<HttpClient> createHttpClient();
    static std::unique_ptr<WebSocketConnection> createWebSocket();
    
    // 工具函数
    static std::string urlEncode(const std::string& str);
    static std::string urlDecode(const std::string& str);
    static std::map<std::string, std::string> parseQueryString(const std::string& query);
    static std::string buildQueryString(const std::map<std::string, std::string>& params);
};

} // namespace foundation::net