// SchemaNames.h — 数据库 schema 名常量化
// 消除散布在 SQL 字符串中的 "mkt." / "ref." / "live." / "data." 魔法字符串
#pragma once

namespace astock {
namespace database {
namespace schema {

constexpr const char* kMarket   = "mkt";   // 行情数据: daily_bar, minute_bar, weekly_bar, monthly_bar
constexpr const char* kRef      = "ref";   // 参考数据: symbol_info, industry_classification, product_stock_mapping
constexpr const char* kLive     = "live";  // 实盘数据: strategy, event_bridge, live_order, live_account_daily
constexpr const char* kData     = "data";  // 应用数据: sync_task_log, live_order, live_account_daily

} // namespace schema
} // namespace database
} // namespace astock
