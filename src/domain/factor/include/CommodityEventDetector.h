#pragma once
// CommodityEventDetector — C++ 商品突发事件检测器
// 订阅 EventBus news.* 事件, 关键词匹配 → 写入 PG 信号表
//
// 用法:
//   CommodityEventDetector detector(dbConnection);
//   detector.onNewsEvent(eventFormat);  // EventBus 回调中调用

#include "../../../engine/include/Event/EventFormat.hpp"
#include "../../../infrastructure/include/database/ISqlDatabase.h"

#include <string>
#include <vector>
#include <regex>
#include <unordered_map>
#include <unordered_set>

namespace factor {

/// @brief 单条商品事件检测结果
struct CommodityEventSignal {
    std::string productId;
    std::string eventType;    // supply_disruption / policy_restriction / trade_restriction
    std::string eventName;    // 事故停产 / 政策限产 / 贸易限制
    double urgency = 0.5;     // 0-1
    int direction = 1;        // +1=供给冲击(利多), -1=需求崩塌(利空)
};

/// @brief 商品突发事件检测器
class CommodityEventDetector {
public:
    explicit CommodityEventDetector(std::shared_ptr<astock::database::ISqlDatabase> db = nullptr)
        : m_db(std::move(db)) {}

    /// @brief 处理一条新闻事件, 检测商品信号
    /// @return 检测到的商品事件列表
    std::vector<CommodityEventSignal> detect(const engine::EventFormat& event);

    /// @brief EventBus 回调入口
    void onNewsEvent(const engine::EventFormat& event);

    /// @brief 检测计数
    int detectionCount() const { return m_detectionCount; }

private:
    struct EventPattern {
        std::regex pattern;
        std::string eventType;
        std::string eventName;
        double urgency;
    };

    void initPatterns();
    std::vector<std::string> matchCommodities(const std::string& text) const;
    void writeSignal(const engine::EventFormat& event,
                     const std::vector<CommodityEventSignal>& signals);

    std::shared_ptr<astock::database::ISqlDatabase> m_db;
    std::vector<EventPattern> m_patterns;
    std::unordered_map<std::string, std::vector<std::string>> m_commodityKeywords;
    bool m_patternsInitialized = false;
    int m_detectionCount = 0;
};

} // namespace factor
