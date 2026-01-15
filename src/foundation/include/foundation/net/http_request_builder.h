#pragma once

#include "http_request.h"
#include "http_response.h"
#include <string>
#include <string_view>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <chrono>
#include <optional>
#include <vector>
#include <functional>
#include <future>
#include <stdexcept>
#include <algorithm>

namespace net {

/**
 * @brief HTTP请求构建器，提供流畅的API来构建和配置HTTP请求
 * 
 * 支持：
 * - HTTP方法设置
 * - 请求头和cookie管理
 * - 请求体设置（文本、JSON、表单、文件）
 * - 超时和代理配置
 * - SSL验证和重定向控制
 * - 同步/异步执行
 */
class HttpRequestBuilder {
public:
    // ============ 构造函数 ============
    
    /**
     * @brief 创建HTTP请求构建器
     * @param url 请求URL
     */
    explicit HttpRequestBuilder(std::string url);
    
    /**
     * @brief 创建HTTP请求构建器（通过工厂函数风格）
     * @param url 请求URL
     * @return HttpRequestBuilder实例
     */
    static HttpRequestBuilder create(std::string url);
    
    // ============ HTTP方法设置 ============
    
    /**
     * @brief 设置HTTP方法
     * @param method HTTP方法（GET, POST, PUT, DELETE等）
     * @return 当前构建器引用
     */
    HttpRequestBuilder& method(std::string_view method);
    
    // 便捷方法
    HttpRequestBuilder& get();
    HttpRequestBuilder& post();
    HttpRequestBuilder& put();
    HttpRequestBuilder& delete_();
    HttpRequestBuilder& patch();
    HttpRequestBuilder& head();
    HttpRequestBuilder& options();
    
    // ============ 请求体设置 ============
    
    /**
     * @brief 设置请求体（字符串）
     */
    HttpRequestBuilder& body(std::string body);
    HttpRequestBuilder& body(std::string_view body);
    
    /**
     * @brief 设置JSON请求体
     */
    HttpRequestBuilder& json(std::string json);
    HttpRequestBuilder& json(std::string_view json);
    
    /**
     * @brief 设置表单数据
     */
    HttpRequestBuilder& form(const std::map<std::string, std::string>& form_data);
    HttpRequestBuilder& form(const std::unordered_map<std::string, std::string>& form_data);
    
    /**
     * @brief 添加单个表单字段
     */
    HttpRequestBuilder& form_field(std::string name, std::string value);
    
    /**
     * @brief 设置multipart/form-data（文件上传）
     */
    HttpRequestBuilder& multipart();
    HttpRequestBuilder& add_file(std::string field_name, 
                                 std::string filename, 
                                 std::string content_type,
                                 const std::vector<uint8_t>& data);
    
    // ============ 请求头设置 ============
    
    /**
     * @brief 添加请求头
     */
    HttpRequestBuilder& header(std::string name, std::string value);
    HttpRequestBuilder& header(std::string_view name, std::string_view value);
    
    // 常用头部的便捷方法
    HttpRequestBuilder& content_type(std::string_view type);
    HttpRequestBuilder& accept(std::string_view type);
    HttpRequestBuilder& accept_json();
    HttpRequestBuilder& accept_xml();
    HttpRequestBuilder& user_agent(std::string_view agent);
    HttpRequestBuilder& referer(std::string_view referer);
    HttpRequestBuilder& origin(std::string_view origin);
    
    // 认证相关
    HttpRequestBuilder& authorization(std::string_view scheme, std::string_view credentials);
    HttpRequestBuilder& bearer_token(std::string_view token);
    HttpRequestBuilder& basic_auth(std::string_view username, std::string_view password);
    
    // ============ Cookie管理 ============
    
    HttpRequestBuilder& cookie(std::string name, std::string value);
    HttpRequestBuilder& cookies(const std::map<std::string, std::string>& cookies);
    
    // ============ 查询参数 ============
    
    HttpRequestBuilder& query_param(std::string name, std::string value);
    HttpRequestBuilder& query_params(const std::map<std::string, std::string>& params);
    
    // ============ 超时设置 ============
    
