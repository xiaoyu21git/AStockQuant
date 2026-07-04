// src/market/providers/DatabaseProvider.cpp
#include "market/providers/DatabaseProvider.h"

#include <mysql.h>

// 避免 Windows 头文件中将 ERROR 定义为宏，污染 ProviderStatus::ERROR
#ifdef ERROR
#undef ERROR
#endif

#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <sstream>
#include <string>
#include <iostream>

namespace astock::market {

namespace {

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\n\r");
    if (begin == std::string::npos) return {};
    const auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(begin, end - begin + 1);
}

} // namespace

void DatabaseProvider::parse_config(const std::string& config_str) {
    // 环境变量优先
    if (const char* pw = std::getenv("DB_PASSWORD")) {
        password_ = pw;
    }
    if (const char* user = std::getenv("DB_USERNAME")) {
        username_ = user;
    }
    if (const char* host = std::getenv("DB_HOST")) {
        host_ = host;
    }
    if (const char* db = std::getenv("DB_NAME")) {
        database_ = db;
    }

    std::stringstream ss(config_str);
    std::string item;
    while (std::getline(ss, item, ';')) {
        auto pos = item.find('=');
        if (pos == std::string::npos) continue;
        std::string key = trim(item.substr(0, pos));
        std::string val = trim(item.substr(pos + 1));

        if (key == "host") host_ = val;
        else if (key == "port") port_ = static_cast<std::uint16_t>(std::stoi(val));
        else if (key == "database") database_ = val;
        else if (key == "username" || key == "user") username_ = val;
        else if (key == "password" || key == "pwd") password_ = val;
        else if (key == "charset") charset_ = val;
    }
}

bool DatabaseProvider::is_daily_period(std::uint32_t period) {
    return period == 86400U;
}

std::string DatabaseProvider::period_to_timeframe(std::uint32_t period) {
    switch (period) {
        case 60:   return "1min";
        case 300:  return "5min";
        case 900:  return "15min";
        case 1800: return "30min";
        case 3600: return "60min";
        default:
            return "1min";
    }
}

DatabaseProvider::DatabaseProvider(const std::string& config)
    : config_str_(config) {
    parse_config(config_str_);
}

ProviderStatus DatabaseProvider::get_status() const {
    return status_;
}

bool DatabaseProvider::connect() {
    // 做一次短连接探测
    std::cout << "[DatabaseProvider] trying to connect: host=" << host_
              << " port=" << port_
              << " user=" << username_
              << " db=" << database_ << std::endl;

    MYSQL* conn = mysql_init(nullptr);
    if (!conn) {
        INTERNAL_ERROR_STREAM << "[DatabaseProvider] mysql_init failed";
        std::cout << "[DatabaseProvider] mysql_init failed" << std::endl;
        status_ = ProviderStatus::ERROR;
        return false;
    }

    // 明确使用 0 转成 unsigned int，避免与宏冲突
    unsigned int port = static_cast<unsigned int>(port_);

    if (!mysql_real_connect(
            conn,
            host_.c_str(),
            username_.c_str(),
            password_.c_str(),
            database_.c_str(),
            port,
            nullptr,
            0u)) {
        const char* err = mysql_error(conn);
        INTERNAL_ERROR_STREAM << "[DatabaseProvider] mysql_real_connect failed: "
                  << err
                  << "  host=" << host_
                  << " port=" << port
                  << " user=" << username_
                  << " db=" << database_;
        std::cout << "[DatabaseProvider] mysql_real_connect failed: "
                  << err << " host=" << host_
                  << " port=" << port
                  << " user=" << username_
                  << " db=" << database_ << std::endl;
        mysql_close(conn);
        status_ = ProviderStatus::ERROR;
        return false;
    }

    mysql_close(conn);
    status_ = ProviderStatus::CONNECTED;
    return true;
}

void DatabaseProvider::disconnect() {
    status_ = ProviderStatus::DISCONNECTED;
}

void DatabaseProvider::register_kline_callback(KLineCallback cb) {
    kline_cb_ = std::move(cb);
}

void DatabaseProvider::register_tick_callback(TickCallback cb) {
    tick_cb_ = std::move(cb);
}

bool DatabaseProvider::subscribe_kline(std::uint32_t /*symbol_id*/, std::uint32_t /*period*/) {
    return true;
}

bool DatabaseProvider::unsubscribe_kline(std::uint32_t /*symbol_id*/, std::uint32_t /*period*/) {
    return true;
}

bool DatabaseProvider::subscribe_tick(std::uint32_t /*symbol_id*/) {
    return true;
}

bool DatabaseProvider::unsubscribe_tick(std::uint32_t /*symbol_id*/) {
    return true;
}

KLineBatch DatabaseProvider::get_history_klines(
    std::uint32_t symbol_id,
    std::uint32_t period,
    std::uint64_t start_time,
    std::uint64_t end_time,
    std::size_t  limit) {

    const std::vector<std::uint32_t> symbol_ids{symbol_id};
    return get_history_klines_batch(symbol_ids, period, start_time, end_time, limit);
}

