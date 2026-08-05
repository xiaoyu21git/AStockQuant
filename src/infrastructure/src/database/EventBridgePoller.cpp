#include "database/EventBridgePoller.h"
#include "database/NativePgConnectionPool.h"
#include "database/ISqlDatabase.h"
#include "../../../engine/include/GlobalEventBusRegistry.h"
#include "../../../engine/include/Event/EventFormat.hpp"
#include "foundation/log/logging.hpp"
#include "foundation/json/json_facade.h"

#include <chrono>
#include <sstream>
#include <string>
#include <vector>

namespace astock::infrastructure::database {

EventBridgePoller& EventBridgePoller::instance() {
    static EventBridgePoller s;
    return s;
}

EventBridgePoller::~EventBridgePoller() { stop(); }

void EventBridgePoller::start() {
    if (m_running.load()) return;

    m_running.store(true);
    m_thread = std::make_unique<std::thread>(&EventBridgePoller::pollLoop, this);
    INTERNAL_INFO_STREAM << "[BridgePoller] 启动, 轮询间隔 60s";
}

void EventBridgePoller::stop() {
    m_running.store(false);
    if (m_thread && m_thread->joinable()) {
        m_thread->join();
        m_thread.reset();
    }
    INTERNAL_INFO_STREAM << "[BridgePoller] 停止, 共 " << m_pollCount
                         << " 轮, 发布 " << m_totalPublished << " 条";
}

// PgResultRow::getString/Int/Double are defined in ISqlDatabase
using P = astock::database::SqlParam;

void EventBridgePoller::pollLoop() {
    while (m_running.load()) {
        try {
            engine::EventBus* bus = engine::get_engine_event_bus();
            if (!bus) {
                std::this_thread::sleep_for(std::chrono::seconds(60));
                continue;
            }

            std::shared_ptr<astock::database::ISqlDatabase> db =
                astock::database::NativePgConnectionPool::instance().getConnection();
            if (!db || !db->isOpen()) {
                std::this_thread::sleep_for(std::chrono::seconds(10));
                continue;
            }

            // 查询未消费事件 (最多 200 条/轮)
            auto rows = db->executeQuery(
                "SELECT id, event_type, data, metadata "
                "FROM live.event_bridge "
                "WHERE consumed = FALSE "
                "ORDER BY id LIMIT 200");

            int published = 0;
            for (std::size_t i = 0; i < rows.rowCount(); ++i) {
                const auto& row = rows.getRow(i);
                int64_t id = row.getInt("id");
                std::string eventType = row.getString("event_type");
                std::string dataJson = row.getString("data");
                std::string metaJson = row.getString("metadata");

                // 构造 EventFormat
                engine::EventFormat fmt;
                fmt.type = eventType;

                // 解析 data JSON → fmt.data
                foundation::json::JsonFacade dataObj =
                    foundation::json::JsonFacade::parse(dataJson);
                if (dataObj.isObject()) {
                    std::vector<std::string> dataKeys = dataObj.keys();
                    for (std::size_t ki = 0; ki < dataKeys.size(); ++ki) {
                        const std::string& key = dataKeys[ki];
                        foundation::json::JsonFacade val = dataObj.get(key);
                        if (val.isString()) {
                            fmt.set(key, val.asString());
                        } else if (val.isNumber()) {
                            fmt.set(key, val.asDouble());
                        }
                    }
                }

                // 解析 metadata JSON → fmt.metadata
                foundation::json::JsonFacade metaObj =
                    foundation::json::JsonFacade::parse(metaJson);
                if (metaObj.isObject()) {
                    std::vector<std::string> metaKeys = metaObj.keys();
                    for (std::size_t ki = 0; ki < metaKeys.size(); ++ki) {
                        const std::string& key = metaKeys[ki];
                        foundation::json::JsonFacade val = metaObj.get(key);
                        if (val.isString()) {
                            fmt.metadata[key] = val.asString();
                        }
                    }
                }

                // 发布到 C++ EventBus
                bus->publish(fmt);
                ++published;

                // 标记已消费
                std::ostringstream updateSql;
                updateSql << "UPDATE live.event_bridge SET consumed=TRUE WHERE id=" << id;
                db->executeUpdate(updateSql.str());
            }

            m_totalPublished += published;
            ++m_pollCount;

            if (published > 0) {
                INTERNAL_INFO_STREAM << "[BridgePoller] #" << m_pollCount
                                     << " 发布 " << published << " 条 (累计 "
                                     << m_totalPublished << ")";
            }

            // 每 10 轮清理一次旧数据
            if (m_pollCount % 10 == 0) {
                db->executeUpdate(
                    "DELETE FROM live.event_bridge "
                    "WHERE created_at < NOW() - INTERVAL '7 days'");
            }

        } catch (const std::exception& e) {
            INTERNAL_WARN_STREAM << "[BridgePoller] 异常: " << e.what();
        }

        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
}

} // namespace astock::infrastructure::database
