#include "CommodityEventDetector.h"
#include "infrastructure/include/database/ISqlDatabase.h"
#include "foundation/log/logging.hpp"

#include <sstream>

namespace factor {

// ══════════════════════════════════════════════════════════════════════════════
// 事件模式库 — C++ 版 (与 Python commodity_hook.py 同步)
// ══════════════════════════════════════════════════════════════════════════════

void CommodityEventDetector::initPatterns()
{
    if (m_patternsInitialized) return;
    m_patternsInitialized = true;

    using EP = EventPattern;

    // 矿山/油田事故停产
    m_patterns = {
        EP{std::regex(R"((矿|油田|气田).{0,8}(事故|崩塌|透水|爆炸|火灾|停产|停工|关闭))"),
           "supply_disruption", "事故停产", 1.0},
        EP{std::regex(R"((地震|洪水|暴雨|台风|泥石流|暴雪).{0,10}(矿|油田|厂区|产区|主产区))"),
           "supply_disruption", "自然灾害", 0.8},
        EP{std::regex(R"((环保|能耗|碳达峰|限产|错峰|压减|去产能|淘汰落后).{0,15}(产能|产量|开工|生产))"),
           "policy_restriction", "政策限产", 0.7},
        EP{std::regex(R"((发改委|工信部|生态环境部|应急管理部).{0,15}(产能|限产|停产|整顿|关停))"),
           "policy_restriction", "行政限产", 0.8},
        EP{std::regex(R"((禁止|限制|叫停|暂停).{0,6}(进口|出口|通关|报关))"),
           "trade_restriction", "贸易限制", 0.8},
        EP{std::regex(R"((反倾销|反补贴|加征关税|制裁).{0,10}(进口|产品|商品))"),
           "trade_restriction", "贸易壁垒", 0.7},
        EP{std::regex(R"((港口|码头|铁路|管道).{0,8}(封闭|中断|停运|拥堵|罢工))"),
           "logistics_disruption", "运输中断", 0.8},
        EP{std::regex(R"((国储|收储|抛储|轮储|战略储备).{0,6}(铜|铝|锌|镍|棉花|白糖|橡胶|原油|猪肉))"),
           "reserve_operation", "国储操作", 0.9},
        EP{std::regex(R"((检修|大修|停产检修|投产|新产能|装置投产).{0,10}(万吨|产能|装置|PTA|甲醇|纯碱|尿素|乙烯|丙烯))"),
           "capacity_event", "装置变动", 0.6},
        EP{std::regex(R"((罢工|劳资|工会).{0,6}(矿|油田|港口))"),
           "supply_disruption", "罢工停产", 0.7},
        EP{std::regex(R"((疫情|封控|静默).{0,12}(产区|工厂|矿|码头|运输))"),
           "supply_disruption", "疫情封控", 0.6},
    };

    // 关键词 → product_id
    m_commodityKeywords = {
        {"锂", {"lithium_carbonate","lithium"}}, {"碳酸锂", {"lithium_carbonate"}},
        {"铜", {"copper"}}, {"铝", {"aluminum"}}, {"锌", {"zinc"}}, {"铅", {"lead"}},
        {"镍", {"nickel"}}, {"锡", {"tin"}}, {"黄金", {"gold"}}, {"白银", {"silver"}},
        {"铁矿石", {"iron_ore"}}, {"螺纹钢", {"rebar"}}, {"热轧", {"hot_rolled_coil"}},
        {"焦煤", {"coking_coal"}}, {"焦炭", {"coke"}}, {"动力煤", {"thermal_coal"}},
        {"煤", {"thermal_coal"}},
        {"原油", {"crude_oil"}}, {"石油", {"crude_oil"}}, {"天然气", {"natural_gas"}},
        {"PTA", {"pta"}}, {"乙二醇", {"ethylene_glycol"}}, {"聚丙烯", {"polypropylene"}},
        {"PVC", {"pvc"}}, {"甲醇", {"methanol"}}, {"纯碱", {"soda_ash"}},
        {"烧碱", {"caustic_soda"}}, {"尿素", {"urea"}}, {"苯乙烯", {"styrene"}},
        {"醋酸", {"acetic_acid"}}, {"钛白粉", {"titanium_dioxide"}},
        {"磷酸", {"phosphoric_acid"}}, {"硫酸", {"sulfuric_acid"}},
        {"水泥", {"cement"}}, {"玻璃", {"glass"}}, {"浮法", {"float_glass"}},
        {"豆粕", {"soybean_meal"}}, {"大豆", {"soybean"}}, {"豆油", {"soybean_oil"}},
        {"玉米", {"corn"}}, {"棕榈油", {"palm_oil"}}, {"菜油", {"rapeseed_oil"}},
        {"棉花", {"cotton"}}, {"白糖", {"sugar"}}, {"橡胶", {"rubber"}},
        {"纸浆", {"pulp"}}, {"生猪", {"live_hog"}}, {"猪", {"live_hog"}},
        {"鸡蛋", {"egg"}},
        {"稀土", {"rare_earth"}}, {"钨", {"tungsten"}}, {"钴", {"cobalt"}},
        {"工业硅", {"silicon_metal"}}, {"多晶硅", {"polysilicon"}},
        {"六氟磷酸锂", {"lipf6"}}, {"电解液", {"electrolyte"}},
        {"锂矿", {"lithium_carbonate","lithium"}},
        {"铜矿", {"copper"}}, {"铝矿", {"aluminum"}}, {"铁矿", {"iron_ore"}},
    };
}

// ══════════════════════════════════════════════════════════════════════════════

std::vector<std::string> CommodityEventDetector::matchCommodities(
    const std::string& text) const
{
    std::unordered_set<std::string> result;
    for (const auto& [keyword, pids] : m_commodityKeywords) {
        if (text.find(keyword) != std::string::npos) {
            for (const auto& pid : pids) {
                result.insert(pid);
            }
        }
    }
    return {result.begin(), result.end()};
}

std::vector<CommodityEventSignal> CommodityEventDetector::detect(
    const engine::EventFormat& event)
{
    initPatterns();
    std::vector<CommodityEventSignal> results;

    // 获取新闻文本: 从 data["title"] + data["summary"]
    std::string text;
    if (auto t = event.get<std::string>("title")) {
        text += *t;
    }
    text += " ";
    if (auto s = event.get<std::string>("summary")) {
        text += *s;
    }
    if (text.size() < 4) return results;

    // 1. 匹配事件模式
    const EventPattern* bestPattern = nullptr;
    for (const auto& ep : m_patterns) {
        if (std::regex_search(text, ep.pattern)) {
            if (!bestPattern || ep.urgency > bestPattern->urgency) {
                bestPattern = &ep;
            }
        }
    }
    if (!bestPattern) return results;

    // 2. 匹配商品
    auto commodities = matchCommodities(text);
    if (commodities.empty()) return results;

    // 3. 方向: supply/policy/logistics/reserve → +1, capacity → -1
    int direction = (bestPattern->eventType == "capacity_event") ? -1 : 1;

    for (const auto& pid : commodities) {
        results.push_back({
            pid,
            bestPattern->eventType,
            bestPattern->eventName,
            bestPattern->urgency,
            direction
        });
    }

    return results;
}

void CommodityEventDetector::onNewsEvent(const engine::EventFormat& event)
{
    auto signals = detect(event);
    if (!signals.empty()) {
        ++m_detectionCount;
        INTERNAL_INFO_STREAM << "[CommodityDetector] 检测到 " << signals.size()
                             << " 个商品事件信号 (累计 " << m_detectionCount << ")";

        if (auto t = event.get<std::string>("title")) {
            INTERNAL_INFO_STREAM << "  新闻: " << t->substr(0, 80);
        }
        for (const auto& s : signals) {
            INTERNAL_INFO_STREAM << "  → " << s.productId
                                 << " [" << s.eventName << "]"
                                 << " dir=" << s.direction
                                 << " urgency=" << s.urgency;
        }

        // 写入PG
        writeSignal(event, signals);
    }
}

void CommodityEventDetector::writeSignal(
    const engine::EventFormat& event,
    const std::vector<CommodityEventSignal>& signals)
{
    if (!m_db) {
        INTERNAL_WARN_STREAM << "[CommodityDetector] DB未连接, 跳过写入";
        return;
    }

    try {
        std::string title;
        if (auto t = event.get<std::string>("title")) {
            title = t->substr(0, 500);
        }

        for (const auto& s : signals) {
            std::ostringstream sql;
            sql << "INSERT INTO alpha.commodity_event_signals "
                   "(product_id, event_type, event_name, urgency, direction, title, source) "
                   "VALUES ('"
                << s.productId << "','"
                << s.eventType << "','"
                << s.eventName << "',"
                << s.urgency << ","
                << s.direction << ",'"
                << title << "','C++')";

            m_db->executeUpdate(sql.str());
        }
    } catch (const std::exception& e) {
        INTERNAL_WARN_STREAM << "[CommodityDetector] PG写入失败: " << e.what();
    }
}

} // namespace factor
