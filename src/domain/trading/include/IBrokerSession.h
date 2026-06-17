#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace domain::trading {

class PositionInfo {
public:
    virtual ~PositionInfo() = default;
    virtual const std::string& symbol() const = 0;
    virtual int64_t volume() const = 0;
    virtual double price() const = 0;
    virtual double market_value() const = 0;
    virtual double pnl() const = 0;
};

class BrokerAccountInfo {
public:
    virtual ~BrokerAccountInfo() = default;
    virtual double total_asset() const = 0;
    virtual double available_cash() const = 0;
    virtual double market_value() const = 0;
    virtual double frozen() const = 0;
};

class OrderRecord {
public:
    virtual ~OrderRecord() = default;
    virtual const std::string& order_id() const = 0;
    virtual const std::string& symbol() const = 0;
    virtual int side() const = 0;
    virtual int status() const = 0;
    virtual int64_t volume() const = 0;
    virtual int64_t filled_volume() const = 0;
    virtual double price() const = 0;
};

class ExecutionReport {
public:
    virtual ~ExecutionReport() = default;
    virtual const std::string& order_id() const = 0;
    virtual const std::string& symbol() const = 0;
    virtual int side() const = 0;
    virtual int64_t volume() const = 0;
    virtual double price() const = 0;
    virtual double amount() const = 0;
};

class CashRecord {
public:
    virtual ~CashRecord() = default;
    virtual double available() const = 0;
    virtual double balance() const = 0;
    virtual double frozen() const = 0;
};

class IBrokerSession {
public:
    virtual ~IBrokerSession() = default;
    virtual bool initialize(const char* token, const char* account_id) = 0;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;
    virtual std::string place_order(const char* symbol, int side, int order_type, int64_t volume, double price) = 0;
    virtual bool cancel_order(const char* order_id) = 0;
    virtual bool cancel_all_orders() = 0;
    virtual std::vector<std::unique_ptr<OrderRecord>> query_orders() = 0;
    virtual std::vector<std::unique_ptr<ExecutionReport>> query_execution_reports() = 0;
    virtual std::vector<std::unique_ptr<PositionInfo>> query_positions() = 0;
    virtual std::vector<std::unique_ptr<CashRecord>> query_cash() = 0;
    virtual std::unique_ptr<BrokerAccountInfo> query_account() = 0;
    virtual const char* last_error() const = 0;
};

} // namespace domain::trading
