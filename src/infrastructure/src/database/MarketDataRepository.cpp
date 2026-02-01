#include "../../include/database/MarketDataRepository.h"
#include <sstream>
#include <iomanip>
#include <cstring>

namespace astock {
namespace database {

MarketDataRepository::MarketDataRepository(std::shared_ptr<ConnectionPool> pool)
    : pool_(pool) {
}

// ============ Symbol Info 操作 ============

bool MarketDataRepository::saveSymbol(const SymbolInfo& symbol) {
    ConnectionGuard guard(*pool_);
    MYSQL* conn = guard.get();
    if (!conn) return false;
    
    std::ostringstream sql;
    sql << "INSERT INTO symbol_info "
        << "(symbol, name, asset_class, exchange, list_date, status, created_at, updated_at) "
        << "VALUES ('"
        << escapeString(conn, symbol.symbol) << "', '"
        << escapeString(conn, symbol.name) << "', '"
        << symbolTypeToString(symbol.symbol_type) << "', '"
        << escapeString(conn, symbol.exchange) << "', "
        << "FROM_UNIXTIME(" << symbol.list_date << "), '"
        << escapeString(conn, symbol.status) << "', "
        << "FROM_UNIXTIME(" << symbol.created_at << "), "
        << "FROM_UNIXTIME(" << symbol.updated_at << ")) "
        << "ON DUPLICATE KEY UPDATE "
        << "name=VALUES(name), "
        << "asset_class=VALUES(asset_class), "
        << "exchange=VALUES(exchange), "
        << "list_date=VALUES(list_date), "
        << "status=VALUES(status), "
        << "updated_at=VALUES(updated_at)";
    
    return mysql_query(conn, sql.str().c_str()) == 0;
}

std::optional<SymbolInfo> MarketDataRepository::getSymbol(const std::string& symbol) {
    ConnectionGuard guard(*pool_);
    MYSQL* conn = guard.get();
    if (!conn) return std::nullopt;
    
    std::ostringstream sql;
    sql << "SELECT symbol, name, asset_class, exchange, "
        << "UNIX_TIMESTAMP(list_date), UNIX_TIMESTAMP(delist_date), "
        << "status, UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(updated_at) "
        << "FROM symbol_info WHERE symbol = '"
        << escapeString(conn, symbol) << "'";
    
    if (mysql_query(conn, sql.str().c_str()) != 0) {
        // 临时调试输出，帮助定位 schema/SQL 问题
        std::fprintf(stderr, "[MarketDataRepository::getSymbol] MySQL error: %s\n", mysql_error(conn));
        return std::nullopt;
    }
    
    MYSQL_RES* result = mysql_store_result(conn);
    if (!result) return std::nullopt;
    
    MYSQL_ROW row = mysql_fetch_row(result);
    std::optional<SymbolInfo> info;
    
    if (row) {
        info = buildSymbolInfo(row);
    }
    
    mysql_free_result(result);
    return info;
}

std::vector<SymbolInfo> MarketDataRepository::getAllSymbols(
    std::optional<SymbolType> symbol_type,
    const std::string& status) {
    
    ConnectionGuard guard(*pool_);
    MYSQL* conn = guard.get();
    if (!conn) return {};
    
    std::ostringstream sql;
    sql << "SELECT symbol, name, asset_class, exchange, "
        << "UNIX_TIMESTAMP(list_date), UNIX_TIMESTAMP(delist_date), "
        << "status, UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(updated_at) "
        << "FROM symbol_info WHERE status = '"
        << escapeString(conn, status) << "'";
    
    if (symbol_type.has_value()) {
        sql << " AND asset_class = '" 
            << symbolTypeToString(symbol_type.value()) << "'";
    }
    
    if (mysql_query(conn, sql.str().c_str()) != 0) {
        // 临时调试输出，帮助定位 schema/SQL 问题
        std::fprintf(stderr, "[MarketDataRepository::getAllSymbols] MySQL error: %s\n", mysql_error(conn));
        return {};
    }
    
    MYSQL_RES* result = mysql_store_result(conn);
    if (!result) return {};
    
    std::vector<SymbolInfo> symbols;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        symbols.push_back(buildSymbolInfo(row));
    }
    
