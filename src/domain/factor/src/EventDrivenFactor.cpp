#include "EventDrivenFactor.h"
#include "CommodityEventDetector.h"

#include "FactorInstanceManager.h"
#include "foundation/json/json_facade.h"
#include "foundation/log/logging.hpp"
#include "infrastructure/include/database/NativePgConnectionPool.h"
#include "infrastructure/include/database/ISqlDatabase.h"
#include "../../../engine/include/GlobalEventBusRegistry.h"
#include "../../../engine/include/Event/EventBus.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>

namespace factor {

// ═══════════════════════════════════════════════════════════════
// 静态实例注册 (广播 EventBus 事件到所有活跃实例)
// ═══════════════════════════════════════════════════════════════

static std::vector<EventDrivenFactor*> s_instances;
static std::mutex s_instanceMutex;

void EventDrivenFactor::registerInstance(EventDrivenFactor* instance) {
    std::lock_guard<std::mutex> lock(s_instanceMutex);
    s_instances.push_back(instance);
}

void EventDrivenFactor::unregisterInstance(EventDrivenFactor* instance) {
    std::lock_guard<std::mutex> lock(s_instanceMutex);
    auto it = std::find(s_instances.begin(), s_instances.end(), instance);
    if (it != s_instances.end()) s_instances.erase(it);
}

void EventDrivenFactor::subscribeToEventBus() {
    auto* bus = engine::get_engine_event_bus();
    if (!bus) {
        INTERNAL_WARN_STREAM << "[EventDriven] EventBus 不可用, 跳过订阅";
        return;
    }
    engine::EventFormatHandler handler = [](const engine::EventFormat& event) {
        std::lock_guard<std::mutex> lock(s_instanceMutex);
        for (auto* instance : s_instances) {
            instance->onEvent(event);
        }
    };
    bus->subscribe(
        std::string(engine::EventTypes::NEWS_ALL),  // "news."
        std::move(handler),
        nullptr, 0);
    INTERNAL_INFO_STREAM << "[EventDriven] 已订阅 news.* 事件";
}

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
    // ── 商品突发事件检测 (在解析标的前) ──
    static CommodityEventDetector s_commodityDetector;  // 无DB连接时仅日志
    s_commodityDetector.onNewsEvent(event);

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

void EventDrivenFactor::loadEventsFromDb(const std::string& startDate,
                                          const std::string& endDate)
{
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) {
        INTERNAL_WARN_STREAM << "[EventDriven] loadEventsFromDb: DB不可用";
        return;
    }

    using P = astock::database::SqlParam;
    // 1. 查询事件
    auto eventsRes = db->executeQuery(
        "SELECT product_id, event_type, event_name, urgency, direction, title, source "
        "FROM alpha.commodity_event_signals "
        "WHERE created_at::date BETWEEN ? AND ? "
        "ORDER BY created_at",
        {P{startDate}, P{endDate}});

    if (eventsRes.isEmpty()) {
        INTERNAL_INFO_STREAM << "[EventDriven] loadEventsFromDb: "
                             << startDate << "~" << endDate << " 无商品事件";
        return;
    }

    // 2. 查询 product_stock_mapping 构建 product→symbols 映射
    auto mappingRes = db->executeQuery(
        "SELECT product_id, symbol FROM ref.product_stock_mapping "
        "WHERE effective_date <= ? AND expired_date >= ?",
        {P{endDate}, P{startDate}});

    std::unordered_map<std::string, std::vector<std::string>> productStocks;
    for (std::size_t i = 0; i < mappingRes.rowCount(); ++i) {
        auto& row = mappingRes.getRow(i);
        productStocks[row.getString("product_id")].push_back(row.getString("symbol"));
    }

    // 3. 转换为 EventFormat 并注入缓存
    std::vector<engine::EventFormat> formats;
    for (std::size_t i = 0; i < eventsRes.rowCount(); ++i) {
        auto& row = eventsRes.getRow(i);
        std::string productId = row.getString("product_id");
        double urgency = row.getDouble("urgency");
        int direction = row.getInt("direction");

        auto symbolsIt = productStocks.find(productId);
        if (symbolsIt == productStocks.end() || symbolsIt->second.empty())
            continue;

        // sentiment_score = urgency * direction (商品事件→情感分)
        double sentiment = urgency * direction;

        engine::EventFormat evt;
        evt.type = "commodity_event";
        evt.set("symbols", symbolsIt->second);
        evt.set("sentiment_score", std::to_string(sentiment));
        evt.set("confidence", std::to_string(urgency));
        evt.set("title", row.getString("title"));
        evt.metadata["event_type"] = row.getString("event_type");
        evt.metadata["event_name"] = row.getString("event_name");
        evt.metadata["product_id"] = productId;
        formats.push_back(std::move(evt));
    }

    INTERNAL_INFO_STREAM << "[EventDriven] loadEventsFromDb: "
                         << eventsRes.rowCount() << " events → "
                         << formats.size() << " injected ("
                         << startDate << "~" << endDate << ")";

    loadHistoricalEvents(formats);
}

// ═══════════════════════════════════════════════════════════════
// 工厂方法
// ═══════════════════════════════════════════════════════════════

EventDrivenFactor::~EventDrivenFactor() {
    unregisterInstance(this);
}

std::unique_ptr<BaseFactor> EventDrivenFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> /*checker*/)
{
    auto factor = std::make_unique<EventDrivenFactor>();
    factor->loadConfig(info.config);
    registerInstance(factor.get());
    return factor;
}

} // namespace factor
