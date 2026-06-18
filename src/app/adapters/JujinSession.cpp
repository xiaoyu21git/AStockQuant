#include "JujinSession.h"

#include <cstring>

namespace app::adapters {
namespace {

// ── 从 SDK DataArray<T> 释放函数 ──
template <typename T>
struct SdkArrayGuard {
    DataArray<T>* arr;
    ~SdkArrayGuard() { if (arr) arr->release(); }
};

// ── 根据 SDK 枚举构造接口枚举 ──
constexpr int kSdkOrderSideBuy  = 1;
constexpr int kSdkOrderSideSell = 2;
constexpr int kSdkOrderTypeMarket = 1;
constexpr int kSdkOrderTypeLimit  = 2;
constexpr int kSdkPositionEffectOpen  = 1;
constexpr int kSdkPositionEffectClose = 2;
constexpr int kSdkOrderStatusFilled        = 3;
constexpr int kSdkOrderStatusPartiallyFilled = 4;
constexpr int kSdkOrderStatusCancelled     = 5;
constexpr int kSdkOrderStatusRejected      = 8;
constexpr int kSdkModeLive = 1;

// ── 具体值类型实现 ──

class SdkPositionInfo final : public domain::trading::PositionInfo {
    std::string m_symbol;
    int64_t m_volume{};
    double m_price{};
    double m_market_value{};
    double m_pnl{};
public:
    explicit SdkPositionInfo(const Position& p)
        : m_symbol(p.symbol)
        , m_volume(p.volume)
        , m_price(p.vwap)
        , m_market_value(p.amount)
        , m_pnl(p.fpnl) {}
    [[nodiscard]] const std::string& symbol() const override { return m_symbol; }
    [[nodiscard]] int64_t volume() const override { return m_volume; }
    [[nodiscard]] double price() const override { return m_price; }
    [[nodiscard]] double market_value() const override { return m_market_value; }
    [[nodiscard]] double pnl() const override { return m_pnl; }
};

class SdkBrokerAccountInfo final : public domain::trading::BrokerAccountInfo {
    double m_total{};
    double m_available{};
    double m_market_value{};
    double m_frozen{};
public:
    explicit SdkBrokerAccountInfo(const Cash& c)
        : m_total(c.nav), m_available(c.available), m_frozen(c.frozen) {}
    [[nodiscard]] double total_asset() const override { return m_total; }
    [[nodiscard]] double available_cash() const override { return m_available; }
    [[nodiscard]] double market_value() const override { return m_market_value; }
    [[nodiscard]] double frozen() const override { return m_frozen; }
};

class SdkOrderRecord final : public domain::trading::OrderRecord {
    std::string m_order_id;
    std::string m_symbol;
    int m_side{};
    int m_status{};
    int64_t m_volume{};
    int64_t m_filled_volume{};
    double m_price{};
public:
    explicit SdkOrderRecord(const Order& o)
        : m_order_id(o.cl_ord_id)
        , m_symbol(o.symbol)
        , m_side(o.side)
        , m_status(o.status)
        , m_volume(o.volume)
        , m_filled_volume(o.filled_volume)
        , m_price(o.price) {}
    [[nodiscard]] const std::string& order_id() const override { return m_order_id; }
    [[nodiscard]] const std::string& symbol() const override { return m_symbol; }
    [[nodiscard]] int side() const override { return m_side; }
    [[nodiscard]] int status() const override { return m_status; }
    [[nodiscard]] int64_t volume() const override { return m_volume; }
    [[nodiscard]] int64_t filled_volume() const override { return m_filled_volume; }
    [[nodiscard]] double price() const override { return m_price; }
};

class SdkExecReport final : public domain::trading::ExecutionReport {
    std::string m_order_id;
    std::string m_symbol;
    int m_side{};
    int64_t m_volume{};
    double m_price{};
    double m_amount{};
public:
    explicit SdkExecReport(const ExecRpt& r)
        : m_order_id(r.cl_ord_id)
        , m_symbol(r.symbol)
        , m_side(r.side)
        , m_volume(r.volume)
        , m_price(r.price)
        , m_amount(r.amount) {}
    [[nodiscard]] const std::string& order_id() const override { return m_order_id; }
    [[nodiscard]] const std::string& symbol() const override { return m_symbol; }
    [[nodiscard]] int side() const override { return m_side; }
    [[nodiscard]] int64_t volume() const override { return m_volume; }
    [[nodiscard]] double price() const override { return m_price; }
    [[nodiscard]] double amount() const override { return m_amount; }
};

class SdkCashRecord final : public domain::trading::CashRecord {
    double m_available{};
    double m_balance{};
    double m_frozen{};
public:
    explicit SdkCashRecord(const Cash& c)
        : m_available(c.available), m_balance(c.nav), m_frozen(c.frozen) {}
    [[nodiscard]] double available() const override { return m_available; }
    [[nodiscard]] double balance() const override { return m_balance; }
    [[nodiscard]] double frozen() const override { return m_frozen; }
};

} // anonymous namespace

JujinSession::~JujinSession() {
    disconnect();
}

bool JujinSession::initialize(const char* token, const char* account_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!token || !account_id || std::strlen(token) == 0) {
        m_last_error = "invalid token";
        return false;
    }
    m_token = token;
    m_account_id = account_id;