    mysql_free_result(result);
    return symbols;
}

// ============ Daily Bar 操作 ============

size_t MarketDataRepository::saveDailyBars(const std::vector<DailyBar>& bars) {
    if (bars.empty()) return 0;
    
    ConnectionGuard guard(*pool_);
    MYSQL* conn = guard.get();
    if (!conn) return 0;
    
    // 批量插入
    std::ostringstream sql;
    sql << "INSERT INTO daily_bar "
        << "(symbol, trade_date, open, high, low, close, pre_close, "
        << "volume, turnover, change_pct, amplitude, turnover_rate, "
        << "pe_ratio, pb_ratio, market_cap, created_at) VALUES ";
    
    bool first = true;
    for (const auto& bar : bars) {
        if (!first) sql << ", ";
        first = false;
        
        sql << "('"
            << escapeString(conn, bar.symbol) << "', "
            << "FROM_UNIXTIME(" << bar.trade_date << "), "
            << bar.open << ", " << bar.high << ", "
            << bar.low << ", " << bar.close << ", "
            << bar.pre_close << ", " << bar.volume << ", "
            << bar.turnover << ", " << bar.change_pct << ", "
            << bar.amplitude << ", " << bar.turnover_rate << ", "
            << bar.pe_ratio << ", " << bar.pb_ratio << ", "
            << bar.market_cap << ", "
            << "FROM_UNIXTIME(" << bar.created_at << "))";
    }
    
    sql << " ON DUPLICATE KEY UPDATE "
        << "open=VALUES(open), high=VALUES(high), "
        << "low=VALUES(low), close=VALUES(close), "
        << "pre_close=VALUES(pre_close), volume=VALUES(volume), "
        << "turnover=VALUES(turnover), change_pct=VALUES(change_pct), "
        << "amplitude=VALUES(amplitude), turnover_rate=VALUES(turnover_rate), "
        << "pe_ratio=VALUES(pe_ratio), pb_ratio=VALUES(pb_ratio), "
        << "market_cap=VALUES(market_cap)";
    
    if (mysql_query(conn, sql.str().c_str()) != 0) {
        return 0;
    }
    
    return mysql_affected_rows(conn);
}

std::vector<DailyBar> MarketDataRepository::getDailyBars(
    const std::string& symbol,
    std::time_t start_date,
    std::time_t end_date) {
    
    ConnectionGuard guard(*pool_);
    MYSQL* conn = guard.get();
    if (!conn) return {};
    
    std::ostringstream sql;
    sql << "SELECT id, symbol, UNIX_TIMESTAMP(trade_date), "
        << "open, high, low, close, pre_close, volume, turnover, "
        << "change_pct, amplitude, turnover_rate, pe_ratio, pb_ratio, "
        << "market_cap, UNIX_TIMESTAMP(created_at) "
        << "FROM daily_bar WHERE symbol = '"
        << escapeString(conn, symbol) << "' "
        << "AND trade_date >= FROM_UNIXTIME(" << start_date << ") "
        << "AND trade_date <= FROM_UNIXTIME(" << end_date << ") "
        << "ORDER BY trade_date";
    
    if (mysql_query(conn, sql.str().c_str()) != 0) {
        return {};
    }
    
    MYSQL_RES* result = mysql_store_result(conn);
    if (!result) return {};
    
    std::vector<DailyBar> bars;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        bars.push_back(buildDailyBar(row));
    }
    
    mysql_free_result(result);
    return bars;
}

