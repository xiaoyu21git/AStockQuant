#include "../../include/database/MarketDataRepository.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <QDateTime>
#include <QDebug>

namespace astock {
namespace database {

MarketDataRepository::MarketDataRepository(std::shared_ptr<QtMySQLDatabase> database)
    : database_(database) {
}

// ============ 工具方法 ============

QString MarketDataRepository::timeToDateString(std::time_t time) {
    QDateTime dt = QDateTime::fromSecsSinceEpoch(time);
    return dt.toString("yyyy-MM-dd");
}

QString MarketDataRepository::timeToDateTimeString(std::time_t time) {
    QDateTime dt = QDateTime::fromSecsSinceEpoch(time);
    return dt.toString("yyyy-MM-dd hh:mm:ss");
}

// ============ Symbol Info 操作 ============

bool MarketDataRepository::saveSymbol(const SymbolInfo& symbol) {
    if (!database_) return false;
    
    try {
        std::map<QString, QVariant> params;
        params[":symbol"] = QString::fromStdString(symbol.symbol);
        params[":name"] = QString::fromStdString(symbol.name);
        params[":asset_class"] = QString::fromStdString(symbolTypeToString(symbol.symbol_type));
        params[":exchange"] = QString::fromStdString(symbol.exchange);
        params[":list_date"] = timeToDateTimeString(symbol.list_date);
        params[":status"] = QString::fromStdString(symbol.status);
        params[":created_at"] = timeToDateTimeString(symbol.created_at);
        params[":updated_at"] = timeToDateTimeString(symbol.updated_at);
        
        QString sql = "INSERT INTO symbol_info "
                      "(symbol, name, asset_class, exchange, list_date, status, created_at, updated_at) "
                      "VALUES (:symbol, :name, :asset_class, :exchange, FROM_UNIXTIME(:list_date), "
                      ":status, FROM_UNIXTIME(:created_at), FROM_UNIXTIME(:updated_at)) "
                      "ON DUPLICATE KEY UPDATE "
                      "name=VALUES(name), "
                      "asset_class=VALUES(asset_class), "
                      "exchange=VALUES(exchange), "
                      "list_date=VALUES(list_date), "
                      "status=VALUES(status), "
                      "updated_at=VALUES(updated_at)";
        
        int affected = database_->executeUpdate(sql, params);
        return affected > 0;
    } catch (const std::exception& e) {
        qWarning() << "MarketDataRepository::saveSymbol error:" << e.what();
        return false;
    }
}

std::optional<SymbolInfo> MarketDataRepository::getSymbol(const std::string& symbol) {
    if (!database_) return std::nullopt;
    
    try {
        std::map<QString, QVariant> params;
        params[":symbol"] = QString::fromStdString(symbol);
        
        QString sql = "SELECT symbol, name, asset_class, exchange, "
                      "UNIX_TIMESTAMP(list_date), UNIX_TIMESTAMP(delist_date), "
                      "status, UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(updated_at) "
                      "FROM symbol_info WHERE symbol = :symbol";
        
        QueryResult result = database_->executeQuery(sql, params);
        if (result.isEmpty()) {
            return std::nullopt;
        }
        
        return buildSymbolInfo(result.getRow(0));
    } catch (const std::exception& e) {
        qWarning() << "MarketDataRepository::getSymbol error:" << e.what();
        return std::nullopt;
    }
}

std::vector<SymbolInfo> MarketDataRepository::getAllSymbols(
    std::optional<SymbolType> symbol_type,
    const std::string& status) {
    
    std::vector<SymbolInfo> symbols;
    if (!database_) return symbols;
    
    try {
        std::map<QString, QVariant> params;
        params[":status"] = QString::fromStdString(status);
        
        QString sql = "SELECT symbol, name, asset_class, exchange, "
                      "UNIX_TIMESTAMP(list_date), UNIX_TIMESTAMP(delist_date), "
                      "status, UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(updated_at) "
                      "FROM symbol_info WHERE status = :status";
        
        if (symbol_type.has_value()) {
            sql += " AND asset_class = :asset_class";
            params[":asset_class"] = QString::fromStdString(symbolTypeToString(symbol_type.value()));
        }
        
        QueryResult result = database_->executeQuery(sql, params);
        
        for (size_t i = 0; i < result.rowCount(); ++i) {
            symbols.push_back(buildSymbolInfo(result.getRow(i)));
        }
    } catch (const std::exception& e) {
        qWarning() << "MarketDataRepository::getAllSymbols error:" << e.what();
    }
    
    return symbols;
}

// ============ Daily Bar 操作 ============

size_t MarketDataRepository::saveDailyBars(const std::vector<DailyBar>& bars) {
    if (bars.empty() || !database_) return 0;
    
    try {
        // 使用批量更新
        std::vector<std::map<QString, QVariant>> batchParams;
        
        for (const auto& bar : bars) {
            std::map<QString, QVariant> params;
            params[":symbol"] = QString::fromStdString(bar.symbol);
            params[":trade_date"] = timeToDateString(bar.trade_date);
            params[":open"] = bar.open;
            params[":high"] = bar.high;
            params[":low"] = bar.low;
            params[":close"] = bar.close;
            params[":pre_close"] = bar.pre_close;
            params[":volume"] = bar.volume;
            params[":turnover"] = bar.turnover;
            params[":change_pct"] = bar.change_pct;
            params[":amplitude"] = bar.amplitude;
            params[":turnover_rate"] = bar.turnover_rate;
            params[":pe_ratio"] = bar.pe_ratio;
            params[":pb_ratio"] = bar.pb_ratio;
            params[":market_cap"] = bar.market_cap;
            params[":pre_adjust_factor"] = bar.pre_adjust_factor;
            params[":post_adjust_factor"] = bar.post_adjust_factor;
            params[":created_at"] = timeToDateTimeString(bar.created_at);
            
            batchParams.push_back(params);
        }
        
        QString sql = "INSERT INTO daily_bar "
                      "(symbol, trade_date, open, high, low, close, pre_close, "
                      "volume, turnover, change_pct, amplitude, turnover_rate, "
                      "pe_ratio, pb_ratio, market_cap, pre_adjust_factor, post_adjust_factor, created_at) VALUES "
                      "(:symbol, FROM_UNIXTIME(:trade_date), :open, :high, :low, :close, :pre_close, "
                      ":volume, :turnover, :change_pct, :amplitude, :turnover_rate, "
                      ":pe_ratio, :pb_ratio, :market_cap, :pre_adjust_factor, :post_adjust_factor, FROM_UNIXTIME(:created_at)) "
                      "ON DUPLICATE KEY UPDATE "
                      "open=VALUES(open), high=VALUES(high), "
                      "low=VALUES(low), close=VALUES(close), "
                      "pre_close=VALUES(pre_close), volume=VALUES(volume), "
                      "turnover=VALUES(turnover), change_pct=VALUES(change_pct), "
                      "amplitude=VALUES(amplitude), turnover_rate=VALUES(turnover_rate), "
                      "pe_ratio=VALUES(pe_ratio), pb_ratio=VALUES(pb_ratio), "
                      "market_cap=VALUES(market_cap), "
                      "pre_adjust_factor=VALUES(pre_adjust_factor), "
                      "post_adjust_factor=VALUES(post_adjust_factor)";
        
        int affected = database_->executeBatchUpdate(sql, batchParams);
        return static_cast<size_t>(affected);
    } catch (const std::exception& e) {
        qWarning() << "MarketDataRepository::saveDailyBars error:" << e.what();
        return 0;
    }
}

std::vector<DailyBar> MarketDataRepository::getDailyBars(
    const std::string& symbol,
    std::time_t start_date,
    std::time_t end_date) {
    
    std::vector<DailyBar> bars;
    if (!database_) return bars;
    
    try {
        std::map<QString, QVariant> params;
        params[":symbol"] = QString::fromStdString(symbol);
        params[":start_date"] = timeToDateString(start_date);
        params[":end_date"] = timeToDateString(end_date);
        
        QString sql = "SELECT id, symbol, UNIX_TIMESTAMP(trade_date), "
                      "open, high, low, close, pre_close, volume, turnover, "
                      "change_pct, amplitude, turnover_rate, pe_ratio, pb_ratio, "
                      "market_cap, pre_adjust_factor, post_adjust_factor, UNIX_TIMESTAMP(created_at) "
                      "FROM daily_bar WHERE symbol = :symbol "
                      "AND trade_date >= FROM_UNIXTIME(:start_date) "
                      "AND trade_date <= FROM_UNIXTIME(:end_date) "
                      "ORDER BY trade_date";
        
        QueryResult result = database_->executeQuery(sql, params);
        
        for (size_t i = 0; i < result.rowCount(); ++i) {
            bars.push_back(buildDailyBar(result.getRow(i)));
        }
    } catch (const std::exception& e) {
        qWarning() << "MarketDataRepository::getDailyBars error:" << e.what();
    }
    
    return bars;
}

std::optional<DailyBar> MarketDataRepository::getLatestBar(const std::string& symbol) {
    if (!database_) return std::nullopt;
    
    try {
        std::map<QString, QVariant> params;
        params[":symbol"] = QString::fromStdString(symbol);
        
        QString sql = "SELECT id, symbol, UNIX_TIMESTAMP(trade_date), "
                      "open, high, low, close, pre_close, volume, turnover, "
                      "change_pct, amplitude, turnover_rate, pe_ratio, pb_ratio, "
                      "market_cap, pre_adjust_factor, post_adjust_factor, UNIX_TIMESTAMP(created_at) "
                      "FROM daily_bar WHERE symbol = :symbol "
                      "ORDER BY trade_date DESC LIMIT 1";
        
        QueryResult result = database_->executeQuery(sql, params);
        if (result.isEmpty()) {
            return std::nullopt;
        }
        
        return buildDailyBar(result.getRow(0));
    } catch (const std::exception& e) {
        qWarning() << "MarketDataRepository::getLatestBar error:" << e.what();
        return std::nullopt;
    }
}

// ============ Minute Bar 操作 ============

size_t MarketDataRepository::saveMinuteBars(const std::vector<MinuteBar>& bars) {
    if (bars.empty() || !database_) return 0;
    
    try {
        std::vector<std::map<QString, QVariant>> batchParams;
        
        for (const auto& bar : bars) {
            std::map<QString, QVariant> params;
            params[":symbol"] = QString::fromStdString(bar.symbol);
            params[":datetime"] = timeToDateTimeString(bar.datetime);
            params[":frequency"] = bar.frequency;
            params[":open"] = bar.open;
            params[":high"] = bar.high;
            params[":low"] = bar.low;
            params[":close"] = bar.close;
            params[":volume"] = bar.volume;
            params[":turnover"] = bar.turnover;
            params[":created_at"] = timeToDateTimeString(bar.created_at);
            
            batchParams.push_back(params);
        }
        
        QString sql = "INSERT INTO minute_bar "
                      "(symbol, datetime, frequency, open, high, low, close, "
                      "volume, turnover, created_at) VALUES "
                      "(:symbol, FROM_UNIXTIME(:datetime), :frequency, :open, :high, :low, :close, "
                      ":volume, :turnover, FROM_UNIXTIME(:created_at)) "
                      "ON DUPLICATE KEY UPDATE "
                      "open=VALUES(open), high=VALUES(high), "
                      "low=VALUES(low), close=VALUES(close), "
                      "volume=VALUES(volume), turnover=VALUES(turnover)";
        
        int affected = database_->executeBatchUpdate(sql, batchParams);
        return static_cast<size_t>(affected);
    } catch (const std::exception& e) {
        qWarning() << "MarketDataRepository::saveMinuteBars error:" << e.what();
        return 0;
    }
}

std::vector<MinuteBar> MarketDataRepository::getMinuteBars(
    const std::string& symbol,
    std::time_t start_datetime,
    std::time_t end_datetime,
    int frequency) {
    
    std::vector<MinuteBar> bars;
    if (!database_) return bars;
    
    try {
        std::map<QString, QVariant> params;
        params[":symbol"] = QString::fromStdString(symbol);
        params[":start_datetime"] = timeToDateTimeString(start_datetime);
        params[":end_datetime"] = timeToDateTimeString(end_datetime);
        params[":frequency"] = frequency;
        
        QString sql = "SELECT id, symbol, UNIX_TIMESTAMP(datetime), frequency, "
                      "open, high, low, close, volume, turnover, "
                      "UNIX_TIMESTAMP(created_at) "
                      "FROM minute_bar WHERE symbol = :symbol "
                      "AND frequency = :frequency "
                      "AND datetime >= FROM_UNIXTIME(:start_datetime) "
                      "AND datetime <= FROM_UNIXTIME(:end_datetime) "
                      "ORDER BY datetime";
        
        QueryResult result = database_->executeQuery(sql, params);
        
        for (size_t i = 0; i < result.rowCount(); ++i) {
            bars.push_back(buildMinuteBar(result.getRow(i)));
        }
    } catch (const std::exception& e) {
        qWarning() << "MarketDataRepository::getMinuteBars error:" << e.what();
    }
    
    return bars;
}

// ============ Tick Data 操作 ============

size_t MarketDataRepository::saveTickData(const std::vector<TickData>& ticks) {
    if (ticks.empty() || !database_) return 0;
    
    try {
        std::vector<std::map<QString, QVariant>> batchParams;
        
        for (const auto& tick : ticks) {
            std::map<QString, QVariant> params;
            params[":symbol"] = QString::fromStdString(tick.symbol);
            params[":datetime"] = timeToDateTimeString(tick.datetime);
            params[":last_price"] = tick.last_price;
            params[":volume"] = tick.volume;
            params[":turnover"] = tick.turnover;
            params[":bid_price"] = tick.bid_price;
            params[":bid_volume"] = tick.bid_volume;
            params[":ask_price"] = tick.ask_price;
            params[":ask_volume"] = tick.ask_volume;
            params[":created_at"] = timeToDateTimeString(tick.created_at);
            
            batchParams.push_back(params);
        }
        
        QString sql = "INSERT INTO tick_data "
                      "(symbol, datetime, last_price, volume, turnover, "
                      "bid_price, bid_volume, ask_price, ask_volume, created_at) VALUES "
                      "(:symbol, FROM_UNIXTIME(:datetime), :last_price, :volume, :turnover, "
                      ":bid_price, :bid_volume, :ask_price, :ask_volume, FROM_UNIXTIME(:created_at))";
        
        int affected = database_->executeBatchUpdate(sql, batchParams);
        return static_cast<size_t>(affected);
    } catch (const std::exception& e) {
        qWarning() << "MarketDataRepository::saveTickData error:" << e.what();
        return 0;
    }
}

std::vector<TickData> MarketDataRepository::getTickData(
    const std::string& symbol,
    std::time_t start_datetime,
    std::time_t end_datetime) {
    
    std::vector<TickData> ticks;
    if (!database_) return ticks;
    
    try {
        std::map<QString, QVariant> params;
        params[":symbol"] = QString::fromStdString(symbol);
        params[":start_datetime"] = timeToDateTimeString(start_datetime);
        params[":end_datetime"] = timeToDateTimeString(end_datetime);
        
        QString sql = "SELECT id, symbol, UNIX_TIMESTAMP(datetime), "
                      "last_price, volume, turnover, bid_price, bid_volume, "
                      "ask_price, ask_volume, UNIX_TIMESTAMP(created_at) "
                      "FROM tick_data WHERE symbol = :symbol "
                      "AND datetime >= FROM_UNIXTIME(:start_datetime) "
                      "AND datetime <= FROM_UNIXTIME(:end_datetime) "
                      "ORDER BY datetime";
        
        QueryResult result = database_->executeQuery(sql, params);
        
        for (size_t i = 0; i < result.rowCount(); ++i) {
            ticks.push_back(buildTickData(result.getRow(i)));
        }
    } catch (const std::exception& e) {
        qWarning() << "MarketDataRepository::getTickData error:" << e.what();
    }
    
    return ticks;
}

// ============ 衍生数据：资金流向 & 龙虎榜 ============

std::vector<MoneyFlowDaily> MarketDataRepository::getMoneyFlowDaily(
    const std::string& symbol,
    std::time_t start_date,
    std::time_t end_date) {
    
    std::vector<MoneyFlowDaily> rows;
    if (!database_) return rows;
    
    try {
        std::map<QString, QVariant> params;
        params[":symbol"] = QString::fromStdString(symbol);
        params[":start_date"] = timeToDateString(start_date);
        params[":end_date"] = timeToDateString(end_date);
        
        QString sql = "SELECT id, symbol, UNIX_TIMESTAMP(trade_date), "
                      "main_inflow, main_outflow, net_main_inflow, "
                      "large_inflow, large_outflow, medium_inflow, medium_outflow, "
                      "small_inflow, small_outflow, net_amount, UNIX_TIMESTAMP(created_at) "
                      "FROM money_flow_daily WHERE symbol = :symbol "
                      "AND trade_date >= FROM_UNIXTIME(:start_date) "
                      "AND trade_date <= FROM_UNIXTIME(:end_date) "
                      "ORDER BY trade_date";
        
        QueryResult result = database_->executeQuery(sql, params);
        
        for (size_t i = 0; i < result.rowCount(); ++i) {
            rows.push_back(buildMoneyFlowDaily(result.getRow(i)));
        }
    } catch (const std::exception& e) {
        qWarning() << "MarketDataRepository::getMoneyFlowDaily error:" << e.what();
    }
    
    return rows;
}

std::vector<DragonTigerRecord> MarketDataRepository::getDragonTigerRecords(
    const std::string& symbol,
    std::time_t start_date,
    std::time_t end_date) {
    
    std::vector<DragonTigerRecord> rows;
    if (!database_) return rows;
    
    try {
        std::map<QString, QVariant> params;
        params[":symbol"] = QString::fromStdString(symbol);
        params[":start_date"] = timeToDateString(start_date);
        params[":end_date"] = timeToDateString(end_date);
        
        QString sql = "SELECT id, symbol, UNIX_TIMESTAMP(trade_date), reason, "
                      "buy_amount, sell_amount, net_amount, buy_count, sell_count, "
                      "institution_buy, institution_sell, turnover_rate, UNIX_TIMESTAMP(created_at) "
                      "FROM dragon_tiger_list WHERE symbol = :symbol "
                      "AND trade_date >= FROM_UNIXTIME(:start_date) "
                      "AND trade_date <= FROM_UNIXTIME(:end_date) "
                      "ORDER BY trade_date";
        
        QueryResult result = database_->executeQuery(sql, params);
        
        for (size_t i = 0; i < result.rowCount(); ++i) {
            rows.push_back(buildDragonTigerRecord(result.getRow(i)));
        }
    } catch (const std::exception& e) {
        qWarning() << "MarketDataRepository::getDragonTigerRecords error:" << e.what();
    }
    
    return rows;
}

// ============ 工具方法 ============

bool MarketDataRepository::executeQuery(const std::string& sql) {
    if (!database_) return false;
    
    try {
        QString qsql = QString::fromStdString(sql);
        int affected = database_->executeUpdate(qsql);
        return affected >= 0;
    } catch (const std::exception& e) {
        qWarning() << "MarketDataRepository::executeQuery error:" << e.what();
        return false;
    }
}

bool MarketDataRepository::beginTransaction() {
    if (!database_) return false;
    
    try {
        auto guard = database_->beginTransaction();
        return guard != nullptr;
    } catch (const std::exception& e) {
        qWarning() << "MarketDataRepository::beginTransaction error:" << e.what();
        return false;
    }
}

bool MarketDataRepository::commit() {
    if (!database_) return false;
    
    try {
        return database_->commitTransaction();
    } catch (const std::exception& e) {
        qWarning() << "MarketDataRepository::commit error:" << e.what();
        return false;
    }
}

bool MarketDataRepository::rollback() {
    if (!database_) return false;
    
    try {
        return database_->rollbackTransaction();
    } catch (const std::exception& e) {
        qWarning() << "MarketDataRepository::rollback error:" << e.what();
        return false;
    }
}

// ============ 构建方法 ============

SymbolInfo MarketDataRepository::buildSymbolInfo(const QueryResultRow& row) {
    SymbolInfo info;
    info.symbol = row.getString("symbol").toStdString();
    info.name = row.getString("name").toStdString();
    info.symbol_type = stringToSymbolType(row.getString("asset_class").toStdString());
    info.exchange = row.getString("exchange").toStdString();
    info.list_date = row.getInt("UNIX_TIMESTAMP(list_date)");
    info.delist_date = row.getInt("UNIX_TIMESTAMP(delist_date)");
    info.status = row.getString("status").toStdString();
    info.created_at = row.getInt("UNIX_TIMESTAMP(created_at)");
    info.updated_at = row.getInt("UNIX_TIMESTAMP(updated_at)");
    return info;
}

DailyBar MarketDataRepository::buildDailyBar(const QueryResultRow& row) {
    DailyBar bar;
    bar.id = row.getInt("id");
    bar.symbol = row.getString("symbol").toStdString();
    bar.trade_date = row.getInt("UNIX_TIMESTAMP(trade_date)");
    bar.open = row.getDouble("open");
    bar.high = row.getDouble("high");
    bar.low = row.getDouble("low");
    bar.close = row.getDouble("close");
    bar.pre_close = row.getDouble("pre_close");
    bar.volume = row.getDouble("volume");
    bar.turnover = row.getDouble("turnover");
    bar.change_pct = row.getDouble("change_pct");
    bar.amplitude = row.getDouble("amplitude");
    bar.turnover_rate = row.getDouble("turnover_rate");
    bar.pe_ratio = row.getDouble("pe_ratio");
    bar.pb_ratio = row.getDouble("pb_ratio");
    bar.market_cap = row.getDouble("market_cap");
    bar.pre_adjust_factor = row.getDouble("pre_adjust_factor");
    bar.post_adjust_factor = row.getDouble("post_adjust_factor");
    bar.created_at = row.getInt("UNIX_TIMESTAMP(created_at)");
    return bar;
}

MinuteBar MarketDataRepository::buildMinuteBar(const QueryResultRow& row) {
    MinuteBar bar;
    bar.id = row.getInt("id");
    bar.symbol = row.getString("symbol").toStdString();
    bar.datetime = row.getInt("UNIX_TIMESTAMP(datetime)");
    bar.frequency = row.getInt("frequency");
    bar.open = row.getDouble("open");
    bar.high = row.getDouble("high");
    bar.low = row.getDouble("low");
    bar.close = row.getDouble("close");
    bar.volume = row.getDouble("volume");
    bar.turnover = row.getDouble("turnover");
    bar.created_at = row.getInt("UNIX_TIMESTAMP(created_at)");
    return bar;
}

TickData MarketDataRepository::buildTickData(const QueryResultRow& row) {
    TickData tick;
    tick.id = row.getInt("id");
    tick.symbol = row.getString("symbol").toStdString();
    tick.datetime = row.getInt("UNIX_TIMESTAMP(datetime)");
    tick.last_price = row.getDouble("last_price");
    tick.volume = row.getDouble("volume");
    tick.turnover = row.getDouble("turnover");
    tick.bid_price = row.getDouble("bid_price");
    tick.bid_volume = row.getDouble("bid_volume");
    tick.ask_price = row.getDouble("ask_price");
    tick.ask_volume = row.getDouble("ask_volume");
    tick.created_at = row.getInt("UNIX_TIMESTAMP(created_at)");
    return tick;
}

MoneyFlowDaily MarketDataRepository::buildMoneyFlowDaily(const QueryResultRow& row) {
    MoneyFlowDaily mf;
    mf.id = row.getInt("id");
    mf.symbol = row.getString("symbol").toStdString();
    mf.trade_date = row.getInt("UNIX_TIMESTAMP(trade_date)");
    mf.main_inflow = row.getDouble("main_inflow");
    mf.main_outflow = row.getDouble("main_outflow");
    mf.net_main_inflow = row.getDouble("net_main_inflow");
    mf.large_inflow = row.getDouble("large_inflow");
    mf.large_outflow = row.getDouble("large_outflow");
    mf.medium_inflow = row.getDouble("medium_inflow");
    mf.medium_outflow = row.getDouble("medium_outflow");
    mf.small_inflow = row.getDouble("small_inflow");
    mf.small_outflow = row.getDouble("small_outflow");
    mf.net_amount = row.getDouble("net_amount");
    mf.created_at = row.getInt("UNIX_TIMESTAMP(created_at)");
    return mf;
}

DragonTigerRecord MarketDataRepository::buildDragonTigerRecord(const QueryResultRow& row) {
    DragonTigerRecord rec;
    rec.id = row.getInt("id");
    rec.symbol = row.getString("symbol").toStdString();
    rec.trade_date = row.getInt("UNIX_TIMESTAMP(trade_date)");
    rec.reason = row.getString("reason").toStdString();
    rec.buy_amount = row.getDouble("buy_amount");
    rec.sell_amount = row.getDouble("sell_amount");
    rec.net_amount = row.getDouble("net_amount");
    rec.buy_count = static_cast<unsigned int>(row.getInt("buy_count"));
    rec.sell_count = static_cast<unsigned int>(row.getInt("sell_count"));
    rec.institution_buy = row.getDouble("institution_buy");
    rec.institution_sell = row.getDouble("institution_sell");
    rec.turnover_rate = row.getDouble("turnover_rate");
    rec.created_at = row.getInt("UNIX_TIMESTAMP(created_at)");
    return rec;
}

} // namespace database
} // namespace astock
