#include "../include/EventRiskSubscriber.h"
#include "../include/RiskManager.h"
#include "../include/RiskEvaluator.h"
#include "../../../engine/include/GlobalEventBusRegistry.h"
#include "foundation/log/logging.hpp"

#include <algorithm>
#include <sstream>
#include <string>

namespace domain::strategy {

EventRiskSubscriber& EventRiskSubscriber::instance() {
    static EventRiskSubscriber s;
    return s;
}

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
    // 解析分级元数据 (Python 侧写入 event.tags, 序列化为 metadata["tag.xxx"])
    auto tagGet = [&](const std::string& key) -> std::string {
        auto it = event.metadata.find("tag." + key);
        return (it != event.metadata.end()) ? it->second : "";
    };
    std::string level   = tagGet("level");
    std::string action  = tagGet("action");
    std::string sevStr  = tagGet("severity");
    std::string title   = tagGet("cm_0_name");  // commodity event name
    double severityVal  = 0.0;
    try { severityVal = std::stod(tagGet("severity_val")); } catch (...) {}

    if (level.empty() || action == "ignore") return;

    // 解析标的列表 (仅 stock 级别)
    auto symbolsOpt = event.get<std::vector<std::string>>("symbols");
    std::vector<std::string> symbols;
    if (symbolsOpt) symbols = *symbolsOpt;

    // ── stock 级别 ──
    if (level == "stock" && !symbols.empty()) {
        for (const auto& sym : symbols) {
            if (action == "liquidate") {
                m_liquidatedSymbols.insert(sym);
                INTERNAL_WARN_STREAM << "[EventRisk] 个股利空清仓: " << sym
                    << " severity=" << severityVal
                    << " title=" << tagGet("commodity_event_name");
            } else if (action == "reduce_position") {
                tightenSingleSymbolLimit(sym, 2.0);
            }
        }
    }
    // ── sector 级别 ──
    else if (level == "sector") {
        std::string sectorCodes = tagGet("sector_codes");
        std::string productId   = tagGet("cm_0_pid");
        if (!sectorCodes.empty() || !productId.empty()) {
            if (action == "reduce_exposure") {
                capSectorExposure(sectorCodes, productId, 30.0);
            } else if (action == "reduce_position") {
                capSectorExposure(sectorCodes, productId, 50.0);
            }
        }
    }
    // ── market 级别 ──
    else if (level == "market") {
        auto& risk = domain::strategy::RiskManager::instance();
        auto cfg = risk.riskConfig();
        if (action == "reduce_exposure" && severityVal >= 0.5) {
            cfg.maxTotalExposurePercent = (std::min)(cfg.maxTotalExposurePercent, 50.0);
            INTERNAL_WARN_STREAM << "[EventRisk] 系统性风险: 总敞口降至50% severity="
                << severityVal;
        } else if (action == "alert") {
            cfg.maxTotalExposurePercent = (std::min)(cfg.maxTotalExposurePercent, 80.0);
            INTERNAL_INFO_STREAM << "[EventRisk] 市场预警: 总敞口降至80%";
        }
        risk.setRiskConfig(cfg);
    }
}

void EventRiskSubscriber::tightenSingleSymbolLimit(
    const std::string& symbol, double maxPct)
{
    auto cfg = RiskManager::instance().riskConfig();
    const double old = cfg.maxPositionPercent;
    cfg.maxPositionPercent = (std::min)(cfg.maxPositionPercent, maxPct);
    if (cfg.maxPositionPercent < old) {
        INTERNAL_WARN_STREAM << "[EventRisk] 单股限仓: " << symbol
            << " " << old << "% -> " << cfg.maxPositionPercent << "%";
        RiskManager::instance().setRiskConfig(cfg);
    }
}

void EventRiskSubscriber::capSectorExposure(
    const std::string& sectorCodes,
    const std::string& productId,
    double maxPct)
{
    // 解析逗号分隔的行业代码
    std::stringstream ss(sectorCodes.empty() ? productId : sectorCodes);
    std::string code;
    while (std::getline(ss, code, ',')) {
        code = code.empty() ? "" : code;
        if (code.empty()) continue;
        auto it = m_sectorLimits.find(code);
        if (it == m_sectorLimits.end() || it->second > maxPct) {
            m_sectorLimits[code] = maxPct;
            INTERNAL_WARN_STREAM << "[EventRisk] 行业限仓: " << code
                << " -> " << maxPct << "%";
        }
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