std::optional<DailyBar> MarketDataRepository::getLatestBar(const std::string& symbol) {
    ConnectionGuard guard(*pool_);
    MYSQL* conn = guard.get();
    if (!conn) return std::nullopt;
    
    std::ostringstream sql;
    sql << "SELECT id, symbol, UNIX_TIMESTAMP(trade_date), "
        << "open, high, low, close, pre_close, volume, turnover, "
        << "change_pct, amplitude, turnover_rate, pe_ratio, pb_ratio, "
        << "market_cap, UNIX_TIMESTAMP(created_at) "
        << "FROM daily_bar WHERE symbol = '"
        << escapeString(conn, symbol) << "' "
        << "ORDER BY trade_date DESC LIMIT 1";
    
    if (mysql_query(conn, sql.str().c_str()) != 0) {
        return std::nullopt;
    }
    
    MYSQL_RES* result = mysql_store_result(conn);
    if (!result) return std::nullopt;
    
    MYSQL_ROW row = mysql_fetch_row(result);
    std::optional<DailyBar> bar;
    
    if (row) {
        bar = buildDailyBar(row);
    }
    
    mysql_free_result(result);
    return bar;
}

// ============ Minute Bar 操作 ============

size_t MarketDataRepository::saveMinuteBars(const std::vector<MinuteBar>& bars) {
    if (bars.empty()) return 0;
    
    ConnectionGuard guard(*pool_);
    MYSQL* conn = guard.get();
    if (!conn) return 0;
    
    std::ostringstream sql;
    sql << "INSERT INTO minute_bar "
        << "(symbol, datetime, frequency, open, high, low, close, "
        << "volume, turnover, created_at) VALUES ";
    
    bool first = true;
    for (const auto& bar : bars) {
        if (!first) sql << ", ";
        first = false;
        
        sql << "('"
            << escapeString(conn, bar.symbol) << "', "
            << "FROM_UNIXTIME(" << bar.datetime << "), "
            << bar.frequency << ", "
            << bar.open << ", " << bar.high << ", "
            << bar.low << ", " << bar.close << ", "
            << bar.volume << ", " << bar.turnover << ", "
            << "FROM_UNIXTIME(" << bar.created_at << "))";
    }
    
    sql << " ON DUPLICATE KEY UPDATE "
        << "open=VALUES(open), high=VALUES(high), "
        << "low=VALUES(low), close=VALUES(close), "
        << "volume=VALUES(volume), turnover=VALUES(turnover)";
    
    if (mysql_query(conn, sql.str().c_str()) != 0) {
        return 0;
    }
    
    return mysql_affected_rows(conn);
}

std::vector<MinuteBar> MarketDataRepository::getMinuteBars(
    const std::string& symbol,
    std::time_t start_datetime,
    std::time_t end_datetime,
    int frequency) {
    
    ConnectionGuard guard(*pool_);
    MYSQL* conn = guard.get();
    if (!conn) return {};
    
    std::ostringstream sql;
    sql << "SELECT id, symbol, UNIX_TIMESTAMP(datetime), frequency, "
        << "open, high, low, close, volume, turnover, "
        << "UNIX_TIMESTAMP(created_at) "
        << "FROM minute_bar WHERE symbol = '"
        << escapeString(conn, symbol) << "' "
        << "AND frequency = " << frequency << " "
        << "AND datetime >= FROM_UNIXTIME(" << start_datetime << ") "
        << "AND datetime <= FROM_UNIXTIME(" << end_datetime << ") "
        << "ORDER BY datetime";
    
    if (mysql_query(conn, sql.str().c_str()) != 0) {
        return {};
    }
    
    MYSQL_RES* result = mysql_store_result(conn);
    if (!result) return {};
    
    std::vector<MinuteBar> bars;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        bars.push_back(buildMinuteBar(row));
    }
    
    mysql_free_result(result);
    return bars;
}

// ============ Tick Data 操作 ============

