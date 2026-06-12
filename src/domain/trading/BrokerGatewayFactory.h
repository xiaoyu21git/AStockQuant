#pragma once

#include "IBrokerGateway.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace domain {
namespace trading {

// ============================================================
// BrokerGatewayFactory — 券商网关工厂 (纯 C++, 零 Qt)
//
// 启动时注册所有可用网关, 根据配置创建实例。
// ============================================================

class BrokerGatewayFactory {
public:
    using Creator = std::function<std::unique_ptr<IBrokerGateway>()>;

    static BrokerGatewayFactory& instance();

    // 注册网关
    void registerGateway(const std::string& name, Creator creator);

    // 根据名称创建网关实例 (用于启动时配置选择)
    std::unique_ptr<IBrokerGateway> createGateway(const std::string& name) const;

    // 列出所有已注册网关
    std::vector<std::string> registeredGateways() const;

    // 预设 capbability 查询能力 (用于上层判断而不创建实例)
    BrokerCapability queryCapability(const std::string& name) const;
    void registerCapability(const std::string& name, BrokerCapability capability);

private:
    BrokerGatewayFactory() = default;

    std::unordered_map<std::string, Creator> m_creators;
    std::unordered_map<std::string, BrokerCapability> m_capabilities;
};

} // namespace trading
} // namespace domain