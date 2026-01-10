// network_manager.hpp
#pragma once
#include "net_facade.h"

namespace foundation {

class NetworkManager {
private:
    std::unique_ptr<net::HttpClient> http_client_;
    std::unique_ptr<net::WebSocketConnection> websocket_;
    
public:
    NetworkManager() 
        : http_client_(net::NetFacade::createHttpClient()) {
    }
    
    net::HttpClient& http() { return *http_client_; }
    
    net::WebSocketConnection& websocket() {
        if (!websocket_) {
            websocket_ = net::NetFacade::createWebSocket();
        }
        return *websocket_;
    }
    
    // 工具函数
    static std::string urlEncode(const std::string& str) {
        return net::NetFacade::urlEncode(str);
    }
    
    static std::string urlDecode(const std::string& str) {
        return net::NetFacade::urlDecode(str);
    }
};

} // namespace foundation