size_t MarketDataRepository::saveTickData(const std::vector<TickData>& ticks) {
    if (ticks.empty()) return 0;
    
    ConnectionGuard guard(*pool_);
    MYSQL* conn = guard.get();
    if (!conn) return 0;
    
    std::ostringstream sql;
    sql << "INSERT INTO tick_data "
        << "(symbol, datetime, last_price, volume, turnover, "
        << "bid_price, bid_volume, ask_price, ask_volume, created_at) VALUES ";
    
    bool first = true;
    for (const auto& tick : ticks) {
        if (!first) sql << ", ";
        first = false;
        
        sql << "('"
            << escapeString(conn, tick.symbol) << "', "
            << "FROM_UNIXTIME(" << tick.datetime << "), "
            << tick.last_price << ", " << tick.volume << ", "
            << tick.turnover << ", " << tick.bid_price << ", "
            << tick.bid_volume << ", " << tick.ask_price << ", "
            << tick.ask_volume << ", "
            << "FROM_UNIXTIME(" << tick.created_at << "))";
    }
    
    if (mysql_query(conn, sql.str().c_str()) != 0) {
        return 0;
    }
    
    return mysql_affected_rows(conn);
}

std::vector<TickData> MarketDataRepository::getTickData(
    const std::string& symbol,
    std::time_t start_datetime,
    std::time_t end_datetime) {
    
    ConnectionGuard guard(*pool_);
    MYSQL* conn = guard.get();
    if (!conn) return {};
    
    std::ostringstream sql;
    sql << "SELECT id, symbol, UNIX_TIMESTAMP(datetime), "
        << "last_price, volume, turnover, bid_price, bid_volume, "
        << "ask_price, ask_volume, UNIX_TIMESTAMP(created_at) "
        << "FROM tick_data WHERE symbol = '"
        << escapeString(conn, symbol) << "' "
        << "AND datetime >= FROM_UNIXTIME(" << start_datetime << ") "
        << "AND datetime <= FROM_UNIXTIME(" << end_datetime << ") "
        << "ORDER BY datetime";
    
    if (mysql_query(conn, sql.str().c_str()) != 0) {
        return {};
    }
    
    MYSQL_RES* result = mysql_store_result(conn);
    if (!result) return {};
    
    std::vector<TickData> ticks;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        ticks.push_back(buildTickData(row));
    }
    
    mysql_free_result(result);
    return ticks;
}

// ============ 衍生数据：资金流向 & 龙虎榜 ============

std::vector<MoneyFlowDaily> MarketDataRepository::getMoneyFlowDaily(
    const std::string& symbol,
    std::time_t start_date,
    std::time_t end_date) {

    ConnectionGuard guard(*pool_);
    MYSQL* conn = guard.get();
    if (!conn) return {};

    std::ostringstream sql;
    sql << "SELECT id, symbol, UNIX_TIMESTAMP(trade_date), "
        << "main_inflow, main_outflow, net_main_inflow, "
        << "large_inflow, large_outflow, medium_inflow, medium_outflow, "
        << "small_inflow, small_outflow, net_amount, UNIX_TIMESTAMP(created_at) "
        << "FROM money_flow_daily WHERE symbol = '"
        << escapeString(conn, symbol) << "' "
        << "AND trade_date >= FROM_UNIXTIME(" << start_date << ") "
        << "AND trade_date <= FROM_UNIXTIME(" << end_date << ") "
        << "ORDER BY trade_date";

    if (mysql_query(conn, sql.str().c_str()) != 0) {
        return {};
    }

    MYSQL_RES* result = mysql_store_result(conn);
    if (!result) return {};

    std::vector<MoneyFlowDaily> rows;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        rows.push_back(buildMoneyFlowDaily(row));
    }

    mysql_free_result(result);
    return rows;
}

std::vector<DragonTigerRecord> MarketDataRepository::getDragonTigerRecords(
    const std::string& symbol,
    std::time_t start_date,
    std::time_t end_date) {

    ConnectionGuard guard(*pool_);
    MYSQL* conn = guard.get();
    if (!conn) return {};

    std::ostringstream sql;
    sql << "SELECT id, symbol, UNIX_TIMESTAMP(trade_date), reason, "
        << "buy_amount, sell_amount, net_amount, buy_count, sell_count, "
        << "institution_buy, institution_sell, turnover_rate, UNIX_TIMESTAMP(created_at) "
        << "FROM dragon_tiger_list WHERE symbol = '"
        << escapeString(conn, symbol) << "' "
        << "AND trade_date >= FROM_UNIXTIME(" << start_date << ") "
        << "AND trade_date <= FROM_UNIXTIME(" << end_date << ") "
        << "ORDER BY trade_date";

    if (mysql_query(conn, sql.str().c_str()) != 0) {
        return {};
    }

    MYSQL_RES* result = mysql_store_result(conn);
    if (!result) return {};

    std::vector<DragonTigerRecord> rows;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        rows.push_back(buildDragonTigerRecord(row));
    }

    mysql_free_result(result);
    return rows;
}

