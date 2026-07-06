#include "../include/EventRiskSubscriber.h"
#include "../include/RiskManager.h"
#include "../include/RiskEvaluator.h"
#include "../../../engine/include/GlobalEventBusRegistry.h"
#include "foundation/log/logging.hpp"

#include <algorithm>
#include <string>

namespace domain::strategy {

EventRiskSubscriber::EventRiskSubscriber() = default;

EventRiskSubscriber::~EventRiskSubscriber() {
    stop();
}

// ═══════════════════════════════════════════════════════════════
// 生命周期
// ═══════════════════════════════════════════════════════════════

void EventRiskSubscriber::start() {
    m_bus = engine::get_engine_event_bus();
    if (!m_bus) {
        INTERNAL_WARN_STREAM << "[EventRisk] EventBus not available";
        return;
    }

    engine::EventFormatHandler handler =
        [this](const engine::EventFormat& event) {
            this->onFinancialEvent(event);
        };

    m_subscriptionId = m_bus->subscribe(
        std::string(engine::EventTypes::NEWS_ALL),  // "news."
        std::move(handler),
        nullptr,   // 无 filter
        0          // 默认优先级
    );

    INTERNAL_INFO_STREAM << "[EventRisk] 已订阅 news.* 事件";
}

void EventRiskSubscriber::stop() {
    if (m_bus && m_subscriptionId.is_valid()) {
        m_bus->unsubscribe(m_subscriptionId);
        m_subscriptionId = foundation::utils::Uuid{};
        INTERNAL_INFO_STREAM << "[EventRisk] 已退订";
    }
}

// ═══════════════════════════════════════════════════════════════
// EventBus 回调
// ═══════════════════════════════════════════════════════════════

void EventRiskSubscriber::onFinancialEvent(
    const engine::EventFormat& event)
{
    // 解析标的列表
    auto symbolsOpt = event.get<std::vector<std::string>>("symbols");
    if (!symbolsOpt || symbolsOpt->empty()) return;

    // 解析标签
    std::unordered_map<std::string, std::string> tags;
    for (const auto& kv : event.metadata) {
        if (kv.first.find("tag.") == 0) {
            tags[kv.first.substr(4)] = kv.second;
        }
    }

    // 解析情感分
    double sentiment = 0.0;
    auto sOpt = event.get<std::string>("sentiment_score");
    if (sOpt) {
        try { sentiment = std::stod(*sOpt); } catch (...) {}
    }

    // 解析置信度
    double confidence = 0.0;
    auto cOpt = event.get<std::string>("confidence");
    if (cOpt) {
        try { confidence = std::stod(*cOpt); } catch (...) {}
        confidence = (std::max)(0.0, (std::min)(1.0, confidence));
    }

    // 对每个关联标的执行风控动作
    for (const auto& sym : *symbolsOpt) {
        applyEventTags(event.type, sym, tags, sentiment, confidence);
    }
}

void EventRiskSubscriber::applyEventTags(
    const std::string& eventType,
    const std::string& symbol,
    const std::unordered_map<std::string, std::string>& tags,
    double sentiment,
    double confidence)
{
    bool configChanged = false;
    auto config = RiskManager::instance().riskConfig();

    // 记录变更前的值 (用于日志)
    const double oldStopLoss = config.stopLossPercent;
    const double oldPositionLimit = config.maxPositionPercent;
    const double oldExposure = config.maxTotalExposurePercent;

    // ── 规则1: 立案调查 → 封禁开仓 + 收紧止损 ──
    auto it = tags.find("立案调查");
    if (it != tags.end() && it->second == "true") {
        m_blockedSymbols.insert(symbol);

        if (m_overrides.find(symbol) == m_overrides.end()) {
            m_overrides[symbol] = {config.stopLossPercent,
                                   config.maxPositionPercent};
        }
        config.stopLossPercent = (std::min)(config.stopLossPercent, 5.0);
        config.maxPositionPercent =
            (std::min)(config.maxPositionPercent, 2.0);
        configChanged = true;
    }

    // ── 规则2: ST 警示 → 封禁开仓 + 降仓位上限 ──
    it = tags.find("ST警示");
    if (it != tags.end() && it->second == "true") {
        m_blockedSymbols.insert(symbol);

        if (m_overrides.find(symbol) == m_overrides.end()) {
            m_overrides[symbol] = {config.stopLossPercent,
                                   config.maxPositionPercent};
        }
        config.maxPositionPercent =
            (std::min)(config.maxPositionPercent, 2.0);
        configChanged = true;
    }

    // ── 规则3: 高置信度负面政策 → 降总敞口 ──
    if (sentiment < -0.7 && confidence > 0.8 &&
        eventType == engine::EventTypes::NEWS_POLICY)
    {
        config.maxTotalExposurePercent =
            (std::min)(config.maxTotalExposurePercent, 80.0);
        configChanged = true;
    }

    // ── 应用变更 ──
    if (configChanged) {
        INTERNAL_WARN_STREAM
            << "[EventRisk] 风控参数变更:"
            << " trigger=" << eventType
            << " symbol=" << symbol
            << " stopLoss: " << oldStopLoss
                << "% -> " << config.stopLossPercent << "%"
            << " positionLimit: " << oldPositionLimit
                << "% -> " << config.maxPositionPercent << "%"
            << " totalExposure: " << oldExposure
                << "% -> " << config.maxTotalExposurePercent << "%";

        RiskManager::instance().setRiskConfig(config);
    }
}

} // namespace domain::strategy