KLineBatch DatabaseProvider::get_history_klines_batch(
    const std::vector<std::uint32_t>& symbol_ids,
    std::uint32_t period,
    std::uint64_t start_time,
    std::uint64_t end_time,
    std::size_t limit_per_symbol) {

    if (symbol_ids.empty()) {
        return KLineBatch{};
    }

    if (status_ != ProviderStatus::CONNECTED) {
        if (!connect()) {
            return KLineBatch(limit_per_symbol ? symbol_ids.size() * limit_per_symbol : symbol_ids.size());
        }
    }

    MYSQL* conn = mysql_init(nullptr);
    if (!conn) {
        return KLineBatch(limit_per_symbol ? symbol_ids.size() * limit_per_symbol : symbol_ids.size());
    }

    unsigned int port = static_cast<unsigned int>(port_);

    if (!mysql_real_connect(
            conn,
            host_.c_str(),
            username_.c_str(),
            password_.c_str(),
            database_.c_str(),
            port,
            nullptr,
            0u)) {
        mysql_close(conn);
        status_ = ProviderStatus::ERROR;
        return KLineBatch(limit_per_symbol ? symbol_ids.size() * limit_per_symbol : symbol_ids.size());
    }

    if (limit_per_symbol == 0U) {
        limit_per_symbol = 1000U;
    }

    KLineBatch batch(symbol_ids.size() * limit_per_symbol);

    std::ostringstream inClause;
    for (std::size_t index = 0; index < symbol_ids.size(); ++index) {
        if (index > 0U) {
            inClause << ", ";
        }
        inClause << symbol_ids[index];
    }

    std::ostringstream sql;
    if (is_daily_period(period)) {
        sql << "SELECT s.symbol_id, EXTRACT(EPOCH FROM d.trade_date) AS ts, d.open, d.high, d.low, d.close, d.volume, d.turnover "
            << "FROM mkt.daily_bar d "
            << "INNER JOIN ref.symbol_info s ON d.symbol_id = s.id "
            << "WHERE s.symbol_id IN (" << inClause.str() << ") ";

        if (start_time > 0) {
            sql << "AND d.trade_date >= to_timestamp(" << start_time << ")::date ";
        }
        if (end_time > 0) {
            sql << "AND d.trade_date <= to_timestamp(" << end_time << ")::date ";
        }

        sql << "ORDER BY s.symbol_id ASC, d.trade_date ASC";
    } else {
        const std::string timeframe = period_to_timeframe(period);
        sql << "SELECT symbol_id, EXTRACT(EPOCH FROM bar_time) AS ts, open, high, low, close, volume, turnover "
            << "FROM minute_bar "
            << "WHERE symbol_id IN (" << inClause.str() << ") "
            << "AND timeframe = '" << timeframe << "' ";

        if (start_time > 0) {
            sql << "AND bar_time >= to_timestamp(" << start_time << ") ";
        }
        if (end_time > 0) {
            sql << "AND bar_time <= to_timestamp(" << end_time << ") ";
        }

        sql << "ORDER BY symbol_id ASC, bar_time ASC";
    }

    std::cout << "[DatabaseProvider] executing SQL: " << sql.str() << std::endl;

    if (mysql_query(conn, sql.str().c_str()) != 0) {
        mysql_close(conn);
        return batch;
    }

    MYSQL_RES* result = mysql_store_result(conn);
    if (!result) {
        mysql_close(conn);
        return batch;
    }

    MYSQL_ROW row;
    std::unordered_map<std::uint32_t, std::size_t> rowCountBySymbol;
    rowCountBySymbol.reserve(symbol_ids.size());
    while ((row = mysql_fetch_row(result))) {
        if (!row[0] || !row[1] || !row[2] || !row[3] || !row[4] || !row[5] || !row[6]) {
            continue;
        }

        const std::uint32_t currentSymbolId = static_cast<std::uint32_t>(std::strtoul(row[0], nullptr, 10));
        std::size_t& emittedRows = rowCountBySymbol[currentSymbolId];
        if (emittedRows >= limit_per_symbol) {
            continue;
        }

        KLine k{};
        k.symbol_id = currentSymbolId;
        k.period    = period;
        k.timestamp = static_cast<uint64_t>(std::strtoull(row[1], nullptr, 10));
        k.open      = std::strtod(row[2], nullptr);
        k.high      = std::strtod(row[3], nullptr);
        k.low       = std::strtod(row[4], nullptr);
        k.close     = std::strtod(row[5], nullptr);
        k.volume    = static_cast<float>(std::strtod(row[6], nullptr));
        k.amount    = static_cast<float>(std::strtod(row[7], nullptr));
        k.turnover  = k.amount;

        batch.push_back(k);
        ++emittedRows;
    }

    mysql_free_result(result);
    mysql_close(conn);

    return batch;
}

} // namespace astock::market