// ============ 工具方法 ============

bool MarketDataRepository::executeQuery(const std::string& sql) {
    ConnectionGuard guard(*pool_);
    MYSQL* conn = guard.get();
    if (!conn) return false;
    
    return mysql_query(conn, sql.c_str()) == 0;
}

bool MarketDataRepository::beginTransaction() {
    return executeQuery("START TRANSACTION");
}

bool MarketDataRepository::commit() {
    return executeQuery("COMMIT");
}

bool MarketDataRepository::rollback() {
    return executeQuery("ROLLBACK");
}

std::string MarketDataRepository::escapeString(MYSQL* conn, const std::string& str) {
    std::vector<char> buffer(str.length() * 2 + 1);
    mysql_real_escape_string(conn, buffer.data(), str.c_str(), str.length());
    return std::string(buffer.data());
}

SymbolInfo MarketDataRepository::buildSymbolInfo(MYSQL_ROW row) {
    SymbolInfo info;
    info.symbol = row[0] ? row[0] : "";
    info.name = row[1] ? row[1] : "";
    info.symbol_type = row[2] ? stringToSymbolType(row[2]) : SymbolType::STOCK;
    info.exchange = row[3] ? row[3] : "";
    info.list_date = row[4] ? std::stoll(row[4]) : 0;
    info.delist_date = row[5] ? std::stoll(row[5]) : 0;
    info.status = row[6] ? row[6] : "active";
    info.created_at = row[7] ? std::stoll(row[7]) : 0;
    info.updated_at = row[8] ? std::stoll(row[8]) : 0;
    return info;
}

DailyBar MarketDataRepository::buildDailyBar(MYSQL_ROW row) {
    DailyBar bar;
    bar.id = row[0] ? std::stoi(row[0]) : 0;
    bar.symbol = row[1] ? row[1] : "";
    bar.trade_date = row[2] ? std::stoll(row[2]) : 0;
    bar.open = row[3] ? std::stod(row[3]) : 0.0;
    bar.high = row[4] ? std::stod(row[4]) : 0.0;
    bar.low = row[5] ? std::stod(row[5]) : 0.0;
    bar.close = row[6] ? std::stod(row[6]) : 0.0;
    bar.pre_close = row[7] ? std::stod(row[7]) : 0.0;
    bar.volume = row[8] ? std::stod(row[8]) : 0.0;
    bar.turnover = row[9] ? std::stod(row[9]) : 0.0;
    bar.change_pct = row[10] ? std::stod(row[10]) : 0.0;
    bar.amplitude = row[11] ? std::stod(row[11]) : 0.0;
    bar.turnover_rate = row[12] ? std::stod(row[12]) : 0.0;
    bar.pe_ratio = row[13] ? std::stod(row[13]) : 0.0;
    bar.pb_ratio = row[14] ? std::stod(row[14]) : 0.0;
    bar.market_cap = row[15] ? std::stod(row[15]) : 0.0;
    bar.created_at = row[16] ? std::stoll(row[16]) : 0;
    return bar;
}

