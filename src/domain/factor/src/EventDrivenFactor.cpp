#include "EventDrivenFactor.h"

#include "FactorInstanceManager.h"
#include "foundation/json/json_facade.h"
#include "foundation/log/logging.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

namespace factor {

// ═══════════════════════════════════════════════════════════════
// Params — JSON 配置解析
// ═══════════════════════════════════════════════════════════════

void EventDrivenFactor::Params::fromJson(
    const foundation::json::JsonFacade& json)
{
    CommonParams::fromJson(json);
    if (json.has("sentimentWeight"))
        sentimentWeight = json.get("sentimentWeight").asDouble();
    if (json.has("superExpectedBonus"))
        superExpectedBonus = json.get("superExpectedBonus").asDouble();
    if (json.has("investigationPenalty"))
        investigationPenalty = json.get("investigationPenalty").asDouble();
    if (json.has("maxEventAgeHours"))
        maxEventAgeHours = json.get("maxEventAgeHours").asInt();
    if (json.has("maxRecordsPerSymbol"))
        maxRecordsPerSymbol = json.get("maxRecordsPerSymbol").asInt();
}

void EventDrivenFactor::loadConfig(
    const foundation::json::JsonFacade& config)
{
    m_params.fromJson(config);
    BaseFactor::loadConfig(config);
}

// ═══════════════════════════════════════════════════════════════
// EventBus 回调 — 写入事件缓存
// ═══════════════════════════════════════════════════════════════

void EventDrivenFactor::onEvent(const engine::EventFormat& event) {
    // 解析关联标的列表 (EventFormat::get<T> 返回 optional<T>)
    auto symbolsOpt = event.get<std::vector<std::string>>("symbols");
    if (!symbolsOpt || symbolsOpt->empty()) return;

    // 解析情感分 (string → double, 防御非法值)
    double sentiment = 0.0;
    auto sentimentOpt = event.get<std::string>("sentiment_score");
    if (sentimentOpt) {
        try {
            size_t pos = 0;
            sentiment = std::stod(*sentimentOpt, &pos);
            if (pos != sentimentOpt->size() || !std::isfinite(sentiment))
                sentiment = 0.0;
        } catch (const std::exception&) {
            INTERNAL_WARN_STREAM
                << "[EventDrivenFactor] invalid sentiment: " << *sentimentOpt;
            sentiment = 0.0;
        }
    }

    // 解析置信度
    double confidence = 0.0;
    auto confOpt = event.get<std::string>("confidence");
    if (confOpt) {
        try { confidence = std::stod(*confOpt); } catch (...) {}
        confidence = (std::max)(0.0, (std::min)(1.0, confidence));
    }

    // 解析标签 (metadata 中以 "tag." 为前缀的键)
    std::unordered_map<std::string, std::string> tags;
    for (const auto& kv : event.metadata) {
        if (kv.first.find("tag.") == 0) {
            tags[kv.first.substr(4)] = kv.second;
        }
    }

    // 解析时间戳 (metadata 中)
    int64_t ts = 0;
    auto tsIt = event.metadata.find("timestamp");
    if (tsIt != event.metadata.end()) {
        try { ts = std::stoll(tsIt->second); } catch (...) {}
    }
    if (ts == 0) {
        ts = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // 写入缓存
    EventRecord record{event.type, sentiment, confidence, tags, ts};

    std::unique_lock<std::shared_mutex> lock(m_cacheMutex);
    for (const auto& sym : *symbolsOpt) {
        auto& records = m_eventCache[sym];
        records.push_back(record);
        if (static_cast<int>(records.size()) > m_params.maxRecordsPerSymbol) {
            records.erase(records.begin());
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// 因子计算
// ═══════════════════════════════════════════════════════════════

double EventDrivenFactor::computeSignalScore(
    const std::string& /*symbol*/,
    const std::vector<EventRecord>& records) const
{
    if (records.empty()) return 0.0;

    double totalScore = 0.0;
    int count = 0;

    for (const auto& r : records) {
        double eventScore = 0.0;

        // 1. 情感分贡献
        eventScore += r.sentimentScore * m_params.sentimentWeight;

        // 2. 标签调整
        auto tagsIt = r.tags.find("超预期");
        if (tagsIt != r.tags.end() && tagsIt->second == "true") {
            eventScore += m_params.superExpectedBonus;
        }
        tagsIt = r.tags.find("低于预期");
        if (tagsIt != r.tags.end() && tagsIt->second == "true") {
            eventScore -= m_params.superExpectedBonus;
        }
        tagsIt = r.tags.find("立案调查");
        if (tagsIt != r.tags.end() && tagsIt->second == "true") {
            eventScore -= m_params.investigationPenalty;
        }
        tagsIt = r.tags.find("ST警示");
        if (tagsIt != r.tags.end() && tagsIt->second == "true") {
            eventScore -= m_params.investigationPenalty;
        }

        // 3. 置信度加权
        eventScore *= r.confidence;

        totalScore += eventScore;
        ++count;
    }

    return count > 0 ? totalScore / static_cast<double>(count) : 0.0;
}

CalculationResult EventDrivenFactor::calculate(
    const CalculationContext& ctx)
{
    CalculationResult result;

    const auto nowMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    const int64_t cutoff =
        nowMs - static_cast<int64_t>(m_params.maxEventAgeHours) * 3600000LL;

    // 读锁 + 惰性清理过期事件
    std::unordered_map<std::string, std::vector<EventRecord>> localCache;
    {
        std::shared_lock<std::shared_mutex> lock(m_cacheMutex);
        for (const auto& pair : m_eventCache) {
            auto& local = localCache[pair.first];
            local.reserve(pair.second.size());
            for (const auto& r : pair.second) {
                if (r.timestampMs >= cutoff) {
                    local.push_back(r);
                }
            }
        }
    }

    // 计算每个标的的因子值
    for (const auto& sym : ctx.symbols) {
        auto it = localCache.find(sym);
        double score = (it != localCache.end())
            ? computeSignalScore(sym, it->second)
            : 0.0;
        if (std::isfinite(score)) {
            result.values[sym] = score;
        }
    }

    // 标准化
    if (m_params.standardization != StandardizationMethod::None) {
        applyCommonStandardization(result.values, m_params.standardization);
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════
// 边界规则
// ═══════════════════════════════════════════════════════════════

BoundaryRules EventDrivenFactor::getBoundaryRules() const {
    BoundaryRules rules;
    rules.handleNewStock = NewStockHandling::INCLUDE;       // 事件因子对次新股有效
    rules.handleSuspended = SuspendedHandling::EXCLUDE;     // 停牌股无成交, 排除
    rules.handleDelisted = DelistedHandling::EXCLUDE;       // 退市股排除
    rules.handleOutliers = OutlierHandling::WINSORIZE_3SIGMA;
    return rules;
}

// ═══════════════════════════════════════════════════════════════
// 回测兼容 — 加载历史事件
// ═══════════════════════════════════════════════════════════════

void EventDrivenFactor::loadHistoricalEvents(
    const std::vector<engine::EventFormat>& events)
{
    std::unique_lock<std::shared_mutex> lock(m_cacheMutex);
    for (const auto& event : events) {
        auto symbolsOpt = event.get<std::vector<std::string>>("symbols");
        if (!symbolsOpt || symbolsOpt->empty()) continue;

        double sentiment = 0.0;
        auto sOpt = event.get<std::string>("sentiment_score");
        if (sOpt) {
            try { sentiment = std::stod(*sOpt); } catch (...) {}
        }

        double confidence = 0.0;
        auto cOpt = event.get<std::string>("confidence");
        if (cOpt) {
            try { confidence = std::stod(*cOpt); } catch (...) {}
            confidence = (std::max)(0.0, (std::min)(1.0, confidence));
        }

        std::unordered_map<std::string, std::string> tags;
        for (const auto& kv : event.metadata) {
            if (kv.first.find("tag.") == 0) {
                tags[kv.first.substr(4)] = kv.second;
            }
        }

        int64_t ts = 0;
        auto tsIt = event.metadata.find("timestamp");
        if (tsIt != event.metadata.end()) {
            try { ts = std::stoll(tsIt->second); } catch (...) {}
        }

        EventRecord record{event.type, sentiment, confidence, tags, ts};
        for (const auto& sym : *symbolsOpt) {
            m_eventCache[sym].push_back(record);
        }
    }
}

void EventDrivenFactor::clearCache() {
    std::unique_lock<std::shared_mutex> lock(m_cacheMutex);
    m_eventCache.clear();
}

// ═══════════════════════════════════════════════════════════════
// 工厂方法
// ═══════════════════════════════════════════════════════════════

std::unique_ptr<BaseFactor> EventDrivenFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> /*checker*/)
{
    auto factor = std::make_unique<EventDrivenFactor>();
    factor->loadConfig(info.config);
    return factor;
}

} // namespace factor