    HttpRequestBuilder& timeout(std::chrono::milliseconds timeout);
    HttpRequestBuilder& connect_timeout(std::chrono::milliseconds timeout);
    HttpRequestBuilder& read_timeout(std::chrono::milliseconds timeout);
    HttpRequestBuilder& write_timeout(std::chrono::milliseconds timeout);
    
    // 便捷方法
    HttpRequestBuilder& timeout_seconds(int seconds);
    HttpRequestBuilder& timeout_milliseconds(int ms);
    
    // ============ 代理设置 ============
    
    HttpRequestBuilder& proxy(std::string host, uint16_t port);
    HttpRequestBuilder& proxy(std::string_view host, uint16_t port);
    HttpRequestBuilder& proxy_auth(std::string username, std::string password);
    
    // ============ SSL/安全设置 ============
    
    HttpRequestBuilder& verify_ssl(bool verify = true);
    HttpRequestBuilder& ssl_cert_path(std::string cert_path);
    HttpRequestBuilder& ssl_key_path(std::string key_path);
    HttpRequestBuilder& ssl_ca_path(std::string ca_path);
    
    // ============ 重定向设置 ============
    
    HttpRequestBuilder& follow_redirects(bool follow = true);
    HttpRequestBuilder& max_redirects(int max_redirects);
    
    // ============ 其他设置 ============
    
    HttpRequestBuilder& keep_alive(bool keep_alive = true);
    HttpRequestBuilder& compress(bool compress = true);
    HttpRequestBuilder& chunked(bool chunked = true);
    
    // ============ 构建和执行 ============
    
    /**
     * @brief 构建HttpRequest对象
     * @return 配置好的HttpRequest对象
     * @throws std::runtime_error 如果验证失败
     */
    HttpRequest build() const;
    
    /**
     * @brief 验证当前配置是否有效
     * @return true如果配置有效
     */
    bool validate() const;
    
    /**
     * @brief 获取验证错误信息
     * @return 错误信息列表
     */
    std::vector<std::string> get_errors() const;
    
    /**
     * @brief 同步执行HTTP请求
     * @return HTTP响应
     */
    HttpResponse execute() const;
    
    /**
     * @brief 异步执行HTTP请求
     * @return future对象，包含HTTP响应
     */
    std::future<HttpResponse> execute_async() const;
    
    /**
     * @brief 使用回调异步执行HTTP请求
     * @param callback 回调函数
     */
    void execute_async(std::function<void(HttpResponse)> callback) const;
    
    // ============ 工具方法 ============
    
    /**
     * @brief 重置构建器（保留URL）
     */
    HttpRequestBuilder& reset();
    
    /**
     * @brief 克隆当前构建器
     */
    HttpRequestBuilder clone() const;
    
private:
    // 私有辅助方法
    void validate_method() const;
    void validate_url() const;
    void prepare_headers()const;
    void prepare_body()const;
    void prepare_query_string()const;
    
    // 编码工具
    static std::string url_encode(std::string_view str);
    static std::string form_url_encode(const std::map<std::string, std::string>& data);
    
    // 验证相关
    bool is_valid_method(std::string_view method) const;
    bool is_valid_url(std::string_view url) const;
    
    // 数据成员
    mutable HttpRequest request_;
    mutable std::vector<std::string> errors_;
    
    // 静态常量
    static const std::unordered_set<std::string> valid_methods_;
    static const std::unordered_set<std::string> content_type_to_method_;
};

// ============ 工厂函数 ============

/**
 * @brief 创建GET请求构建器
 */
inline HttpRequestBuilder get(std::string url) {
    return HttpRequestBuilder(std::move(url)).get();
}

/**
 * @brief 创建POST请求构建器
 */
inline HttpRequestBuilder post(std::string url) {
    return HttpRequestBuilder(std::move(url)).post();
}

/**
 * @brief 创建JSON POST请求构建器
 */
inline HttpRequestBuilder post_json(std::string url, std::string json) {
    return HttpRequestBuilder(std::move(url)).post().json(std::move(json));
}

/**
 * @brief 创建表单POST请求构建器
 */
inline HttpRequestBuilder post_form(std::string url, 
                                    const std::map<std::string, std::string>& form_data) {
    return HttpRequestBuilder(std::move(url)).post().form(form_data);
}

} // namespace net