MinuteBar MarketDataRepository::buildMinuteBar(MYSQL_ROW row) {
    MinuteBar bar;
    bar.id = row[0] ? std::stoi(row[0]) : 0;
    bar.symbol = row[1] ? row[1] : "";
    bar.datetime = row[2] ? std::stoll(row[2]) : 0;
    bar.frequency = row[3] ? std::stoi(row[3]) : 1;
    bar.open = row[4] ? std::stod(row[4]) : 0.0;
    bar.high = row[5] ? std::stod(row[5]) : 0.0;
    bar.low = row[6] ? std::stod(row[6]) : 0.0;
    bar.close = row[7] ? std::stod(row[7]) : 0.0;
    bar.volume = row[8] ? std::stod(row[8]) : 0.0;
    bar.turnover = row[9] ? std::stod(row[9]) : 0.0;
    bar.created_at = row[10] ? std::stoll(row[10]) : 0;
    return bar;
}

TickData MarketDataRepository::buildTickData(MYSQL_ROW row) {
    TickData tick;
    tick.id = row[0] ? std::stoi(row[0]) : 0;
    tick.symbol = row[1] ? row[1] : "";
    tick.datetime = row[2] ? std::stoll(row[2]) : 0;
    tick.last_price = row[3] ? std::stod(row[3]) : 0.0;
    tick.volume = row[4] ? std::stod(row[4]) : 0.0;
    tick.turnover = row[5] ? std::stod(row[5]) : 0.0;
    tick.bid_price = row[6] ? std::stod(row[6]) : 0.0;
    tick.bid_volume = row[7] ? std::stod(row[7]) : 0.0;
    tick.ask_price = row[8] ? std::stod(row[8]) : 0.0;
    tick.ask_volume = row[9] ? std::stod(row[9]) : 0.0;
    tick.created_at = row[10] ? std::stoll(row[10]) : 0;
    return tick;
}

MoneyFlowDaily MarketDataRepository::buildMoneyFlowDaily(MYSQL_ROW row) {
    MoneyFlowDaily mf;
    mf.id = row[0] ? std::stoi(row[0]) : 0;
    mf.symbol = row[1] ? row[1] : "";
    mf.trade_date = row[2] ? std::stoll(row[2]) : 0;
    mf.main_inflow = row[3] ? std::stod(row[3]) : 0.0;
    mf.main_outflow = row[4] ? std::stod(row[4]) : 0.0;
    mf.net_main_inflow = row[5] ? std::stod(row[5]) : 0.0;
    mf.large_inflow = row[6] ? std::stod(row[6]) : 0.0;
    mf.large_outflow = row[7] ? std::stod(row[7]) : 0.0;
    mf.medium_inflow = row[8] ? std::stod(row[8]) : 0.0;
    mf.medium_outflow = row[9] ? std::stod(row[9]) : 0.0;
    mf.small_inflow = row[10] ? std::stod(row[10]) : 0.0;
    mf.small_outflow = row[11] ? std::stod(row[11]) : 0.0;
    mf.net_amount = row[12] ? std::stod(row[12]) : 0.0;
    mf.created_at = row[13] ? std::stoll(row[13]) : 0;
    return mf;
}

DragonTigerRecord MarketDataRepository::buildDragonTigerRecord(MYSQL_ROW row) {
    DragonTigerRecord rec;
    rec.id = row[0] ? std::stoi(row[0]) : 0;
    rec.symbol = row[1] ? row[1] : "";
    rec.trade_date = row[2] ? std::stoll(row[2]) : 0;
    rec.reason = row[3] ? row[3] : "";
    rec.buy_amount = row[4] ? std::stod(row[4]) : 0.0;
    rec.sell_amount = row[5] ? std::stod(row[5]) : 0.0;
    rec.net_amount = row[6] ? std::stod(row[6]) : 0.0;
    rec.buy_count = row[7] ? static_cast<unsigned int>(std::stoul(row[7])) : 0u;
    rec.sell_count = row[8] ? static_cast<unsigned int>(std::stoul(row[8])) : 0u;
    rec.institution_buy = row[9] ? std::stod(row[9]) : 0.0;
    rec.institution_sell = row[10] ? std::stod(row[10]) : 0.0;
    rec.turnover_rate = row[11] ? std::stod(row[11]) : 0.0;
    rec.created_at = row[12] ? std::stoll(row[12]) : 0;
    return rec;
}

} // namespace database
} // namespace astock