    try {
        m_strategy = std::make_unique<Strategy>(token, "", kSdkModeLive);
    } catch (...) {
        m_last_error = "Strategy creation failed";
        return false;
    }
    return true;
}

void JujinSession::set_gm_strategy_id(const char* strategy_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_strategy && strategy_id && std::strlen(strategy_id) > 0) {
        m_strategy->set_strategy_id(strategy_id);
    }
}

bool JujinSession::connect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_strategy) {
        m_last_error = "not initialized";
        return false;
    }
    // SDK::run() 是阻塞调用，后台线程执行
    m_runFuture = std::async(std::launch::async, [this]() {
        m_strategy->run();
    });
    m_connected = 1;  // SDK 已启动（后台线程运行），标记为已连接
    return true;
}

void JujinSession::disconnect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_strategy.reset();
    m_connected = 0;
}

bool JujinSession::is_connected() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_connected != 0 && m_strategy != nullptr;
}

void JujinSession::ensureRun() {
    // SDK::run() 在 connect() 中已通过 std::async 启动，m_connected 已设为 1
    // 此处仅做防御性检查，不阻塞等待（run() 是事件循环永不返回）
    if (m_connected == 0 && m_strategy && m_runFuture.valid()) {
        // 异常情况：connect() 没设 m_connected，补设并记录
        m_connected = 1;
    }
}

std::string JujinSession::place_order(
    const char* symbol, int side, int order_type,
    int64_t volume, double price) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_strategy) { m_last_error = "not initialized"; return {}; }
    ensureRun();

    int sdk_side = (side == kSdkOrderSideBuy) ? kSdkOrderSideBuy : kSdkOrderSideSell;
    int sdk_type = (order_type == kSdkOrderTypeMarket) ? kSdkOrderTypeMarket : kSdkOrderTypeLimit;

    auto order = m_strategy->place_order(
        symbol, static_cast<int>(volume),
        sdk_side, sdk_type,
        kSdkPositionEffectOpen, price);

    if (order.cl_ord_id[0] == '\0') {
        m_last_error = "place_order returned empty order id";
        return {};
    }
    return std::string(order.cl_ord_id);
}

bool JujinSession::cancel_order(const char* order_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_strategy) return false;
    return m_strategy->order_cancel(order_id) == 0;
}

bool JujinSession::cancel_all_orders() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_strategy) return false;
    return m_strategy->order_cancel_all() == 0;
}

std::vector<std::unique_ptr<domain::trading::OrderRecord>>
JujinSession::query_orders() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::unique_ptr<domain::trading::OrderRecord>> result;
    if (!m_strategy) return result;
    ensureRun();

    auto* arr = m_strategy->get_orders();
    if (!arr || arr->status() != 0) return result;
    SdkArrayGuard<Order> guard{arr};

    for (int i = 0; i < arr->count(); ++i)
        result.push_back(std::make_unique<SdkOrderRecord>(arr->at(i)));
    return result;
}

std::vector<std::unique_ptr<domain::trading::ExecutionReport>>
JujinSession::query_execution_reports() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::unique_ptr<domain::trading::ExecutionReport>> result;
    if (!m_strategy) return result;
    ensureRun();

    auto* arr = m_strategy->get_execution_reports();
    if (!arr || arr->status() != 0) return result;
    SdkArrayGuard<ExecRpt> guard{arr};

    for (int i = 0; i < arr->count(); ++i)
        result.push_back(std::make_unique<SdkExecReport>(arr->at(i)));
    return result;
}

std::vector<std::unique_ptr<domain::trading::PositionInfo>>
JujinSession::query_positions() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::unique_ptr<domain::trading::PositionInfo>> result;
    if (!m_strategy) return result;
    ensureRun();

    auto* arr = m_strategy->get_position();
    if (!arr || arr->status() != 0) return result;
    SdkArrayGuard<Position> guard{arr};

    for (int i = 0; i < arr->count(); ++i)
        result.push_back(std::make_unique<SdkPositionInfo>(arr->at(i)));
    return result;
}

std::vector<std::unique_ptr<domain::trading::CashRecord>>
JujinSession::query_cash() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::unique_ptr<domain::trading::CashRecord>> result;
    if (!m_strategy) return result;
    ensureRun();

    auto* arr = m_strategy->get_cash();
    if (!arr || arr->status() != 0) return result;
    SdkArrayGuard<Cash> guard{arr};

    for (int i = 0; i < arr->count(); ++i)
        result.push_back(std::make_unique<SdkCashRecord>(arr->at(i)));
    return result;
}

std::unique_ptr<domain::trading::BrokerAccountInfo>
JujinSession::query_account() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_strategy) return {};
    ensureRun();

    auto* arr = m_strategy->get_cash();
    if (!arr || arr->status() != 0 || arr->count() == 0) return {};
    SdkArrayGuard<Cash> guard{arr};

    return std::make_unique<SdkBrokerAccountInfo>(arr->at(0));
}

const char* JujinSession::last_error() const {
    return m_last_error.c_str();
}

} // namespace app::adapters
