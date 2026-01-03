// net_facade.h
#pragma once
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <chrono>
#include <map>
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
    std::string url;
    std::string method = "GET";
    std::string body;
    std::multimap<std::string, std::string> headers;
    std::chrono::milliseconds timeout = std::chrono::seconds(30);
    
    static HttpRequest createGet(const std::string& url);
    static HttpRequest createPost(const std::string& url, const std::string& body = "");
    static HttpRequest createPut(const std::string& url, const std::string& body = "");
    static HttpRequest createDelete(const std::string& url);
    
    HttpRequest& setHeader(const std::string& name, const std::string& value);
    HttpRequest& setBody(const std::string& content, 
                        const std::string& contentType = "application/json");
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