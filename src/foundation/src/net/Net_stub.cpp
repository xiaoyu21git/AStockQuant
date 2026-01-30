#include "foundation/net/Net_facade.h"

namespace foundation::net {

// Minimal stub implementations when libcurl is not available.

HttpClient::HttpClient() : impl_(nullptr) {}
HttpClient::~HttpClient() = default;

void HttpClient::setDefaultTimeout(std::chrono::milliseconds) {}
void HttpClient::setDefaultHeaders(const std::multimap<std::string, std::string>&) {}
void HttpClient::addDefaultHeader(const std::string&, const std::string&) {}

HttpResponse HttpClient::get(const std::string&){ return HttpResponse{0, "", "", {}, std::chrono::milliseconds(0)}; }
HttpResponse HttpClient::post(const std::string&, const std::string&){ return HttpResponse{0, "", "", {}, std::chrono::milliseconds(0)}; }
HttpResponse HttpClient::put(const std::string&, const std::string&){ return HttpResponse{0, "", "", {}, std::chrono::milliseconds(0)}; }
HttpResponse HttpClient::delete_(const std::string&){ return HttpResponse{0, "", "", {}, std::chrono::milliseconds(0)}; }
HttpResponse HttpClient::send(const HttpRequest&){ return HttpResponse{0, "", "", {}, std::chrono::milliseconds(0)}; }
void HttpClient::getAsync(const std::string&, Callback){ }
void HttpClient::postAsync(const std::string&, const std::string&, Callback){ }
void HttpClient::sendAsync(const HttpRequest&, Callback){ }
std::vector<HttpResponse> HttpClient::batchSend(const std::vector<HttpRequest>&){ return {}; }
void HttpClient::set_timeout(std::chrono::milliseconds) {}
void HttpClient::set_proxy(const std::string&, uint16_t) {}

// WebSocket stub
WebSocketConnection::WebSocketConnection(std::unique_ptr<Impl> impl) : impl_(nullptr) {}

bool WebSocketConnection::connect(const std::string&){ return false; }
bool WebSocketConnection::isConnected() const { return false; }
void WebSocketConnection::disconnect() {}
bool WebSocketConnection::sendText(const std::string&) { return false; }
bool WebSocketConnection::sendBinary(const std::vector<uint8_t>&) { return false; }
bool WebSocketConnection::receive(WebSocketMessage&, std::chrono::milliseconds) { return false; }
bool WebSocketConnection::tryReceive(WebSocketMessage&) { return false; }
void WebSocketConnection::setMessageHandler(MessageHandler) {}
void WebSocketConnection::setErrorHandler(ErrorHandler) {}
void WebSocketConnection::runEventLoop() {}
void WebSocketConnection::stopEventLoop() {}

// NetFacade factory stubs
std::unique_ptr<HttpClient> NetFacade::createHttpClient() { return std::make_unique<HttpClient>(); }
std::unique_ptr<WebSocketConnection> NetFacade::createWebSocket() { return std::make_unique<WebSocketConnection>(nullptr); }

std::string NetFacade::urlEncode(const std::string& str) { return str; }
std::string NetFacade::urlDecode(const std::string& str) { return str; }
std::map<std::string, std::string> NetFacade::parseQueryString(const std::string&) { return {}; }
std::string NetFacade::buildQueryString(const std::map<std::string, std::string>&) { return std::string(); }

} // namespace foundation::net
